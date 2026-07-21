# 跑cc

## 目标

用真实板端 UI/operator 路径验证 fresh active model 对应的原始 `cc-ac.ngc` 或 `cc-bc.ngc`。完整发布验收仍先闭合设置页A-G；针对已部署且本轮未改参数、模型、驱动绑定或比例链的focused性能复测，允许用同一启动周期的fresh owner readback证明这些前置未变，直接进入 `取消急停 -> 机械全轴回零 -> 打开程序 -> 选择并打开模型匹配程序 -> 启动 -> 运行态采样 -> 急停`，不得把focused结果冒充另一模型或完整设置页验收。

## 硬要求

- AI按[`功能/自动闭环测试方式.md`](../功能/自动闭环测试方式.md)第5节使用`capture_v5_board_ui.sh`抓屏，并用`collect_v5_remote_input_evidence.py`执行单次pointer/mouse模拟；不得自动操作WinRemote。每一次点击或双击前必须抓取fresh frame，并核对页面、目标按钮/列表项、位置和高亮状态。
- 每一次输入后必须重新抓屏并按需保存 fresh frame，确认页面状态变化，再进入下一步。
- 不允许裸坐标连点，不允许跳过 fresh frame 确认，不允许用 direct UDS 或 linuxcncrsh 直接命令替代 UI/operator 路径。
- 编码器反馈必须从板端运行态采样，优先记录 `/dev/shm/v3_status_shm` 中的编码器反馈机械坐标 `mcs[0..4]`、`cmd_mcs[0..4]`、速度、frame seq/epoch 和 valid mask。
- 编码器反馈采样必须特别保留 A/C raw 反馈、A/C 等效相位、目标段前后差值和最短角差判断；若只能拿到 `mcs[3]/mcs[4]`，采样记录中也必须计算并标注 `phase = angle mod 360`、`nearest_zero_error = shortest_angle(phase, 0)`，用于判断 `A0 C0` 和回零是否走等效零。
- `Set Run`、按钮事件或弹窗成功只算操作提交证据，不算运动证明；必须有编码器反馈位移和最终状态 readback。
- 抓屏/pointer工具的fresh frame证据放`D:\v5\截图\跑cc\`；采样CSV/JSONL、临时脚本和中间日志放`D:\v5\repo_ignored\temp\跑cc\`。

## 前置条件

- 测试台架处于允许真实电机运动的空载状态。
- 板端、VM、继电器控制链路可用。
- LinuxCNC/EtherCAT/command gate/UI relay 在断电重启后恢复，服务启动保持 realtime latch 安全态和 `Machine Off`，不得自动 reset 或上使能。只有真实 UI `取消急停` 后 fresh native `estop_active=false && machine_enabled=true` 才允许 Home。
- 程序只使用板端正式部署的 `cc-ac.ngc` / `cc-bc.ngc`；选择由 fresh native active model 裁决，不使用临时改写文件、旧 `cc.ngc` 或 UI 预览结果代替。

## 执行步骤

1. 完整发布验收在部署后用合规operator面进入设置页，只点击一次 `设置驱动`；等待同一 `run_id` 的 `DRIVE_SET_OK + write_verified_readback`。focused性能复测若本轮未改参数、模型、驱动绑定或比例链，则只做fresh readback核对，不重复设置驱动和重启。
2. 点击右上角 `保存并重启`，等待 canonical clean restart；回读全部当前目标轴—从站绑定、mode/egear/statusword/error_code、LinuxCNC/HAL/EtherCAT 比例链和 PDO/axis binding。缺项时停止，设置驱动不在后续每轮重复执行。
3. 确认合规operator面可用且当前页面正确，板端为fresh `estop_active=true / machine_enabled=false`；启动编码器反馈采样并记录active model、`mcs/cmd_mcs/velocity/seq/valid_mask`和当前模型旋转轴的raw/count-domain基线。
4. 先完成一次 `取消急停 -> 机械全轴回零 -> 打开程序 -> 选择并打开 active model 对应程序`：Home 必须有新的 native transaction、真实编码器位移、RTCP force-off actual、各轴到位和 fresh `all_homed`；程序页第一次点击模型匹配行完成选中，第二次点击同一行完成打开，并在主页核对程序名、runtime identity 和原始 hash。
5. 在同一 program identity 和有效 `all_homed` 上连续执行至少三轮 `启动 -> 1 秒后急停`。每轮 Start 都持续采样编码器反馈并取得新运动；每次急停都在 owner 时限内确认 native 安全 actual、运动停止、clean generation、队列清空，同时保留 program identity、刀路和 homed。轮间只点击一次 `取消急停`，确认 fresh latch 清除和 machine enable actual 后继续；不重新 Home、不重新打开程序。最后一轮急停后保持 `estop_active=true && machine_enabled=false`。
6. `功能/自动闭环测试方式.md` 的完整设置页 A-G 必须在任何三轮 Golden Motion 之前闭合；完成 model switch、人工从站下拉覆盖、保存并 clean restart 及 fresh readback 后，才按本节执行 model-matched 三轮。任一步失败只修第一条真实失败层并从安全边界重测；不得重复点击、direct UDS/linuxcncrsh、固定延时兜底或复用旧 result。

## 通过标准

- 所有AI UI输入均有抓屏/pointer工具取得的点击前/后fresh frame。
- 模型匹配的 `cc-ac.ngc` / `cc-bc.ngc` 是通过真实程序页面先选中、再第二次点击同一行打开，不是直接写入路径或 direct command。
- clean restart 后保持 ESTOP / Machine Off；每次 UI `取消急停` 都由 fresh native latch 与 machine-enable actual 证明恢复，按钮显示与 actual 一致。
- 设置驱动和 clean restart 后的全部轴—从站/驱动/比例链 readback 合格；一次 Home/Program Open 取得 fresh transaction/identity，连续三轮中的每次 1 秒运动和急停停止均有独立编码器/native 证据，且轮间保留 homed、program identity 与刀路。
- 程序中的旋转轴目标必须由编码器反馈证明走最近等效角；若采样显示当前模型旋转轴向 raw 机器 0 长距离回转，则测试失败。
- 前置 `回零` 必须由编码器反馈证明按 360 度最短角差回到等效零；`359.999`、`-0.001` 这类等效零只能在编码器/native 实际到位且最短角差在容差内时判定通过。
- 急停后底层安全状态确认生效；取消急停后底层 latch 清除并恢复 machine enable actual。
- 最终急停后保持同一 program identity、刀路和 valid `all_homed`；当前模型旋转轴在三轮运动中的连续相位和急停停止均由编码器反馈证明。

## 阻塞条件

- 抓屏或pointer/mouse工具不可用、无法确认目标位置或页面不正确。
- 继电器断电重启失败，或板端无法恢复 UI/LinuxCNC。
- UI `取消急停` 后 1 秒内未取得 `estop_active=false && machine_enabled=true`，或 machine enable actual 与 UI 右下按钮状态不一致。
- 编码器反馈 `mcs` 无效、stale、seq 不变化，或采样链路中断。
- A/C 编码器反馈无法支持最短角差判断，或无法区分最近等效零与 raw 机器 0 长距离回转。
- 驱动故障、急停无法取消、machine enable 无法恢复。
- 任一阶段出现非预期运动、速度不归零、按钮状态与 native readback 冲突。
