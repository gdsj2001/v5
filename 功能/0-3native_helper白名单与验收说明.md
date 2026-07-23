# Native Helper 白名单与最终边界

> [!IMPORTANT]
> **最高目标：UI 简单直控微内核。**
> 产品控制链只允许 UI C 进程通过 Unix Domain Socket (UDS) 长连接向本机 native command gate 发送极简控制报文；native gate 再复用到板端 `linuxcncrsh` 的单条 TCP 文本连接。外置辅助程序、命令行 `halcmd` 旁路、Python 脚本多级中转、C 端手写 JSON/Broker 胶水均不得作为产品控制路径。机床 UI 显示状态只消费允许的 30Hz SHM 显示投影；按钮、急停显示和其它 UI 刷新分层以 `REQ-UI-RUNTIME-REFRESH-RATE` 为准；控制结果和安全事实以微内核/native owner 为准。

---

## AI 阅读入口

<!-- AI_FAST_READ_BEGIN -->
owner_reqs: []
read_when: [native helper, linuxcncrsh, command gate, UDS request, helper 白名单, 控制旁路]
truth: [UI C -> UDS native command gate -> 单条常驻 linuxcncrsh 连接 -> native owner readback]
forbidden: [halcmd 产品旁路, Python 多级中转, 多 socket 并发, 短生命周期 helper, 未登记程序]
readback: [同一请求的 native response, owner actual, gate health, 顺序一致的 generation]
impact: [UI 控制入口, native gate, helper allow-list, 状态 provider, runtime manifest]
acceptance: [白名单与动作登记一致；产品路径无旁路；动作成功由 native owner 回读证明]
detail_sections: [linuxcncrsh 长连接与高频输入边界, UDS request 强类型校验边界, 2. 状态与通道白名单, 3. 合规边界]
<!-- AI_FAST_READ_END -->

- 启动内存/热路径通用规则：见 `REQ-PARAM-MEMORY-LIGHTWEIGHT-SAVE` / `功能/0-1开机参数入内存.md`，本文只保留 native helper 特有边界。
- native helper 特有边界：linuxcncrsh/native gate、UI 控制入口、allowed helper 表、动作登记表和状态 provider 必须随产品自写运行闭包开机进入 RAM；运行期控制热路径不得临时扫描 helper、懒导入脚本、按文件名执行未登记程序或用旧 wrapper 兜底。
- 修改 helper 白名单时只允许收缩到微内核/native owner 明确需要的长驻 gate 或诊断入口；已退役 helper 不得以 renamed wrapper、环境变量、测试入口或 VM 打包校验形式保留。

---

## linuxcncrsh 长连接与高频输入边界

linuxcncrsh 只解决 LinuxCNC command API 的离散控制入口，不承担实时运动数据通道。为消除“每条命令短连接握手导致 RTT 堆积”的瓶颈，v5 native command gate 必须在启动初始化阶段建立一条到板端 linuxcncrsh 的常驻 TCP 连接，完成一次 hello/握手后复用该连接发送后续 `Set <LinuxCNC command>` 指令。运行期不得为每条 Set 命令重新 connect/handshake/close；连接断开、协议错误或读写失败时，当前命令必须返回明确失败并由 gate 进入受控重连，不能静默丢命令、不能改走第二控制路径。

linuxcncrsh 当前模型是 TCP 文本命令 + 串行请求响应：发送一条 `Set` 指令，等待协议返回，再发送下一条。这个模型适合按钮类命令、状态切换、低频控制和诊断读回；不适合超高频连续下发。

| 风险 | 触发条件 | 要求 |
| --- | --- | --- |
| 时序瓶颈 | 高频命令连续进入同一条 request/response 链路，上一条未返回时下一条只能排队 | 保持离散控制语义；不能把排队后的文本命令当实时周期 |
| 抖动变大 | TCP、linuxcncrsh 文本解析、LinuxCNC 非实时层响应叠加 | 不能用于伺服周期、插补周期、脉冲级控制或微秒/毫秒级低抖动控制 |
| 控制语义错位 | 用 linuxcncrsh 下发连续轨迹点、脉冲、伺服闭环或实时 override 流 | 实时控制必须归 LinuxCNC realtime/HAL/EtherCAT/驱动/FPGA/微内核 native owner |
| 多 socket 乱序 | 试图用多个 linuxcncrsh socket 并发硬冲吞吐 | 禁止连接池并发抢发；命令顺序、互锁和 readback 必须由单队列保持 |

长连接不是连接池。linuxcncrsh 客户端必须保持单连接、单 writer、单 FIFO 命令队列和串行 request/response 确认，保证 `Set Open`、`Set Mode`、`Set Run`、`Set Pause`、`Set Home`、MDI、Work Zero 等命令按 UI/native gate 提交顺序生效。不得用多个 linuxcncrsh socket 并发抢发来绕过串行确认；需要硬实时、低抖动或高频闭环时，责任必须下沉到 LinuxCNC realtime、HAL realtime component、EtherCAT、驱动、FPGA 或微内核/native owner。

| 允许用途 | 禁止用途 |
| --- | --- |
| 运行、暂停、恢复、停止/Abort | 高频轨迹流、逐点轨迹下发、实时插补 |
| 回零、Work Zero、MDI、模式切换 | 脉冲输出、伺服闭环、硬实时安全互锁 |
| 设零、WCS/G92 等离散写入请求；RTCP 仅限已登记 UI latch 或 G-code 程序输出对应的微内核 owner 控制 | 微秒/毫秒级低抖动控制、实时 override 流 |
| 低频倍率请求、低频控制命令、诊断读取 | 用多 socket 并发抢发来绕过串行模型 |

心跳/健康检查只允许作为内存态连接健康事实：socket read/write 错误、协议响应错误、低频空闲探测或 native gate health 可触发重连诊断，但不得写周期性 JSON/ready/perf/heartbeat 文件，不得把心跳结果同步到 SHM，也不得把心跳当运动准入或动作成功证明。

## UDS request 强类型校验边界

UI 到 native command gate 的 UDS 报文必须是登记 opcode、枚举和有限数值字段组成的结构化 request。反序列化阶段必须拒绝未知 opcode、非法轴名、非法 WCS/RTCP/override 枚举、非 finite 数值、超出字段类型范围的整数/浮点数和任何无法格式化为单条登记命令的 payload。

linuxcncrsh 格式化函数只能从已校验的结构化 request 输出有限命令模板，不得拼接未清洗文本，不得让 UI/Broker 直接传入完整 `Set ...` 文本行，也不得在 `v5_linuxcncrsh_client.c` 对整行文本用单一正则当作唯一防线。坐标、速度、限位和比例相关的物理范围必须来自 runtime/native owner 或登记 readback；UI/Broker 不得 invent 物理硬限，也不得把上层参数清洗扩展成运动安全 owner。

UI 高频输入只允许对非运动连续量做合流。进给倍率、主轴倍率这类操作员 slider/knob 连续变化，前端或 native gate 可以按 100ms 窗口 debounce/throttle：拖动开始立即发送一次，拖动中 100ms 内只保留并发送最新值，拖动结束必须 flush 最后值；动作证明和最终显示仍以 LinuxCNC/HAL/native actual readback 或 SHM 允许的倍率显示投影为准。这里的倍率合流只适用于操作员低频调节请求，不允许扩展成伺服周期的实时 override 流。急停、取消急停、Start、Pause/Resume、Abort、Home、Jog 按下/松开边沿、MDI、Work Zero、设零、驱动使能/失能、BUS/Pulse 模式切换、WCS/G92 等一次性控制、安全、运动或坐标写入命令不得 debounce、延迟合流或被倍率队列阻塞；RTCP 按 `REQ-RTCP-NATIVE-STATUS-SOURCE` 使用 UI `native_rtcp_control` latch 和 G-code 程序 `M64/M65` 已登记输出两条直驱微内核链路，也不得 debounce、延迟合流或被倍率队列阻塞。

验收要求：重复下发离散 Set 命令时，必须证明 linuxcncrsh 客户端没有按命令重建 TCP 连接，命令顺序由单 FIFO 队列保持；非法 UDS request、非 finite 数值、未知 opcode 和未登记完整文本命令必须在格式化前失败；倍率连续拖动时，100ms 内后端只收到最新倍率值，拖动结束值必达；急停和其它一次性/安全/运动命令绕过倍率合流队列并保持原生时序。

---

## 1. 架构去中介化规则

1.  **禁止业务层 Python 动作中介**：
    Home、Work Zero、Jog、Start、RTCP/WCS 等控制动作不得经 Python product action 中转；UI C 端必须直达微内核/native gate。
2.  **禁止外置临时 Helper 进程**：
    不得通过编译或执行 `re_hal_safety_helper`、`re_hal_select_helper` 等外部小程序间接读写 HAL。RTCP 有两条直驱微内核链路：UI 按钮通过 `native_rtcp_control` latch，G-code 程序通过程序内 `M64/M65` 已登记输出；两条链路互不代发、互不预置，最后只在同一个 HAL/native switchkins actual 上合流，不得改走外部 helper、MDI 或 linuxcncrsh 普通文本。
3.  **发送与状态显示分域**：
    UI 控制函数只发送请求，不把 SHM、JSON、Broker result 或本地缓存作为动作成功真源。物理动作结果、安全事实和控制拒绝由微内核/native owner 返回或回读；30Hz SHM 只承担允许的 UI 显示投影。

---

## 2. 状态与通道白名单

本表定义产品允许通道和禁止通道。外置 Helper、脚本中介和 C JSON/Broker 胶水不得恢复或改名保留。

| 通道 / Helper 例外 | 源码路径 | 状态 | 最终要求 |
| --- | --- | --- | --- |
| **微内核产品登记合同** | `board/services/microkernel/v5_microkernel_manifest.*`、`v5_native_gate_registry.*`、`v5_native_status_api.*` | **[CANONICAL] owner/gate/status 登记真源** | 开机常驻文件、控制 gate 和非 SHM native readback 必须在这里登记；`board/services/command_gate/` 只实现已登记 adapter，不能另建第二张 allow-list。 |
| **linuxcncrsh native command gate** | 产品登记在 `board/services/microkernel/v5_native_gate_registry.*`；执行在 `board/services/command_gate/v5_command_gate*.c`、`v5_command_table.*`、`v5_linuxcncrsh_*.*`；UI 侧为 `board/app/src/v5_command_*.*` | **[CANONICAL] LinuxCNC 离散命令标准控制路径** | **保留并作为 LinuxCNC 离散命令合规网关**。UI C 进程通过当前 v5 native command gate 发送 `Set Open`、`Set Run`、`Set Mode` 等极简控制报文；gate 启动时建立单条常驻 linuxcncrsh TCP 连接并复用，按单 FIFO 队列串行确认，不按命令短连接，也不使用多 socket 连接池。不进行任何前置 logical precheck，机床动作由微内核硬安全直接拦截或放行。RTCP UI 按钮和 G-code 程序输出按 `REQ-RTCP-NATIVE-STATUS-SOURCE` 使用各自登记的微内核直驱输入，不归并成 linuxcncrsh 普通文本命令。旧 v3 native run 源码只能作只读历史参考，不再作为产品 owner。 |
| **HAL safety helper** | 实时 owner 为 `linuxcnc/src/hal/components/v5_safety_latch.comp`，由 `board/linuxcnc/hal/v5_bus_1ms.hal` 装载；状态合同为 `board/services/microkernel/v5_native_status_api.*`，Command Gate adapter 为 `board/services/command_gate/v5_native_safety.*`、`v5_native_readback.*` | **[FORBIDDEN] 禁止外置 helper** | **不得恢复历史 helper**。UI 不得通过外部程序拉取 `cia402.N.stat-op-enabled` 等状态。使能与驱动运行证据只能来自登记的微内核/native 状态直连读取；SHM 只作允许显示投影。 |
| **HAL select helper** | gate/status 登记在 `board/services/microkernel/v5_native_gate_registry.*`、`v5_native_status_api.*`；RTCP actual owner 为 `linuxcnc/src/hal/user_comps/v5_native_hal_owner.comp`，由 `board/linuxcnc/hal/v5_bus_1ms.hal` 装载；控制/读取 adapter 为 `board/services/command_gate/v5_native_rtcp_control.*`、`v5_native_rtcp_status.*` | **[FORBIDDEN] 禁止外置 helper** | **不得恢复历史 helper**。RTCP/switchkins 的 ON/OFF 状态直接作为运行期 actual；UI 链路只能通过 `native_rtcp_control` latch 直驱微内核，G-code 链路只能通过程序内 `M64/M65` 已登记输出直驱微内核，两者互不代发、互不预置，最后由 HAL/native owner 合成为同一个 switchkins actual。禁止外部引脚写盘、helper 中转、UI MDI 文本或 linuxcncrsh 普通文本替代。 |
| **backend realtime cleanup** | `board/services/command_gate/init.d/v5-linuxcnc-command-gate` 的受控启动/停止闭包 | **[INTERNAL] 后端自用** | 仅作为后台系统拉起前的 LinuxCNC 进程及实时层脏状态自愈清理使用，禁止向 UI 进程或普通控制热路径暴露任何接口；历史 v3 backend lifecycle 脚本已删除且不得恢复。 |

---

## 3. 合规边界

*   **源码要求**：
    product Python 或普通 C UI 中不得出现未登记的 `halcmd`、`subprocess.run` 或直接 `import linuxcnc` 控制路径；不得出现 UI/Broker 传完整 linuxcncrsh 文本行、格式化阶段拼接未校验自由文本、或用整行正则替代结构化 request 校验的路径。
*   **禁止保留要求**：
    Helper 源码、编译目标、VM 打包校验和部署入口不得存在。RTCP、Home、WCS 等功能只能使用 UDS/native/LinuxCNC/微内核 canonical 实现；发现 helper、脚本中介、C JSON/Broker direct 胶水或等价 fallback survivor 时，必须在同一切片物理删除。
