# BUS一毫秒实时周期目标与落实方案

引用需求真源：`REQ-CPU0-MOTION-REALTIME-DOMAIN`、`REQ-GCODE-RUN-HOT-PATH`、`REQ-MICROKERNEL-MOTION-LOOKAHEAD`、`REQ-MAIN-ESTOP-LATENCY`、`REQ-BUS-PULSE-MODE-DIFFERENCE`、`REQ-POWER-ON-HOME-PRECONDITION`、`REQ-AUTO-STATE-DRIVEN`、`REQ-BOARD-PROGRAM-FULL-BUILD-CLOSURE`

本工作是跨 owner 的原子实施包，不是新的需求 owner，也不能用本文覆盖以下真源：

- servo/EtherCAT 周期、CPU0预算和DC/WKC：`REQ-CPU0-MOTION-REALTIME-DOMAIN` / `功能/0-1开机参数入内存.md`。
- trajectory前瞻与短段吞并时间语义：`REQ-MICROKERNEL-MOTION-LOOKAHEAD` / `功能/微内核.md`。
- realtime安全latch、heartbeat watchdog和0.1s急停：`REQ-MAIN-ESTOP-LATENCY` / `功能/3-1主页面按钮功能.md`。
- BUS Home状态机与墙钟语义：`REQ-BUS-PULSE-MODE-DIFFERENCE` / `功能/2-1总线脉冲区别.md`，主页面Home流程与前置门禁继续读`REQ-POWER-ON-HOME-PRECONDITION` / `功能/3-2回零按钮开机强制回零与双模式真实回零方案.md`。
- 构建、部署、退役文件清理、CPU1压力工具和板端验收：`REQ-AUTO-STATE-DRIVEN`、`REQ-BOARD-PROGRAM-FULL-BUILD-CLOSURE` / `功能/自动闭环测试方式.md`。

## 1. 当前事实与目标

- 当前BUS主线仍是2ms（500Hz）：`board/linuxcnc/ini/v5_bus.ini`的`SERVO_PERIOD=2000000`，HAL入口为`v5_bus_2ms.hal`，EtherCAT XML的`appTimePeriod=2000000`。
- 2026-07-22当前板端只读基线：`servo-thread`周期2,000,000ns，当前一次采样执行时间约265,834ns、历史`Max-Time`约586,985ns。该数据只用于1ms迁移前预算，不代表压力窗口已经通过。
- 目标是把BUS LinuxCNC servo、HAL realtime function、EtherCAT主站应用周期、5台驱动Sync0/DC与同周期安全链原子统一为1ms（1000Hz）。
- Pulse链当前`SERVO_PERIOD=4500000`，不属于本目标；本工作不得顺手修改Pulse周期。
- 当前状态是“需求和落地方法已明确，代码仍为2ms”。没有完成构建、部署和板端验收前，禁止写“1ms已生效”。

## 2. 最终结果

1. Windows canonical source只保留一套BUS 1ms实现；2ms HAL/XML路径在引用迁移完成后物理删除。
2. 板端`halcmd show thread`回读`servo-thread period=1000000ns`，LinuxCNC、lcec、Sync0和安全latch使用同一周期身份。
3. 5台汇川SV630N在每个登记采样点均为OP，Domain WKC每个样本均为10/10且complete，DC reference每个样本均fresh；1ms初始工程上限固定为`abs(lcec.0.phase-jitter)<=100000ns`，即不超过周期的10%。
4. idle、冷/热启动、连续G-code、UI冻结和CPU1满载窗口均无servo overrun；`Max-Time<1000000ns`，工程放行目标`Max-Time<=700000ns`。
5. 编码器连续、跟随误差不退化、电机无沙粒感/周期顿挫，轨迹队列不枯竭，0.1s急停链继续成立。
6. 验收结束恢复`ESTOP active`和`Machine Off`。

## 3. 硬边界

- 不能只改`SERVO_PERIOD`；INI、HAL、EtherCAT XML、DC/PLL、启动、部署、readiness和测试必须同一slice原子迁移。
- 不允许1ms/2ms混装，不允许环境变量、双文件、启动参数或自动探测选择周期。
- 不允许为了过1ms关闭EtherCAT证据、降低安全轮询、降低servo功能、把非运动任务塞进CPU0或牺牲UI/remote功能。
- `refClockSyncCycles`、`ARC_BLEND_GAP_CYCLES`、safety watchdog和Home稳定/等待常量的初始1ms值已经在第5.3节裁决；其它heartbeat、debounce/filter、freshness/timeout等“按周期计数”参数仍必须逐项审计。周期从2ms减半后，保持墙钟语义的cycle count必须相应翻倍。
- 失败时保持ESTOP/Machine Off。回退使用Git把整个变更slice恢复到最后已验证2ms基线并重新原子部署；产品运行目录不得保留2ms fallback。

## 4. 实施前基线

在不修改板端状态的前提下先固定同一组可比较证据：

1. 回读当前`SERVO_PERIOD`、`halcmd show thread`、servo函数顺序、CPU affinity和调度策略。
2. 回读`ethercat master/slaves/domains`、5轴OP、Domain WKC、DC reference valid/age、phase-jitter、datagram错误增量。
3. 记录idle、正常主页面、remote relay连接、同模型golden motion和CPU1压力窗口的`Time/Max-Time`。
4. 记录编码器连续性、每轴following error、`motion.requested-vel/current-vel`、轨迹queue深度和急停时间。
5. 通过驱动/ESI或原生EtherCAT对象确认5台SV630N支持1ms Sync0/CSP周期；重点核对`0x1C32/0x1C33`同步能力、watchdog和需要时的`0x60C2`插补周期语义，未知即停止实施。

## 5. Canonical source改造

### 5.1 唯一周期身份

- `board/linuxcnc/ini/v5_bus.ini`
  - `SERVO_PERIOD=1000000`。
  - `HALFILE`只指向新的1ms HAL owner。
- `board/linuxcnc/hal/v5_bus_2ms.hal`
  - 引用全部迁移后改名为`v5_bus_1ms.hal`；旧文件物理删除。
  - 保持`lcec.read-all -> router/cia402/safety/motion -> cia402/router -> lcec.write-all`同一servo-thread原子顺序。
- `board/linuxcnc/hal/ethercat-conf-2ms.xml`
  - 改名为`ethercat-conf-1ms.xml`；`appTimePeriod=1000000`。
  - `sync0Cycle`保持与应用周期同源；`sync0Shift`和reference-clock策略必须板端单变量调优，不能凭2ms值猜测。
  - 旧XML物理删除。

### 5.2 必须同步的直接消费者

- `board/config/deploy/v5_runtime_deploy_manifest.tsv`
- `board/tools/deploy/install_v5_runtime.sh`
- `board/services/microkernel/v5_microkernel_manifest.c`
- `board/app/src/v5_boot_closure.c`
- `board/services/command_gate/init.d/v5-linuxcnc-command-gate`
- `linuxcnc/src/hal/components/v5_safety_latch.comp`
- `linuxcnc/src/hal/components/v5_bus_homecomp.comp`
- `board/tools/deploy/check_v5_runtime_policy.py`
- `board/tools/deploy/verify_v5_board_runtime.sh`
- `board/tools/linuxcnc/v5_bus_module_owner_smoke.py`
- `board/tools/deploy/v5_home_router_bridge_smoke.py`
- `board/tools/deploy/test_v5_estop_abort_rtcp_output_policy.py`
- `board/tools/deploy/measure_v5_cold_boot_smoke.py`
- 新增登记的`board/tools/linuxcnc/v5_cpu1_motion_stress.py`及其focused self-test；没有该工具时CPU1压力验收为未就绪，禁止用临时SSH busy loop替代。
- 当前引用2ms路径的功能文档与需求索引位置。

manifest只会安装登记的新文件，不会自动删除板端旧目的文件。因此`install_v5_runtime.sh::cleanup_retired_runtime_files()`必须显式删除`/opt/8ax/v5/linuxcnc/hal/v5_bus_2ms.hal`和`/opt/8ax/v5/linuxcnc/hal/ethercat-conf-2ms.xml`；focused installer测试必须先构造旧文件、执行安装、再证明两者不存在且1ms文件hash正确。板端readiness前再次用`test ! -e`或等价只读检查证明旧2ms文件不存在。

迁移完成后用`rg`检查产品source/config/test/manifest中不再存在活动`v5_bus_2ms.hal`、`ethercat-conf-2ms.xml`和BUS `SERVO_PERIOD=2000000`引用；活动工作文档在真源和验收闭合后删除，不作为历史第二真源保留。

### 5.3 时间语义审计

初始1ms迁移固定保持现有2ms产品的墙钟语义，以下值不得再留作“实施时再决定”：

| 消费者 | 当前2ms值与墙钟语义 | 初始1ms值 | 真源/依据 |
| --- | --- | --- | --- |
| EtherCAT reference-clock同步 | `refClockSyncCycles=5`，约10ms | `10`，仍约10ms | `ethercat-conf-*.xml`；后续若调DC策略必须作为独立单变量A/B，不夹入周期迁移 |
| trajectory短段吞并 | `ARC_BLEND_GAP_CYCLES=4`，阈值约8ms | `8`，仍约8ms | `blendmath.c`明确计算`gap_cycles * cycle_time`；语义owner为`功能/微内核.md` |
| realtime safety heartbeat watchdog | `watchdog_cycles=50`，名义约100ms | `100`，名义仍约100ms | `v5_safety_latch.comp`；最终仍以native fail-closed实际时间和0.1s急停owner验收 |
| BUS Home稳定样本 | `V5_HOME_STABLE_CYCLES=3`，约6ms | `6`，仍约6ms | `v5_bus_homecomp.comp`的首次PRECHECK、RTCP后PRECHECK、分组release和终态稳定 |
| BUS Home有界等待 | `V5_HOME_WAIT_CYCLES=500`，约1s | `1000`，仍约1s | 同组件的moving precheck、RTCP ACK、sequence start和terminal readback等待 |

继续逐项核对尚未裁决的cycle-based逻辑：lcec DC/PLL更新时间、Sync0 shift、datagram timeout、router/cia402滤波、驱动状态稳定计数、readiness freshness、两次DC fresh pair以及激活后的下一完整servo周期。动态Home运动timeout已经按`ceil(seconds/servo_period)`计算，迁移时必须证明它随周期自动换算且没有再乘一次2倍。

只改周期但遗漏上述任一cycle-based语义，视为未完成。

## 6. 验证梯级

### 6.1 Windows focused gate

1. 文档严格路由和`git diff --check`。
2. INI/HAL/XML静态一致性检查：三者都为1ms，Pulse仍为4.5ms。
3. 运行受影响的runtime policy、microkernel manifest、boot closure、Home/router、急停、冷启动解析smoke。
4. 检查退役2ms路径没有source/config/test/manifest survivor。

### 6.2 VM最小构建

1. 独占`vm_board.lock`，只同步本slice直接输入到唯一projection并逐文件核hash。
2. 构建受影响LinuxCNC/HAL/lcec/runtime target；未过最小target前不生成rootfs/image。
3. 检查ARM ELF/ABI和runtime manifest；只生成本次最小部署需要的canonical artifact。

### 6.3 板端无运动否决

1. 原子部署后保持ESTOP和Machine Off，完成一次clean backend restart。
2. 回读`servo-thread period=1000000ns`及全部realtime function顺序。
3. 回读CPU0 realtime线程/IRQ、CPU1非运动进程边界。
4. 连续观察idle和UI/relay工作窗口：overrun=0、`Max-Time<=700000ns`、5轴OP、WKC 10/10、DC fresh稳定。
5. 出现任一WKC跌落、DC失锁、reference clock错误、`TIMED OUT/UNMATCHED/SKIPPED`持续增长或`Max-Time>=1000000ns`，立即否决并保持安全态。

可重复测量合同：

- 新建servo-thread的冷/热启动窗口不得清零`tmax`；从thread创建持续采样到readiness后30秒，保留该启动代完整峰值。
- idle、UI/relay、golden motion、CPU1压力和UI冻结等稳态窗口开始前，先保存上一窗口读数，再执行`halcmd setp servo-thread.tmax 0`并清零本次servo-thread内各function的RW `*.tmax`，同时记录monotonic起点；窗口结束先读回再允许下一次清零。没有起止generation和时间戳的Max-Time无效。
- idle与正常UI/relay各采60秒；每次golden motion从Start采到native terminal；CPU1满载采60秒；UI冻结单独采30秒。每100ms采样一次，漏采必须记录并使该窗口无效，不能把缺样本当稳定。
- 每个有效样本必须同时满足：5台从站全OP、Domain WKC=`10/10`且complete、`dc-phased=TRUE`、`dc-time-valid=TRUE`、`dc-time-age-cycles=0`、`dc-time-ok-seq`推进、`dc-time-error-count`不增加、`abs(phase-jitter)<=100000ns`；并在窗口首尾比较datagram、`TIMED OUT/UNMATCHED/SKIPPED`和reference-clock错误计数均无增量。

### 6.4 冷/热启动

- 先做一次冷启动单变量否决；通过后做3次整板冷启动和3次受控backend热重启。
- 每次必须证明唯一lcec activation、下一完整1ms周期开始普通function list、5台从站OP、WKC 10/10、DC fresh pair、CPU合同和UI ready。
- 启动阶段异常日志只有与当前generation原生actual一起解释，不能用旧baseline或瞬时OP冒充成功。

### 6.5 真实运动与压力验收

1. 按active model从原始UI路径打开匹配的`cc-ac.ngc`或`cc-bc.ngc`，完成Home和三轮连续golden motion。
2. 同步采集`motion.requested-vel/current-vel`、queue、servo Time/Max-Time、overrun、DC/WKC、编码器与following error。
3. 实施前新增并通过`board/tools/linuxcnc/v5_cpu1_motion_stress.py` self-test。板端只把同hash工具放到`/tmp/v5_test_tools/`，默认运行60秒、只接受30～60秒；工具启动后必须用`sched_setaffinity`和`/proc/self/status`双重证明只允许CPU1，发现CPU0在allowed mask中立即退出且不得开始负载。
4. 在持续运动窗口先运行CPU1-only压力60秒，再用工具登记的精确UI PID/cmdline保护执行30秒UI freeze；工具必须在正常结束、异常、SIGINT/SIGTERM和超时路径恢复UI、停止全部worker并删除板端临时文件，结束后证明无残留PID且CPU1回到基线。压力期间按第6.3节100ms节拍同步采集，运动必须连续，CPU0实时链不得受影响。
5. AC和BC模型在产品支持范围内分别完成后，才能把1ms作为全产品基线；只过当前模型只能算该模型切片通过。
6. 完成0.1s急停链验证，清执行态但保留程序身份；最终回读`estop_active=1`、`machine_enabled=0`。

## 7. 放行判定

以下条件必须同时满足：

- canonical source、artifact、板端安装文件hash一致。
- BUS周期所有owner回读均为1ms，无2ms活动入口或运行时fallback。
- 全部压力窗口servo overrun为0，`Max-Time<=700000ns`；任何样本达到1ms直接失败。
- 所有登记窗口按100ms采样且无漏样本；每个样本5轴全OP、WKC 10/10 complete、DC fresh、`abs(phase-jitter)<=100000ns`，相关错误计数零增量。
- 运动速度、队列、编码器、following error、电机平顺性和急停均不退化。
- 工作树中的需求owner、实现、部署清单、测试与板端readback一致。

只有满足以上条件才能声明`board_verified`。仅改文档、改INI、通过本地smoke、完成VM编译或板端回读周期值，都不能单独宣称1ms完成。
