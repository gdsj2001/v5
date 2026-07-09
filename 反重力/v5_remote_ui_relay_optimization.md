# v5_remote_ui_relay CPU 收口作业指导书

## 1. 作业目标

本作业只处理远程画面 relay 链路的 CPU 占用、dirty rect 传输和板端验收闭环。当前代码已经不是旧的逐行 `seek/read` 方案，`v5_remote_ui_relay.py` 已经使用 framebuffer `mmap`、预分配 payload、`memoryview` 拷贝、多 dirty rect 合并和 diagnostics counter。本文件不再作为“重写为 mmap”的方案文档，而作为后续审查、修复和验收的作业指导书。

最终结论只能按实际验证标记：

- `source_only`：只改了文档或源码，未跑本地有效门禁。
- `local_verified_only`：本地静态/单元/烟测通过，未完成板端原路径验证。
- `board_verified`：Windows truth 同步到 VM、完整板端构建/部署/重启后，在 Windows 远程画面保持长连接的状态下完成同窗口 CPU 与 relay diagnostics 验收。
- `blocked`：板端、远程端或必要诊断不可用，且无法安全继续。

## 2. 需求 owner

改动前必须先核对 owner：

- 远程投屏边界：`功能/0-4共享内存.md`。
- CPU 调度和诊断边界：`功能/0-1开机参数入内存.md`。

如果本文件、旧报告、代码注释或测试与 owner 冲突，以 `功能/` owner 为准；确实需要改产品要求时，先改 owner，再改代码。本文件只记录作业步骤，不记录完成进度、板端证据或聊天结论。

## 3. 当前代码基线

当前支持路径如下：

- UI 进程只写 `/run/8ax_v5_product_ui/remote_framebuffer.bgra` 和 `remote_dirty`，不监听 `18080`，不解析 HTTP/WebSocket，不在 UI 主线程发送网络帧。
- `board/services/ui/v5_remote_ui_relay.py` 是唯一远程画面/输入网络 relay，提供 `/remote/info`、`/remote/frame/full`、`WS /remote/stream`、`WS /remote/input` 和 `/remote/diagnostics`。
- `FrameState.framebuffer_map()` 按 framebuffer 文件 identity/size 管理只读 `mmap`，并通过 `framebuffer_mmap_refreshes` 计数证明 relay 读端走 mmap。
- `FrameState.dirty_payload()` 接收一个或多个 dirty rect，预分配 `bytearray(total_bytes)`，用 `memoryview` 从 mmap 拷贝；全宽连续区域走连续拷贝快路径，非连续区域按行拷贝。
- `wait_dirty_batch_after()` 和 `coalesce_dirty_events()` 在 stream 路径中合并 dirty 通知；正常情况下优先保留多个原始 rect，只有 rect 数超过上限时才退化为 bounded union rect。
- `v5_remote_ui_relay_coalesce_smoke.py` 是当前本地 focused smoke，用于验证 CPU sampler、dirty coalesce、多 rect payload、mmap 计数和 missing-dirty repair 信号。

旧瓶颈代码特征是：每个 dirty rect 按行打开文件、`seek`、`read`、`payload.extend()`。该路径不得在产品 relay 中恢复。

## 4. 作业入口判断

开始作业时先判断问题属于哪一类：

1. 远程端打开但 CPU 不高：不得把 `stream_sessions > 0` 解释成高 CPU 根因。
2. CPU 高且 dirty/full-frame/payload 计数同步增长：优先查 UI dirty 区域过大、dirty 频率过高、coalesce 失效、full-frame repair/polling 或 payload 拷贝路径。
3. CPU 高但 relay diagnostics 没有增长：优先查 LinuxCNC/RTAPI、State Publisher、WCS publisher、SSH/诊断采样、一次性验证脚本、启动/重启瞬时负载或其它进程。
4. 远程画面卡住或丢帧：查 `base_frame_id` 连续性、dirty history 窗口、repair full-frame 计数、WebSocket 断连和 Windows 端是否有 polling fallback。

## 5. 修改边界

允许修改：

- `board/services/ui/v5_remote_ui_relay.py` 中的 dirty 读取、coalesce、payload 组包、diagnostics、异常客户端清理和 relay CPU 控制。
- `board/services/ui/v5_remote_ui_relay_coalesce_smoke.py` 等 focused relay 测试。
- Windows 远程端中违反 initial/repair full-frame 规则、导致 polling fallback 的实现。
- UI 侧 dirty 区域生成逻辑，但只能保持 mmap/FIFO IPC，不得把网络逻辑带回 UI 进程。

禁止修改：

- 禁止通过断开 Windows 远程端、停止 `v5-ui-relay`、关闭 `/remote/stream`、改成手动刷新或删除远程输入来降低 CPU。
- 禁止让 UI 进程监听 `18080` 或解析 HTTP/WebSocket。
- 禁止恢复逐行文件 `seek/read` dirty payload 路径。
- 禁止把 LinuxCNC/RTAPI realtime/servo 从 CPU0 挪走来掩盖 relay 问题。
- 禁止用单次 `ps %CPU`、`top` 瞬时排序、SSH/dropbear 或验证脚本瞬时负载作为常驻 CPU 结论。

## 6. 本地检查步骤

1. 静态检查产品 relay 是否仍满足单一路径：
   - UI 代码不得监听 `18080`。
   - 产品 relay 中不得出现 dirty payload 热路径的 `framebuffer_path.open("rb")`、逐行 `seek()`、逐行 `read()`。
   - `/remote/frame/full` 只能作为 HTTP full-frame 或 stream initial/repair 使用，不能作为 steady-state polling 依赖。
2. 运行 focused smoke：
   - `v5_remote_ui_relay_coalesce_smoke.py` 必须通过。
   - 失败时只修对应单点，不扩大到无关 UI 或 LinuxCNC 链路。
3. 检查 diagnostics key：
   - `full_frame_requests`
   - `stream_sessions`
   - `stream_initial_full_frames`
   - `stream_repair_full_frames`
   - `stream_repair_missing_dirty_events`
   - `dirty_events`
   - `dirty_coalesced_events`
   - `dirty_rect_frames`
   - `dirty_payload_bytes`
   - `dirty_payload_rows`
   - `dirty_payload_rects`
   - `dirty_payload_contiguous_frames`
   - `framebuffer_mmap_refreshes`
   - `input_sessions`
   - `input_messages`
   - `input_accepted`
   - `input_rejected`

## 7. 板端验收步骤

板端验收必须使用同一采样窗口，不得把不同时间的数字拼成因果结论。

1. 从 Windows truth 同步到 VM，完整构建当前 board source，部署到板端，并重启相关服务或执行 canonical restart。
2. 确认服务归属：
   - `18080` listener 必须属于 `v5_remote_ui_relay.py`，不是 `v5_lvgl_shell`。
   - `v5_lvgl_shell` 只持有本地 framebuffer/FIFO 链路。
3. 打开 Windows 远程画面并保持长连接，确认 `/remote/info`、`WS /remote/stream` 和远程输入可用。
4. 取同一时间窗口内的数据：
   - `/proc/stat` 的 CPU0/CPU1 busy delta。
   - relay 进程和线程 CPU ticks delta。
   - `/proc/<pid>/task/<tid>/status` 的 affinity readback。
   - `/remote/diagnostics` metrics delta。
5. 判断远程链路是否是 CPU 来源：
   - 若 `stream_sessions > 0`，但 `dirty_events`、`dirty_rect_frames`、`dirty_payload_bytes`、`full_frame_requests`、`stream_repair_full_frames` 都没有增长，远程长连接不是当前高 CPU 根因。
   - 若 dirty/payload 持续增长，继续查 dirty 区域、coalesce、payload 大小和 UI 重绘来源。
   - 若 full-frame 计数持续增长，必须定位 initial/repair 以外的 polling 或 repair 风暴。
6. 只有 Windows 远程画面不断开、远程输入可用、full-frame 仅出现在 initial/repair、relay metrics 与 CPU delta 同窗口闭合后，才能报告 `board_verified`。

## 8. 预期修复方向

按优先级处理：

1. 删除 full-frame steady-state polling 或 Windows 端 fallback。
2. 收敛 UI dirty 区域，避免整屏/整页周期性刷新。
3. 保持 33ms coalesce 窗口，优先多 rect payload，只有异常多 rect 才 bounded union。
4. 对大 dirty rect 做限频或拆分策略，但不得伪造成 full-frame polling。
5. 保持 mmap 读端和预分配 payload；如果必须 remap，保证 `memoryview` 生命周期在本次组包内释放，避免 `BufferError` 导致旧 map 残留。
6. 普通 relay、UI、publisher 进程不得主动绑定或抢占 CPU0；CPU0 优先留给 LinuxCNC/RTAPI realtime/servo。

## 9. 交付口径

最终回复必须说明：

- 改了哪些文件。
- 跑了哪些本地门禁。
- 是否完成板端构建/部署/原路径验收。
- CPU 结论基于哪一个同窗口采样。
- 如果没有板端验收，只能报 `source_only` 或 `local_verified_only`，不得写 `board_verified`。
