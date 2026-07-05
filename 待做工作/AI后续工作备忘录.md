# AI 后续工作备忘录

更新时间：2026-07-03

本文是后续 AI 接手 v3 时的工作提醒，不是需求 owner、不是任务看板、不是过程记录，也不替代 `AGENTS.md` 或 `功能/` owner。若本文与当前规则、当前用户消息或 `REQ-*` owner 冲突，以当前规则和 owner 为准，先修正文档口径再继续代码。

固定入口：

- [AGENTS.md](../AGENTS.md)
- [功能/需求真源索引.md](../功能/需求真源索引.md)
- [系统代码架构硬边界守则.md](../系统代码架构硬边界守则.md)
- [项目软硬件架构和后期修改指导说明.md](../项目软硬件架构和后期修改指导说明.md)

## 1. 当前接手判断

当前主线不是干净树，已经有多组代码、测试和功能文档改动在同一 worktree 里。接手前必须先跑 `git status --short` 和相关 diff，默认其中包含用户或前序 AI 的有效工作，不能 reset、checkout、覆盖或删除无关改动。

当前阶段仍是小步闭环阶段：每次只收敛一个明确 slice，用 owner、代码事实和本地/板端证据说话。Git commit/push 只能算备份同步，不算功能完成。

## 2. 当前代码状态快照

| 范围 | 当前代码锚点 | 接手结论 |
| :--- | :--- | :--- |
| 后台长任务取消 | `lvgl_app/scripts/broker_jobs.py`、`lvgl_app/scripts/broker_routes.py` | 旧 `lvgl_app/src/v3_product_command.c` 与 `lvgl_app/src/v3_ui_command_drive_async.inc` 已物理删除，不得作为当前入口恢复；剩余长任务取消只按仍存在的 Broker job owner 审计 |
| 急停与后台任务 | `lvgl_app/scripts/broker_action_handlers_fault_estop.py`、`lvgl_app/tests/command_broker_protocol_estop_part00.py` | `estop_force` 会请求取消活动 async jobs；急停本身仍必须以 native/LinuxCNC/HAL/EtherCAT realtime 证据为准 |
| BUS/Pulse active mode | `lvgl_app/scripts/state_publisher_motion_driver.py`、`motion_driver_active_status_daemon.py`、`v3_state_shm_typed*.py`、`broker_active_driver_mode.py` | `motion_driver.conf` 只是 requested owner；active mode 必须由 backend proof 写入 typed SHM；不能按旧 UI 字段、默认值或单点配置读回判断 |
| BUS-only 动作禁用 | `broker_action_handlers_device.py`、`broker_action_handlers_drive.py`、`v3_ui_widgets.c` | 旧 `v3_product_command.c` 已删除，不得作为 BUS-only 门禁或 UI 中介恢复；BUS/Pulse 可见状态以后只按真实 owner、当前 UI 入口和板端路径验证 |
| 设置页轴设零 | `actions_axis_zero_*`、`settings_runtime_drive_only_zero_model.py`、`v3_ui_command_axis_zero.c`、`v3_ui_command_popup_axis_zero.inc` | BUS 与 Pulse 分支已分离：BUS 走 count-domain/驱动读回，Pulse 只写当前轴 0 位 `HOME_OFFSET`；不要把 Pulse 设零写成驱动参数写入 |
| Program Open / Start | `actions_program.py`、`actions_program_open.py`、`v3_ui_command_program.c`、`REQ-START-RUN-HOT-PATH` owner | Program Open/Load 与 Start/Auto Run 分工已经收敛；Start 热路径不应新增慢 I/O、hash、授权刷新或大计算 |
| State Publisher / SHM | `v3_state_publisher.py`、`v3_state_shm_header.py`、`v3_state_shm_typed.py` | UI/Broker 读 typed SHM；运行态真值不得从 JSON、旧快照、默认值或硬盘轮询兜底 |
| 授权 / VPS / profile | `v3_device_authorization_download.py`、`device_dna_register_auth.py`、`device_auth_latch.py`、`v3_drive_profile_download.py` | 当前授权链识别 DNA、签名、有效期、`permissions` 和 `drive_profile_download`；商业 feature 字段、Pulse/Hybrid 授权轴数还没实现 |
| Pulse/Hybrid | `tools/v3_hardware_profile_manifest.py`、`config/hardware_profile.json`、`lvgl_app/linuxcnc/v3_pulse.*` | `stepdir/pulse` 仍是 `future_not_implemented`，`hybrid` 只是资源边界并集，不是 runtime 混合插补能力 |

## 3. 必须继续使用的 owner 口径

- `REQ-DOC-SINGLE-SOURCE`：一个需求一个 owner，非 owner 文档只引用 ID 和保留本地实现细节。
- `REQ-PARAM-MEMORY-LIGHTWEIGHT-SAVE`、`REQ-SETTINGS-RUNTIME-DRIVE-ONLY`：运行态真值走内存/SHM；`settings_runtime.json` 只保留 Settings Drive 私域和深度清洗后的驱动/count-domain 证据。
- `REQ-NATIVE-OWNER-FIRST`、`REQ-LINUXCNC-COMMAND-GATE`：native/LinuxCNC/HAL/EtherCAT 可拥有的语义不能被 UI、Broker 或 product Python 私有化。
- `REQ-BUS-PULSE-MODE-DIFFERENCE`：`motion_driver.conf` 是 requested；active mode 以 mode-switch/backend/State Publisher/typed SHM proof 为准。
- `REQ-G92-NATIVE-RUNTIME`、`REQ-WCS-NATIVE-OWNER`：WCS/G92/Work Zero 只能走登记 native gate 和 SHM readback，不写 V3 私有坐标补丁。
- `REQ-START-RUN-HOT-PATH`、`REQ-GCODE-RUN-HOT-PATH`：Start 之后避免慢校验、文件 I/O、诊断写入和非必要投影重建。
- `REQ-MAIN-ESTOP-LATENCY`：急停 edge event 直通底层安全链，不能被身份、busy、后台清理或产品预检挡住。

## 4. 每次改代码前的最小检查

1. 读最新用户消息、`AGENTS.md`、`功能/需求真源索引.md` 和本 slice 对应 owner。
2. 跑 `git status --short`，查看目标文件 diff，确认不覆盖无关改动。
3. 判断风险等级：native、SHM、Broker/control、motion/safety、参数、State Publisher、fallback survivor 默认按 P0/P1 严格处理。
4. 若行为或要求变更，先改 `功能/` owner，再改代码和工作文档。
5. 编辑前备份到 `repo_ignored/<task>/backup/`。
6. 禁止创建或更新 `process.md`、`过程.md`、旧任务看板或平行规则文档。

## 5. 禁止恢复的产品路径

这些路径如果在产品热路径、测试入口或文档中重新出现，优先删除或记录真实 blocker：

- `native_command.fifo`
- `native_command_response.json`
- `command_broker_response_*.json`
- `v3_native_command_daemon.py`
- 旧 `v3_linuxcnc_*.py` direct fallback
- UI 热路径 `v3_status_snapshot.json` 轮询
- `v3_state_shm_writer.py --input-json` 产品状态链
- product action short-lived Python fallback
- 全局临时文件控制 IPC
- UI settings scratch JSON in `/run`
- UI `system/popen/fork/exec` 热路径

Broker、SHM、status epoch、backend ready 不可用时只能 fail-closed 或 degraded display，不能静默回退到旧 IPC、JSON、短命脚本或私有缓存。

## 6. 验证用词

- 只改文档或源码未测：`source_only`。
- 本地 pytest、编译、静态审计通过但没上板：`local_verified_only`。
- 跑了真实 UI/operator 路径但未完成板端运动闭环：写清楚证据，不能叫 `board_verified`。
- 涉及运动能力、Home/Jog/Start/RTCP/Rotary/WCS 的闭环，缺 `nc/cc.ngc` golden loop 或原路径板端证据时，不得说通过。
- Git commit/push 是恢复点和备份同步，不是功能验收。

## 7. 当前未完成但容易被误称完成的事项

- 所有后台长任务“可取消”当前是本地代码/合同测试口径；每个具体按钮仍需原始 UI/operator 关闭弹窗、取消、状态回读验证。
- BUS/Pulse 切换和 BUS-only 动作禁用已有本地门禁，但未跑完整板端 UI 原路径时只能写 `local_verified_only`。
- Pulse 硬件 runtime、per-axis Hybrid、Pulse/Hybrid 商业授权 feature 字段未实现。
- Start 热路径、Program Open 背景任务、状态 epoch、取消 token 的组合还需要按具体缺陷做 focused test，不要用一次全量自动化代替单点闭环。
- 任何新增 resident daemon、native helper、SHM 字段或 Broker action 都必须接入 registry/manifest/init/focused tests；不能只新增文件。

## 8. 结束前自检

最终回复前确认：

- 改了哪些文件，是否只改了本 slice。
- 是否碰到 owner/控制链/状态链/参数真源。
- 是否新增文件，新增文件是否接入构建、registry、manifest、init 或明确只是文档/测试。
- 跑了哪些本地 gate，输出是什么。
- 是否上板；未上板必须明确不是 `board_verified`。
- 是否还有 blocker、未闭环事项，是否需要更新 `待做工作/遗留.md`。

一句话底线：没有 owner 不改行为，没有 SHM/native readback 不报运行真值，没有板端原路径不报板端闭环。
