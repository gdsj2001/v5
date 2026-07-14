# 跑cc

## 目标

用真实板端 UI/operator 路径验证 `cc.ngc`：继电器断电重启开发板后，AI 统一通过 Codex 内嵌浏览器的实时开发板镜像逐步点击打开程序、双击打开 `cc`、回零、启动、1 秒后急停、取消急停、再次回零，并在全过程监测编码器反馈机械坐标。监测编码器反馈的核心目的不是只证明“动了”，而是证明 G-code 中一圈内 `A0 C0` 目标是否实际走了最近 `k*360` 等效角，以及两次 `回零` 是否实际走了 A/C 的 360 度最短角差等效回零，而不是落回 raw 机器 0、坐标清零或只写 homed 标记。

## 硬要求

- AI 只能按 [`功能/自动闭环测试方式.md`](../功能/自动闭环测试方式.md) 第 5 节启动 AI 专用 bridge，并使用 Codex 内嵌浏览器 `http://127.0.0.1:18777/` 查看和模拟输入；每一次点击、双击或键盘确认前必须确认非零 `frame`、`stream=live`、`input=ready`，并在当前 live frame 核对页面、目标按钮/列表项、位置和高亮状态。
- 每一次输入后必须观察并按需保存新的内嵌浏览器 live frame，确认页面状态变化，再进入下一步。
- 不允许脱离内嵌浏览器裸坐标连点，不允许跳过 live frame 确认，不允许用逐次抓屏脚本、direct UDS 或 linuxcncrsh 直接命令替代 UI/operator 路径。
- 编码器反馈必须从板端运行态采样，优先记录 `/dev/shm/v3_status_shm` 中的编码器反馈机械坐标 `mcs[0..4]`、`cmd_mcs[0..4]`、速度、frame seq/epoch 和 valid mask。
- 编码器反馈采样必须特别保留 A/C raw 反馈、A/C 等效相位、目标段前后差值和最短角差判断；若只能拿到 `mcs[3]/mcs[4]`，采样记录中也必须计算并标注 `phase = angle mod 360`、`nearest_zero_error = shortest_angle(phase, 0)`，用于判断 `A0 C0` 和回零是否走等效零。
- `Set Run`、按钮事件或弹窗成功只算操作提交证据，不算运动证明；必须有编码器反馈位移和最终状态 readback。
- 内嵌浏览器截图证据放 `D:\v5\截图\跑cc\`；采样 CSV/JSONL、临时脚本和中间日志放 `D:\v5\repo_ignored\temp\跑cc\`。

## 前置条件

- 测试台架处于允许真实电机运动的空载状态。
- 板端、VM、继电器控制链路可用。
- LinuxCNC/EtherCAT/command gate/UI relay 可在断电重启后恢复，并且必须等微内核/native 运行闭包完全起来后才自动进入 `Machine On`。这里的“微内核完全起来”至少包含 LinuxCNC/EtherCAT OP、command gate 常驻连接、RTCP/WCS/G53/native safety 状态块、State Publisher 和 UI relay 可读，不允许只在 SSH 或 LinuxCNC backend 刚起来时提前 `Machine On`。若 `linuxcncrsh Get Machine`、native safety readback 或同源 machine enable actual 显示仍为 `MACHINE OFF`/`enabled=false`，本测试不能继续点击回零或启动，必须先修复开机自动上使能链路。
- `cc.ngc` 使用板端已部署的正式路径，不使用临时改写文件或 UI 预览结果代替。

## 执行步骤

1. 通过继电器断电重启开发板。
2. 等待板端 SSH、LinuxCNC、UI relay、State Publisher、RTCP/WCS/G53/native safety 状态块全部恢复，并确认系统在这些微内核/native owner 完全起来之后已自动 `Machine On`；此处不得通过手工点 `取消急停`、direct UDS、linuxcncrsh 或测试脚本补发 `Set Machine On` 来掩盖开机链路缺陷。
3. 启动编码器反馈采样，记录基线：`mcs/cmd_mcs/velocity/seq/valid_mask`，并记录 A/C 的 raw 角、`phase mod 360` 和到最近等效零的最短角差。
4. 在内嵌浏览器 live frame 确认主页面可见、`打开程序` 按钮位置正确。
5. 在内嵌浏览器点击 `打开程序`。
6. 用新的 live frame 确认进入程序页面，并确认 `cc` 或 `cc.ngc` 列表项位置。
7. 在内嵌浏览器对 `cc` 或 `cc.ngc` 执行双击打开；双击前必须在 live frame 确认目标项，双击后用新 frame 确认已回到主页面或程序名已载入。
8. 在内嵌浏览器 live frame 确认主页面 `回零` 按钮位置正确。
9. 点击 `回零`，持续采样编码器反馈，确认回零是实际运动，不是坐标清零；A/C 到位必须按 360 度最短角差判定等效零，不能按 raw 线性差值或 UI 显示 0 判定。
10. 用内嵌浏览器新 live frame 确认回零结束、主页面稳定、`启动` 按钮位置正确。
11. 点击 `启动`，持续采样编码器反馈。
12. 启动后等待 1 秒，期间采样必须显示真实运动或运动开始趋势；重点观察 `cc.ngc` 中 `A0 C0` 或等效归零/normalization 段前后的 A/C 编码器反馈，确认目标是最近等效角，不是 raw 机器 0 的长距离回转。
13. 在内嵌浏览器 live frame 确认 `急停` 按钮位置正确。
14. 点击 `急停`，采样确认编码器反馈停止、速度归零或趋近归零，并读取 native safety/LinuxCNC machine state。
15. 用内嵌浏览器新 live frame 确认按钮已切换为 `取消急停` 或等价恢复入口。
16. 点击 `取消急停`，确认急停 latch 清除、machine enable actual 恢复。
17. 在内嵌浏览器 live frame 确认 `回零` 按钮位置正确。
18. 再次点击 `回零`，采样确认真实回零运动和最终回零状态；A/C 再次按 360 度最短角差确认等效零。
19. 保存最终内嵌浏览器 live frame，并采集最终 `mcs/cmd_mcs/velocity/seq/valid_mask`、native safety 状态、machine state。
20. 除非现场要求保持使能，否则采证后执行安全停机或急停收尾，确保无人值守时机器不保持可运动状态。

## 通过标准

- 所有 AI UI 输入均有内嵌浏览器点击前/后的 live frame 截图。
- `cc` 是通过真实程序页面双击打开，不是直接写入路径或 direct command。
- 继电器断电重启后，LinuxCNC/微内核恢复完成时必须已经自动 `Machine On`，且右下急停/取消急停按钮状态与 native machine enable actual 一致；若重启后仍是 `MACHINE OFF`，测试失败。
- 第一次回零、`cc.ngc` 启动后的 1 秒运动、急停停止、取消急停后的第二次回零，均有编码器反馈采样证据。
- `cc.ngc` 中 `A0 C0` 或一圈内 A/C 绝对相位目标必须由编码器反馈证明走最近等效角；若采样显示 A/C 向 raw 机器 0 长距离回转，则测试失败。
- 两次 `回零` 必须由编码器反馈证明按 360 度最短角差回到等效零；`359.999`、`-0.001` 这类等效零只能在编码器/native 实际到位且最短角差在容差内时判定通过。
- 急停后底层安全状态确认生效；取消急停后底层 latch 清除并恢复 machine enable actual。
- 最终回零完成后，X/Y/Z/A/C 编码器反馈处于回零完成状态；A/C 按 360 度等效零判定。

## 阻塞条件

- 内嵌浏览器不是 live/ready、无法确认目标位置或页面不正确。
- 继电器断电重启失败，或板端无法恢复 UI/LinuxCNC。
- 断电重启恢复后未自动 `Machine On`，或 machine enable actual 与 UI 右下按钮状态不一致。
- 编码器反馈 `mcs` 无效、stale、seq 不变化，或采样链路中断。
- A/C 编码器反馈无法支持最短角差判断，或无法区分最近等效零与 raw 机器 0 长距离回转。
- 驱动故障、急停无法取消、machine enable 无法恢复。
- 任一阶段出现非预期运动、速度不归零、按钮状态与 native readback 冲突。
