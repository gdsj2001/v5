# BUS一毫秒实时周期目标与落实方案

引用需求真源：`REQ-CPU0-MOTION-REALTIME-DOMAIN`、`REQ-GCODE-RUN-HOT-PATH`、`REQ-MICROKERNEL-MOTION-LOOKAHEAD`、`REQ-MAIN-ESTOP-LATENCY`、`REQ-BOARD-PROGRAM-FULL-BUILD-CLOSURE`

唯一需求owner：`功能/0-1开机参数入内存.md`

## 1. 当前事实与目标

- 当前BUS主线仍是2ms（500Hz）：`board/linuxcnc/ini/v5_bus.ini`的`SERVO_PERIOD=2000000`，HAL入口为`v5_bus_2ms.hal`，EtherCAT XML的`appTimePeriod=2000000`。
- 2026-07-22当前板端只读基线：`servo-thread`周期2,000,000ns，当前一次采样执行时间约265,834ns、历史`Max-Time`约586,985ns。该数据只用于1ms迁移前预算，不代表压力窗口已经通过。
- 目标是把BUS LinuxCNC servo、HAL realtime function、EtherCAT主站应用周期、5台驱动Sync0/DC与同周期安全链原子统一为1ms（1000Hz）。
- Pulse链当前`SERVO_PERIOD=4500000`，不属于本目标；本工作不得顺手修改Pulse周期。
- 当前状态是“需求和落地方法已明确，代码仍为2ms”。没有完成构建、部署和板端验收前，禁止写“1ms已生效”。

## 2. 最终结果

1. Windows canonical source只保留一套BUS 1ms实现；2ms HAL/XML路径在引用迁移完成后物理删除。
2. 板端`halcmd show thread`回读`servo-thread period=1000000ns`，LinuxCNC、lcec、Sync0和安全latch使用同一周期身份。
3. 5台汇川SV630N持续OP，Domain WKC持续10/10，DC reference fresh且phase-jitter不贴近或跨越1ms。
4. idle、冷/热启动、连续G-code、UI冻结和CPU1满载窗口均无servo overrun；`Max-Time<1000000ns`，工程放行目标`Max-Time<=700000ns`。
5. 编码器连续、跟随误差不退化、电机无沙粒感/周期顿挫，轨迹队列不枯竭，0.1s急停链继续成立。
6. 验收结束恢复`ESTOP active`和`Machine Off`。

## 3. 硬边界

- 不能只改`SERVO_PERIOD`；INI、HAL、EtherCAT XML、DC/PLL、启动、部署、readiness和测试必须同一slice原子迁移。
- 不允许1ms/2ms混装，不允许环境变量、双文件、启动参数或自动探测选择周期。
- 不允许为了过1ms关闭EtherCAT证据、降低安全轮询、降低servo功能、把非运动任务塞进CPU0或牺牲UI/remote功能。
- `refClockSyncCycles`、`ARC_BLEND_GAP_CYCLES`、watchdog、heartbeat、Home稳定计数、debounce/filter、freshness/timeout等所有“按周期计数”的参数必须按真实时间语义逐项裁决，不能机械原值复制。周期从2ms减半后，若目标墙钟时间不变，cycle count必须相应翻倍。
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
- `board/services/microkernel/v5_microkernel_manifest.c`
- `board/app/src/v5_boot_closure.c`
- `board/services/command_gate/init.d/v5-linuxcnc-command-gate`
- `board/tools/deploy/check_v5_runtime_policy.py`
- `board/tools/deploy/verify_v5_board_runtime.sh`
- `board/tools/linuxcnc/v5_bus_module_owner_smoke.py`
- `board/tools/deploy/v5_home_router_bridge_smoke.py`
- `board/tools/deploy/test_v5_estop_abort_rtcp_output_policy.py`
- `board/tools/deploy/measure_v5_cold_boot_smoke.py`
- 当前引用2ms路径的功能文档与需求索引位置。

迁移完成后用`rg`检查产品source/config/test/manifest中不再存在活动`v5_bus_2ms.hal`、`ethercat-conf-2ms.xml`和BUS `SERVO_PERIOD=2000000`引用；活动工作文档在真源和验收闭合后删除，不作为历史第二真源保留。

### 5.3 时间语义审计

逐项核对以下逻辑使用的是纳秒/秒还是“周期次数”：

- `refClockSyncCycles=5`当前在2ms下等于约10ms；迁移时必须先裁决目标是“每5周期”还是“约10ms”。
- `ARC_BLEND_GAP_CYCLES=4`在2ms和1ms下墙钟时间不同，必须由trajectory语义和运动证据裁决。
- lcec DC/PLL更新时间、Sync0 shift、datagram timeout、watchdog。
- `v5_safety_latch` heartbeat 0.1s、急停确认和reset epoch。
- Home release/stable cycles、router/cia402滤波和驱动状态稳定计数。
- readiness freshness、两次DC fresh pair、启动激活后的下一完整servo周期。

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

### 6.4 冷/热启动

- 先做一次冷启动单变量否决；通过后做3次整板冷启动和3次受控backend热重启。
- 每次必须证明唯一lcec activation、下一完整1ms周期开始普通function list、5台从站OP、WKC 10/10、DC fresh pair、CPU合同和UI ready。
- 启动阶段异常日志只有与当前generation原生actual一起解释，不能用旧baseline或瞬时OP冒充成功。

### 6.5 真实运动与压力验收

1. 按active model从原始UI路径打开匹配的`cc-ac.ngc`或`cc-bc.ngc`，完成Home和三轮连续golden motion。
2. 同步采集`motion.requested-vel/current-vel`、queue、servo Time/Max-Time、overrun、DC/WKC、编码器与following error。
3. 在持续运动窗口冻结UI并把CPU1压满；运动必须连续，CPU0实时链不得受影响。
4. AC和BC模型在产品支持范围内分别完成后，才能把1ms作为全产品基线；只过当前模型只能算该模型切片通过。
5. 完成0.1s急停链验证，清执行态但保留程序身份；最终回读`estop_active=1`、`machine_enabled=0`。

## 7. 放行判定

以下条件必须同时满足：

- canonical source、artifact、板端安装文件hash一致。
- BUS周期所有owner回读均为1ms，无2ms活动入口或运行时fallback。
- 全部压力窗口servo overrun为0，`Max-Time<=700000ns`；任何样本达到1ms直接失败。
- 5轴持续OP、WKC 10/10、DC fresh/phase稳定，无持续datagram/reference-clock错误。
- 运动速度、队列、编码器、following error、电机平顺性和急停均不退化。
- 工作树中的需求owner、实现、部署清单、测试与板端readback一致。

只有满足以上条件才能声明`board_verified`。仅改文档、改INI、通过本地smoke、完成VM编译或板端回读周期值，都不能单独宣称1ms完成。
