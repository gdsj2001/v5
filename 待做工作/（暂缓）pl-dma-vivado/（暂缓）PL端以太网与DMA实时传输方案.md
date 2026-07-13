# （暂缓）PL 端以太网与 DMA 实时传输方案

Updated: 2026-06-28

本文只记录 PL Ethernet + DMA 方向的暂缓边界和未来恢复条件。它不是当前产品实现说明，不是当前 Vivado 构建入口，也不是可以替换 EtherCAT/LinuxCNC/HAL 的交付结论。

当前仓库口径：

- 当前产品仍走 `UI C -> Command Broker UDS -> product action/native gate -> LinuxCNC/HAL/EtherCAT` 控制链。
- 当前状态仍走 `v3_state_publisher.py -> typed SHM -> UI reader` 状态链。
- 当前运动真相仍是 LinuxCNC/HAL/EtherCAT。
- `vivado_hw_project` 是当前 Vivado 工程口径；`待做工作/（暂缓）pl-dma-vivado` 下的 PL/DMA 实验方向保持暂缓。

## 1. 当前结论

PL Ethernet + DMA 暂缓产品化推进。

默认结论：

- 不综合、不实现、不 bitgen、不导出 XSA、不打包启动镜像。
- 不同步 VM/板端，不部署实验 bit，不把旧实验输出当当前产品真源。
- 不替换当前 PS GEM/macb/EtherCAT 链路。
- 不接入 LinuxCNC/HAL 运动 authority。
- 不进入 release profile。

只有用户明确重新开启 PL/DMA 实验方向时，才允许按本文恢复；恢复时也只能 one-shot、可回滚、默认产品链路不动。

## 2. 禁止误写

后续文档、代码或结论不得在无新证据时写成：

- 当前产品已经有 PL Ethernet DMA 实时通道。
- 当前 eth1 是 PL AXI Ethernet MAC 或独立 PL NIC。
- PL/DMA 工程可作为当前 Vivado 工程、构建入口、同步输入、板端部署输入或 release 输入。
- H3 netdev 已通过。
- 新 PL bit 可进入默认 `image.ub`、`BOOT.BIN`、`boot.scr` 或 release。
- PL 网口可替换 PS GEM/macb/EtherCAT。
- 板端已证明 15us、30us 或微秒级抖动。
- Zynq-7000 当前板端有 CPU3 可隔离。
- PL DMA 可替代 LinuxCNC/HAL/EtherCAT 的运动控制。
- PL 数据包可直接写 HAL pin、驱动状态、STEP/DIR 或 CiA402 目标。
- PL 通道成功等于 Home/Jog/Start/E-stop/RTCP/WCS 等产品功能通过。

正确口径：PL Ethernet DMA 只能是未来硬件传输、隔离、时间戳和队列能力；它必须先完成独立硬件原型、Linux netdev/driver bring-up、DMA ABI、测量证据、fail-closed 测试和回滚证明，才能讨论低风险产品接入。

## 3. 已知事实

这些事实只作为当前仓库记录，恢复实验前必须重新核对。

| 项目 | 当前口径 |
| --- | --- |
| 当前 eth1 | PS GEM1 / macb / EtherCAT 使用链路，不按已投产 PL AXI Ethernet 处理 |
| 当前运动链 | LinuxCNC/HAL/EtherCAT 仍是最终 truth |
| 当前 CPU 口径 | Zynq-7000 双核；不得写 CPU3 隔离 |
| H3 状态 | 未通过；缺独立 `xilinx_axienet` netdev 的 shell/sysfs/IRQ/counter 证据 |
| 旧实验风险 | 旧 H0 bit 曾出现 kernel early boot 卡住；不能写成单纯 DTS probe 问题 |
| 旧分支状态 | In13/offset57 只是未验证实验分支，不是 H3 完成 |

任何新结论必须重新采集 Vivado/XSA/DTS/板端 sysfs/IRQ/counter/boot 证据。

## 4. 未来目标

未来若恢复，目标不是让 PL 接管运动，而是建立可测量、可回滚、fail-closed 的底层传输能力：

```text
Ethernet PHY
  -> PL Ethernet MAC / verified packet ingress
  -> packet classifier
  -> AXI Stream FIFO
  -> AXI DMA RX/TX rings
  -> kernel driver or controlled mmap interface
  -> resident user-space adapter
  -> existing Broker / State Publisher contracts
  -> LinuxCNC/HAL/EtherCAT validation
```

允许承载：

- remote UI framebuffer delta 或诊断流量。
- 可信状态 owner 产生的状态镜像。
- Broker framed command 的传输层优化，前提是保留 `request_id`、`status_epoch`、typed 参数、stale 拒绝和 per-client response。
- heartbeat、watchdog、stop intent 的低延迟传输，前提是最终仍由 Broker/HAL/LinuxCNC 安全链确认。

禁止承载：

- 绕过 LinuxCNC/HAL 的真实运动目标。
- 绕过 Broker 串行域的 Start/Jog/Home/E-stop/WCS/RTCP/axis zero。
- 直接把以太网包翻译成 HAL 写操作。
- 直接向驱动写位置、速度、设零、rebase 或 CiA402 状态。
- 用 PL 里的角度取模替代 A/C 连续多圈运动真相。

## 5. 队列原则

若恢复设计，第一版 ABI 就必须区分队列，不能让 UI 大帧阻塞急停/心跳。

| 队列 | 内容 | 规则 |
| --- | --- | --- |
| Q0 urgent stop/heartbeat | stop intent、watchdog、alive | 最高优先级；丢失即 fail-closed；不得被 UI bulk 阻塞 |
| Q1 command request | Broker framed request | 保留串行域、epoch、request_id；stale 拒绝 |
| Q2 state stream | 状态镜像、telemetry | 可降采样；不得成为 UI 最终 truth 的旁路 |
| Q3 UI bulk | framebuffer delta、大诊断包 | 可丢弃、可限速；不得阻塞 Q0/Q1 |

## 6. 恢复阶段

恢复时只能按阶段推进；任一阶段未通过，不得提前进入后续阶段。

| 阶段 | 目标 | 关闭条件 |
| --- | --- | --- |
| H0 事实重查 | 重新确认当前硬件、Vivado、DTS、启动链、eth1、EtherCAT、CPU、回滚路径 | 有板端和仓库证据；默认产品链路未变 |
| H1 ABI 设计 | 定义 ring、descriptor、队列、header、CRC、epoch、fail-closed 语义 | 文档和本地契约测试通过；未接产品运动 |
| H2 Vivado 原型 | 独立实验 bit，可 one-shot 加载和回滚 | bit/hash/manifest/rollback 完整；默认 image 不变 |
| H3 netdev bring-up | Linux 看到独立 PL netdev/driver/IRQ/counter | shell/sysfs/IRQ/counter/up-down 证据齐全 |
| H4 可靠性测量 | 测吞吐、丢包、延迟、抖动、队列隔离 | 有 JSON/日志/图表证据，且不破坏 EtherCAT |
| H5 shadow mode | 只读镜像接入现有 Broker/State Publisher 合同 | 不参与 authority；断链 fail-closed |
| H6 低风险产品接入 | 只接诊断或 UI bulk 等低风险流量 | 可回滚；不影响 Home/Jog/Start/E-stop |
| H7 EtherCAT 候选评估 | 仅评估，不替换 | 有独立安全论证和板端证据后再决策 |
| H8 release 清理 | 若最终采用，清理旧实验和文档 | release profile、回滚、测试和文档闭合 |

## 7. 启动链和回滚

恢复实验时必须保持默认启动链可回滚：

- 实验 bit 只能 one-shot 或独立 profile 加载。
- 不覆盖默认 `image.ub`、`BOOT.BIN`、`boot.scr`，除非该阶段目标就是启动链验证且已备份可回滚。
- 开始前记录当前启动文件 hash、板端分区、继电器恢复方法、COM/serial 观察方式。
- 板端失联时先走继电器重启和 COM 诊断；不得靠猜测继续部署。
- 实验失败后必须能回到默认产品链路并证明 eth1/EtherCAT/remote relay/UI 状态正常。

## 8. 验证口径

不得把单一证据扩大解释。

| 证据 | 能证明 | 不能证明 |
| --- | --- | --- |
| Vivado bit 生成 | 原型可构建 | 板端可启动、netdev 可用、产品可接入 |
| one-shot bit 加载 | 实验 bit 可加载 | 可进入默认启动或 release |
| shell 看到 netdev | H3 的一部分 | 延迟、可靠性、运动安全 |
| DMA counter 增长 | 数据路径有活动 | Broker/SHM/运动链已闭合 |
| 延迟测试通过 | H4 指标 | Home/Jog/Start/E-stop 功能通过 |
| shadow mode 正常 | 可只读观察 | 可写 HAL、驱动或运动目标 |

声明任何产品功能通过，仍必须回到对应功能的原始 UI/operator 路径、板端证据和运动闭环。

## 9. 失败规则

以下情况立即停止当前 PL/DMA 实验：

- 默认启动链被破坏或板端无法稳定回默认 SD。
- eth1/EtherCAT、remote relay、UI 或 SSH 受到影响。
- 新 bit 导致 kernel early boot 卡住、panic 或无法进入 shell。
- PL netdev/IRQ/counter 证据缺失，却开始接产品路径。
- 任何代码绕过 Broker、State Publisher、LinuxCNC/HAL/EtherCAT 安全边界。
- 无法解释的运动、急停、使能、回零或 Jog 异常。

停止后只保留简要结论、证据位置、回滚结果和下次恢复条件；不写长过程流水。

## 10. 关联入口

恢复前至少重读：

- `AGENTS.md`
- `功能/系统代码架构硬边界守则.md`
- `功能/项目软硬件架构和后期修改指导说明.md`
- `CMV自动化体系.md`
- `功能/需求真源索引.md`
- `待做工作/脉冲总线切换.md`
- `待做工作/遗留.md`

## 11. 最终判断

PL Ethernet + DMA 可以作为未来硬件传输方向保留，但当前不属于产品主线。现阶段最重要的是防止误用：不要把暂缓实验当当前工程，不要把传输通道当运动 truth，不要把实验 bit 进入默认启动或 release。
