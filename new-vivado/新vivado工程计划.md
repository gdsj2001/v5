# 新 Vivado 工程计划

## 验收硬标准

为了彻底防范间歇性/偶发性故障，保障功能安全边界，硬件工程交付必须卡住以下 **3条简要硬标准**；`WNS >= 0.1ns` 仅作为裕量观察/优化目标，不再作为硬失败门槛：

1. **时序硬指标（拒绝负裕量）**：
   - 硬门槛为最终 `report_timing_summary`/timing history 显示 `TIMING_STATUS=timing_met`，即 WNS/WHS 非负且 TNS/THS 为 0；`WNS >= 0.1ns` 不再卡死交付，只记录为建议裕量目标。
   - 2026-07-01 时序复核：已对 `clk_wiz_0` DRP 配置寄存器端点增加精确 `set_false_path`，`MAXIGP0ACLK -> clk_wiz_0/CLK_CORE_DRP_I` 不再是真实 top WNS；`axi_stepdir_enc_v2` 已拆分 `GLOBAL_APPLY` 启动准备，并在本轮把 8 轴 quiet/done 归约改为寄存器级 pipeline，切断 `space_cnt -> slice_seq_done` 负裕量路径。本轮 180 项 IO/IO-owner/RS485/TP 更新后已重新完成 synth/impl/bitstream/XSA，最终 `TIMING_STATUS=timing_met`、`WNS=0.193ns`、`WHS=0.034ns`，`WNS >= 0.1ns` 裕量观察目标当前满足。
   - `auto_cc` 异步 FIFO `-datapath_only max_delay` 或 AXI/auto_cc Pblock 只允许在 `report_timing_summary` 连续证明真实 top worst path 落在 `auto_cc` 后再实施；当前证据显示该建议不是主解法，盲目扩大 CDC 例外或锁 AXI 区域会掩盖真实 `step_ip` 时序问题。
   - `TIMING_STATUS` 必须显示为 `timing_met`（当前已通过）。
2. **警告受控指标（拒绝漏网）**：
   - 意外警告数（`unexpected_warning_codes`）**必须精确为 `0`**。所有剩余常规警告必须进入白名单审计报告并明确指派 Owner。
3. **代码侧复查指标（取消实际测量 gate）**：
   - 当前计划不再要求 A11/BV01-BV14 现场示波器、板上实测或 `board_verified` 证据作为交付门槛。
   - 当前验收只从 Vivado 代码侧复查：RTL、BD、active XDC、仿真、timing、warning summary、XSA/manifest、portability、PL 急停 AXI register map 和 handoff gate。
   - 代码侧复查只能支持 `local_verified_only` / `code_review_only` 结论；不得据此声明板上安全功能已实测、已认证或可上线。

---

## 目的

- 在 `new-vivado/` 下创建一个全新的、独立的 Vivado 工程，用于 Z20 v1.5 板卡硬件工程迁移和验证准备。
- 复用 `vivado_hw_project/` 老工程里已经能工作的 BD、RTL、本地 IP、HLS frozen RTL 和构建脚本结构，避免从零重建风险。
- 新工程必须使用 `new-vivado/z20-v1_5_20260623.xdc` 作为 Z20 v1.5 板卡约束来源；旧工程 `system.xdc` 只能用于对照，不能作为新工程 active 约束加载。
- 保留 `vivado_hw_project/` 老工程作为只读、可回退、可对照的基线；所有新实验、迁移、报告和产物都在新目录完成。

## 边界

- 只允许在 `new-vivado/` 及其子目录内创建、复制、修改新工程文件。
- 不修改 `vivado_hw_project/` 老工程任何文件，不在老工程目录里生成 Vivado 输出、缓存、日志或临时文件。
- 所有项目内路径必须相对 `PROJECT_ROOT` 书写；文档、`.xpr`、`.bd`、`.xci`、Tcl、XDC、README 和脚本示例中不得固化盘符绝对路径。
- 新工程不能依赖 `vivado_hw_project/` 的绝对路径或相对路径；`.xpr`、BD、XCI、IP repo、RTL、脚本和约束引用必须全部指向 `new-vivado/` 内的文件。
- 新工程必须加载 `new-vivado/z20-v1_5_20260623.xdc` 或同目录内由它逐行派生并明确标注来源的 wrapper 适配版；不得加载老工程 `system.xdc`。
- 如当前 wrapper 端口名与 `z20-v1_5_20260623.xdc` 原理图网络名不一致，必须在新工程内改 wrapper/BD 端口或生成清晰可追溯的适配约束，不能回退引用老工程约束。
- 未确认的脚位、复用冲突、方向不明信号和轴数差异必须保持 fail-closed：先注释或标记待复核，不能驱动真实硬件。
- 本计划不包含板上部署、LinuxCNC/HAL 行为修改、UI 修改、运动验证或发布结论；Vivado 新工程通过构建前，状态只能是准备阶段或本地工程阶段。
- Vivado 生成目录、缓存、run 输出、bitstream、XSA 和报告是产物，不作为源文件混入工程源结构。

## 项目规则落实

- 执行本计划前必须先读 `AGENTS.md`，再看当前 `git status` 和相关 diff；不能从旧聊天、旧记忆或旧目录假设当前规则。
- 当前用户最新要求优先：新工程必须独立、只用相对路径、使用 `new-vivado/z20-v1_5_20260623.xdc`，任何旧文档或旧工程做法与此冲突时都按本计划更新。
- 本计划只是 `new-vivado/` 下的执行计划，不是跨功能需求真源；不得在这里新增 LinuxCNC/HAL、UI、运动控制或系统架构规则。
- 不创建或更新 `process.md`、`过程.md`、任务看板、并行 AI 看板或锁文件；必要证据只放报告、diff、最终回复或已有 backlog 位置。
- 编辑前备份将要改动的新工程文件到 `repo_ignored/vivado_new_project_plan/backup/`，但备份不得成为工程依赖。
- 保护用户工作：已有文件先盘点再处理，不用删除重建来掩盖半成品状态，不回滚无关改动。
- 生成物和 scratch 输出必须留在 `new-vivado/z20_v1_5_hw_project/` 或 `repo_ignored/vivado_new_project_plan/` 下，不能散落到项目外。
- 只有实际跑过的验证才能写入结论；未运行 Vivado、未生成 bit/XSA、未板上验证时，状态分别只能写为准备阶段、`source_only` 或 `local_verified_only`。
- 任何板上可用、运动正确、已修复、已验证、release-ready 之类结论，都必须另按 `AGENTS.md` 的板级闭环规则执行后才能说。
- 若后续改到 `AGENTS.md`、`功能/`、架构文档或 `待做工作/改进.md`，必须额外运行 doc single-source audit；本计划自身修改至少要运行文本 sanity gate。

## 当前输入

- 老工程参考：`vivado_hw_project/`
- 老工程当前约束：`vivado_hw_project/vivado_hw_project.srcs/constrs_1/system.xdc`，仅供人工对照，不得被新工程加载。
- 老工程顶层 wrapper：`vivado_hw_project/vivado_hw_project.srcs/sources_1/imports/hdl/system_wrapper.v`
- 新工程指定约束：`new-vivado/z20-v1_5_20260623.xdc`
- ADC 最终需求：模拟量只有 1 路 0-10V 输入，使用 Zynq 芯片自带 XADC 模拟量脚。当前按 `XADC_VP=L11`、`XADC_VN=M12` 作为一路 XADC 模拟输入处理；前端保护、分压和滤波在硬件原理图落实，Vivado 侧只负责 XADC 接入、约束/说明和软件 handoff。
- ADC 当前状态：当前 `new-vivado/z20-v1_5_20260623.xdc` 的 ADC 段写成 XADC 单通道，和 1 路模拟量需求一致；`ADC_IN2` 和外部 SPI ADC 方案不再作为当前需求。
- ADC 防回退要求：后续脚本、handoff 和文档不得再把 `U10/U9/AA12/AB12` 写成外部 ADC，也不得把模拟量写成两路。
- PL 急停设计输入：`new-vivado/pl急停.md`
- PL 急停源文档同步：`new-vivado/pl急停.md` 已记录通用 IO 输出 `DO1` - `DO14`、`PWM1` - `PWM2` 必须随 PL 急停强制关闭，以及接 PL 的总线 TX 只封锁发送/驱动、不断开物理 Link、恢复前清空或失效 stale TX 的硬边界；`scripts/verify_pl_estop_safety_boundary.ps1` 已把该源文档纳入检查。
- HDMI/MPG 当前状态：放弃 HDMI/DVI。旧 HDMI/TMDS 管脚已从 BD 删除；本轮已按源 XDC 接完整 DB25 MPG 输入：`AXIS_SEL0` - `AXIS_SEL7`、`MPG_A`、`MPG_B`、`SCALE_SEL0` - `SCALE_SEL2`。`PULS5_IO` 已从临时手轮用法还原为第 5 轴脉冲输出。
- PL 急停 DO/PWM 顶层硬切断：通用 IO 输出 `DO1` - `DO14`、`PWM1` - `PWM2` 在急停执行时必须由 PL 急停路径强制关闭。当前 `system_top.v` 已把 `EGS_DI/AA19` 作为 `estop_nc_in`，低电平或断线视为 `estop_hw_active`；`do_out[13:0]` 与 `pwm_out[1:0]` 已通过顶层 `pl_estop_general_output_gate` 后再出 active XDC，safe level 暂定 0；正常输出 owner 已改为 `z20_v15_io_owner_axi_lite`，`do_o[13:0]` 供应 14 路 DO，两路 PWM 由该 owner 的 period/duty/enable 寄存器生成后进入同一个急停 gate。本计划只复查 RTL/BD/active XDC/仿真/manifest 是否保持该 gate，不再要求每路负载 off 极性、实际接线和板上测量作为当前门槛；后续板端软件写输出时必须通过该 owner，不能绕过急停 gate。
- PL 急停网口 TX 硬切断：当前 `system_top.v` 把 `estop_nc_in` 传入 wrapper，当前 XSA 的 BD synth netlist 在 `gmii2rgmii` 前用 1 路 `pl_estop_bus_tx_gate` 封锁 GMII `TX_EN`，并在急停时把 GMII `TXD` 强制为 `8'h00`、清除 GMII `TX_ER`；RGMII `rgmii_tx_ctl`、`rgmii_td`、`rgmii_txc` 仍由 `gmii2rgmii`/ODDR 直接驱动，RX、MDIO、PHY reset/link power 不作为急停动作关闭，因此释放后不需要重建物理 Link。同一 generated synth 边界已把 `pl_estop/estop_nc_in` 从保存 BD 的常量占位改为顶层 `estop_nc_in`，所以 AXI STATUS/IRQ 观测的是同一个物理急停输入；`scripts/patch_gmii_pre_oddr_estop_gate.ps1` 会在综合/实现前恢复该边界补丁。该实现是针对此 XSA 的 pre-ODDR TX 发送封锁；PL 内没有 TX FIFO owner，急停期间被封锁的发送在 GMII 侧丢弃，不由 PL 重放。当前 `scripts/verify_pl_estop_bus_gate_owner.ps1` 已确认本地 gate owner 对准 `config/hardware_profile.json` 里的生产运动总线：EtherCAT over PS GEM1/EMIO，且门控点位于 PS GEM1 EMIO 到 `gmii2rgmii` 之前。当前计划只从代码/网表角度复查 Link/RX/MDIO 保持设计意图、TX gate 位置和 stale TX 不由 PL 重放；不再执行 BV10-BV12 实测。
- 轴/离散 IO 当前状态：8 轴 `PULS/DIR` 已从 BD `step_ip` 的 8 位输出接到 wrapper `axis_puls_o[7:0]`、`axis_dir_o[7:0]`，再由 `system_top.v` 顶层急停 gate 输出到 `PULS1-8/DIR1-8`；8 轴 `A/B/Z` 编码器输入已按 v1.5 原理图接到顶层输入、active XDC 和 wrapper `axis_enc_*_i[7:0]`。`ENA1-8`、`ALM1-8`、`DI1-18`、`FR_DI1-16`、`TS_DI`、MPG、SCALE、`TP_INT/TP_RST`、正常 `DO/PWM` 已由新增 `z20_v15_io_owner_axi_lite` 接入，AXI 地址 `0x41270000/64K`，复位默认 DO=0、PWM disabled、ENA=0、TP_RST_N=1；`DO/PWM` 与 `ENA` 仍在顶层急停 gate 后才出 pin。RS485 已通过 PS UART1 EMIO 导出到 `RS485_FPGA_RX/TX`。后续仍必须做轴号/极性、输入滤波策略、输出负载 off 极性和板端软件读写复核；当前不声明板测通过。
- Active Constraints 清理要求：Vivado 工程只加载 `constraints/z20_v1_5_active_mapped.xdc` 这一份 active 约束。`../z20-v1_5_20260623.xdc` 只能作为来源注释和脚本校验输入，不能加入 active constrs set，否则会把未使用的真源约束一起编译并产生 100+ 无关 warning。重新编译和导出 XSA 前必须确认 `.xpr`/constrs set 不加载真源 XDC，`verify_active_xdc_traceability.ps1` 仍能逐条追溯到真源。
- 新工程目标目录：`new-vivado/z20_v1_5_hw_project/`
- 当前已知状态：目标目录已存在，但应先按本计划检查内容完整性；不得用删除重建掩盖已有文件状态。

## 新增和语义变化 IO 清单

这里只写两类 IO：一类是老 Vivado 工程里没有、这次新底板必须新增的需求；另一类是老工程虽然有端口，但这次用途、方向、数量或接口语义已经变化的需求。老工程已有且功能不用改的 LCD/CTP、CAN、RS232、RS485、PL UART、RGMII、I2C 等，不在这里重复列；如果只是新原理图换了封装脚但功能没变，放到端口映射或约束核对章节处理。

状态说明：

- 已接入：当前顶层、active XDC 和约束检查已经对上，但仍可能需要接正式逻辑。
- 未接入：源 XDC 或原理图里有该信号，当前顶层/active XDC 没有作为最终功能接出。
- 待统一：原理图、源 XDC、active XDC、BD 或文档之间仍有冲突，先统一方案再接。

| 类型 | 方向 | 需要处理的信号 | 数量 | 当前工程状态 | 下一步 |
| --- | --- | --- | ---: | --- | --- |
| 8 轴 DB15 脉冲/方向 | 输出 | `PULS1_IO` - `PULS8_IO`、`DIR1_IO` - `DIR8_IO` | 16 输出 | 已接入 top/active XDC：BD `step_ip` 8 位输出经 wrapper `axis_puls_o[7:0]`、`axis_dir_o[7:0]` 进入顶层急停 gate 后输出 | 后续补正式 8 轴运动 owner、轴号、极性和急停封锁合同 |
| 8 轴 DB15 编码器 | 输入 | `A1_IO` - `A8_IO`、`B1_IO` - `B8_IO`、`Z1_IO` - `Z8_IO` | 24 输入 | 已接入 top/active XDC：8 轴 A/B/Z 全部接入 wrapper `axis_enc_a_i[7:0]`、`axis_enc_b_i[7:0]`、`axis_enc_z_i[7:0]` | 后续接正式 8 轴编码器 owner 的软件/寄存器读出和轴号复核 |
| 新增每轴使能和报警 | 输出/输入 | `ENA1_IO` - `ENA8_IO`、`ALM1_IO` - `ALM8_IO` | 8 输出 + 8 输入 | 已接入 top/active XDC：`ENA1-8` 由 `z20_v15_io_owner_axi_lite` 输出后经顶层急停 gate，`ALM1-8` 进入 IO owner 状态寄存器 | 后续确认 ENA off 极性、报警极性和板端行为 |
| 新增 36 路输入端子 | 输入 | `DI1` - `DI18`、`FR_DI1` - `FR_DI16`、`EGS_DI`、`TS_DI` | 36 输入 | 已接入 top/active XDC：`EGS_DI -> estop_nc_in`，其余 DI/FR_DI/TS_DI 进入 `z20_v15_io_owner_axi_lite` 输入同步/status 寄存器 | 后续确认普通 DI、限位、对刀输入语义、滤波策略和板端行为 |
| HDMI/TMDS 管脚改 DB25 手轮相关输入 | 输入 | `AXIS_SEL0` - `AXIS_SEL7`、`MPG_A`、`MPG_B`、`SCALE_SEL0` - `SCALE_SEL2` | 13 输入 | 已接入 top/active XDC 并进入 `z20_v15_io_owner_axi_lite` 输入 status；`PULS5_IO` 已还原为第 5 轴脉冲输出 | HDMI/DVI 不再恢复；后续补 MPG 解码/软件消费策略和板端行为 |
| 旧 GPIO/PL LED 管脚语义变化 | 输入/输出 | `MPG_A`、`MPG_B`、`SCALE_SEL0` - `SCALE_SEL2`、`BEEP_EN` | 5 输入 + 1 输出 | 已接入 top/active XDC：MPG/倍率为输入，`BEEP_EN` 先安全低电平；旧 GPIO0/PL LED wrapper 仍退役 | 后续确认蜂鸣器 owner 和输出极性 |
| 新增 14 路普通 DO | 输出 | `DO1` - `DO14` | 14 输出 | 老工程没有 14 路 DO；当前 `z20_v15_io_owner_axi_lite.do_o[13:0]` 是正常输出 owner，之后进入 `pl_estop_general_output_gate` 再到 `do_out[13:0]` | 保持在 PL 急停 gate 前级，后续确认 off 极性/负载/板端行为 |
| 新增 2 路 PWM | 输出 | `PWM1`、`PWM2` | 2 输出 | 老工程没有这两路独立 PWM 输出；当前 `z20_v15_io_owner_axi_lite` 提供 period/duty/enable PWM owner，之后进入 `pl_estop_general_output_gate` 再到 `pwm_out[1:0]` | 保持在 PL 急停 gate 前级，后续确认频率范围、off 极性/负载/板端行为 |
| RS485 导出 | 输入/输出 | `RS485_FPGA_RX`、`RS485_FPGA_TX` | 1 RX + 1 TX | 已接入 top/active XDC：PS UART1 EMIO wrapper `rs485_rxd/rs485_txd` 导出到 `U14/R7` | 后续确认新板串口方向、收发器控制和 `B13_IO_0` 标签复核 |
| 触摸中断/复位 | 输入/输出 | `TP_INT`、`TP_RST` | 1 输入 + 1 输出 | 已接入 top/active XDC：`TP_INT` 进入 IO owner 状态，`TP_RST` 由 IO owner reset 寄存器驱动且默认释放高 | 后续确认触摸控制器复位极性和板端行为 |
| 新增一路 0-10V 模拟量 | 模拟输入 | `ADC_IN1`，经硬件保护、分压、滤波后接芯片自带 `XADC_VP/XADC_VN` | 1 路模拟 | 老工程没有这路模拟输入；当前源 XDC 已按 XADC 单通道写出 | 保持一路模拟量，不再引入 `ADC_IN2` 或外部 SPI ADC |

不进 PL XDC 的硬件项不在本表展开，包括 24V 输入和电源、核心板板对板连接器、USB 供电、SD/TF、PS 维护网口、PS USB、板载保护和连接器机械定义。

## 目标产物

- `new-vivado/z20_v1_5_hw_project/README.md`：新工程说明、构建入口、当前状态。
- `new-vivado/z20_v1_5_hw_project/z20_v1_5_hw_project.xpr`：新工程入口，路径只指向新目录内文件。
- `new-vivado/z20_v1_5_hw_project/z20_v1_5_hw_project.srcs/`：新工程 BD、XCI、wrapper、约束和工程源。
- `new-vivado/z20_v1_5_hw_project/ip_repo/`：新工程本地 IP 仓库副本。
- `new-vivado/z20_v1_5_hw_project/rtl/`：新工程直接引用 RTL。
- `new-vivado/z20_v1_5_hw_project/scripts/`：新工程专用 Vivado batch 脚本。
- 旧约束参考副本已从新工程交付边界移除；当前只保留 `constraints/z20_v1_5_active_mapped.xdc` 和上级 `z20-v1_5_20260623.xdc` 作为 active/真源约束输入。
- `new-vivado/z20_v1_5_hw_project/constraints/`：新工程 active 约束；必须直接使用或派生自 `new-vivado/z20-v1_5_20260623.xdc`。
- `new-vivado/z20_v1_5_hw_project/docs/port_mapping.md`：老 wrapper 端口到新原理图网络名的映射表、冲突和待复核项。
- `new-vivado/z20_v1_5_hw_project/docs/pl_estop_integration.md`：PL 急停端口、寄存器、时序、验证和未决项设计记录。
- `new-vivado/z20_v1_5_hw_project/docs/pl_estop_wiring_evidence.csv`：PL 急停 E1/E2 硬件接线证据输入；未达到 `ready_for_rtl_xdc` 前，不允许把真实急停/STO/抱闸/DO/PWM/总线 TX gate 接入 active XDC 或 RTL 输出。
- `new-vivado/z20_v1_5_hw_project/docs/pl_estop_board_validation_evidence.csv`：保留为历史/后续现场验证参考矩阵；当前计划取消 A11 板上实际测量 gate，不要求填到 `board_verified`。
- `new-vivado/z20_v1_5_hw_project/docs/pl_estop_evidence_gap.md`：保留为证据缺口参考报告；当前计划不把 pending 板测项作为代码侧复查失败条件。
- `new-vivado/z20_v1_5_hw_project/scripts/check_active_xdc.ps1`：active XDC 与 wrapper 顶层端口一致性检查，以及剩余未约束端口清单。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_adc_spi_mapping.ps1`：旧 ADC gate 名称需要后续整改。当前 ADC 方案为芯片自带 XADC 一路模拟量；如保留该脚本，内容必须改为校验 `XADC_VP/XADC_VN` 单通道，不能再校验外部 ADC。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_pl_estop_sim.ps1`：重新编译并运行 `pl_estop_core_tb` 与 `pl_estop_axi_lite_tb`，把 PL 急停 RTL/AXI 仿真从文档记录提升为可重复 gate；当前输出 `pl_estop_sim=ok`、`pl_estop_core_tb=pass`、`pl_estop_axi_lite_tb=pass`、`sim_outputs=persistent_none`，不在新工程内留下 `.vvp` 临时产物。
- `new-vivado/z20_v1_5_hw_project/scripts/export_remaining_drc_ports.ps1`：导出剩余 bitstream DRC 端口的机器可读 CSV，包含功能组、阻断类型和下一动作，作为后续逐组收口的输入；当前写入 `docs/remaining_drc_ports.csv` 时使用共用的 `scripts/write_text_with_retry.ps1` UTF-8 重试写入 helper，避免多个本地 gate 并行刷新时出现文件占用。
- `new-vivado/z20_v1_5_hw_project/scripts/export_active_pin_conflicts.ps1`：从剩余 DRC CSV 导出 active 管脚已占用冲突报告，并在 N3C 中补充冲突端口的 BD/net 连接证据，作为退役、内部 tie-off 或重映射输入；当前写入 `docs/active_pin_conflicts.md` 时同样使用共用的 `scripts/write_text_with_retry.ps1` UTF-8 重试写入 helper，避免并行 readiness/handoff gate 竞争。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_remaining_drc_ports.ps1`：刷新并校验剩余 DRC CSV 的字段、端口唯一性、分组计数、阻断类型、active 同脚位冲突摘要，以及 CSV 端口集合是否与 `check_active_xdc.ps1` 未约束端口集合一致。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_project_independence.ps1`：校验新工程可移动边界，确认 `.xpr` 工程路径为 `$PPRDIR` 相对路径、active XDC 来源为 `constraints/z20_v1_5_active_mapped.xdc`、工程源/配置无盘符绝对路径、无老工程源码依赖。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_active_xdc_traceability.ps1`：逐条校验 active XDC 中每个 `PACKAGE_PIN` 都有 `../z20-v1_5_20260623.xdc` 来源注释，并且注释中的源 net 在 v1.5 源 XDC 中映射到同一 package pin；当前输出 `active_xdc_traceability=ok`、`active_pin_assignments=180`、`traced_assignments=180`、`old_project_xdc_dependency=none`。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_active_xdc_electrical_contract.ps1`：逐条校验 active XDC 中每个 `PACKAGE_PIN` 都有且只有一个匹配的 `IOSTANDARD LVCMOS33`，拒绝 orphan/duplicate IOSTANDARD 行和非 LVCMOS33 active-port 标准；当前输出 `active_xdc_electrical_contract=ok`、`active_pin_assignments=180`、`iostandard_assignments=180`、`lvcmos33_assignments=180`、`missing_iostandard_assignments=0`、`orphan_iostandard_assignments=0`、`non_lvcmos33_assignments=0`。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_project_portability.ps1`：全工程文本可移动性 gate，扫描工程文本、生成报告、Vivado run 文本、仿真文本产物、board-input 文件和 manifest，拒绝盘符绝对路径、老工程源码依赖、非相对 manifest artifact path 和临时写入文件；当前输出 `project_portability=ok`、`absolute_path_scan=ok`、`manifest_relative_paths=ok`、`manifest_artifact_paths=19`、`tmp_files=0`、`text_files_scanned=288`。本轮已把 Vivado/Icarus 生成文本中的当前机器路径相对化或替换为工具链占位符，原始文件备份在 `repo_ignored/new_vivado_portability_gate/absolute_path_text_backup/`。
- `new-vivado/z20_v1_5_hw_project/scripts/scrub_vivado_absolute_paths.ps1`：Vivado 重新生成 `.xpr`、run log、报告或 warning summary 后，先把当前工程路径替换为 `$PPRDIR`/项目占位符、把 Xilinx 安装路径替换为工具链占位符，再运行 portability/independence gate；该脚本不改 bit/XSA 二进制内容。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_vivado_xsa_cleanliness.ps1`：校验 Active Constraints 清理和 XSA 重新导出洁净度，确认 `.xpr` 只加载 `constraints/z20_v1_5_active_mapped.xdc`，Vivado run 没有解析 `../z20-v1_5_20260623.xdc` 或旧 `system.xdc`，run log 无 `ERROR:`/`CRITICAL WARNING:`，routed DRC 无 `NSTD-1`/`UCIO-1` 或 error 级规则，当前只允许 `RTSTAT-10` warning，且 `board_inputs/system.xsa` 不旧于当前 bitstream；当前输出 `vivado_xsa_cleanliness=ok`、`active_constraints_loaded=mapped_only`、`truth_source_xdc_loaded=no`、`old_project_xdc_loaded=no`、`drc_blocking_rules=0`、`drc_allowed_warning_rules=RTSTAT-10`、`build_status=bitstream_generated`、`timing_status=timing_met`。
- `new-vivado/z20_v1_5_hw_project/scripts/export_vivado_warning_summary.ps1`：从当前 `synth_1/runme.log` 与 `impl_1/runme.log` 导出 `docs/vivado_warning_summary.csv` 和 `docs/vivado_warning_summary.md`，按 warning code 分类剩余普通 warning，并把约束真源/旧约束 warning 计数单独列出；当前输出 `vivado_warning_summary=classified`、`vivado_warning_lines=457`、`vivado_warning_codes=17`、`unexpected_warning_codes=0`、`constraint_truth_warning_lines=0`、`retired_hdmi_warning_lines=0`。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_vivado_warning_summary.ps1`：校验 warning summary 与当前 run log 一致，拒绝未知 warning code、真源/旧 XDC warning、`NSTD-1`/`UCIO-1`、`No ports matched` 或 `set_property expects at least one object` 等约束误加载类 warning；当前输出 `vivado_warning_summary_verify=ok`、`constraint_truth_warning_lines=0`、`unexpected_warning_codes=0`。该 gate 不声明全部普通 Vivado warning 已清零，只证明用户要求清掉的 Active Constraints/真源 XDC warning 链没有进入当前 XSA 构建。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_new_vivado_local_closure.ps1`：本地交付前一键只读 gate，聚合 active XDC、工程独立性、工程可移动性、Vivado/XSA cleanliness、Vivado warning summary、XADC 一路模拟量映射、剩余 DRC、active 管脚冲突、PL 急停安全边界、PL 急停 timing params、PL 急停 register map、board-input handoff 和 manifest hash；当前输出目标为 `new_vivado_local_closure=local_verified_only`、`vivado_xsa_cleanliness=ok`、`active_constraints_loaded=mapped_only`、`truth_source_xdc_loaded=no`、`drc_blocking_rules=0`、`drc_allowed_warning_rules=RTSTAT-10`、`vivado_warning_summary_verify=ok`、`constraint_truth_warning_lines=0`、`retired_hdmi_warning_lines=0`、`pl_estop_timing_params=ok`、`pl_estop_register_map=ok`、`board_closure_state=local_verified_only`。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_pl_estop_timing_params.ps1`：代码侧校验 PL 急停 E3 时钟与计数参数，确认 RTL 默认 `CLK_HZ=100000000`、`DEBOUNCE_MS=10`、`BRAKE_LEAD_US=50`、`AXIS_COUNT=8`，推导 `debounce_cycles=1000000`、`brake_cycles=5000`，并确认 BD `pl_estop/S_AXI` 位于 `processing_system7_0/FCLK_CLK0` 100MHz 时钟网、AXI `TIMING`/`AXIS_CONFIG` 寄存器暴露这些配置；当前输出目标为 `pl_estop_timing_params=ok`。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_pl_estop_safety_boundary.ps1`：校验 PL 急停当前安全边界，确认 16 路 DO/PWM 顶层 forced-off gate、1 路 GMII pre-ODDR TX 发送封锁、AXI 状态位观测顶层 `estop_nc_in`、BD 占位连接、退役 HDMI wrapper 端口未连接、未提前占用待确认 STO/抱闸/轴 gate 输出且无 pending 安全管脚；同时确认 `scripts/patch_gmii_pre_oddr_estop_gate.ps1` 覆盖 generated wrapper/BD synth netlist，且 `scripts/vivado_synth_current.tcl`、`scripts/vivado_impl_current.tcl` 会在 run 前执行该补丁；当前输出 `do_pwm_gate=top_hard_gate_local_unverified`、`bus_tx_gate=top_rgmii_tx_gate_local_unverified`、`pl_estop_axi_observation=top_estop_input_local_unverified`、`gmii_pre_oddr_patch_flow=ok`、`active_do_pwm_pin_assignments=16`、`active_estop_input_pin_assignments=1`、`active_pending_wiring_pin_assignments=0`。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_pl_estop_output_shutdown_contract.ps1`：专门校验 DO/PWM forced-off 和 bus TX 只封锁发送的代码侧输出关闭合同，覆盖 `pl急停.md`、本计划、README、集成文档、RTL、AXI 状态、仿真、active XDC 和 bus gate owner；当前输出目标为 `pl_estop_output_shutdown_contract=code_review_only`、`do_pwm_wiring_rows=16`、`do_pwm_pending_rows=0`、`bus_tx_wiring_rows=2`、`bus_tx_pending_rows=0`、`bus_gate_owner=ps_gem1_emio_rgmii_local_verified`、`bus_gate_transport=EtherCAT over PS GEM1/EMIO`、`bus_gate_before_gmii2rgmii=ok`、`bus_gate_board_evidence=pending`、`active_do_pwm_pin_assignments=16`、`active_output_gate_ports=0`。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_pl_estop_bus_gate_owner.ps1`：校验当前生产硬件 profile、BD 和 generated synth netlist 的总线门控归属，确认 `PCW_EN_EMIO_ENET1=1`、PS GEM1 GMII 接 `gmii2rgmii`、外部 RGMII 仍由 `gmii2rgmii` 驱动、GMII `TX_EN/TXD/TX_ER` 在 `gmii2rgmii` 前被 `estop_hw_active` 门控、RX/MDIO 路径保持；当前输出 `pl_estop_bus_gate_owner=ps_gem1_emio_rgmii_local_verified`、`production_transport=EtherCAT over PS GEM1/EMIO`、`gate_inserted_before_gmii2rgmii=ok`、`tx_en_gated=yes`、`txd_forced_idle_zero=yes`、`tx_er_forced_idle_zero=yes`、`rx_path_preserved_by_design=yes`、`mdio_path_preserved_by_design=yes`、`board_tests_required=BV10,BV11,BV12`、`board_evidence_state=pending`；BV10-BV12 仅保留为后续现场参考，不再是当前代码侧复查门槛。
- `new-vivado/z20_v1_5_hw_project/docs/evidence/pl_estop/README.md`：PL 急停实物证据文件根目录说明；未来所有 `board_verified` 接线/板测证据文件必须放在 `docs/evidence/pl_estop/` 下，并以非占位 `.md` 证据记录作为 CSV evidence path，原始波形、照片和日志作为该 `.md` 记录引用的附件。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_pl_estop_wiring_evidence.ps1`：校验 `docs/pl_estop_wiring_evidence.csv` 的字段、必需行、证据状态和 gate 点约束；任何 `board_verified` 行都必须让 `bench_evidence` 与 `board_evidence` 指向 `docs/evidence/pl_estop/` 下真实存在的项目相对 `.md` 证据记录，记录包含 `Evidence State: board_verified`、匹配信号名、真实 metadata 和真实存在的项目相对附件文件，且无占位标记；当前输出 `pl_estop_wiring_evidence=not_ready`、`ready_for_real_pins=no`、`verified_wiring_evidence_files=0`、`board_verified_evidence_contract=md_non_placeholder`、`board_verified_attachment_contract=project_relative_existing_files`。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_pl_estop_board_validation.ps1`：校验 `docs/pl_estop_board_validation_evidence.csv` 的字段、必需测试和板上证据完整性；任何 `board_verified` 行都必须填写项目相对 `evidence_path`，且该路径必须指向 `docs/evidence/pl_estop/` 下真实存在的 `.md` 证据记录，记录包含 `Evidence State: board_verified`、匹配 test_id、真实 metadata 和真实存在的项目相对附件文件，且无占位标记；当前输出 `pl_estop_board_validation=not_ready`、`board_validation_ready=no`、`verified_evidence_files=0`、`board_verified_evidence_contract=md_non_placeholder`、`board_verified_attachment_contract=project_relative_existing_files`。
- `new-vivado/z20_v1_5_hw_project/scripts/export_pl_estop_evidence_gap.ps1`：从 `docs/pl_estop_wiring_evidence.csv` 和 `docs/pl_estop_board_validation_evidence.csv` 生成 `docs/pl_estop_evidence_gap.md`，并输出 `wiring_pending_rows=3`、`board_pending_tests=14`，用于后续 STO/抱闸/轴 gate 接线和全部板测资料填报。
- `new-vivado/z20_v1_5_hw_project/scripts/export_pl_estop_hardware_evidence_request.ps1`：从接线证据和板上验证 CSV 生成 `docs/pl_estop_hardware_evidence_request.md`，作为下一步实物接线/极性/板测证据采集清单，并明确证据文件根目录为 `docs/evidence/pl_estop/`；当前输出 `pl_estop_hardware_evidence_request=open`、`wiring_request_items=3`、`board_request_items=14`、`do_pwm_request_items=0`、`bus_tx_request_items=0`。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_pl_estop_hardware_evidence_request.ps1`：校验硬件证据采集清单与两个 CSV 的计数、相对路径、证据根目录、`.md` 证据记录合同、每个待采集行和 DO/PWM/总线 TX 不可绕过边界一致；当前输出 `pl_estop_hardware_evidence_request_verify=ok`、`hardware_evidence_request_state=open`。
- `new-vivado/z20_v1_5_hw_project/scripts/export_pl_estop_field_packet.ps1`：从接线证据和板上验证 CSV 生成 `docs/pl_estop_field_packet.md`，作为现场证据录入包，列出每行必须填写的 CSV 字段、建议 `.md` 证据记录文件名、证据记录合同和 DO/PWM/总线 TX 不可绕过边界；当前输出 `pl_estop_field_packet=open`、`wiring_rows=22`、`board_tests=14`、`do_pwm_rows=16`、`bus_tx_rows=2`。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_pl_estop_field_packet.ps1`：校验现场证据录入包与两张 CSV 的行、计数、证据根目录、`.md` 证据记录合同和边界一致；当前输出 `pl_estop_field_packet_verify=ok`、`field_packet_state=open`。
- `new-vivado/z20_v1_5_hw_project/docs/pl_estop_field_execution_runbook.md`：现场执行 runbook，给出实物接线证据采集顺序、`.md` 证据记录创建、CSV 更新、field-intake/readiness gate、DO/PWM 强制关闭、总线 TX 只封锁发送使能且保持 Link、队列清空和 XADC 模拟量路径保护的停止条件；它不是实测证据，不得作为 CSV evidence path。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_pl_estop_field_runbook.ps1`：校验现场执行 runbook 是否包含硬件证据入口、现场录入包、证据根目录、field-intake/readiness gate、DO/PWM、bus TX、Link 保持、queue flush、EtherCAT over PS GEM1/EMIO 边界、E11/A11 准入和 XADC 模拟量路径保护；当前输出 `pl_estop_field_runbook_verify=ok`、`field_runbook_state=open`。
- `new-vivado/z20_v1_5_hw_project/docs/pl_estop_evidence_record_templates.md`：由接线证据和板上验证 CSV 生成的 `.md` 证据记录模板入口；模板只用于创建真实证据记录，不得被 CSV evidence path 直接引用，当前覆盖 22 个接线模板、14 个板测模板、16 个 DO/PWM 模板和 2 个总线 TX 模板。
- `new-vivado/z20_v1_5_hw_project/scripts/export_pl_estop_evidence_templates.ps1`：生成 `docs/pl_estop_evidence_record_templates.md`，输出 `pl_estop_evidence_templates=open`、`wiring_templates=22`、`board_validation_templates=14`、`do_pwm_templates=16`、`bus_tx_templates=2`、`board_verified_template_records=0`。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_pl_estop_evidence_templates.ps1`：校验证据记录模板覆盖全部 CSV 行、使用项目相对路径、不包含绝对路径/老工程引用，并确认模板内没有 `board_verified` 实证记录；当前输出 `pl_estop_evidence_templates_verify=ok`、`template_state=open`。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_pl_estop_evidence_root.ps1`：校验整个 `docs/evidence/pl_estop/` 证据根目录，拒绝过程日志、绝对路径、老工程引用，以及未被 `board_verified` CSV 行引用的孤立 `board_verified` 证据记录；当前输出 `pl_estop_evidence_root_verify=ok`、`evidence_root_files=1`、`orphan_board_verified_records=0`。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_pl_estop_field_intake.ps1`：现场证据录入后的统一入口 gate；汇总硬件证据采集清单、现场录入包、现场执行 runbook、证据模板、证据根目录、接线 CSV、板测 CSV 和 safety boundary。后续真实急停/STO/抱闸/DO/PWM/轴封锁/总线 TX 管脚提升前必须先运行该脚本；当前输出 `pl_estop_field_intake=not_ready`、`field_intake_structural_contract=ok`、`field_runbook_verify=ok`、`ready_for_real_pins=no`、`board_evidence_ready=no`、`board_validation_ready=no`。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_pl_estop_real_pin_promotion_gate.ps1`：真实管脚提升防误接 gate；交叉检查 active XDC 中的安全/DO-PWM/总线 TX 候选 PACKAGE_PIN、`docs/pl_estop_wiring_evidence.csv` 对应行和 `scripts/verify_pl_estop_readiness.ps1`。当前状态必须是 `pl_estop_real_pin_promotion_gate=local_hard_gate_promoted`、`active_promoted_wiring_assignments=17`、`active_promoted_do_pwm_assignments=16`、`active_promoted_estop_input_assignments=1`、`active_promoted_bus_tx_assignments=0`、`local_hard_gate_promoted=yes`、`promotion_requires_e11=no`；未来只要新增 STO/抱闸/轴 gate 输出、额外总线 TX gate 输出或改变 DO/PWM gate 归属，就必须重新满足对应证据和 readiness gate。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_pl_estop_readiness.ps1`：汇总 active XDC、DRC、项目独立性、安全边界、硬件证据采集清单 verify、现场执行 runbook verify、证据记录模板 verify、field-intake gate 和接线证据，给出 E11/A11 是否可启动；当前输出 `pl_estop_readiness=not_ready`、`pl_estop_hardware_evidence_request_verify=ok`、`pl_estop_field_runbook_verify=ok`、`field_runbook_state=open`、`pl_estop_evidence_templates_verify=ok`、`pl_estop_field_intake=not_ready`、`field_intake_structural_contract=ok`、`e11_rtl_xdc_ready=no`、`a11_board_validation_ready=no`。
- `new-vivado/z20_v1_5_hw_project/board_inputs/pl_estop_register_map.md`：仅放在 Vivado 工程内的 PL 急停 AXI register map，记录 `0x41260000/64K`、`MAGIC/VERSION/STATUS/CONTROL/TIMING/AXIS_CONFIG/BUILD_ID/GENERAL_CONFIG/BUS_TX_CONFIG`、STATUS/CONTROL bit 定义和代码侧复查边界。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_pl_estop_register_map.ps1`：校验 PL 急停 register map 与 `rtl/pl_estop_axi_lite.v` 的地址、版本、BUILD_ID、STATUS bit 顺序、CONTROL 写脉冲位、base address 和 IRQ route 一致；当前目标输出 `pl_estop_register_map=ok`、`registers=9`、`status_bits=9`、`control_bits=2`。
- `new-vivado/z20_v1_5_hw_project/board_inputs/software_handoff.md`：仅放在 Vivado 工程内的板端软件交付支持说明，集中说明板端软件应消费的 XSA/manifest/active XDC、XADC 一路模拟量接口、PL 急停 AXI/IRQ、PL 急停 register map、DO/PWM 和总线 TX gate 边界、禁止绝对路径，以及当前只做代码侧复查、禁止把 local-only bit/XSA 误报为板上安全验证通过。
- `new-vivado/z20_v1_5_hw_project/scripts/export_board_input_handoff.ps1`：生成 `board_inputs/README.md`，作为板上输入目录第一入口，汇总 local-only 状态、timing、artifact hash、Vivado/XSA cleanliness、Vivado warning summary、硬件证据入口、现场证据录入包、现场执行 runbook、证据记录模板、证据根目录、`board_inputs/software_handoff.md` 和安全边界；当前输出 `board_input_handoff=local_verified_only`、`board_input_handoff_software_handoff=board_inputs/software_handoff.md`、`board_input_handoff_vivado_xsa_cleanliness=ok`、`board_input_handoff_vivado_warning_summary=classified`、`board_input_handoff_vivado_warning_summary_verify=ok`、`board_input_handoff_field_packet_verify=ok`、`board_input_handoff_field_runbook_verify=ok`、`board_input_handoff_evidence_templates_verify=ok`、`board_input_handoff_evidence_root_verify=ok`。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_board_input_handoff.ps1`：校验 `board_inputs/README.md` 与当前 Vivado/XSA cleanliness、Vivado warning summary、readiness、硬件证据清单、现场证据录入包、现场执行 runbook、证据记录模板、证据根目录、板端软件 handoff 入口和安全边界一致；当前输出 `board_input_handoff_verify=ok`、`board_input_handoff_software_handoff=board_inputs/software_handoff.md`、`board_input_handoff_vivado_xsa_cleanliness=ok`、`board_input_handoff_vivado_warning_summary=classified`、`board_input_handoff_vivado_warning_summary_verify=ok`、`board_input_handoff_field_packet_verify=ok`、`board_input_handoff_field_runbook_verify=ok`、`board_input_handoff_evidence_templates_verify=ok`、`board_input_handoff_evidence_root_verify=ok`。
- `new-vivado/z20_v1_5_hw_project/scripts/export_board_input_manifest.ps1`：导出板上验证前的输入产物身份清单，记录 bit/XSA/active XDC/工程入口/DRC/timing history/硬件证据采集清单/现场证据录入包/现场执行 runbook/真实管脚提升 gate/证据记录模板/warning summary 的相对路径、SHA256、最新 timing 行、active XDC 检查输出、Vivado/XSA cleanliness 输出和 Vivado warning summary 输出。
- `new-vivado/z20_v1_5_hw_project/scripts/verify_board_input_manifest.ps1`：校验 manifest 中的路径均为相对路径，bit/XSA/active XDC/工程入口/DRC/timing history/硬件证据采集清单/现场证据录入包/现场执行 runbook/证据记录模板/warning summary/板端软件 handoff/PL 急停 register map 的大小和 SHA256 与当前文件一致，并确认 Vivado/XSA cleanliness、Vivado warning summary 与 manifest 仍是 `local_verified_only`；当前目标输出 `artifact_count=19`、`board_software_handoff_artifact=present`、`pl_estop_register_map_artifact=present`、`vivado_xsa_cleanliness=ok`、`active_constraints_loaded=mapped_only`、`truth_source_xdc_loaded=no`、`drc_blocking_rules=0`、`drc_allowed_warning_rules=RTSTAT-10`、`vivado_warning_summary=classified`、`vivado_warning_summary_verify=ok`、`constraint_truth_warning_lines=0`、`retired_hdmi_warning_lines=0`、`pl_estop_register_map=ok`。
- `new-vivado/z20_v1_5_hw_project/scripts/write_text_with_retry.ps1`：板上输入相关生成脚本共用的 UTF-8 重试写入 helper；当前已用于剩余 DRC CSV、active 管脚冲突报告、证据缺口报告、硬件证据请求、现场证据录入包、证据记录模板、Vivado warning summary、handoff README 和 manifest，减少并行 gate 抢写生成物的风险。
- `new-vivado/z20_v1_5_hw_project/board_inputs/manifest.json`：当前 bit/XSA handoff 的机器可读 manifest；当前 artifact count 目标为 19，只证明本地产物身份和代码侧检查结果，不证明板上行为。
- `new-vivado/z20_v1_5_hw_project/docs/remaining_drc_ports.csv`：当前 0 个未约束顶层端口，文件为 header-only；若未来重新暴露真实端口，此 CSV 必须重新记录方向、功能组、阻断类型、旧约束/v1.5 线索、active 同脚位占用、关闭条件和下一动作清单。
- `new-vivado/z20_v1_5_hw_project/docs/active_pin_conflicts.md`：N3C active 管脚冲突报告；当前 `active_pin_conflicts=0`，并记录当前 8 轴 `PULS/DIR/ABZ` 边界、IO owner 驱动的 ENA/DI/MPG/ALM/DO/PWM/TP 边界、RS485 导出和 ADC SPI 退役状态。
- `new-vivado/z20_v1_5_hw_project/reports/`：本地检查、Vivado 报告、约束检查输出。

## 执行方法

每次继续本计划时只按“当前下一项”推进，不从头重做、不删除重建、不改老工程。每完成一项必须同步对应文档状态，并运行该项最小检查。

每个可执行项必须落成“行动卡”，行动卡至少写清：

- 前置条件：继续前必须已经成立的文件状态、硬件状态或人工结论。
- 输入材料：本项只允许读取哪些文档、XDC、RTL、BD 或脚本。
- 修改对象：本项会改哪些新工程文件；不能写“相关文件”这类模糊范围。
- 操作步骤：按顺序写到能直接执行，脚本名、BD cell、接口名、地址、IRQ 位都要明确。
- 禁止动作：本项内不得触碰的老工程、外部管脚、板上动作或安全输出。
- 最小检查：至少一条可运行命令或 Vivado 检查点。
- 通过条件：用实际检查结果判断，不用“基本完成”“应该可以”等主观描述。
- 失败处理：如果失败，先停在本项修正；不得跳到后续阶段掩盖失败。

执行记录不写 `process.md` 或 `过程.md`。可追踪信息放在以下位置：

- 计划状态：更新本文件 `当前状态` 和相关任务表。
- 端口和约束依据：更新 `z20_v1_5_hw_project/docs/port_mapping.md`。
- PL 急停依据：更新 `z20_v1_5_hw_project/docs/pl_estop_integration.md`。
- Vivado 结果：放在 `z20_v1_5_hw_project/reports/` 或 Vivado run 产物目录。
- 临时诊断：放在 `repo_ignored/vivado_new_project_plan/`，不得成为工程依赖。

每项任务使用同一完成标准：

- 该任务声明的文件已创建或修改。
- 该任务声明的检查命令已运行，结果写入文档状态或最终回复。
- 如果检查失败，先修正本任务范围内问题；不能用后续阶段掩盖。
- 如果缺少硬件资料、原理图、接线或板上条件，任务状态写为 `blocked`，并写清缺什么证据才能继续。

## 当前可执行任务清单

| ID | 状态 | 目标 | 具体动作 | 修改文件 | 最小检查 | 通过条件 |
| --- | --- | --- | --- | --- | --- | --- |
| A0 | done | 固定工作目录 | 将含中文路径的新工程迁移到 `new-vivado/`，清理乱码目录 | `new-vivado/` | `git status --short -- vivado_hw_project new-vivado` | 老工程无修改，新目录存在 |
| A1 | done | 自包含工程入口 | `.xpr`、Tcl 脚本和工程源只指向新目录内文件 | `.xpr`、`scripts/` | `rg -n "vivado_hw_project" new-vivado/z20_v1_5_hw_project` | 无老工程源依赖 |
| A2 | done | 约束来源固定 | 保留原始 v1.5 XDC，active XDC 改为可被 Vivado 读取的派生文件 | `.xpr`、`constraints/` | Vivado BD validation | BD 能打开，旧 `system.xdc` 未加载 |
| A3 | local_verified_current_drc_closed | 端口映射闭环 | 当前 active XDC 与 `system_top.v` 自洽，LCD、RGMII/MDIO、CAN、PL UART、RS232、I2C3、触摸 I2C、完整 MPG、8 轴 `PULS/DIR/ENA/ALM` 顶层壳、8 轴编码器 ABZ、DI/FR_DI/TS_DI、`EGS_DI`、`BEEP_EN` 和 DO/PWM 顶层硬门控已进入 active XDC；RS485、TP_INT/TP_RST、PL_RST、sys_clk 和备用 FPGA IO 仍保持 fail-closed | `rtl/system_top.v`、`.xpr`、`docs/port_mapping.md`、`constraints/`、`scripts/check_active_xdc.ps1` | active XDC 重复端口/重复管脚检查、剩余端口清单检查、Vivado synthesis/implementation | 当前只能说明 DRC 和 active/top 一致性已收口，不能说明最终整板 IO owner 或板端功能已完成 |
| A4 | code_review_only | PL 急停管脚候选 | 已从 v1.5 XDC 列出 DI/FR_DI/EGS/TS 输入、DO/PWM 输出和轴封锁候选；2026-06-30 已把 `EGS_DI`、`DO1` - `DO14`、`PWM1` - `PWM2` 和 GMII pre-ODDR TX 封锁证据补入 `docs/pl_estop_wiring_evidence.csv`，并在 `system_top.v`/active XDC/BD synth 中实现本地硬门控；最新计划取消实际测量 gate，只从代码、约束、BD、仿真、generated netlist 和 manifest 角度复查 | `docs/pl_estop_integration.md`、`docs/pl_estop_wiring_evidence.csv` | `scripts/verify_pl_estop_wiring_evidence.ps1`、`scripts/verify_pl_estop_output_shutdown_contract.ps1`、`scripts/verify_pl_estop_real_pin_promotion_gate.ps1` | 当前 `pl_estop_output_shutdown_contract=code_review_only`、`do_pwm_ready_rows=16`、`bus_tx_ready_rows=2`、`pl_estop_real_pin_promotion_gate=local_hard_gate_promoted`；不声明板上安全功能通过 |
| A5 | local_verified | PL 急停 RTL 方案固化 | 已新增 `pl_estop_core.v` 和 `pl_estop_axi_lite.v`，包含去抖、锁存、抱闸提前、轴输出门控、16 路通用 DO/PWM forced-off 占位门控、1 路总线 TX send-enable 占位门控、状态寄存器、复位请求、TX 队列清空互锁和 IRQ 清除 | `rtl/`、`.xpr` | Icarus Verilog compile | RTL 边界和 AXI 地址不含临时散逻辑 |
| A6 | local_verified | PL 急停仿真 | 已新增 core 和 AXI wrapper testbench，覆盖 NC 低有效、短毛刺、锁存、复位拒绝、抱闸提前、DO/PWM 16 路 forced-off 占位、总线 TX idle 占位、TX 队列未清空时拒绝复位、AXI 状态和 IRQ 清除 | `sim/`、`.xpr` | `PASS: pl_estop_core_tb`、`PASS: pl_estop_axi_lite_tb` | 不靠板上测试才发现基础逻辑错误 |
| A7 | local_verified_internal_bd | BD 接入 | 已按“A7 BD 接入”行动卡新增幂等 Tcl；AXI 接 `M21_AXI`，地址 `0x41260000/64K`，IRQ 接 `xlconcat_0/In14`；`estop_nc_in` 接 fail-closed 常量 0；`general_output_in[15:0]` 接 `pl_estop_do_zero/dout`；`bus_tx_enable_in` 接 `pl_estop_tx_zero/dout`，`bus_tx_queue_flushed_in` 接 `pl_estop_tx_flushed/dout`；外部管脚未确认前只接 fail-closed/占位常量和 AXI/IRQ，不接真实输出 | `.bd`、`.xpr`、`scripts/vivado/add_pl_estop_axi_lite.tcl`、文档 | `validate_bd_design`、路径扫描、老工程状态检查 | BD 无新增 blocker；无绝对路径；老工程无修改；未确认输出不进 active XDC |
| A8 | local_verified_integrated_top | 综合检查 | N3C-2 切换到 `system_top` 后，当前 top 已用 active XDC 重新完成综合；这证明内部 wrapper fail-closed 接入可综合，但仍不是真实急停硬件链路验证 | Vivado run 输出 | `SYNTH_STATUS:synth_design Complete!` | 当前 `system_top` 综合完成且无新的 error/critical warning |
| A9 | pending_rerun | 实现和时序 | 本轮新增 IO owner、RS485、TP_INT/TP_RST 后必须重新完成 Vivado synth/impl/bitstream；仍保留 `clk_wiz_0` DRP 精确 false path 和现有实现策略，`WNS >= 0.1ns` 仅作为裕量观察目标，不作为硬失败门槛 | Vivado run 输出、`artifacts/vivado/timing_history.csv`、`z20_v1_5_hw_project.runs/impl_1/system_top_drc_routed.rpt`、`.xpr`、`docs/vivado_warning_summary.csv`、`docs/vivado_warning_summary.md` | `scripts/vivado_synth_current.tcl`、`scripts/vivado_impl_current.tcl`、`scripts/verify_vivado_xsa_cleanliness.ps1`、`scripts/verify_vivado_warning_summary.ps1` | 必须重新生成当前 180 项 active XDC 对应 bitstream，并保持 `TIMING_STATUS=timing_met` |
| A10 | pending_rerun | XSA 和板上输入 manifest 输出 | 本轮新增 IO owner、RS485、TP_INT/TP_RST 后必须重新导出带 bit 的 `board_inputs/system.xsa`，并刷新 handoff、manifest 和哈希 | `board_inputs/`、manifest、handoff 文档 | `scripts/verify_board_input_handoff.ps1`、`scripts/verify_board_input_manifest.ps1` | 只能作为本地 Vivado 交付输入，不能声明板上功能通过 |
| A11 | canceled_by_scope | 板上安全验证 | 最新计划取消实际测量测试，不再执行示波器延迟、断线、去抖、复位互锁、DO/PWM forced-off、bus TX/link/stale replay 等实测；A11 CSV 保留为后续现场参考 | `docs/pl_estop_board_validation_evidence.csv` | 当前不作为交付 gate | 当前只允许 `local_verified_only` / `code_review_only`，不能声明板上安全功能通过 |

当前代码侧复查已推进到 local handoff 侧：`scripts/check_active_xdc.ps1` 输出 `unassigned_top_ports_count=0`，active assignments 为 180，`docs/remaining_drc_ports.csv` 为 header-only；HDMI/DVI 已从 active XDC、顶层 wrapper 和 BD 源头退役。完整 MPG 输入已按当前 XDC 接入，`PULS5_IO` 已恢复为第五轴脉冲输出；DO/PWM、ENA、DI/MPG/ALM/TP 由 `z20_v15_io_owner_axi_lite` 接入，RS485 已导出；当前 bitstream/XSA 已重新生成并通过本地 gate，只能写 `local_verified_only`，不能写板上安全功能通过。

## 下一步行动卡（从这里继续）

### N1：A3-RGMII/MDIO 文档和约束收口（已执行）

- 当前状态：已执行到 RGMII/MDIO 和 LCD 子集自洽。ADC 不再按本旧记录认定为完成，必须按“ADC 方案一致性整改”重新核对。
- 前置条件：如后续复核本项，必须保持当前 wrapper 端口、active XDC 和 `docs/port_mapping.md` 三者一致。
- 输入材料：`../../z20-v1_5_20260623.xdc`、`constraints/z20_v1_5_active_mapped.xdc`、`docs/port_mapping.md`、当前 `system_wrapper.v`。
- 修改对象：只改 `docs/port_mapping.md`、本计划和必要的 active XDC 注释；不改老工程、不改 BD。
- 操作步骤：
  1. 在 `docs/port_mapping.md` 的 active 子集中列出 `mdio_mdc`、`mdio_mdio_io`、`rgmii_rxc`、`rgmii_rx_ctl`、`rgmii_rd[0..3]`、`rgmii_txc`、`rgmii_tx_ctl`、`rgmii_td[0..3]`。
  2. 把共享管脚表中 `B15,C15,C19,D15,D16,D17,D18,E15,E16,E19,E20,F16,G15,G16` 的决策改为 RGMII/MDIO 已按 wrapper-adapted 进入 active XDC，来源追溯到 v1.5 XDC。
  3. 确认 active XDC 中 RGMII timing 只引用当前 wrapper 端口：`rgmii_rxc`、`rgmii_txc`、`rgmii_rd[*]`、`rgmii_rx_ctl`、`rgmii_td[*]`、`rgmii_tx_ctl`。
  4. 运行 active assignment 检查；历史 active assignment 结果只能说明当时 active/top 自洽，不能替代当前 XADC 一路模拟量、完整手轮、36 路输入和 8 轴输出需求复核。
  5. 运行路径和老工程依赖扫描；若 Vivado 把 `.xpr` 的 `Path=` 改成盘符绝对路径，立即改回相对路径。
- 禁止动作：不回退 HDMI 放弃/MPG 绑定裁决；不推广 touch/serial/轴/未确认急停管脚；不把未确认端口写进 active XDC。
- 最小检查：
  ```powershell
  $files = @('new-vivado\z20_v1_5_hw_project\z20_v1_5_hw_project.xpr','new-vivado\新vivado工程计划.md') + (Get-ChildItem -LiteralPath 'new-vivado\z20_v1_5_hw_project\scripts','new-vivado\z20_v1_5_hw_project\docs','new-vivado\z20_v1_5_hw_project\constraints' -File -Recurse).FullName
  Select-String -LiteralPath $files -Pattern '(?<![A-Za-z])[A-Za-z]:[\\/]'
  rg -n "vivado_hw_project" new-vivado/z20_v1_5_hw_project -g "*.xpr" -g "*.bd" -g "*.tcl" -g "*.xdc" -g "*.v"
  git diff --check -- new-vivado
  ```
- 通过条件：当前 active XDC 为 180 个约束且无重复/缺失；文档、约束和计划一致；没有项目内绝对路径和老工程源依赖。
- 失败处理：如果端口缺失或管脚重复，先撤回本次新增的对应 active XDC 项，只保留 port_mapping 候选说明。

### N2：A9-RGMII/MDIO 与 XADC 后重新实现

- 当前状态：上一轮 DO/PWM forced-off gate、GMII pre-ODDR 总线 TX gate 与 generated AXI 急停观测同源补丁接入后已经生成 bitstream/XSA，并满足 `TIMING_STATUS=timing_met`。本轮 180 项 IO/owner 扩展已经完成 source/top/active XDC 一致性检查，但必须重新运行 `scripts/vivado_synth_current.tcl`、`scripts/vivado_impl_current.tcl` 和 XSA 导出后，才能把 bit/XSA 视为当前。
- 前置条件：N1 和 N3A 当前检查通过，active XDC 路径扫描 clean，ADC SPI 只允许作为 wrapper 内部退役端口存在，不能作为板级 active 端口。
- 输入材料：新工程 `.xpr`、active XDC、`scripts/vivado_impl_current.tcl`。
- 修改对象：Vivado run 产物、`reports/`、本计划和 README 的状态；不改老工程。
- 操作步骤：
  1. 从 `new-vivado/z20_v1_5_hw_project/` 运行 `vivado.bat -mode batch -source scripts/vivado_impl_current.tcl`。
  2. 记录 `TIMING_STATUS`、`TIMING_WNS`、`BUILD_STATUS`、`NSTD-1`、`UCIO-1`。
  3. 如果 timing 不满足，优先检查 RGMII clock/delay 约束是否引用了不存在端口或错误时钟；同时确认 XADC 相关路径没有被错误加入高速 timing 例外，不得用虚假 false path 掩盖。
  4. 如果 bitstream 仍因未约束端口 DRC 阻止，保持 `blocked_constraints_*`，继续 N3/N4。
  5. 如果 Vivado 改写 `.xpr` 路径，修回相对路径并重跑路径扫描。
- 禁止动作：不把 `NSTD-1`/`UCIO-1` 降级，不临时约束未知端口，不导出 XSA。
- 通过条件：implementation 报告可追溯；timing 和 DRC 状态写入计划；bitstream 只有在所有未确认顶层端口关闭后才允许生成。
- 失败处理：timing 失败先修约束；DRC 失败按端口映射继续闭环，不跳到 XSA 或板上验证。

### N3：A3-touch/serial 候选分组处理

- 前置条件：N2 已给出当前 DRC 缺口，且待处理端口来自当前 wrapper 已存在的顶层端口。
- 输入材料：`docs/port_mapping.md`、`../../z20-v1_5_20260623.xdc`、当前 wrapper、当前 active XDC。
- 修改对象：只改 `docs/port_mapping.md` 和确认后的 active XDC 条目。
- 操作步骤：
  1. 每次只处理一个功能组：`RS232`、`PL_UART`、`I2C/TP`、`RS485`，不得混在一次提交里。
  2. 对每个候选端口写清 wrapper 端口、旧管脚、新 v1.5 net、新管脚、方向、是否同物理管脚、是否存在命名冲突。
  3. 只有“wrapper 端口存在、物理管脚一致、方向一致、无复用冲突”的条目才能进入 active XDC。
  4. 触摸按老工程一致处理：`AA4` 为 touch int，`W7` 为 touch rst_n，不作为新增或语义变化 IO；`RS485_FPGA_TX` 这类仍存在方向或管脚差异的条目保持 fail-closed，当前旧 RS485 顶层端口已退役。
  5. 每提升一个功能组后重跑 active assignment 检查，并更新本计划 A3 状态；当前 CAN、PL UART、RS232、RS485、I2C3、触摸 I2C、TP_INT/TP_RST、完整 MPG、`EGS_DI`、DI/FR_DI/TS_DI、8 轴 PULS/DIR/ENA/ALM、编码器 ABZ、`BEEP_EN` 和 DO/PWM 顶层硬门控已提升，检查结果为 `active_assignments=180 missing=[] duplicate_ports={} duplicate_pins={}`。
  6. `rs485_rxd=U14` 不能再作为单个“物理脚一致”条目单独进入 active XDC；当前已通过 `system_top` 退役旧 RS485 顶层端口，后续必须先确认 RX/TX 语义、TX 新脚位和 `B13_IO_0` 证据，才能重建新 RS485 顶层端口和 active XDC。
- 禁止动作：不因减少 DRC 数量而约束语义未确认的串口、触摸或 RS485 输出。
- 通过条件：新增 active 条目逐项有 v1.5 来源注释，检查无缺失/重复，冲突项仍在 fail-closed 清单。
- 失败处理：任何方向/管脚/复用冲突或 Vivado 放置失败无法闭合时，该功能组不进入 active XDC，写入待复核清单；RS485_RX 单点提升已按此规则撤回。

### N3B：A3-剩余 bitstream DRC 端口闭环矩阵

- 当前状态：已执行并已被 N3C-2、RS485 导出、TP_INT/TP_RST 导出、N3D i2c4 BD 层退役、PL LED/GPIO0 BD 层退役、HDMI 放弃和 BD 级 HDMI 删除、完整 MPG、`EGS_DI`、DI/FR_DI/TS_DI、8 轴 PULS/DIR/ENA/ALM、编码器 ABZ、`BEEP_EN` 和 DO/PWM 顶层硬门控收窄到 0。`scripts/check_active_xdc.ps1` 当前输出 `top_module=system_top`、`active_assignments=180 missing=[] duplicate_ports={} duplicate_pins={}` 和 `unassigned_top_ports_count=0`；`docs/port_mapping.md` 已保留 Remaining Bitstream DRC Closure Matrix；`scripts/export_remaining_drc_ports.ps1` 已导出 header-only `docs/remaining_drc_ports.csv`。
- 前置条件：不新增 active XDC 条目；只根据现有 wrapper、active XDC 和 v1.5 XDC 做分组。
- 输入材料：`system_wrapper.v`、`constraints/z20_v1_5_active_mapped.xdc`、`../../z20-v1_5_20260623.xdc`、Vivado DRC 输出。
- 输出对象：`scripts/check_active_xdc.ps1`、`scripts/export_remaining_drc_ports.ps1`、`scripts/verify_remaining_drc_ports.ps1`、`docs/port_mapping.md` 剩余 DRC 闭环矩阵、`docs/remaining_drc_ports.csv`、本计划当前状态。
- 操作步骤：
  1. 用脚本列出当前 active XDC 的端口/管脚重复、缺失 wrapper 端口和剩余未约束顶层端口。
  2. 先将原 52 个剩余顶层端口按功能组归类；N3C-2、RS485 fail-closed top 更新、N3D i2c4 BD 层退役、PL LED fail-closed top 更新、GPIO0 BD 层退役和 HDMI BD 级退役后，当前 DRC 端口清单已收窄为 0。
  3. 用 `scripts/export_remaining_drc_ports.ps1` 生成 `docs/remaining_drc_ports.csv`，每行必须包含端口、方向、功能组、阻断类型、旧约束线索、v1.5 线索、active 同脚位占用证据、关闭条件和下一动作；bus 端口如 `step_o[0]`、`dir_o[0]`、`enc_a_i[0]` 必须解析到旧脚位。
  4. 对每组写明当前证据、关闭条件和 active XDC 决策，并把 CSV 分组计数和阻断类型计数写入计划状态。
  5. 任一组未满足关闭条件时保持 fail-closed，不允许用降级 DRC 或临时 PACKAGE_PIN 通过 bitstream。
- 禁止动作：不从矩阵直接生成 active XDC；不把旧 `system.xdc` 中的老语义当成新板事实；不把脚位相同等同于功能相同。
- 最小检查：
  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\check_active_xdc.ps1
  powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\export_remaining_drc_ports.ps1
  powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_remaining_drc_ports.ps1
  (Import-Csv new-vivado\z20_v1_5_hw_project\docs\remaining_drc_ports.csv).Count
  Import-Csv new-vivado\z20_v1_5_hw_project\docs\remaining_drc_ports.csv | Group-Object group | Sort-Object Name
  ```
- 通过条件：脚本输出与 Vivado DRC 剩余端口数量一致；当前 CSV 为 header-only，`csv_rows=0`；`verify_remaining_drc_ports.ps1` 输出 `check_active_unassigned_ports=0`、`csv_matches_check_active=yes`、`required_columns=present` 且无错误；`active_pin_conflicts=0`；旧 RS485 端口已通过 `system_top` 外壳退役，旧 i2c4 外部接口已通过 BD 层 fail-closed 退役，旧 PL LED 输出已通过 `system_top` 外壳退役，旧 GPIO0 外部接口已通过 BD 层 fail-closed 退役，旧 HDMI/TMDS 已通过 BD 层删除 `hdmi_out` 和外部 `tmds` 接口；当前 ADC 只按芯片自带 XADC 一路模拟量处理；未来如重新启用 HDMI 或改变 MPG 占用，必须另立变更、新增已确认端口和约束。
- 失败处理：如果脚本输出、CSV 行数或 Vivado DRC 不一致，先修脚本或 wrapper/active XDC 清单，不继续新增约束。

### N3C：A3-active 管脚已占用端口退役/重映射

- 当前状态：N3C-1/N3C-2 已执行并更新为当前 8 轴边界。`rtl/system_top.v` 已作为工程 top，`PULS1-8/DIR1-8` 由 wrapper `axis_puls_o[7:0]`、`axis_dir_o[7:0]` 经顶层急停 gate 输出，8 轴 `A/B/Z` 编码器输入进入 wrapper `axis_enc_*_i[7:0]`；`docs/active_pin_conflicts.md` 当前输出 `active_pin_conflicts=0`。ADC 已改为芯片自带 XADC 一路模拟量，`U10/U9/AA12/AB12` 不再作为外部 ADC 管脚处理。
- 已知 BD 连接事实：`step_ip/step_o[7:0]` 直接连接到 wrapper `axis_puls_o[7:0]`，`step_ip/dir_o[7:0]` 直接连接到 wrapper `axis_dir_o[7:0]`，wrapper `axis_enc_a/b/z_i[7:0]` 直接进入 `step_ip/enc_a/b/z_i[7:0]`。旧外部 ADC SPI 和旧轴外部 bus 端口不得恢复。
- 前置条件：ADC 只按芯片自带 XADC 一路模拟量处理；`U10/U9/AA12/AB12` 不能再作为外部 ADC 的保留前提。
- 输入材料：`docs/remaining_drc_ports.csv`、`docs/active_pin_conflicts.md`、`z20_v1_5_hw_project.srcs/sources_1/imports/hdl/system_wrapper.v`、`z20_v1_5_hw_project.srcs/sources_1/bd/system/system.bd`、`constraints/z20_v1_5_active_mapped.xdc`。
- 修改对象：N3C-1/N3C-2 已落在 `scripts/export_active_pin_conflicts.ps1`、`docs/active_pin_conflicts.md`、`docs/port_mapping.md`、本计划、`rtl/system_top.v`、`.xpr`、`scripts/check_active_xdc.ps1`、`scripts/export_remaining_drc_ports.ps1`、`scripts/verify_remaining_drc_ports.ps1` 和 `scripts/vivado/add_failclosed_system_top.tcl`；后续如需再改接口，只允许修改新工程内的 BD/top wrapper/可重复接口脚本和文档；不得修改老工程，不得把这 3 个旧端口重新写入 active XDC。
- 操作步骤：
  1. N3C-1：已扩展 `scripts/export_active_pin_conflicts.ps1`，从当前 `system.bd` 读取每个冲突端口和 active XDC 占用情况，导出到 `docs/active_pin_conflicts.md`。报告至少包含旧 wrapper 端口、旧 pin、v1.5 同 pin net、active XDC port、旧 BD 连接、active BD 连接和 required action。
  2. N3C-1：已完成历史冲突证据确认。旧报告曾确认 `step_o[0]` 归属 `step_ip/step_o`，`enc_a_i[3]`、`enc_a_i[4]` 归属 `step_ip/enc_a_i`；现在 ADC 已改为芯片自带 XADC 一路模拟量，旧外部 SPI ADC 口径作废。N3C-2 后当前检查口径为 `active_pin_conflicts=0`。
  3. N3C-2：已判定并执行 fail-closed 外壳方案。`step_o[0]` 所属旧 `step_o[]`/`dir_o[]` 外部顶层端口整体退役，不再出板级 top；`enc_a_i[3]`、`enc_a_i[4]` 所属旧 `enc_*_i[]` 在 `system_top` 内明确 tie-off 为 0；后续若重新启用轴/编码器/DI/MPG，必须先完成 v1.5 轴模型和新 XDC 管脚归属，再通过可重复脚本重新映射，不能恢复旧外部 ADC 管脚语义。
  4. 如果选择退役或内部 tie-off，必须通过 BD/Tcl 或可重复的 wrapper 生成流程处理 `step_ip` 连接和顶层端口集合；不得只手工编辑生成 wrapper。处理后更新 `docs/remaining_drc_ports.csv`、`docs/active_pin_conflicts.md` 和 `docs/port_mapping.md`。
  5. 如果选择重映射，必须先有 v1.5 XDC/原理图/接线证据、未被 active XDC 占用的目标管脚、明确 IOSTANDARD 和方向；重映射后才允许新增 active XDC 条目。
  6. 改动后重新运行 `scripts/check_active_xdc.ps1`、`scripts/verify_remaining_drc_ports.ps1`、`scripts/export_active_pin_conflicts.ps1`，并在 RTL/BD/top 变化时运行 Vivado validate/synthesis。
- 禁止动作：不恢复旧外部轴 bus 端口；不把旧轴语义当作新 v1.5 轴语义；不通过恢复旧外部 ADC 端口来回避冲突；不只改 generated wrapper 而不留下可重复 BD/top 更新路径。
- 最小检查：
  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\export_active_pin_conflicts.ps1
  powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\check_active_xdc.ps1
  powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_remaining_drc_ports.ps1
  Select-String -LiteralPath new-vivado\z20_v1_5_hw_project\docs\active_pin_conflicts.md -Pattern 'step_ip/step_o|step_ip/enc_a_i|active_pin_conflicts'
  git diff --check -- new-vivado
  ```
- 通过条件：N3C-1 通过时，`docs/active_pin_conflicts.md` 与 CSV 中历史 `active_pin_already_claimed` 行一致，且每行都含旧 BD 连接和 active BD 连接；N3C-2 通过时，这 3 个旧端口均有退役、内部 tie-off 或重映射决策并已落到可重复 top 更新路径；当前 `rtl/system_top.v` 已完成 RS485、PL LED 源级 fail-closed 更新，N3D 已完成 i2c4 BD 层 fail-closed 退役，GPIO0 已完成 BD 层 fail-closed 退役，HDMI 已完成 BD 层 `hdmi_out`/`tmds` 删除，`check_active_xdc.ps1` 输出 `top_module=system_top` 和 `unassigned_top_ports_count=0`，`verify_remaining_drc_ports.ps1` 输出 `csv_rows=0`、`active_pin_conflicts=0`；XADC 一路模拟量不占用 `U10/U9/AA12/AB12`。
- 失败处理：如果无法决定退役、tie-off 或重映射，保持 fail-closed，不新增 active XDC 约束，不生成 bitstream，并继续停留在 N3C；缺少 v1.5 轴/编码器/DI/MPG 归属证据时，先关闭对应证据而不是猜测管脚。

### N3E：A3-编码器输入打开计划

- 当前状态：local_source_done。用户最新要求编码器输入要打开；当前 8 轴 `A/B/Z` 已进入 `system_top`、active XDC 和 wrapper `axis_enc_*_i[7:0]`，并直接接入 BD `step_ip` 8 轴编码器输入。后续仍需补正式软件/寄存器读出 owner、轴号和方向复核。
- 目标：在新 Vivado 工程内恢复/新增编码器输入通路，使真实编码器输入能进入 PL/step_ip 或后续确认的编码器 owner；同时保持未确认输入 fail-closed，不恢复旧工程物理语义。
- 输入材料：`z20-v1_5_20260623.xdc`、`docs/port_mapping.md`、`docs/active_pin_conflicts.md`、`z20_v1_5_hw_project.srcs/sources_1/bd/system/system.bd`、`z20_v1_5_hw_project.srcs/sources_1/imports/hdl/system_wrapper.v`、`rtl/system_top.v`、`constraints/z20_v1_5_active_mapped.xdc`。
- 操作步骤：
  1. 先盘点 v1.5 XDC 中所有可能属于编码器的输入脚，逐项区分真实编码器 A/B/Z、普通 DI/FR_DI、MPG/SCALE、触摸/串口/备用 IO 和 DO/PWM 候选。
  2. 明确新工程编码器 owner：若继续接 `axi_stepdir_enc_v2`/`step_ip`，必须确认轴数、索引、A/B/Z 方向和信号极性；若改为新 wrapper 端口名，必须同步 `system_top.v`、BD wrapper、port mapping 和 active XDC。
  3. 已确认的 8 轴编码器输入必须保持接入 `axis_enc_*_i[7:0]`；未确认的新输入只能继续保持 0 或安全默认态，不得为了减少 DRC 恢复退役轴外部 bus。
  4. 对已确认的编码器输入新增 active XDC 约束时，必须使用当前 wrapper/top 真实端口名、`LVCMOS33`、唯一 PACKAGE_PIN，并记录来源行；不得直接把老工程 `system.xdc` 的旧语义当成新板事实。
  5. 更新 `docs/port_mapping.md` 和 `docs/active_pin_conflicts.md`，写清每个打开的编码器输入：新板网络名、package pin、当前顶层端口、BD/RTL owner、是否仍存在复用冲突。
  6. 修改后运行 `scripts/check_active_xdc.ps1`、`scripts/export_remaining_drc_ports.ps1`、`scripts/verify_remaining_drc_ports.ps1`、`scripts/export_active_pin_conflicts.ps1`；如果 RTL/BD/top 变化，还必须重新运行 Vivado synthesis/implementation 并刷新 timing、warning summary、handoff 和 manifest。
- 禁止动作：不得恢复退役的轴外部 bus 到板级 top；不得把 DI/FR_DI/MPG/SCALE/DO/PWM 候选误当编码器输入；不得只改 generated wrapper 而不保留可重复 BD/top/Tcl 路径；不得在未确认端口存在前向 active XDC 写 PACKAGE_PIN。
- 当前通过条件：编码器输入端口已存在，active XDC 无重复 pin/port，remaining DRC 与 active conflict 均闭合；本轮 180 项 IO/owner 扩展后还必须重新运行 Vivado synthesis/implementation，且当前结论仍只能是 `local_verified_only`，不声明板上编码器已实测。
- 失败处理：只要编码器管脚语义、轴索引、方向或复用冲突没有确认，对应位继续 fail-closed；不能用临时 PACKAGE_PIN 或降低 DRC 规则生成 bitstream。

### N3D：A3-i2c4 剩余 DRC 关闭路径

- 当前状态：local_verified_bd_failclosed。`i2c4_scl_io`/`i2c4_sda_io` 原属于 20 个剩余未约束端口中的 `spare_i2c4_fpga1_io5=2`。已试验“只在 `system_top` 外层隐藏 i2c4 端口并用内部 `tri1`/tie-off 保持空闲”的退役方式，综合可继续，但 implementation 在 `place_design` 失败；根因是 `system_wrapper.v` 内 generated `i2c4_scl_iobuf`/`i2c4_sda_iobuf` 仍实例化 IOBUF，外层隐藏端口后这些 IOBUF 不再直接连接真实 top-level I/O，IO placer 无法放置。随后已执行 `scripts/vivado/retire_i2c4_failclosed.tcl`，在 BD 层删除外部接口 `i2c4`/`i2c4_ms7200_IIC`，把 `i2c4/scl_i` 和 `i2c4/sda_i` 接到 `cnc_const_one/dout` 空闲高电平，重新生成 wrapper，并从 `system_top` 移除 i2c4 顶层端口。当前恢复实现后剩余 18 个顶层 DRC 端口，未再出现 `system_wrapper_i/i2c4_*_iobuf` 放置错误。
- 输入材料：`rtl/system_top.v`、`z20_v1_5_hw_project.srcs/sources_1/imports/hdl/system_wrapper.v`、`docs/remaining_drc_ports.csv`、`docs/port_mapping.md`、`../../z20-v1_5_20260623.xdc`、原理图/外接设备证据。
- 可执行路径 1：确认 `W12/V12` 上确实接外部 I2C 设备且有正确上拉、电压域和复用选择；然后保留 `system_top` 的 `i2c4_scl_io`/`i2c4_sda_io` 顶层端口，在 active XDC 中以当前 wrapper 端口名约束到 `W12/V12`，标注来源为 `FPGA1_IO5_P/N` 的确认 I2C 用途，再运行 `check_active_xdc.ps1`、`verify_remaining_drc_ports.ps1`、implementation。
- 可执行路径 2：已执行。未确认 `W12/V12` 外部 I2C 用途时，不能只在 `system_top` 外层藏端口，必须在 BD/custom wrapper 层关闭这路 I2C 的 IOBUF 来源；当前选择为删除 BD 外部接口并把内部 I2C 输入按空闲高电平 fail-closed，脚本为 `scripts/vivado/retire_i2c4_failclosed.tcl`。已更新 `system_top`、CSV、port mapping，并重跑 source checks、synthesis、implementation。
- 禁止动作：不使用外层 `tri1`/内部 wire 隐藏 generated IOBUF；不把 `FPGA1_IO5_P/N` 当普通备用 IO 临时约束过 DRC；不通过降低 `NSTD-1`/`UCIO-1` 生成 bitstream。
- 通过条件：已满足本地条件。`spare_i2c4_fpga1_io5` 已从 CSV 中消失；`check_active_xdc.ps1`、`verify_remaining_drc_ports.ps1`、`export_active_pin_conflicts.ps1` 一致；Vivado implementation 不再出现 `system_wrapper_i/i2c4_*_iobuf` 未放置错误。后续 PL LED、GPIO0 和 HDMI/MPG fail-closed 后，当前已无 `NSTD-1`/`UCIO-1` 端口阻断。
- 失败处理：如果未来确认 `W12/V12` 需要真实外部 I2C，必须回到路径 1，用新证据重新导出端口和 active XDC；在此之前不新增 active XDC，不启动 bitstream/XSA。

### N3A：A3-ADC 一路 XADC 模拟量收口

- 当前状态：需求已改为 1 路 0-10V 模拟量，使用 Zynq 芯片自带 XADC 模拟量脚。当前 `new-vivado/z20-v1_5_20260623.xdc` 的 ADC 段写成 `XADC_VP=L11`、`XADC_VN=M12` 单通道，和当前需求一致。
- 必须保持：`ADC_IN1` 只作为一路模拟量输入，经硬件保护、分压、滤波后进入 `XADC_VP/XADC_VN`；不再新增 `ADC_IN2`，不再引入外部 SPI ADC。
- 禁止动作：不得把 `U10/U9/AA12/AB12` 写成外部 ADC，也不得把 XADC VP/VN 写成两路独立 ADC。
- 最小检查：
  ```powershell
  Select-String -LiteralPath 'new-vivado\z20-v1_5_20260623.xdc' -Pattern 'ADC|XADC|ADC_SPI|ADC_IN2'
  Select-String -LiteralPath 'new-vivado\z20_v1_5_hw_project\constraints\z20_v1_5_active_mapped.xdc' -Pattern 'ADC|XADC|ADC_SPI|U10|U9|AA12|AB12'
  Select-String -LiteralPath 'new-vivado\z20_v1_5_hw_project\rtl\system_top.v' -Pattern 'adc|spi|xadc'
  ```
- 通过条件：源 XDC、active XDC、BD wrapper、`system_top.v` 和软件 handoff 都只描述一路 XADC 模拟量；不得出现外部 ADC 或两路模拟量 handoff。

### N4：A4-PL 急停硬件证据收口

- 前置条件：必须有原理图或现场接线证据；仅凭 `pl急停.md`、老 `system.xdc` 或信号名不能确认真实安全链。
- 输入材料：`pl急停.md`、`docs/pl_estop_integration.md`、`../../z20-v1_5_20260623.xdc`、原理图、实际接线记录。
- 修改对象：`docs/pl_estop_integration.md`、本计划；管脚未确认前不改 active XDC。
- 操作步骤：
  1. 确认 NC 急停输入：net 名、管脚、连接器脚、正常电平、断线/掉电状态、上拉/下拉。
  2. 确认 STO/enable 输出：对应驱动、极性、默认安全态、是否由 PL 直接封锁。
  3. 确认 Z 或垂直轴抱闸输出：管脚、极性、抱闸先动作时间。
  4. 确认当前 8 轴 DB15 输出、ENA 占位、STO/抱闸和通用 DO/PWM 的封锁点映射。
  5. 只有 E1/E2/E3 全部关闭后，才允许创建 E11 active XDC 接线计划。
- 禁止动作：不把 `EGS_DI`、`DO*`、`PWM*`、`ENA*_IO` 当作已确认安全信号；不接 `pl_estop` 输出到真实顶层端口。
- 通过条件：急停输入、STO/enable、抱闸、轴封锁点均有物理证据和安全默认态；否则 A4 保持 `blocked_external_wiring`。
- 失败处理：缺任一证据时继续 fail-closed，PL 急停状态最多保持 `local_verified_only`。

### N5：A10 与代码侧复查启动条件

- A10 只在 bitstream fresh 且位于新工程目录下时启动；否则保持 `blocked_by_A9_bitstream`。
- A11/BV 实际测量测试已按最新计划取消，不作为当前交付门槛启动。
- 当前只启动代码侧复查：RTL、BD、active XDC、仿真、timing、warning summary、register map、handoff 和 manifest；任何阶段不得把本地 Vivado timing、RTL 仿真或 BD validation 写成板上安全功能已验证。

## A7 BD 接入行动卡（已执行）

### A7 目标

把已通过本地仿真的 `pl_estop_core.v` 和 `pl_estop_axi_lite.v` 接入当前 BD，使 PS 可以通过 AXI 读到急停状态并接收 IRQ。由于外部急停、STO、抱闸、轴封锁接线仍未确认，本轮只允许做内部可观测和 fail-closed 接入，不允许驱动真实外部安全输出。

### A7 前置条件

- `rtl/pl_estop_core.v` 和 `rtl/pl_estop_axi_lite.v` 已在新工程内存在。
- `sim/pl_estop_core_tb.v` 和 `sim/pl_estop_axi_lite_tb.v` 已本地通过。
- 当前 BD 已能 `validate_bd_design`。
- `ps7_0_axi_periph/M20_AXI` 已被 `z20_dna_reader` 使用；PL 急停使用下一个 AXI master slot。
- `xlconcat_0/In0` - `xlconcat_0/In13` 已被现有中断占用；PL 急停使用 `In14`。
- 外部 E-stop/STO/brake/axis mapping 未确认，A4 仍是 `blocked_external_wiring`。

### A7 修改对象

- 新增或更新：`z20_v1_5_hw_project/scripts/vivado/add_pl_estop_axi_lite.tcl`
- 更新：`z20_v1_5_hw_project/z20_v1_5_hw_project.xpr`
- 更新：`z20_v1_5_hw_project/z20_v1_5_hw_project.srcs/sources_1/bd/system/system.bd`
- 更新文档：本计划、`z20_v1_5_hw_project/docs/pl_estop_integration.md`、`z20_v1_5_hw_project/README.md`

### A7 禁止动作

- 不修改 `vivado_hw_project/`。
- 不把急停、STO、抱闸、`PULS/DIR/ENA` 或当前 `step_o/dir_o` 写入 active XDC。
- 不把 `step_out`、`enable_out`、`brake_z_out` 连接到真实顶层端口。
- 不修改 LinuxCNC/HAL、UI、Broker 或板上文件。
- 不把机器本地 Vivado 安装路径写进 `.xpr`、Tcl、README 或本计划。

### A7 BD 连接方案

| 对象 | 操作 | 具体连接 | 通过条件 |
| --- | --- | --- | --- |
| RTL source | 脚本内确认加入工程 | `rtl/pl_estop_core.v`、`rtl/pl_estop_axi_lite.v` | `sources_1` 中存在且路径为新工程相对路径 |
| module ref | 创建 BD cell | `pl_estop` -> `pl_estop_axi_lite` | BD 中只有一个 `pl_estop` cell |
| AXI | 扩展 PS AXI interconnect | `ps7_0_axi_periph/M21_AXI` -> `pl_estop/S_AXI` | `CONFIG.NUM_MI >= 22`，连接存在 |
| clock | 接系统 FCLK | `processing_system7_0/FCLK_CLK0` -> `pl_estop/S_AXI_ACLK`、`ps7_0_axi_periph/M21_ACLK` | 无未连接 AXI clock |
| reset | 接外设复位 | `rst_ps7_0_100M/peripheral_aresetn` -> `pl_estop/S_AXI_ARESETN`、`ps7_0_axi_periph/M21_ARESETN` | 无未连接 AXI reset |
| fail-closed input | 用现有常量保持急停态 | `cnc_const_zero/dout` -> `pl_estop/estop_nc_in` | 物理输入未确认前，PL 急停保持锁存安全态 |
| dummy axis input | 用 0 向量占位 | 新建 `pl_estop_axis_zero`，宽度 6，值 0；接 `step_in` 和 `enable_in` | 不驱动真实轴输出 |
| IRQ | 接 PS IRQ concat | `pl_estop/estop_irq` -> `xlconcat_0/In14` | `CONFIG.NUM_PORTS >= 15` |
| AXI address | 固定地址窗口 | `pl_estop/S_AXI/Reg` -> `0x41260000`，range `64K` | address editor 中无重叠 |
| 输出 | 保持未接真实硬件 | `step_out`、`enable_out`、`brake_z_out` 暂不接外部端口 | 未确认硬件前不产生真实输出 |

### A7 脚本要求

`scripts/vivado/add_pl_estop_axi_lite.tcl` 必须是幂等脚本：

1. 从脚本自身路径推导 `project_dir`，打开 `z20_v1_5_hw_project.xpr`，不使用盘符绝对路径。
2. 如果 RTL 已在工程中，不重复添加；如果缺失，则从 `rtl/` 相对路径添加。
3. 如果 BD cell `pl_estop` 已存在，不重复创建；否则 `create_bd_cell -type module -reference pl_estop_axi_lite pl_estop`。
4. 如果 `ps7_0_axi_periph` 的 `CONFIG.NUM_MI` 小于 22，则提升到 22。
5. 只连接缺失的 net/interface，重复运行不能产生重复连接或重名 cell。
6. 地址分配后强制设置 PL 急停 AXI base 为 `0x41260000`，range 为 `64K`。
7. 最后执行 `validate_bd_design` 和 `save_bd_design`。

### A7 执行命令

从 `new-vivado/z20_v1_5_hw_project/` 执行：

```powershell
vivado.bat -mode batch -source scripts/vivado/add_pl_estop_axi_lite.tcl
vivado.bat -mode batch -source scripts/vivado_validate_current.tcl
```

如果本机 `vivado.bat` 不在 `PATH`，只在当前 shell 设置可执行文件，不写入工程文件。

### A7 完成后的文本和路径检查

从 `PROJECT_ROOT` 执行：

```powershell
rg -n "[A-Za-z]:[\\/]" new-vivado/z20_v1_5_hw_project/z20_v1_5_hw_project.xpr new-vivado/z20_v1_5_hw_project/scripts new-vivado/z20_v1_5_hw_project/docs new-vivado/新vivado工程计划.md
rg -n "vivado_hw_project" new-vivado/z20_v1_5_hw_project
git status --short -- vivado_hw_project new-vivado
git diff --check -- new-vivado
```

若 Vivado 自动把 `.xpr` 的 `Path=` 改回本机绝对路径，必须立即改回相对路径再重新跑文本检查。

### A7 通过条件

- `add_pl_estop_axi_lite.tcl` 可重复运行，不产生重复 cell、重复 net 或地址重叠。
- `validate_bd_design` 通过，无新增 blocker。
- PL 急停 AXI 地址固定为 `0x41260000`，range `64K`。
- `pl_estop/estop_irq` 已进入 `xlconcat_0/In14`。
- `estop_nc_in` 当前接常量 0，体现未接外部安全链时 fail-closed。
- `step_out`、`enable_out`、`brake_z_out` 未接真实硬件。
- 新工程文本文件无项目内绝对路径、无老工程源依赖。
- `vivado_hw_project/` 仍无修改。

### A7 失败处理

- 如果 BD validation 失败，先修复 `add_pl_estop_axi_lite.tcl` 或 BD 连接，不进入 implementation。
- 如果地址冲突，重新读取 address map，选择下一个空闲 64K 窗口，并同步更新本计划和 `docs/pl_estop_integration.md`。
- 如果 Vivado 生成绝对路径，先修复 `.xpr` 或脚本路径写法，再继续。
- 如果发现必须连接真实 E-stop/STO/brake/axis 输出才能继续，A7 停止，状态保持 `blocked_external_wiring`，先补齐 A4 接线证据。

## 主要差异

- 老工程顶层端口是 BD/Wrapper 命名，例如 `step_o[]`、`dir_o[]`、`enc_*[]`、`lcd_rgb_tri_io[]`、`rgmii_*`。
- 新 XDC 使用原理图网络名，例如 `PULS*_IO`、`DIR*_IO`、`LCD_*`、`GMAC2_*`。
- 当前新工程按 8 轴 DB15 网络和新增 DI/DO/MPG/ADC/急停边界收口；旧轴接口只允许作为已退役历史背景，不得作为可恢复实现路径。
- HDMI/DVI 与 MPG 复用冲突已按最新用户裁决关闭：放弃 HDMI/DVI，9 个共享脚全部绑定 MPG；不得再把这些脚作为 HDMI/TMDS 输出推进。
- 一路 0-10V 模拟量使用芯片自带 `XADC_VP/XADC_VN`；不再外接 ADC，两路 ADC 和 `U10/U9/AA12/AB12` 外部 ADC 方案取消。
- 触摸脚位按老工程一致处理：`AA4` 为 touch int，`W7` 为 touch rst_n；当前 `TP_INT` 进入 IO owner 状态，`TP_RST` 由 IO owner reset 寄存器驱动并默认释放高。RS485 当前已通过 PS UART1 EMIO 导出到 `RS485_FPGA_RX=U14`、`RS485_FPGA_TX=R7`，但 `B13_IO_0` 重复标签仍需最终 PCB/HDL release 前复核；HDMI TMDS 与 MPG `AXIS_SEL*` 复用冲突已关闭，未来重启 HDMI 必须先撤销 MPG 占用并重新评审。

## PL 急停纳入范围

`new-vivado/pl急停.md` 是新 Vivado 工程的安全功能输入之一，但不能直接当作已实现逻辑。纳入计划时按以下边界执行：

- 输入原则：急停按钮和限位安全链采用常闭回路，正常为高电平，急停、断线、掉电或光耦故障时为低电平，PL 必须 fail-safe 触发。
- PL 逻辑：需要新增或扩展硬件急停模块，包含 10ms - 20ms 积分型去抖、急停锁存、硬件封锁输出、状态寄存器和软件复位互锁。
- 硬动作输出：PL 急停触发后必须直接封锁轴脉冲/PWM、STO 或使能相关输出；垂直轴抱闸需要先动作，再延迟数十微秒封锁对应轴驱动。
- 总线 TX 门控：接 PL 的总线驱动/控制发送在急停时只封锁 TX 发送使能或 driver-enable，使总线 TX 进入 idle/off；不得为了急停动作而断开物理 Link、复位 PHY/收发器、关链路时钟/电源/复位或关闭 RX/状态观测。解除急停前必须清空或失效急停期间积压的 TX FIFO、命令或控制帧。
- PS 状态链路：PL 急停触发后需要通过 IRQ 或 AXI 状态寄存器让 PS/Broker/SHM/UI 能观测状态，但 PS 观测不是急停动作的前置条件。
- 复位互锁：物理急停仍为低电平时，PS 写任何复位/使能寄存器都必须被 PL 硬件拒绝；解除必须满足物理恢复高电平和软件复位两步。
- 端口来源：急停输入、STO、抱闸、脉冲封锁相关引脚必须从 `z20-v1_5_20260623.xdc` 和硬件接线共同确认；未确认前不得接入 active 输出。
- 验证要求：当前计划只做代码侧复查，不能只靠单一 Vivado 综合/实现结论关闭；必须同时复查 RTL、BD、active XDC、仿真、timing、warning summary、generated netlist patch、register map、handoff 和 manifest。
- 结论边界：本计划不执行硬件接线、板上示波器证据和断线测试；状态只能写 `local_verified_only` / `code_review_only`，不能写安全功能已完成、已实测、已认证或可上线。

### PL 急停实现方案

PL 急停必须按“硬件先动作、软件后观察”的结构实现。PS/LinuxCNC/UI 可以显示状态、停 G 代码、发复位请求，但不能作为急停物理动作的前置条件。

实现链路：

1. 外部急停按钮、限位安全链或安全继电器输出以常闭 NC 回路接入 PL 输入。
2. PL 输入先经过双触发器同步，避免异步输入亚稳态。
3. 同步后的输入进入 10ms - 20ms 积分型去抖模块；输入持续为低才确认急停，短毛刺只计入滤波，不触发锁存。
4. 急停确认后进入锁存模块；锁存后即使物理输入恢复高电平，也保持急停状态，等待软件复位条件。
5. 锁存状态直接进入输出封锁模块，封锁 step/PWM/STO/使能等硬件输出；该路径不等待 PS 中断、AXI 读写、LinuxCNC 或 UI。
6. 垂直轴需要独立抱闸时序：急停锁存后先动作抱闸输出，再经硬件计数器延迟数十微秒后封锁对应垂直轴驱动输出。
7. 同一急停状态通过 AXI-Lite 状态寄存器和可选 IRQ 上报给 PS，供 Broker、SHM 和 UI 显示。
8. 复位必须满足双条件：物理 NC 输入已恢复健康高电平，且 PS 通过 AXI 写入软件复位请求；任一条件不满足时 PL 拒绝恢复输出。
9. 总线 TX 门控与本地硬关断分开实现：急停锁存时封锁 TX send-enable/driver-enable，保持 Link/时钟/复位/RX 活着；复位允许恢复 TX 前先清空或失效急停期间积压的发送请求。

建议模块边界：

- `pl_estop_filter`：输入同步、NC 低有效判断、积分型去抖和断线低电平识别。
- `pl_estop_latch`：急停锁存、物理低电平强制保持、软件复位接受条件。
- `pl_estop_gate`：step/PWM/STO/enable 输出封锁；优先插在现有运动输出到顶层端口之间，不能只做状态显示。
- `pl_estop_general_output_gate`：`DO1` - `DO14`、`PWM1` - `PWM2` 输出封锁；急停锁存时按已确认极性强制 off。
- `pl_estop_bus_tx_gate`：总线 TX send-enable/driver-enable 封锁；急停锁存时强制 TX idle/off，但保持 Link、时钟、复位、MDIO/配置和 RX 观测路径不断。
- `pl_estop_brake_timer`：垂直轴抱闸提前动作和延迟封锁计数。
- `pl_estop_axi`：AXI-Lite 状态/控制寄存器、复位写入、可选 IRQ 触发。

寄存器建议：

- `STATUS.estop_latched`：PL 已锁存急停。
- `STATUS.estop_input_raw`：同步后的物理输入状态。
- `STATUS.estop_input_filtered`：去抖后的物理输入状态。
- `STATUS.reset_allowed`：物理输入已恢复且允许软件复位。
- `STATUS.brake_delay_active`：垂直轴抱闸延迟计数中。
- `STATUS.general_output_forced_off`：通用 DO/PWM 输出正在被急停门控强制 off；当前已实现为 `STATUS[6]`，由 16 路本地占位 gate 驱动，真实 DO/PWM pin 接入后继续使用该状态位。
- `STATUS.bus_tx_gate_active`：总线 TX send-enable 或 driver-enable 正被急停门控保持 idle/off；当前已实现为 `STATUS[7]`，由 1 路本地占位 gate 驱动，真实总线接入后继续使用该状态位。
- `STATUS.bus_tx_queue_flushed`：急停锁存期间的积压发送请求已被清空或失效；当前已实现为 `STATUS[8]`，BD 占位接常量 1；存在真实 TX FIFO/队列时，恢复 TX 前必须由真实队列 owner 确认成立。
- `CONTROL.reset_request`：PS 请求解除锁存；物理输入仍低时必须被 PL 拒绝。
- `CONTROL.irq_clear`：PS 清除中断标志；不能清除急停锁存本身。

初期接入策略：

- 未确认急停输入、STO、抱闸、轴输出对应新板管脚前，不把这些信号加入 active XDC，也不驱动真实硬件输出。
- 先在 RTL/BD 中保留模块接口和 AXI 状态路径，输出保持安全默认态或未连接状态。
- 轴封锁优先在现有 `step_o[]`、`dir_o[]`、使能/STO 候选信号离开 PL 前统一门控；如果后续切换为 8 轴 `PULS/DIR/ENA/A/B/Z/ALM` 网络，必须重新确认封锁点。
- 总线门控只允许接到已经确认的 TX send-enable、driver-enable、TX idle 或 TX_CTL 类信号；不得用 PHY reset、Link down、电源关闭或关闭 RX 来代替。RGMII/CAN/RS485/现场总线各自必须先确认信号 owner 和 idle/off 电平。
- 如果修改 `axi_stepdir_enc_v2`，急停子模块仍要保持独立边界；不能把去抖、锁存、复位互锁和 AXI 状态散落成临时 wrapper 逻辑。
- 任何 SIL3/PLe 文字只能作为设计目标或参考等级，不能在未完成安全认证和板级证据前作为已达成结论。

### PL 急停专项执行清单

| ID | 当前状态 | 动作 | 输入 | 输出文件或对象 | 下一步 | 通过条件 |
| --- | --- | --- | --- | --- | --- | --- |
| E1 | blocked_external_wiring | 确认急停输入候选 | `z20-v1_5_20260623.xdc`、原理图、实际接线 | `docs/pl_estop_integration.md` 候选表 | 人工确认 `EGS_DI` 或其他 DI 是否为 NC 安全链 | 写清管脚、极性、上拉/下拉、是否 NC；不确认则保持 `unassigned` |
| E2 | blocked_external_wiring | 确认被封锁输出 | 当前 8 轴 DB15 网络、驱动接线、DO/PWM 接线、总线 TX/driver-enable 接线 | `docs/pl_estop_integration.md` 输出表 | 人工确认 STO、enable、brake、step/PWM、DO/PWM、总线 TX gate 的实际接线 | step/PWM/STO/enable/抱闸/DO/PWM/总线 TX 分别标出封锁点 |
| E3 | local_verified | 确认时钟和计数参数 | BD 时钟、目标去抖时间、抱闸提前时间 | `rtl/pl_estop_core.v`、`rtl/pl_estop_axi_lite.v`、BD `system.bd`、`scripts/verify_pl_estop_timing_params.ps1` | 后续修改 PL 急停时钟、去抖、抱闸提前或轴数前必须先更新本 gate | 当前 `CLK_HZ=100000000`、`DEBOUNCE_MS=10`、`BRAKE_LEAD_US=50`、`AXIS_COUNT=8`；脚本已推导 `debounce_cycles=1000000`、`brake_cycles=5000`，并确认 `pl_estop/S_AXI_ACLK` 挂在 `processing_system7_0/FCLK_CLK0` 100MHz 时钟网 |
| E4 | local_verified | 实现 `pl_estop_filter` | E3、NC 低有效原则 | `rtl/pl_estop_core.v` | 后续只在管脚确认后复核极性，不重写已验证逻辑 | 两级同步、低有效、积分去抖，断线低电平触发 |
| E5 | local_verified | 实现 `pl_estop_latch` | E4 | `rtl/pl_estop_core.v` | 保持锁存和复位互锁独立 | 急停锁存后必须等待物理恢复和软件复位 |
| E6 | local_verified | 实现 `pl_estop_brake_timer` | E3、E5 | `rtl/pl_estop_core.v` | 管脚确认后再连接真实 brake 输出 | 垂直轴抱闸先动作，延迟后封锁对应轴驱动 |
| E7 | local_verified_unwired | 实现 `pl_estop_gate` | E2、E5、E6 | `rtl/pl_estop_core.v` | 真实门控点未确认前保持输出不接外部硬件 | 急停锁存直接封锁硬件输出，不等待 PS |
| E7A | code_review_only | 实现 `pl_estop_general_output_gate` | E5；当前取消 DO/PWM off 极性和负载板测 gate | `rtl/pl_estop_core.v`、`rtl/pl_estop_axi_lite.v`、`rtl/system_top.v`、active XDC `do_out[13:0]`/`pwm_out[1:0]`、AXI `STATUS[6]` | 代码侧复查 owner 必须保持在 gate 前级，不允许绕过急停 | 16 路顶层 hard gate 已实现，急停锁存时强制到默认 safe level 0；正常 owner 当前为 `z20_v15_io_owner_axi_lite`，板级 off 极性和负载验证不在当前计划内 |
| E7B | code_review_only | 实现 `pl_estop_bus_tx_gate` | E5；当前取消 RGMII TX Link/RX/释放行为和 stale TX 实测 gate | `rtl/pl_estop_core.v`、`rtl/pl_estop_axi_lite.v`、`rtl/system_top.v`、BD synth `system_rgmii_tx_ctl_estop_gate`、AXI `STATUS[7]`/`STATUS[8]` | 代码侧复查 gate 位于 `gmii2rgmii` 前且不关闭 Link/RX/MDIO/时钟 | GMII pre-ODDR TX send gate 已实现：急停锁存时封锁 `TX_EN`、`TXD=0`、`TX_ER=0`，RGMII 输出仍由 `gmii2rgmii` 直接驱动；板上 TX 停止、Link/RX 保持和释放恢复不在当前计划内 |
| E8 | local_verified | 实现 `pl_estop_axi` | E5、E6、E7 | `rtl/pl_estop_axi_lite.v` | 已在 A7 中接入 PS AXI 和 IRQ | 状态可读，低电平时复位写入被拒绝，IRQ 清除不解除锁存 |
| E9 | local_verified_current_scope | 写仿真 | E4 - E8 | `sim/pl_estop_core_tb.v`、`sim/pl_estop_axi_lite_tb.v` | 修改 RTL 后必须重跑两个 testbench；后续真实 DO/PWM 极性、真实总线 idle 极性或真实 TX 队列 owner 接入时必须扩展用例 | 当前 scope 已覆盖低电平触发、短毛刺不触发、断线触发、复位拒绝、抱闸提前、16 路 DO/PWM 默认 0 forced-off、AXI `STATUS[6]`、1 路总线 TX 默认 idle 0、AXI `STATUS[7]`/`STATUS[8]`、TX 队列未清空时拒绝复位 |
| E10 | local_verified_internal_bd | 接入 BD | E8、PS IRQ/AXI 资源 | BD、`.xpr`、`scripts/vivado/add_pl_estop_axi_lite.tcl` | 后续只在 E1/E2 关闭后接真实输出 | `validate_bd_design` 通过，地址和 IRQ 连接明确 |
| E11 | blocked_by_E1_E2 | 接入 active XDC | E1、E2 管脚已确认 | active XDC | 等急停输入/STO/抱闸/轴封锁管脚确认 | 只约束已确认端口，未确认项不进入 active XDC |
| E12 | canceled_by_scope | 板上验证 | 当前计划取消实际测量测试 | 不生成板测验证报告 | 不启动 A11/BV 实测 | 仅保留后续现场参考，不作为当前交付条件 |

E1、E2 未完成前，不允许新增真实硬件输入或输出。E4 - E8 已完成的是当前 RTL/仿真/代码侧边界，不代表硬件安全链已实测闭环；E7A 已完成本地 16 路占位 gate、AXI 状态和 BD 常量接入，但不代表真实 `DO1` - `DO14`、`PWM1` - `PWM2` 已经板上安全；E7B 已完成本地 1 路总线 TX gate 占位、AXI 状态和 BD 常量接入，但不代表真实总线 TX/driver-enable 已经板上安全。E10 已按 A7 行动卡完成内部 AXI/IRQ/fail-closed 接入。E12/A11 已取消为当前计划门槛，状态最多是 `local_verified_only` / `code_review_only`。

### PL 急停最小 RTL 接口草案

后续实现时先按以下接口收敛，避免边做边扩散：

```verilog
module pl_estop_core #(
  parameter integer CLK_HZ = 100000000,
  parameter integer DEBOUNCE_MS = 10,
  parameter integer BRAKE_LEAD_US = 50,
  parameter integer AXIS_COUNT = 6
) (
  input  wire clk,
  input  wire rst_n,
  input  wire estop_nc_in,
  input  wire sw_reset_req,
  input  wire irq_clear,
  input  wire [AXIS_COUNT-1:0] step_in,
  input  wire [AXIS_COUNT-1:0] enable_in,
  output wire [AXIS_COUNT-1:0] step_out,
  output wire [AXIS_COUNT-1:0] enable_out,
  output wire brake_z_out,
  output wire estop_irq,
  output wire estop_latched,
  output wire estop_input_raw,
  output wire estop_input_filtered,
  output wire reset_allowed
);
```

接口草案只是实现收敛点，不代表已确认管脚。`AXIS_COUNT`、Z 轴索引、STO/enable 极性和抱闸极性必须在 E1 - E3 关闭后写成参数或寄存器定义。

当前 DO/PWM 占位 gate 与总线 TX 占位 gate 已实现；后续接真实输出时必须保持同一急停锁存源：

- 已增加 `GENERAL_OUTPUT_COUNT`、`GENERAL_OUTPUT_SAFE_LEVELS`、`general_output_in[]`、`general_output_out[]` 和 `general_output_forced_off`；当前 BD 固定 16 路，`general_output_in[15:0]` 接 `pl_estop_do_zero/dout`，默认 off/safe level 为 0。真实 DO/PWM 接入时必须把正常输出 owner 后级接到该 gate，并把 `GENERAL_OUTPUT_SAFE_LEVELS` 改为已确认 off 极性。
- 已增加 `BUS_TX_GATE_COUNT`、`BUS_TX_IDLE_LEVELS`、`bus_tx_enable_in[]`、`bus_tx_enable_out[]`、`bus_tx_queue_flush_req`、`bus_tx_queue_flushed_in` 和 `bus_tx_queue_flushed`；当前 BD 固定 1 路，`bus_tx_enable_in` 接 `pl_estop_tx_zero/dout`，`bus_tx_queue_flushed_in` 接 `pl_estop_tx_flushed/dout`，默认 TX idle/off level 为 0。真实总线接入时必须把已确认 bus owner 的 TX send-enable/driver-enable 后级接到该 gate，并把 `BUS_TX_IDLE_LEVELS` 改为已确认 idle/off 极性。
- 总线 gate 只作用于 TX send-enable、driver-enable、TX idle 或 TX_CTL 类信号；PHY reset、Link power、link clock、MDIO/配置和 RX/status 不能作为急停封锁目标。
- `reset_allowed` 对总线 TX 恢复已等待 `bus_tx_queue_flushed`，避免急停期间积压的控制帧在解除急停后重放；当前占位为常量 1，真实队列/FIFO 接入时必须由真实 owner 驱动。

## 实施阶段

### 阶段 0：保护和盘点

1. 记录 `vivado_hw_project/` 的 Git 状态，确认老工程没有被修改。
2. 盘点新目录现状，确认是否已有半成品文件；已有文件先保留，除非明确确认是无效生成物。
3. 复制前列出老工程必须复制的源文件和必须排除的生成物。
4. 备份将要编辑的新工程文件，备份放在 `repo_ignored/vivado_new_project_plan/backup/` 下；不创建过程流水文档。
5. 确认没有需要等待或创建的项目锁文件；只在真实 Vivado、VM、板卡进程发生资源冲突时停止诊断。

阶段验收：
- `vivado_hw_project/` 无修改。
- 新目录现状清楚。
- 复制清单和排除清单明确。
- 无过程文件、任务看板或锁文件新增。

### 阶段 1：创建自包含新工程副本

1. 在 `new-vivado/z20_v1_5_hw_project/` 内放置新工程源结构。
2. 从老工程复制必要内容：`.xpr`、BD、XCI、wrapper、RTL、本地 IP repo、HLS frozen RTL、Vivado 脚本和必要说明。
3. 排除生成目录和产物：`.Xil/`、`*.runs/`、`*.gen/`、`*.cache/`、`board_inputs/*.xsa`、日志、journal、bitstream、临时报告。
4. 将新副本内的工程名和路径改为新工程路径，优先使用 `$PPRDIR`、`$PSRCDIR`、脚本相对路径。
5. 搜索并清除新工程内对 `vivado_hw_project/` 老目录的依赖引用。
6. 检查 `.xpr`、`.bd`、`.xci`、Tcl 脚本和约束文件，确保所有源文件路径均落在 `new-vivado/` 下。
7. 扫描新工程所有文本文件，确认没有盘符绝对路径、老工程绝对路径或机器专用路径。

阶段验收：
- 新 `.xpr` 可以从新目录打开。
- 新工程文件中没有引用 `vivado_hw_project/` 作为源路径。
- 新工程没有引用 `new-vivado/` 之外的项目源文件。
- 新工程文件中没有项目内绝对路径。
- 老工程仍无修改。

### 阶段 2：建立约束参考和端口映射

1. 将老 `system.xdc` 复制为参考文件，仅供人工比较，不加入新工程 constraints set。
2. 将 `new-vivado/z20-v1_5_20260623.xdc` 作为 v1.5 原理图和管脚约束真源。
3. 从 `system_wrapper.v` 抽取当前真实顶层端口列表。
4. 从新 XDC 抽取原理图网络名、管脚、CN 来源和注释。
5. 建立 `docs/port_mapping.md`，至少包含：老端口、旧管脚、新网络名、新管脚、方向、处理决策、证据来源、待复核原因。
6. 对冲突项保持 fail-closed；如果直接加载原始新 XDC 会约束不存在端口，则先在新工程内完成端口适配或生成派生 XDC，并保留从原始新 XDC 到 active XDC 的映射证据。

阶段验收：
- 端口映射表覆盖 LCD、RGMII、XADC、I2C、UART、RS232、RS485、触摸、step/dir/encoder 或 8 轴候选网络。
- 冲突项明确列出，不能默默选择一个。
- 原始新 XDC 和 active XDC 均无重复端口、重复 PACKAGE_PIN 的未解释问题。

### 阶段 3：生成新工程 active 约束

1. 新 active XDC 的管脚来源必须是 `new-vivado/z20-v1_5_20260623.xdc`。
2. 优先让新工程 wrapper/BD 端口名匹配新 XDC 原理图网络名；若短期不能改名，才生成 wrapper 适配版 XDC。
3. 适配版 XDC 必须逐项记录原始新 XDC 网络名、管脚、目标 wrapper 端口和转换原因。
4. 能确认等价的信号才写入 active XDC。
5. 不能确认的信号只写入参考或待复核表，不能进入 active 约束。
6. 不同时加载旧 `system.xdc` 和新 v1.5 active XDC，避免同一物理管脚被双重约束。
7. 保留必要 timing 约束，例如 RGMII 时钟、DDR I/O delay、DVI reset exception；但每条例外必须对应当前仍存在的端口或 cell。

阶段验收：
- active XDC 没有重复 `PACKAGE_PIN`。
- active XDC 没有约束不存在端口造成的隐藏误判。
- active XDC 能追溯到 `new-vivado/z20-v1_5_20260623.xdc`，且旧 `system.xdc` 未加载。
- RGMII、LCD、XADC、触摸、串口、轴相关信号的迁移状态可追踪。

### 阶段 3A：PL 急停设计接入

1. 从 `pl急停.md` 提取硬件急停输入、STO/使能、抱闸、轴脉冲封锁、IRQ/AXI 状态和复位互锁需求，写入 `docs/pl_estop_integration.md`，但不得把该文档当作已实现证明。
2. 在 `z20-v1_5_20260623.xdc`、原理图和实际接线之间确认候选管脚；确认前只记录候选项，不进入 active XDC。
3. 明确安全链输入类型：常闭 NC、低有效急停、断线等同急停、上电默认锁存或安全态；该行为必须在 RTL 参数或寄存器说明中固定。
4. 新增或扩展 PL 急停模块，按 `pl_estop_filter`、`pl_estop_latch`、`pl_estop_gate`、`pl_estop_brake_timer`、`pl_estop_axi` 的边界拆分。
5. 将输入同步和去抖放在 PL 内部第一层；去抖参数按 10ms - 20ms 设计，时钟来源和计数宽度在文档中写清楚。
6. 将锁存输出直接接到硬件输出封锁路径；封锁路径必须短于 PS 状态上报路径，不能等待 IRQ、AXI 读写、LinuxCNC 或 UI。
7. 在现有运动输出离开 PL 前增加门控点：当前 8 轴 `PULS/DIR` 已经经过顶层急停 gate，`ENA1-8` 由 `z20_v15_io_owner_axi_lite` 正常输出并经过顶层急停 gate；后续接入 STO/抱闸 owner 时必须分别列出封锁策略。
8. 对 Z 轴或其他垂直轴增加抱闸提前逻辑：急停触发后先动作抱闸输出，硬件计数器延迟后再封锁对应轴驱动输出。
9. 对通用 IO 输出增加急停关断门控：`DO1` - `DO14`、`PWM1` - `PWM2` 必须在 `estop_latched` 后由 PL 强制到代码侧定义的关闭态；当前仅复查 gate 位置、safe level、AXI status 和仿真，不做板上测量。
10. DO/PWM 不允许绕过 `pl_estop_general_output_gate` 作为普通输出进入 active XDC 或真实 pin；当前计划只复查 RTL/AXI/status/仿真/active XDC 链路，不执行台架/板上 forced-off 验证。
11. 对接 PL 的总线驱动/控制发送增加 TX 门控：急停锁存时只封锁 TX send-enable、driver-enable、TX idle 或 TX_CTL 类信号，保持物理 Link、链路时钟、复位、配置和 RX/状态观测不断；恢复 TX 前必须清空或失效急停期间积压的发送请求。
12. 建立 AXI-Lite 状态/控制寄存器：至少包含急停锁存、原始输入、去抖输入、复位允许、抱闸延迟中、通用输出强制关闭状态、总线 TX gate active、总线 TX 队列已清空/失效、复位请求和 IRQ 清除。
13. 建立复位互锁：物理输入仍为低时，PS 写 `reset_request` 必须无效；只有物理恢复高电平且软件复位请求有效时，PL 才能解除锁存；存在总线 TX 队列时，TX 恢复还必须等待队列清空或失效确认。
14. 建立可选 IRQ：急停锁存沿触发 IRQ 给 PS；IRQ 清除只能清中断标志，不能绕过物理输入和锁存复位条件。
15. 更新 BD 连接计划：AXI 接入 PS GP 总线，IRQ 接入 PS IRQ 或 concat，急停输出门控接入运动 IP、DO/PWM 生成路径到顶层端口之间，总线 TX gate 接入已确认 bus owner 的发送使能/driver-enable 路径而不是 PHY reset/link 控制。
16. 生成或更新 testbench/静态检查计划，至少覆盖：常闭低电平触发、断线触发、去抖窗口、软件复位被低电平急停拒绝、垂直轴抱闸提前量、`DO1` - `DO14` 和 `PWM1` - `PWM2` 急停强制关闭、总线 TX gate 拉到 idle/off、Link/RX 保持、恢复前积压发送请求被清空或失效。
17. 只有在管脚确认后才把急停输入、STO、抱闸、通用输出和封锁输出写入 active XDC；未确认项继续 fail-closed，不能借老工程 `system.xdc` 推断。

阶段验收：
- `docs/pl_estop_integration.md` 明确列出已确认和未确认的急停相关引脚。
- PL 急停模块边界和寄存器/IRQ 状态路径清楚。
- 未确认硬件输出保持 fail-closed。
- 计划能说明急停输入到输出封锁、DO/PWM 强制 off、总线 TX gate、PS 状态观察和软件复位互锁的完整实现链路。
- 当前计划不执行板上示波器和断线测试，因此不产生安全功能已实测或已验证结论。

### 阶段 4：Vivado 本地打开和综合前检查

1. 用 Vivado 2020.2 打开新 `.xpr`。
2. 执行 `update_compile_order -fileset sources_1`。
3. 打开并验证 BD：`open_bd_design`、`validate_bd_design`。
4. 检查 IP repository 是否全部来自新工程目录。
5. 检查 top module 仍为 `system_wrapper`，或明确记录改名原因。

阶段验收：
- BD validation 无新增 blocker。
- compile order 可以刷新。
- 没有老工程路径依赖。

### 阶段 5：综合、实现、bitstream 和 XSA

1. 先运行综合，确认源文件、IP 和约束能进入 Vivado flow。
2. 综合通过后再运行实现和 timing 报告。
3. timing 通过后再生成 bitstream。
4. bitstream 生成后再导出带 bit 的 XSA。
5. 导出 XSA 后运行 `scripts/verify_vivado_xsa_cleanliness.ps1`，确认 Active Constraints 只加载 mapped XDC、真源 XDC 未进入 Vivado active constrs set、无 error/critical warning、DRC 无阻断规则、XSA 不旧于 bitstream。
6. 输出放入新工程 `reports/`、`artifacts/` 或 Vivado run 目录，不能写回老工程。

阶段验收：
- synthesis 完成。
- implementation 完成。
- `TIMING_STATUS=timing_met` 才能称为本地 Vivado 构建通过。
- XSA 路径位于 `new-vivado/z20_v1_5_hw_project/` 下。
- `verify_vivado_xsa_cleanliness.ps1` 输出 `vivado_xsa_cleanliness=ok`、`active_constraints_loaded=mapped_only`、`truth_source_xdc_loaded=no`、`old_project_xdc_loaded=no`、`drc_blocking_rules=0`；当前仅允许记录 `drc_allowed_warning_rules=RTSTAT-10`，不能把普通 warning 行数误写成全部告警为 0。

## 检查命令建议

从项目根目录执行：

```powershell
git status --short -- vivado_hw_project new-vivado
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\check_active_xdc.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_remaining_drc_ports.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\export_active_pin_conflicts.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_active_xdc_traceability.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_active_xdc_electrical_contract.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_project_independence.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_project_portability.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_vivado_xsa_cleanliness.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\export_vivado_warning_summary.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_vivado_warning_summary.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_adc_spi_mapping.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_pl_estop_sim.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_pl_estop_safety_boundary.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_pl_estop_output_shutdown_contract.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_pl_estop_wiring_evidence.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_pl_estop_board_validation.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\export_pl_estop_evidence_gap.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\export_pl_estop_hardware_evidence_request.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_pl_estop_hardware_evidence_request.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\export_pl_estop_field_packet.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_pl_estop_field_packet.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_pl_estop_field_runbook.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\export_pl_estop_evidence_templates.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_pl_estop_evidence_templates.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_pl_estop_evidence_root.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_pl_estop_field_intake.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_pl_estop_readiness.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\export_board_input_manifest.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File new-vivado\z20_v1_5_hw_project\scripts\verify_board_input_manifest.ps1
git diff --check -- new-vivado
```

若本次只修改本计划，最小检查为：

```powershell
Select-String -LiteralPath 'new-vivado\新vivado工程计划.md' -Pattern '(?<![A-Za-z])[A-Za-z]:[\\/]'
git diff --check -- new-vivado/新vivado工程计划.md
git status --short -- vivado_hw_project new-vivado
```

Vivado 打开检查建议使用新工程目录下的脚本，不使用老工程脚本路径：

```powershell
$env:VIVADO_JOBS='8'
vivado.bat -mode batch -source new-vivado/z20_v1_5_hw_project/scripts/vivado_gate_current.tcl
```

如果本机 `vivado.bat` 不在 `PATH`，只在当前 shell 环境中配置 Vivado 可执行文件位置，不把该机器绝对路径写进工程文件或计划文档。

## 冲突和待复核清单

- `AA4/W7`：触摸按老工程一致处理，`AA4` 为 touch int，`W7` 为 touch rst_n；不再作为方向差异复核项。若后续源 XDC 或原理图命名与此相反，按老工程触摸接法修正。
- `U14/R7`：新 XDC `RS485_FPGA_RX=U14`、`RS485_FPGA_TX=R7` 已按 PS UART1 EMIO 导出到当前 `system_top`，active XDC 已约束；`B13_IO_0` 重复标签仍需最终 PCB/HDL release 前复核，板端 RS485 收发器控制/方向验证不在当前代码侧 gate 内。
- HDMI TMDS 与 MPG `AXIS_SEL*`：已按用户裁决放弃 HDMI/DVI，同一批 9 个管脚全部绑定 MPG：`AXIS_SEL0` - `AXIS_SEL7` 与 `PULS5_IO`。后续不得把这些脚重新作为 HDMI/TMDS 输出驱动；若未来要重启 HDMI，必须另立变更并重新处理 MPG 占用和 active XDC 冲突。
- XADC `L11/M12`：当前一路 0-10V 模拟量使用芯片自带 `XADC_VP/XADC_VN`。前端保护、分压和滤波在硬件原理图落实，Vivado/handoff 只保留一路模拟量。
- `U10/U9/AA12/AB12`：不再作为外部 ADC 使用；后续如果另作备用 IO 或其他功能，必须按新用途重新确认。
- 8 轴边界：当前新工程只接受 v1.5 8 轴 `PULS/DIR/ENA/A/B/Z/ALM` 语义；退役轴外部 bus 不得恢复为板级接口。
- 编码器输入打开：用户最新要求后续必须打开编码器输入。当前 8 轴 `A/B/Z` 已进入 `system_top`、active XDC 和 BD `step_ip`；实施时必须继续按 N3E 逐位确认真实编码器输入、顶层端口、BD/RTL owner 和 active XDC 约束，不能恢复退役 encoder bus。
- `B13_IO_0`：新 XDC 备注中同一标签出现在两个来源位置，最终 PCB/HDL release 前必须关闭。
- `sys_clk`：新 XDC 有 `sys_clk L18`，老工程当前主要由 PS/BD 时钟驱动，需要确认新顶层是否需要新增 PL 外部时钟端口。
- PL 急停输入/STO/抱闸/通用输出/总线 TX：`pl急停.md` 规定了安全行为，当前用户又明确 `DO1` - `DO14`、`PWM1` - `PWM2` 急停执行时也必须关闭，并要求接 PL 的总线驱动只封锁 TX 发送使能/driver-enable、不断开物理 Link、释放后可快速恢复；最新计划取消实际测量测试，因此当前只复查 XDC 映射、RTL/BD 门控设计、generated netlist、仿真、register map、handoff 和 manifest，一律不声明板上安全功能通过。

## 停止条件

- 任何步骤会写入或自动修改 `vivado_hw_project/` 老工程。
- 新工程打开后仍依赖老工程源文件路径。
- 新工程文件、文档或脚本中固化项目内绝对路径。
- 新工程 constraints set 加载了老工程 `system.xdc`。
- 新工程没有使用 `new-vivado/z20-v1_5_20260623.xdc` 或其同目录可追溯派生约束。
- active XDC 出现重复管脚、重复端口或无法解释的管脚复用。
- Vivado 报告出现新的 DRC error、严重 timing exception 覆盖问题或约束不存在端口的误配置。
- 硬件脚位无法从原理图、老工程和新 XDC 中形成一致判断。
- PL 急停输入、STO、抱闸、脉冲封锁、DO/PWM 强制关闭或总线 TX gate 没有明确管脚、owner、gate 信号和安全默认态。
- 需要板上部署、运动验证或 LinuxCNC/HAL 改动才能继续判断。
- 出现必须修改 `功能/`、架构文档或 `AGENTS.md` 才能继续的规则冲突，但未完成对应 owner 文档和 doc audit。

## 最终验收条件

- `vivado_hw_project/` Git 状态保持未修改。
- 新工程可从 `new-vivado/z20_v1_5_hw_project/` 独立打开。
- 新工程所有源依赖均位于 `new-vivado/` 内，不引用老工程。
- 新工程所有项目内路径均为相对 `PROJECT_ROOT` 的路径，未固化盘符绝对位置。
- 新工程使用 `new-vivado/z20-v1_5_20260623.xdc` 或其同目录可追溯派生约束作为 active 约束。
- 约束真源、端口映射表和 active XDC 三者关系清楚。
- active XDC 没有重复 PACKAGE_PIN、没有重复端口约束。
- Vivado 2020.2 至少完成 BD 验证和综合。
- 如实现/bitstream/XSA 已执行，报告和产物均位于新工程目录下。
- 未确认硬件脚位全部留有待复核记录，不能当作已验证。
- 最终回复必须列出改动文件、实际执行的验证、未执行的验证和当前 honest status。

## 当前状态

- 阶段 0 已完成：已盘点目标目录，老工程保持未修改，未创建过程文件、任务看板或锁文件。
- 阶段 1 已完成源码副本：新工程源文件已放入 `new-vivado/z20_v1_5_hw_project/`，入口为 `z20_v1_5_hw_project.xpr`。
- 阶段 2 已完成基础资料：旧约束参考副本已移出当前新工程交付边界，`new-vivado/z20-v1_5_20260623.xdc` 作为约束来源保留，端口映射见 `docs/port_mapping.md`。
- 阶段 3 当前 DRC 已收口：当前 active XDC 与 `system_top.v` 自洽，LCD、RGMII/MDIO、CAN、PL UART、RS232、RS485、I2C3、触摸 I2C、TP_INT/TP_RST、完整 MPG、8 轴 `PULS/DIR/ENA/ALM`、8 轴编码器 ABZ、DI/FR_DI/TS_DI、`EGS_DI`、`BEEP_EN` 和 DO/PWM gate 已适配到当前 top；PL_RST、sys_clk 和备用 FPGA IO 仍保持 fail-closed。
- ADC 当前状态：最终需求已改为 1 路 0-10V 模拟量，使用芯片自带 `XADC_VP/XADC_VN`；当前 `z20-v1_5_20260623.xdc` 写 XADC 单通道，方向一致。旧外部 ADC/SPI 只能作为“已退役”说明出现，不能作为 active 方案。
- 阶段 3A 已推进到内部 BD 接入：`docs/pl_estop_integration.md` 已列出 DI/FR_DI/EGS/TS 输入、DO/PWM 输出、轴封锁候选，并记录总线 TX gate 实施边界；`docs/pl_estop_wiring_evidence.csv` 已补入 v1.5 XDC 对 `EGS_DI`、`DO1` - `DO14`、`PWM1` - `PWM2` 的 connector/package 候选证据，但这些证据仍不是实际安全链/负载/off 极性证明；`rtl/pl_estop_core.v` 与 `rtl/pl_estop_axi_lite.v` 已创建并登记进 `.xpr`；A7 已把 `pl_estop` 作为 BD module ref 接入 PS AXI/IRQ；本轮新增 `z20_v15_io_owner_axi_lite`，通过 `M22_AXI` 接 PS GP，地址 `0x41270000/64K`，拥有 `ENA1-8`、DI/FR_DI/TS_DI/MPG/SCALE/ALM/TP_INT 状态、`TP_RST`、14 路 DO 和 2 路 PWM 正常输出，且 `DO/PWM`、`ENA` 均在顶层急停 gate 后才出 pin；generated synth 边界已把 `pl_estop/estop_nc_in` 接到顶层 `estop_nc_in`，因此 PS 后续读取的 STATUS/IRQ 急停观测与 DO/PWM、GMII TX 硬门控同源；STO、抱闸、专用轴安全 gate 和真实总线 TX 队列 owner 仍未确认，未接真实外部硬件。
- 阶段 4 已完成当前 top 本地检查：Vivado 2020.2 已能从新目录打开工程并完成 BD validation；新增 PL 急停 BD 接入后已重新验证打开和 BD validation。
- 阶段 5 本地 Vivado 产物状态：本轮 180 项 IO/IO owner、RS485、TP_INT/TP_RST 更新后已重新运行 synthesis/implementation/bitstream/XSA；当前 `board_inputs/system.xsa` 已随 10:04:04 timing 结果刷新，`TIMING_STATUS=timing_met`、`WNS=0.193ns`、`WHS=0.034ns`。
- 阶段 5 warning summary 门禁已刷新：`docs/vivado_warning_summary.csv` 与 `docs/vivado_warning_summary.md` 已纳入 `board_inputs/manifest.json` 哈希；当前 `vivado_warning_lines=457`、`vivado_warning_codes=17`、`unexpected_warning_codes=0`、`constraint_truth_warning_lines=0`、`retired_hdmi_warning_lines=0`。该结果只证明真源/旧约束 warning 链已清除且剩余普通 warning 已分类，不代表全部 Vivado warning 为 0。
- 阶段 5 代码侧证据根目录门禁保留为参考：`scripts/verify_pl_estop_evidence_root.ps1` 当前输出 `pl_estop_evidence_root_verify=ok`、`evidence_root_files=1`、`evidence_root_md_files=1`、`referenced_board_verified_evidence_paths=0`、`board_verified_records=0`、`orphan_board_verified_records=0`；该结果只防止误放孤立 `board_verified` 记录，不要求当前计划补实测证据。
- 本轮已推进 IO 清单：当前可以确认 active XDC/top 端口一致、无重复管脚、无剩余未约束顶层端口，active assignments 为 180。完整 MPG、DI/FR_DI/TS_DI、8 轴 PULS/DIR/ENA/ALM、编码器 ABZ、RS485、TP_INT/TP_RST 和 XADC 单通道已经进入 Vivado source；`z20_v15_io_owner_axi_lite` 已拥有 ENA、DI/FR_DI/TS_DI、MPG/SCALE、ALM、TP_INT/TP_RST、DO/PWM 的本地寄存器/PWM 边界；但轴号/极性、输入滤波语义、蜂鸣器 owner、RS485 收发器控制、输出 off 极性和板端安全实测仍未完成。
- N3C-1/N3C-2/RS485/N3D i2c4/PL LED/GPIO0/HDMI-MPG 已完成本地证据闭环：`docs/active_pin_conflicts.md` 当前为 `active_pin_conflicts=0`，`docs/remaining_drc_ports.csv` 当前为 header-only。未来如果启用轴、编码器、DI/DO、MPG、RS485、i2c4、PL LED、GPIO0 或重新启用 HDMI，必须按 v1.5 接线证据新增确认端口和约束，不能恢复旧 wrapper 物理语义。

当前 honest status 为 `local_verified_only`：IO owner、RS485、TP_INT/TP_RST 的 RTL/BD/XDC/文档、Vivado synth/impl/bitstream/XSA、warning summary、handoff、manifest、portability 和 `scripts/verify_new_vivado_local_closure.ps1` 已刷新并通过。最终整板 IO 极性、板端实测和运动闭环仍不在当前计划内完成，不能声明 `board_verified`。

补充状态：DO/PWM 急停关闭要求已进入计划、RTL/AXI/BD 占位、仿真、safety boundary、handoff、PL 急停 register map 和 manifest 门禁；证据根目录/field packet/runbook/模板保留为后续现场参考，不再作为当前代码侧复查完成条件。任何 `board_verified` 记录如果未来恢复现场验证，仍必须有真实项目相对附件；当前计划不创建或要求这些实测记录。

本轮门禁调整：当前交付改为代码侧复查，不再以 `scripts/verify_pl_estop_field_intake.ps1` 或 A11/BV 实测 readiness 作为完成条件；保留这些脚本仅用于未来恢复现场验证时参考。

本轮新增真实管脚提升门禁：`scripts/verify_pl_estop_real_pin_promotion_gate.ps1` 已接入 `board_inputs/README.md`、`board_inputs/manifest.json` 和 `scripts/verify_new_vivado_local_closure.ps1`。当前输出 `pl_estop_real_pin_promotion_gate=local_hard_gate_promoted`，说明 active XDC 中已有 `EGS_DI` 和 16 路 DO/PWM 本地硬门控提升；未来若恢复现场验证或新增 STO/抱闸/轴 gate 输出、额外 bus TX gate 输出、改变 DO/PWM gate 归属，必须另行更新计划和 gate。

下一步执行入口：不要再按剩余 DRC 端口或 A11/BV 实测推进，当前 `unassigned_top_ports_count=0` 且实际测量 gate 已取消。当前代码侧复查入口是 `scripts/verify_z20_v15_io_owner_sim.ps1`、Vivado synth/impl/bitstream/XSA、warning summary、handoff、manifest、`scripts/verify_project_portability.ps1` 和 `scripts/verify_new_vivado_local_closure.ps1`；如修改 RTL、BD、active XDC、generated netlist、timing、warning summary、handoff 或 manifest，必须刷新对应生成物后重跑这些 gate。真实硬件接线、示波器、BV10-BV12 或 A11 实测不属于当前计划；bitstream/XSA 只能作为本地 Vivado 产物，不能声明板上功能通过。
