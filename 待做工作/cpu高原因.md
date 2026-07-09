# CPU 高原因与后续代码边界

记录日期：2026-07-09

引用需求真源：`REQ-GCODE-RUN-HOT-PATH` / `功能/0-1开机参数入内存.md`，`REQ-REMOTE-DISPLAY-RELAY-DECOUPLING` / `功能/0-4共享内存.md`。

## 1. 结论修正

远程端打开本身不是 CPU 高的充分原因。必须区分：

- `stream_sessions > 0`：只说明 Windows 远程端连接存在。
- `dirty_events`、`dirty_rect_frames`、`dirty_payload_bytes`、`full_frame_requests`、`stream_repair_full_frames` 在同一采样窗口持续增长：才说明远程显示链路正在产生实际 CPU/网络负载。

之前把“远程端打开”和“远程画面流有实际负载”混成一个因果，这是错误归因。后续判断 CPU 高必须用同一时间窗口的 `/proc/stat`、线程 CPU ticks、服务 diagnostics 增量和 affinity readback，而不是单次 `ps %CPU` 或是否打开远程端。

## 2. 以前 CPU 为什么高

### 2.1 CPU1 高的那轮

当时 CPU1 接近满载，主要进程为：

| 进程/线程 | 位置 | 占用特征 |
| --- | --- | ---: |
| `v5_remote_ui_relay.py` worker | CPU1 | 约 `32% - 42%` |
| `v5_lvgl_shell --serve` | CPU1 | 约 `24% - 27%` |
| `rtapi_app:T#0` | CPU1 | 约 `14% - 18%` |
| `v5_native_safety_latch_owner.py --interval-ms 2` | CPU1/CPU0 漂移 | 约 `9%` |
| `v5_wcs_status_publisher.py` | CPU1 | 约 `7% - 8%` |

当时高的真实原因是多个因素叠加：

1. LinuxCNC/RTAPI realtime 线程还没有固定到 CPU0，`rtapi_app:T#0` 出现在 CPU1，和 UI/relay/publisher 叠加。
2. 远程显示链路在当时存在实际 dirty 推送和 payload 处理，relay 和 LVGL 都在 CPU1 上消耗明显。
3. `v5_wcs_status_publisher.py`、`v5_state_publisher`、`v5_rtcp_status_publisher.py`、`v5_g53_geometry_memory_owner.py` 等 publisher 与 UI/relay 叠加。
4. 采样期间有 SSH、verify、one-shot publisher、`halcmd`、重启/状态检查等诊断扰动，不能全部算作稳定空闲基线。

### 2.2 CPU0 高的那轮

把 LinuxCNC/RTAPI 固定到 CPU0 后，CPU0 的主要负载变为实时链路基线：

| 进程/线程 | 原因 |
| --- | --- |
| `rtapi_app:T#0` | 2ms LinuxCNC servo-thread 常驻执行 |
| `v5_native_safety_latch_owner.py --interval-ms 2` | native safety 2ms 轮询 |
| `EtherCAT-OP`、`lcec.read-all/write-all`、`cia402.*` | EtherCAT/CiA402 周期扫描 |
| `irq/30-eth%d`、`irq/31-eth%d`、`ksoftirqd/0` | 网卡 IRQ/softirq 在 CPU0 |
| `milltask` | LinuxCNC task 常驻 |

`MACHINE OFF` 不等于实时链路停止。LinuxCNC servo-thread、EtherCAT read/write、安全轮询和网卡 IRQ 在空闲时仍然会跑。CPU0 在这类窗口出现 `30% - 45%` 忙，不应自动判断为异常；要看同窗口线程增量和是否有额外诊断/重启/测试 sampler。

## 3. 现在为什么 CPU 不高

当前远程端仍打开，但 CPU 不高，原因是这段窗口没有远程显示实际负载增长。

最近 3 秒窗口证据：

```text
cpu0 busy=8.3%
cpu1 busy=25.7%
```

远程 metrics 同窗口增量：

```text
stream_sessions=1
dirty_events 157 -> 157 delta=0
dirty_rect_frames 0 -> 0 delta=0
dirty_payload_bytes 0 -> 0 delta=0
full_frame_requests 5 -> 5 delta=0
stream_repair_full_frames 0 -> 0 delta=0
```

这说明 Windows 远程端连接存在，但画面没有 dirty payload、full-frame 或 repair 增量，所以远程链路没有形成高负载。

同一窗口线程增量显示，当前剩余负载主要是稳定基线：

| 线程 | 最近窗口占用 |
| --- | ---: |
| `rtapi_app:T#0` | 约 `12%` |
| `v5_native_safety_latch_owner.py` | 约 `9%` |
| `v5_wcs_status_publisher.py` | 约 `4% - 5%` |
| `irq/31-eth%d`、`ksoftirqd/0`、`milltask` | 各约 `2% - 3%` |

当前看到的 `python3 -`、`dropbear` 属于诊断命令自身开销，不能算作产品常驻负载。

## 4. 后续代码边界要求

### 4.1 CPU 归因边界

- 禁止用“远程端打开”直接解释 CPU 高；必须看同窗口 dirty/full-frame/payload 增量。
- 禁止用单次 `ps %CPU` 或 `top` 排序作为根因结论；必须用 `/proc/stat` + 线程 CPU ticks 增量。
- 报告 CPU 问题时必须剔除 SSH/dropbear、verify、one-shot publisher、`halcmd`、测试 sampler、重启过程等诊断扰动。
- CPU 结论必须同时记录：采样秒数、每核 busy、线程增量、`Cpus_allowed_list`、remote diagnostics delta、LinuxCNC `MACHINE` 状态。

### 4.2 远程显示边界

- Windows 端远程画面是常驻链路，不能作为 CPU 优化被关闭。
- `v5-ui-relay`、`/remote/stream`、远程输入不能被停掉、降级成手动刷新或用断连当作修复方案。
- full-frame 只允许 initial/repair；稳态不得靠 full-frame polling 维持画面。
- relay 必须保留 diagnostics counter：`stream_sessions`、`dirty_events`、`dirty_rect_frames`、`dirty_payload_bytes`、`full_frame_requests`、`stream_repair_full_frames`、`input_sessions`。
- dirty rect 处理必须按 33ms 合并窗口、bounded payload 和必要限频设计；不得让远程连接数量或 WebSocket 就绪状态拖 UI 主循环 busy loop。

### 4.3 实时链路边界

- LinuxCNC/RTAPI realtime/servo 线程必须固定到 CPU0，并用 `/proc/<pid>/task/<tid>/status` 的 `Cpus_allowed_list=0` 验收。
- UI、relay、State Publisher、WCS/RTCP/G53 publisher、settings actiond、诊断脚本不得主动绑定或抢占 CPU0。
- 不得通过停止 LinuxCNC servo、降低 EtherCAT 周期、削弱 native safety 2ms 安全链路、删除 readback 或关闭远程画面来“降低 CPU”。
- 如果未来 CPU0 在真实运动负载下接近满载，要按 `功能/0-1开机参数入内存.md` 更新 owner 后再评估微内核/native realtime/servo owner 是否扩展使用 CPU1；不得临时把普通 UI/诊断和实时链路混跑成第二套 owner。

### 4.4 测试与诊断边界

- 长时间测试 sampler，例如 `/tmp/v5_runcc_sampler.py --duration ... --dt 0.05`，结束后必须确认退出；不得让测试采样残留污染 CPU 基线。
- board verify、one-shot publisher 和调试命令产生的负载只能作为诊断窗口，不得作为稳定空闲基线。
- CPU 优化验收必须在远程端保持打开的状态下完成，并记录远程 metrics delta；不能用关闭远程端后的低 CPU 作为通过证据。

## 5. 当前待改进方向

1. 继续优化 `v5_remote_ui_relay.py`：保留 mmap/低拷贝、dirty 合并和 payload 计数，避免 Python 逐行 framebuffer 读取回归。
2. 继续观察 `v5_native_safety_latch_owner.py --interval-ms 2` 的 2ms Python 轮询成本；如果要优化，必须下沉到 native/realtime owner 或证明不削弱安全链路。
3. 评估 WCS/RTCP/G53 publisher 的 200ms 轮询成本，优先事件化、限时采样或减少不必要 work；不得提权到 `SCHED_FIFO` 或绑定 CPU0。
4. 保留 RTAPI CPU0 readback gate，防止未来重启脚本、部署脚本或 init 改动让 realtime 线程再次漂移到 CPU1。
