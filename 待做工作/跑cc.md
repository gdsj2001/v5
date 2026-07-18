# 跑cc

## 目标

用真实板端 UI/operator 路径验证 fresh active model 对应的原始 `cc-ac.ngc` 或 `cc-bc.ngc`。上板后先通过设置页执行一次 `设置驱动 -> 保存并重启` 并完成 fresh 轴—从站/驱动/比例链回读；随后通过 MCP 板端抓屏与 pointer/mouse 至少连续三轮执行 `取消急停 -> 机械全轴回零 -> 打开程序 -> 选择并打开模型匹配程序 -> 启动 -> 1 秒后急停 -> 取消急停 -> 机械全轴回零 -> 急停`。全过程监测编码器反馈，证明程序目标和两次 Home 都走当前模型旋转轴的最近 `k*360` 等效角，而不是落回 raw 机器 0、坐标清零或只写 homed 标记。

## 硬要求

- AI 只能按 [`功能/自动闭环测试方式.md`](../功能/自动闭环测试方式.md) 第 5 节使用 MCP 板端抓屏与 pointer/mouse 查看和模拟输入；每一次点击、双击或键盘确认前必须抓取 fresh frame，并核对页面、目标按钮/列表项、位置和高亮状态。
- 每一次输入后必须重新抓屏并按需保存 fresh frame，确认页面状态变化，再进入下一步。
- 不允许裸坐标连点，不允许跳过 fresh frame 确认，不允许用 direct UDS 或 linuxcncrsh 直接命令替代 UI/operator 路径。
- 编码器反馈必须从板端运行态采样，优先记录 `/dev/shm/v3_status_shm` 中的编码器反馈机械坐标 `mcs[0..4]`、`cmd_mcs[0..4]`、速度、frame seq/epoch 和 valid mask。
- 编码器反馈采样必须特别保留 A/C raw 反馈、A/C 等效相位、目标段前后差值和最短角差判断；若只能拿到 `mcs[3]/mcs[4]`，采样记录中也必须计算并标注 `phase = angle mod 360`、`nearest_zero_error = shortest_angle(phase, 0)`，用于判断 `A0 C0` 和回零是否走等效零。
- `Set Run`、按钮事件或弹窗成功只算操作提交证据，不算运动证明；必须有编码器反馈位移和最终状态 readback。
- MCP 板端抓屏证据放 `D:\v5\截图\跑cc\`；采样 CSV/JSONL、临时脚本和中间日志放 `D:\v5\repo_ignored\temp\跑cc\`。

## 前置条件

- 测试台架处于允许真实电机运动的空载状态。
- 板端、VM、继电器控制链路可用。
- LinuxCNC/EtherCAT/command gate/UI relay 在断电重启后恢复，服务启动保持 realtime latch 安全态和 `Machine Off`，不得自动 reset 或上使能。只有真实 UI `取消急停` 后 fresh native `estop_active=false && machine_enabled=true` 才允许 Home。
- 程序只使用板端正式部署的 `cc-ac.ngc` / `cc-bc.ngc`；选择由 fresh native active model 裁决，不使用临时改写文件、旧 `cc.ngc` 或 UI 预览结果代替。

## 执行步骤

1. 部署完成后用 MCP 抓屏/鼠标进入设置页，只点击一次 `设置驱动`；等待同一 `run_id` 的 `DRIVE_SET_OK + write_verified_readback`，按本次 fresh scan/轴—从站绑定核对全部目标的 mode/egear/statusword/error_code、全过程无报警且按钮松手退出黄色。
2. 点击右上角 `保存并重启`，等待 canonical clean restart；回读全部当前目标轴—从站绑定、mode/egear/statusword/error_code、LinuxCNC/HAL/EtherCAT 比例链和 PDO/axis binding。缺项时停止，设置驱动不在后续每轮重复执行。
3. 确认 MCP 抓屏/鼠标能力可用且当前页面正确，板端为 fresh `estop_active=true / machine_enabled=false`；启动编码器反馈采样并记录 active model、`mcs/cmd_mcs/velocity/seq/valid_mask` 和当前模型旋转轴的 raw/count-domain 基线。
4. 连续执行至少三轮；每轮开始前确认仍是同一部署 identity，并严格执行以下步骤，每次只做一个动作、动作后先看 fresh frame/readback：
   1. 点击 `取消急停`，只在 fresh native `estop_active=false && machine_enabled=true` 后继续。
   2. 选择 `机械全轴` 并点击 `回零`；确认新的 native Home transaction、真实编码器位移、RTCP force-off actual、各轴到位和 fresh `all_homed`。Home 未成功不得打开程序。
   3. 点击 `打开程序`；按 fresh native active model 在程序页第一次点击 `cc-ac.ngc` 或 `cc-bc.ngc` 完成选中，第二次点击同一行完成打开；回主页面核对程序名、runtime identity 和原始 hash。
   4. 点击 `启动`，持续采样编码器反馈；1 秒后点击 `急停`，在 owner 时限内确认 native 急停 actual、运动停止和按钮切换为 `取消急停`。
   5. 点击 `取消急停`，确认 latch 清除和 machine enable actual；再次选择 `机械全轴` 并点击 `回零`，确认新的 transaction 和当前模型旋转轴最近等效零到位。
   6. 点击 `急停` 收尾，确认 `estop_active=true && machine_enabled=false`，保存本轮最终 live frame 和 native/encoder evidence。
5. 任一步失败只修第一条真实失败层并从本轮安全边界重测；不得重复点击、direct UDS/linuxcncrsh、固定延时兜底或复用旧 result。三轮通过后才进入 `功能/自动闭环测试方式.md` 的完整设置页 A-G 与 model-matched Golden Motion。

## 通过标准

- 所有 AI UI 输入均有 MCP 板端抓屏取得的点击前/后 fresh frame。
- 模型匹配的 `cc-ac.ngc` / `cc-bc.ngc` 是通过真实程序页面先选中、再第二次点击同一行打开，不是直接写入路径或 direct command。
- clean restart 后保持 ESTOP / Machine Off；每次 UI `取消急停` 都由 fresh native latch 与 machine-enable actual 证明恢复，按钮显示与 actual 一致。
- 设置驱动和 clean restart 后的全部轴—从站/驱动/比例链 readback 合格；连续三轮中的第一次 Home、程序启动后的 1 秒运动、急停停止、取消急停后的第二次 Home 均有独立 fresh transaction 和编码器证据。
- 程序中的旋转轴目标必须由编码器反馈证明走最近等效角；若采样显示当前模型旋转轴向 raw 机器 0 长距离回转，则测试失败。
- 两次 `回零` 必须由编码器反馈证明按 360 度最短角差回到等效零；`359.999`、`-0.001` 这类等效零只能在编码器/native 实际到位且最短角差在容差内时判定通过。
- 急停后底层安全状态确认生效；取消急停后底层 latch 清除并恢复 machine enable actual。
- 最终回零完成后，X/Y/Z/A/C 编码器反馈处于回零完成状态；A/C 按 360 度等效零判定。

## 阻塞条件

- MCP 抓屏/鼠标不可用、无法确认目标位置或页面不正确。
- 继电器断电重启失败，或板端无法恢复 UI/LinuxCNC。
- UI `取消急停` 后 1 秒内未取得 `estop_active=false && machine_enabled=true`，或 machine enable actual 与 UI 右下按钮状态不一致。
- 编码器反馈 `mcs` 无效、stale、seq 不变化，或采样链路中断。
- A/C 编码器反馈无法支持最短角差判断，或无法区分最近等效零与 raw 机器 0 长距离回转。
- 驱动故障、急停无法取消、machine enable 无法恢复。
- 任一阶段出现非预期运动、速度不归零、按钮状态与 native readback 冲突。
