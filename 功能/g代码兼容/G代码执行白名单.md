# G代码执行白名单

更新时间：2026-07-15

状态：`方案讨论稿`。本文是 `功能/g代码兼容` 目录内唯一规范性G-code执行白名单。白名单命令、地址字、放行条件、拒绝条件、系统入口和晋级要求只能在本文修改。

相关文档：

- [兼容翻译与跨系统命令对照](./FANUC_三菱_新代_西门子_LinuxCNC_命令功能对照表.md)
- [基于目前代码现状的实施计划](./基于目前代码现状的实施计划书.md)

本文尚未合并进项目需求索引和运行源码，因此“列入本文”表示已批准的方案范围，不表示代码已经实现、构建或板端验证。

## 1. 白名单目标和最高规则

最高目标是“能够安全运行更多真实用户程序”，不是为了安全而把功能缩到无法使用。安全是每条兼容规则的放行条件，可用是白名单最终要达到的结果。

1. 来源代码与LinuxCNC语义相同：自动进入 `allow_identity`，重新序列化为LinuxCNC canonical后执行。
2. 写法不同但语义可以唯一对应：自动进入 `allow_translate`，逐字段翻译后执行。
3. LinuxCNC没有同名单码，但能够无歧义降级为基础运动、模态或受控动作：自动进入 `allow_expand`，在Program Open阶段展开后执行。
4. OEM M码、IO、工具、WCS、RTCP或探测能够绑定项目现有native owner：自动进入 `allow_mapped`，取得同源fresh readback后执行。
5. 只有在精确profile下仍无法实现、无法证明等效、目标确实无能力或副作用无法闭合时，才进入明确黑名单。
6. “尚未编写translator/test”只能进入待实现兼容清单，不能冒充永久黑名单；实现优先级按真实用户程序覆盖率排序。
7. 系统必须先完成格式归一、唯一profile匹配、确定性转换/展开、目标能力检查、canonical生成和LinuxCNC目标预检，正常成功不询问用户。
8. 当前尚未命中可执行rule的节点仍必须fail-closed，使整份程序 `ready_to_run=false`；不得跳行、删词、警告后继续或只运行已通过部分。
9. 用户确认不能把无法等效的输入强制变成可执行，但用户也不应被要求手工完成系统能够自动完成的翻译。
10. 白名单只决定可执行语义，不取代LinuxCNC/native的运动、安全、坐标、刀具、RTCP、探测、IO和actual owner。
11. 外部方言原文不能raw passthrough给LinuxCNC；即使拼写相同，也必须经过typed AST、canonical输出和目标AST复核。
12. `G53`、`G54-G59.3`、基础运动/模态、程序结束、主轴、冷却、换刀、RTCP和G31探测属于真实程序核心覆盖项，必须优先进入可执行白名单，不得因实现工作量长期留在拒绝示例中。

<!-- AI_ONLY_BEGIN: policy_and_profile
## 2. 白名单集合和策略

### 2.1 四个集合

| 集合               | 内容                                                                | 是否可执行                   |
| —                  | —                                                                   | —                            |
| 待实现兼容清单     | 已知且原则上能够identity/translate/expand/map，但实现或证据尚未完成 | 当前否；必须持续收敛为白名单 |
| 执行白名单         | 精确profile下已证明等价且仍有效的rule                               | 是                           |
| 受信任系统入口清单 | 精确remap、系统宏和项目M码及其identity                              | 仅受控调用                   |
| 明确黑名单         | 精确profile下仍无法等效、目标无能力或副作用不可闭合                 | 否                           |

“尚未实现”不等于“无法实现”。能够等效的命令必须进入待实现兼容清单并最终晋级白名单；只有完成技术论证仍无法安全等效的命令才进入黑名单。

### 2.2 闭集策略

| policy                   | 含义                                                                   |
| —                        | —                                                                      |
| `allow_identity`         | 来源和目标语义已证明一致，仍经typed AST重新序列化                      |
| `allow_translate`        | 存在唯一、确定、逐字段证明的语义转换                                   |
| `allow_expand`           | 目标没有同名单码，但可确定展开为LinuxCNC基础canonical节点              |
| `allow_mapped`           | 精确绑定项目native owner、IO、tool、WCS或其它受控能力                  |
| `allow_privileged`       | 只调用精确登记的项目remap/系统入口                                     |
| `pending_implementation` | 已判断可实现，但translator、测试或目标能力接线尚未完成；不得归入黑名单 |
| `deny`                   | 已完成分析后仍无法等效、目标无能力或副作用无法闭合                     |

不得实现 `unknown_passthrough`、`warn_and_run`、`ignore_line` 或 `user_override`。

### 2.3 三层白名单

程序必须同时通过：

1. **Source AST白名单**：每个来源节点恰好命中一个rule。
2. **Typed IR白名单**：每个semantic operation在当前机型允许集合内。
3. **LinuxCNC输出AST白名单**：canonical重新解析后每个目标节点获准且没有新增副作用。

零命中或多命中都拒绝。

## 3. Profile前置条件

### 3.1 Source profile

可执行rule必须绑定精确 `source_profile_id`，至少包含：

- controller family、model、series；
- software/firmware；
- mill/turn/mill-turn；
- axis schema；
- option set；
- parameter profile hash；
- OEM PLC/PMC/M-code profile；
- decimal mode；
- unit contract；
- arc center/R/rapid path mode；
- tool H/D/T schema；
- manual identity和版本。

只写 `FANUC-like`、`Mitsubishi`、`Syntec` 或 `Siemens` 不能获得执行资格。

### 3.2 Target profile

必须绑定：

- LinuxCNC精确版本/build identity；
- active model、轴、kinematics；
- INI/HAL/remap identity；
- WCS、刀具/TLO、主轴、冷却、探测、换刀和IO能力；
- ruleset identity；
- 系统宏/remap manifest identity。

运行时急停、使能、回零、stillness、软限位和fresh actual仍由native owner裁决。

### 3.3 自动profile匹配

1. 客户/机床默认profile、程序metadata和导入来源优先自动匹配。
2. 只有唯一且精确匹配时自动放行，不弹确认。
3. 语法特征只能排除候选，不能单独授权。
4. 零匹配或多匹配时拒绝，并列出造成歧义的源行和profile差异。
5. 人工选择也不能绕过版本、参数hash和rule条件。
AI_ONLY_END -->

## 4. 自动兼容与拒绝边界

### 4.1 自动处理但不扩大白名单

在对应rule已获准时，下列差异自动处理：

- 大小写；
- CRLF/LF、允许的空白和Tab；
- G/M前导零；
- 成对 `%` 包装；
- 合法N行号；
- 已登记普通注释的惰性处理；
- profile已明确的无小数点/尾点规则；
- 已登记同义代码；
- 唯一 `allow_translate` 转换。

自动处理必须保留原始精度、source span和translation report。

### 4.2 不能自动猜测

以下内容不能为了减少报错而猜测：

- 单位、直径/半径和小数倍率；
- G0路径；
- 圆弧中心、长短弧、全圆、多圈；
- H/D/T映射；
- WCS/G92/坐标变换；
- fixed cycle、探测、宏和子程序；
- RTCP、3+2和旋转轴；
- M码IO、PLC和持久副作用；
- 任何可能改变加工目的的默认值。

<!-- AI_ONLY_BEGIN: rule_schema
## 5. 原子rule schema

每条rule只能裁决一个明确语义，至少包含：

| 字段                          | 要求                                     |
| —                             | —                                        |
| `schema_version`              | rule schema版本                          |
| `rule_id/rule_version`        | 永久ID和单rule版本                       |
| `ruleset_id/ruleset_hash`     | 所属规则集identity                       |
| `source_profile_id`           | 精确来源profile                          |
| `source_manual_refs`          | 手册版本、页码和hash                     |
| `source_pattern`              | AST节点类型，不是宽泛字符串regex         |
| `allowed_words`               | 类型、单位、范围、基数、必填、互斥和默认 |
| `modal_preconditions`         | 来源模态和上下文                         |
| `semantic_operation`          | 唯一typed IR操作                         |
| `modal_postconditions`        | 模态和持久副作用                         |
| `policy`                      | 四种闭集policy之一                       |
| `target_profile_constraints`  | LinuxCNC、机型、轴和能力约束             |
| `emitter_id`                  | typed emitter，禁止raw字符串模板         |
| `output_allowlist`            | 允许的目标AST节点和字段                  |
| `runtime_owner_refs`          | 实际执行owner                            |
| `failure_ids`                 | 固定错误ID                               |
| `golden_cases/negative_cases` | 正例、边界、反例和歧义例                 |
| `evidence_identity`           | 测试、解释器和板端证据hash               |
| `approval_state`              | 成熟度和批准状态                         |
| `invalidation`                | 自动失效条件                             |
AI_ONLY_END -->

## 6. 统一执行白名单

本节是唯一面向人的执行白名单：左侧依次列出FANUC、三菱、新代、西门子写法，右侧列出唯一LinuxCNC执行目标。语义相同的代码直接规范化，写法或副作用不同的代码自动翻译、展开或映射；只有确实无法等效的输入才拒绝。

**统一放行前提**

V1包含基础加工、加工坐标系选择、RTCP程序控制和G31直线探测四类规则。共同前提为：

- 自动唯一匹配或受控选择后冻结的精确source profile；
- target capability、ruleset identity和机床配置完整；
- 基础加工规则只覆盖三轴铣削、XYZ和单主轴；
- WCS、RTCP和探测只能使用本节各自的专项rule，不能借基础规则隐式放行；
- RTCP必须绑定fresh active model、G53 geometry、轴映射、工具/TLO和native RTCP owner；
- 探测必须绑定登记探头输入、触发极性、边沿、停止行为、结果readback和运动安全条件；
- 只有未登记、动态不可判定或副作用无法闭合的宏、子程序、坐标写入、固定循环、3+2和自定义IO才拒绝；可确定展开或映射的项目进入本节相应G代码/M代码表。

**等效代码列**

白名单采用“4个外部系统代码列 + 1个LinuxCNC目标代码列”的横向结构：

| 外部来源1 | 外部来源2 | 外部来源3 | 外部来源4 | 唯一执行目标 |
| --------- | --------- | --------- | --------- | ------------ |
| FANUC     | 三菱      | 新代      | 西门子    | LinuxCNC     |

表中代码只在对应的精确source profile已经匹配、版本和机床能力已登记、该行全部放行条件成立时等效。它不是对某品牌所有型号、所有参数或所有OEM PLC配置的通用承诺。

- `—`：当前白名单不接受该系统的该项写法；不得透传或借用其它列。
- `ISO`：只适用于已登记并确认启用ISO方言的source profile，不适用于西门子原生语法。
- `*`：M码除了方言语义，还必须与本机OEM/PLC/IO映射逐项一致；号码相同不能单独证明等效。
- 表内前导零写法会先规范化，但source token、profile和转换证据仍写入translation report。
- 四个来源列都必须转换为本行LinuxCNC列的typed语义；不能把来源原文直接透传到执行链。

**基础代码**

| FANUC     | 三菱      | 新代      | 西门子       | LinuxCNC | 等效功能       | 放行条件与拒绝边界                                                                                    |
| --------- | --------- | --------- | ------------ | -------- | -------------- | ----------------------------------------------------------------------------------------------------- |
| `G17`     | `G17`     | `G17`     | `G17`（ISO） | `G17`    | 选择XY平面     | 本块不带轴字、IJK、R、P；G18/G19不在V1                                                                |
| `G20`     | `G20`     | `G20`     | `G20`（ISO） | `G20`    | 选择inch单位   | 首个数值运动前出现；profile单位冲突或中途切换拒绝                                                     |
| `G21`     | `G21`     | `G21`     | `G21`（ISO） | `G21`    | 选择mm单位     | 首个数值运动前出现；profile单位冲突或中途切换拒绝                                                     |
| `G90`     | `G90`     | `G90`     | `G90`（ISO） | `G90`    | 绝对距离模式   | 首个运动前明确；同码异义或初始状态不明拒绝                                                            |
| `G91`     | `G91`     | `G91`     | `G91`（ISO） | `G91`    | 增量距离模式   | 首个运动前明确；同码异义或初始状态不明拒绝                                                            |
| `G94`     | `G94`     | `G94`     | `G94`（ISO） | `G94`    | 每分钟进给     | 仅三轴铣削；首个G1/G2/G3前明确；车削、每转或逆时间进给拒绝                                            |
| `G00/G0`  | `G00/G0`  | `G00/G0`  | `G0`（ISO）  | `G0`     | 快速定位       | 只允许XYZ且至少一个轴；rapid path必须等价；旋转轴、F、IJKRP或独立轴路径不等价时拒绝                   |
| `G01/G1`  | `G01/G1`  | `G01/G1`  | `G1`（ISO）  | `G1`     | 直线切削       | XYZ和可选F，至少一个轴；所需模态/feed明确；旋转轴、G93/G95、表达式、未知变换/刀补/RTCP拒绝            |
| `G02/G2`  | `G02/G2`  | `G02/G2`  | `G2`（ISO）  | `G2`     | XY顺时针圆弧/螺旋圆弧   | G17；X/Y终点、增量I/J和可选Z/F；source圆心模式、方向、半径和目标螺旋能力必须已登记。普通规则拒绝R、K、未登记P多圈和圆心不明；RTCP姿态轴只能由本节RTCP圆弧专项rule放行 |
| `G03/G3`  | `G03/G3`  | `G03/G3`  | `G3`（ISO）  | `G3`     | XY逆时针圆弧/螺旋圆弧   | 与上行“XY顺时针圆弧/螺旋圆弧”的放行条件相同；连续圆弧链必须逐块验证端点、圆心、切向连续、同步Z和进给模态                                      |
| `F_`      | `F_`      | `F_`      | `F_`（ISO）  | `F_`     | G94模态进给    | 独立块或与G1/G2/G3同块；大于0且不超机床上限；G93/G95、表达式或非法数值拒绝                            |
| `S_`      | `S_`      | `S_`      | `S_`（ISO）  | `S_`     | 单主轴目标转速 | 独立块或与M3/M4同块；非负且不超主轴上限；多主轴、表达式或非法数值拒绝                                 |
| `M00/M0`  | `M00/M0`  | `M00/M0`  | `M0`（ISO）  | `M0`     | 程序暂停       | 程序上下文且本块无运动/其它M码；MDI、宏内或混块拒绝                                                   |
| `M01/M1`  | `M01/M1`  | `M01/M1`  | `M1`（ISO）  | `M1`     | 可选暂停       | source与目标可选停止开关语义一致；程序上下文且无运动/其它M码                                          |
| `M02/M2`  | `M02/M2`  | `M02/M2`  | `M2`（ISO）  | `M2`     | 程序结束       | 必须是最后可执行语义块且无运动/IO/其它M码；M30不得归入本rule                                          |
| `M03/M3`* | `M03/M3`* | `M03/M3`* | `M3`*（ISO） | `M3`     | 主轴正转       | 单主轴映射已登记且已有有效S；执行时由主轴owner fresh gate；方向/映射不明拒绝                          |
| `M04/M4`* | `M04/M4`* | `M04/M4`* | `M4`*（ISO） | `M4`     | 主轴反转       | 单主轴映射已登记且目标允许反转；执行时fresh gate；方向/能力不明拒绝                                   |
| `M05/M5`* | `M05/M5`* | `M05/M5`* | `M5`*（ISO） | `M5`     | 主轴停止       | 单主轴映射已登记且本块无其它M码；多主轴选择不明拒绝                                                   |
| `M07/M7`* | `M07/M7`* | `M07/M7`* | `M7`*（ISO） | `M7`     | 雾冷开启       | source与本机精确IO能力/映射一致；执行时IO owner fresh gate；无能力或映射不明拒绝                      |
| `M08/M8`* | `M08/M8`* | `M08/M8`* | `M8`*（ISO） | `M8`     | 冷却液开启     | source与本机精确IO能力/映射一致；执行时IO owner fresh gate；无能力或映射不明拒绝                      |
| `M09/M9`* | `M09/M9`* | `M09/M9`* | `M9`*（ISO） | `M9`     | 冷却全部关闭   | source与目标“全部关闭”语义及IO映射一致；语义或映射不明拒绝                                            |

**基础地址字**

地址字必须同时被所在命令rule允许；单独出现在本表不能扩大命令白名单。

| FANUC   | 三菱    | 新代    | 西门子         | LinuxCNC | 等效内容                     | 使用限制                                                       |
| ------- | ------- | ------- | -------------- | -------- | ---------------------------- | -------------------------------------------------------------- |
| `X/Y/Z` | `X/Y/Z` | `X/Y/Z` | `X/Y/Z`（ISO） | `X/Y/Z`  | 三个直线轴坐标               | 有限数值、保留精度且在目标轴范围内；仅G0/G1/G2/G3；G2/G3中的Z表示同步螺旋升降，必须验证目标能力和范围；旋转轴只由RTCP专项rule放行 |
| `I/J`   | `I/J`   | `I/J`   | `I/J`（ISO）   | `I/J`    | XY圆弧中心偏置               | 仅G17 G2/G3；source profile必须确认增量圆心模式；普通规则禁止K/R/未登记P多圈和圆心不明，全圆必须有独立精确rule |
| `F`     | `F`     | `F`     | `F`（ISO）     | `F`      | G94进给值                    | 大于0且不超过machine上限；仅G1/G2/G3或独立modal块              |
| `S`     | `S`     | `S`     | `S`（ISO）     | `S`      | 单主轴转速值                 | 大于等于0且不超过主轴上限；M3/M4前须有大于0有效S               |
| `N`     | `N`     | `N`     | `N`（ISO）     | 可省略   | source行号/line map identity | 非负整数且不重复；不得作GOTO目标；canonical可不输出            |

地址字不按字母整体授权，而是由命中的command rule限定上下文：`T/H/D`可由工具/TLO/换刀rule消费，`P/Q/L/R`可由dwell、cycle、subprogram、RTCP或IO rule消费，`A/B/C`可由active-model RTCP rule消费。脱离对应rule、参数范围或owner映射时才拒绝。

**G53和加工坐标系**

加工程序必须能够选择明确WCS。项目LinuxCNC/native WCS owner支持 `G54-G59.3`；translator按“第几个已登记加工坐标系槽位”建立typed语义，再输出LinuxCNC代码，不能只按字符串相同判断。

| FANUC      | 三菱       | 新代              | 西门子                     | LinuxCNC | 等效功能          | 放行条件与拒绝边界                                                         |
| ---------- | ---------- | ----------------- | -------------------------- | -------- | ----------------- | -------------------------------------------------------------------------- |
| `G53`      | `G53`      | `G53`             | `G53`或精确等效`SUPA/G153` | `G53`    | 本块使用机械坐标  | 非模态、绝对目标、fresh homed/limit能力；不得与项目G53旋转中心几何参数混淆 |
| `G54`      | `G54`      | `G54`或`G54 P1`   | `G54`                      | `G54`    | 选择第1加工坐标系 | source槽位1与target G54 identity一致；只选择、不写偏置                     |
| `G55`      | `G55`      | `G55`或`G54 P2`   | `G55`                      | `G55`    | 选择第2加工坐标系 | 同上，槽位2                                                                |
| `G56`      | `G56`      | `G56`或`G54 P3`   | `G56`                      | `G56`    | 选择第3加工坐标系 | 同上，槽位3                                                                |
| `G57`      | `G57`      | `G57`或`G54 P4`   | `G57`                      | `G57`    | 选择第4加工坐标系 | 同上，槽位4                                                                |
| `G58`      | `G58`      | `G58`或`G54 P5`   | `G505`                     | `G58`    | 选择第5加工坐标系 | 西门子必须确认G505为第5 settable WO；旧控制器把G58作可编程偏移时拒绝       |
| `G59`      | `G59`      | `G59`或`G54 P6`   | `G506`                     | `G59`    | 选择第6加工坐标系 | 西门子必须确认G506已配置；任一端槽位缺失拒绝                               |
| `G54.1 P1` | `G54.1 P1` | `G59.1`或`G54 P7` | `G507`                     | `G59.1`  | 选择第7加工坐标系 | 必须按slot identity证明是同一第7槽；禁止仅按P号或小数编号猜测              |
| `G54.1 P2` | `G54.1 P2` | `G59.2`或`G54 P8` | `G508`                     | `G59.2`  | 选择第8加工坐标系 | 同上，槽位8                                                                |
| `G54.1 P3` | `G54.1 P3` | `G59.3`或`G54 P9` | `G509`                     | `G59.3`  | 选择第9加工坐标系 | 同上，槽位9                                                                |

本节允许G53机械坐标单块和WCS选择。`G52/G92`的可确定子集按本节“扩展G代码”表转换；`G10`、直接偏置写入、旋转/缩放frame、G500抑制和第10以上槽位必须另建精确rule。translator不得修改或复制LinuxCNC `PARAMETER_FILE` 真值；执行和显示成功必须以同一LinuxCNC/native active WCS及fresh epoch/readback为准。

**RTCP**

项目程序内RTCP唯一目标入口是 `M64 P0` 开启、`M65 P0` 关闭；两者驱动已登记 `motion.digital-out-00`，由native RTCP owner与UI latch合流到同一个switchkins actual。历史 `M128/M129`、UI代发和普通MDI控制均不允许。

| FANUC      | 三菱       | 新代                 | 西门子              | LinuxCNC                               | 等效功能                   | 放行条件与拒绝边界                                                                                                                       |
| ---------- | ---------- | -------------------- | ------------------- | -------------------------------------- | -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| `G43.4 H_` | `G43.4 H_` | `G43.4 H_ [Q_] [I_]` | `TRAORI`            | `G43 H<resolved>`后`M64 P0`            | 开启刀尖点控制             | 精确profile、active model、G53 geometry、轴映射和工具/TLO均有效；H必须经tool owner解析；Q/I或TRAORI附带frame/TLO副作用不能完整重现时拒绝 |
| `G49`      | `G49`      | `G49`                | `TRAFOOF`           | `M65 P0`，来源同时取消TLO时再输出`G49` | 关闭刀尖点控制             | 必须区分“只关RTCP”和“同时取消TLO”；顺序及终态由typed语义生成，不能统一删掉来源副作用                                                     |
| `G00/G0`   | `G00/G0`   | `G00/G0`             | `G0`或已登记ISO等效 | `G0`                                   | RTCP开启状态下快速定位     | 只允许XYZ加active model声明的AC或BC旋转轴；同一native model/geometry下证明rapid path语义等效                                             |
| `G01/G1`   | `G01/G1`   | `G01/G1`             | `G1`或已登记ISO等效 | `G1`                                   | RTCP开启状态下协调直线进给 | G94、有效F、XYZ加active model旋转轴；switchkins actual必须fresh ON；未知orientation表示法拒绝                                            |
| `G02/G03 X/Y/Z_ I/J_ A/C_` | `G02/G03 X/Y/Z_ I/J_ A/C_` | `G02/G03 X/Y/Z_ I/J_ A/C_` | `G2/G3`加profile登记姿态轴 | `G2/G3 X/Y/Z_ I/J_ A/C_` | RTCP开启状态下协调螺旋圆弧和AC姿态 | 仅G17、增量I/J、G94、有效F和AC active model；每块验证圆心/半径/方向/同步Z/姿态轴，连续链还要验证相邻端点与切向一致。`cc-ac`五圈固定为同一RTCP段内20个相切90°圆弧，禁止密集G1逼近或在中间插入暂停、G0、exact-stop、抬刀、RTCP切换 |
| `A/B/C`    | `A/B/C`    | `A/B/C`              | profile登记轴字     | active model的`A/C`或`B/C`             | RTCP姿态轴目标             | AC只接受A/C，BC只接受B/C；source轴名、正方向、单位、wrap和目标descriptor必须逐轴一致                                                     |

LinuxCNC来源profile中的 `M64 P0/M65 P0` 允许identity转换，但其它P号仍按普通数字输出/IO规则处理，不因本规则放行。RTCP ON/OFF、Home强制OFF和UI按钮是不同请求来源，最终只认同一native actual。

语义依据：项目owner见[微内核](../微内核.md)；外部命令需绑定对应版本官方手册，包括 [FANUC TCP G43.4](https://www.fanucamerica.com/docs/default-source/cnc-files/mwa-030-en_01_1511_5-axis.pdf)、[三菱M800V/M80V编程手册](https://dl.mitsubishielectric.com/dl/fa/document/manual/cnc/ib1501621%28eng%29/ib1501621-1501622engh.pdf)、[新代G43.4/G49说明](https://www.syntecclub.com/cncrel/Manual/PDF/%E8%87%AA%E5%8B%95%E5%8C%96%E7%94%A2%E5%93%81%E7%A8%8B%E5%BC%8F%E6%89%8B%E5%86%8A%28%E8%8B%B1%E6%96%87%29.pdf)和[西门子TRAORI/TRAFOOF说明](https://support.industry.siemens.com/cs/attachments/109762409/SIN_WF5_0918_en-US.pdf)。

**G31探头探测**

G31按“朝工件方向、探头由未触发变为触发、触发后删除剩余行程并受控停止、到终点未触发不自动报警”的精确语义映射到LinuxCNC `G38.3`。不得把G31泛化为任意G38.x。

| FANUC             | 三菱              | 新代              | 西门子                        | LinuxCNC               | 等效功能                                     | 放行条件与拒绝边界                                                                                                                                                  |
| ----------------- | ----------------- | ----------------- | ----------------------------- | ---------------------- | -------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `G31 X/Y/Z_ F_`   | `G31 X/Y/Z_ F_`   | `G31 X/Y/Z_ F_`   | `MEAS=1 G1 X/Y/Z_ F_`         | `G38.3 X/Y/Z_ F_`      | 朝工件直线探测；触发停止；未触发到终点不报警 | 仅登记probe 1、上升沿、未触发到触发；G94正进给；至少一个XYZ目标；motion.probe-input、极性和fresh初态有效；刀补、RTCP、旋转轴、G31.n/P/L、多步skip、MEAW和下降沿拒绝 |
| G31 skip位置/状态 | G31 skip位置/状态 | G31 skip位置/状态 | `$AA_MM/$AA_MW`和`$AC_MEA[1]` | `#5061-#5069`和`#5070` | 探测位置与成功状态                           | 只允许typed result readback；source变量引用必须有单独变量rule；不得从显示坐标、旧结果或UI缓存推导                                                                   |

若某精确source profile规定“未触发必须报警”，必须建立独立rule并映射 `G38.2`；`G38.4/G38.5`只用于从接触状态离开工件的独立规则。探测开始前probe已触发、输入无效、超限、未回零、未使能、非coord mode或fresh结果缺失均由LinuxCNC/native运动owner准确失败。

语义依据：[LinuxCNC G38.2-G38.5](../../linuxcnc/docs/src/gcode/g-code.adoc)、[三菱G31 Skip Function](https://dl.mitsubishielectric.com/dl/fa/document/manual/cnc/ib1500269%28eng%29/ib1500269engf.pdf)、[新代G31说明](https://www.syntecclub.com/cncrel/Manual/PDF/%E8%87%AA%E5%8B%95%E5%8C%96%E7%94%A2%E5%93%81%E7%A8%8B%E5%BC%8F%E6%89%8B%E5%86%8A%28%E8%8B%B1%E6%96%87%29.pdf)和[西门子MEAS/MEAW说明](https://support.industry.siemens.com/cs/attachments/54714410/PGA_en.pdf)。FANUC列只有在精确型号官方编程手册确认同一skip边沿、停止和未触发行为后才能从`specified`晋级为可执行。

**扩展G代码**

本节采用 `allow_identity`、`allow_translate` 或 `allow_expand`。目标LinuxCNC原生支持且语义一致时直接生成目标码；字段或副作用不同但能唯一转换时生成等效目标码；目标没有同名单码但刀路可静态证明时，在Program Open阶段展开为基础canonical blocks。用户不需要逐条确认。

| FANUC                         | 三菱                    | 新代                    | 西门子                       | LinuxCNC目标                              | 等效策略           | 放行条件与拒绝边界                                                         |
| ----------------------------- | ----------------------- | ----------------------- | ---------------------------- | ----------------------------------------- | ------------------ | -------------------------------------------------------------------------- |
| `G04 X_/P_`                   | `G04 X_/P_`             | `G04 P_`                | `G4 F_`或profile登记格式     | `G4 P<seconds>`                           | translate          | source时间单位、最大值和是否占用X/P必须精确；统一换算秒                    |
| `G18`                         | `G18`                   | `G18`                   | `G18`（ISO）                 | `G18`                                     | identity           | 目标机型存在对应轴和平面                                                   |
| `G19`                         | `G19`                   | `G19`                   | `G19`（ISO）                 | `G19`                                     | identity           | 目标机型存在对应轴和平面                                                   |
| profile登记`G90.1`            | profile登记`G90.1`      | `G90.1`                 | profile登记格式              | `G90.1`                                   | translate          | source必须明确绝对圆心模式；不靠默认值猜测                                 |
| profile登记`G91.1`            | profile登记`G91.1`      | `G91.1`                 | profile登记格式              | `G91.1`                                   | translate          | source必须明确增量圆心模式                                                 |
| `G93`                         | `G93`                   | `G93`                   | `G93`（ISO/profile）         | `G93`                                     | identity           | 每块有效F、目标支持inverse-time且单位一致                                  |
| `G95`                         | `G95`                   | `G95`                   | `G95`（ISO/profile）         | `G95`                                     | identity           | fresh spindle feedback和每转进给能力存在                                   |
| `G40`                         | `G40`                   | `G40`                   | `G40`                        | `G40`                                     | identity           | cutter compensation状态已知                                                |
| `G41 D_`                      | `G41 D_`                | `G41 D_`                | `G41 D_`                     | `G41 D<resolved>`                         | translate          | D经tool owner映射；入口/出口几何满足LinuxCNC补偿约束                       |
| `G42 D_`                      | `G42 D_`                | `G42 D_`                | `G42 D_`                     | `G42 D<resolved>`                         | translate          | 与G41相同                                                                  |
| `G43 H_`                      | `G43 H_`                | `G43 H_`                | `G43 D_`或profile登记格式    | `G43 H<resolved>`                         | translate          | tool/TLO owner能把source register唯一映射到目标tool                        |
| `G49`                         | `G49`                   | `G49`                   | profile登记取消码            | `G49`                                     | translate          | 非RTCP上下文；RTCP关闭使用本节“RTCP”规则处理组合副作用                     |
| profile登记exact-stop码       | profile登记exact-stop码 | profile登记exact-stop码 | profile登记exact-stop码      | `G61`或`G61.1`                            | translate          | 必须区分exact path与exact stop；不能用G60/G61编号猜测                      |
| profile登记continuous码和容差 | profile登记格式         | profile登记格式         | `G64`（ISO/profile）         | `G64 P_ Q_`                               | translate          | P/Q由source轮廓/容差语义确定；无唯一容差时拒绝                             |
| `G80`                         | `G80`                   | `G80`                   | `MCALL`取消或profile登记格式 | `G80`                                     | translate          | 当前cycle状态已知                                                          |
| `G81`                         | `G81`                   | `G81`                   | `CYCLE81`                    | `G81`或展开`G0/G1`                        | identity/expand    | Z/R/F、绝对/增量和retract mode完全映射                                     |
| `G82`                         | `G82`                   | `G82`                   | `CYCLE82`                    | `G82`或展开`G0/G1/G4`                     | translate/expand   | dwell单位、深度、R平面和返回点一致                                         |
| `G83`                         | `G83`                   | `G83`                   | `CYCLE83`                    | `G83`或展开基础运动                       | translate/expand   | peck、退刀、排屑、最终深度和retract语义一致                                |
| profile刚性攻丝码             | profile刚性攻丝码       | profile刚性攻丝码       | `CYCLE84`                    | `G33.1`或已验证展开                       | translate          | 禁止同码猜测LinuxCNC G84；必须绑定spindle sync、pitch、方向和退刀          |
| `G85/G86/G89`                 | profile登记格式         | profile登记格式         | `CYCLE85/CYCLE86`等          | 对应LinuxCNC cycle或展开                  | translate/expand   | 主轴停止/方向、dwell、退刀和返回点全部等效                                 |
| `G98/G99`                     | `G98/G99`               | `G98/G99`               | cycle参数/profile登记格式    | `G98/G99`                                 | translate          | 只在固定循环上下文；车削同码异义时拒绝                                     |
| `G28/G30`                     | `G28/G30`               | `G28/G30`               | profile登记reference命令     | 展开为已登记中间点和`G53`目标             | expand             | 中间点、顺序、reference位置、homed和soft-limit全部已登记                   |
| `G52`                         | `G52`                   | `G52`                   | `TRANS/ATRANS`的静态子集     | 坐标预变换或登记native offset             | translate/expand   | 全部值静态、可逆且source scope明确；动态变量拒绝                           |
| `G92/G92.1`的精确profile      | 精确profile             | 精确profile             | `PRESET/TRANS`的精确子集     | 项目登记`G92/G92.1/G92.2/G92.3`语义       | translate          | 必须走项目native G92 owner并保留持久/取消语义                              |
| `G68/G69`                     | `G68/G69`               | `G68/G69`               | `ROT/AROT`静态子集           | Program Open静态旋转坐标后输出基础运动    | expand             | 旋转中心/角度/plane全部常量；探测、宏、RTCP或动态frame混用拒绝             |
| `G51/G50`                     | profile登记格式         | `G51/G50`               | `SCALE/ASCALE`静态子集       | Program Open静态缩放后输出基础运动        | expand             | 比例和中心常量，刀补/圆弧精度及单位转换可证明                              |
| `G33`类profile                | profile登记格式         | profile登记格式         | `G33`或profile登记格式       | `G33/G33.1`                               | translate          | encoder index、pitch、方向和spindle sync能力完整                           |
| profile登记直径/半径模式      | profile登记格式         | profile登记格式         | `DIAMON/DIAMOF`              | `G7/G8`                                   | translate          | 仅车床capability；X值和offset按直径/半径精确换算                           |
| `G96/G97`                     | `G96/G97`               | `G96/G97`               | `G96/G97`                    | `G96/G97`                                 | identity/translate | 仅车床；D限速、主轴方向和单位完整                                          |
| profile登记thread cycle       | profile登记thread cycle | profile登记thread cycle | `CYCLE97/98`等               | `G76`或展开`G33`                          | translate/expand   | 牙型、进刀方式、退刀、复切、锥度和单位全部可证明                           |
| `M98 P_ L_ / M99`             | 对应call/return         | `M98 P_ L_ / M99`       | `L/CALL`静态调用             | Program Open内联展开或受控LinuxCNC O-call | expand             | 被调文件identity已冻结、无动态文件名、递归和展开上限明确；运行期找文件禁止 |

**扩展M代码**

M码不能按号码范围整体信任。通用语义直接转换；机床OEM/PLC M码通过精确profile映射成LinuxCNC原生M码或已登记native IO动作。只要映射的输入、输出、时序、ack和副作用完整，就应自动转换，不要求用户改程序。

| FANUC                 | 三菱                     | 新代               | 西门子                    | LinuxCNC目标                                 | 等效功能                 | 放行条件与拒绝边界                                                              |
| --------------------- | ------------------------ | ------------------ | ------------------------- | -------------------------------------------- | ------------------------ | ------------------------------------------------------------------------------- |
| `M30`                 | `M30`                    | `M30`              | `M30`                     | 默认`M2`；仅有托盘合同时`M30`                | 程序结束/复位/回卷       | 外部M30通常不得触发LinuxCNC托盘交换；source reset语义由artifact生命周期完成     |
| `T_`                  | `T_`                     | `T_`               | `T_`或`T="name"`          | `T<resolved>`                                | 选择刀具                 | tool identity、刀库slot、长度/半径和名称唯一映射                                |
| `M06/M6`              | `M06/M6`                 | `M06/M6`           | `M6`                      | `M6`                                         | 执行换刀                 | 目标toolchanger/native owner、safe position、spindle/IO、ack和失败合同完整      |
| `M19`及profile参数    | `M19`及profile参数       | `M19`及profile参数 | `SPOS/SPOSA`或profile命令 | `M19 R_ Q_ P_`                               | 主轴定向                 | 角度、方向、超时、spindle index和actual readback等效                            |
| `M48`或profile命令    | profile登记命令          | profile登记命令    | profile登记命令           | `M48`                                        | 允许进给/主轴倍率        | source必须同时控制相同override集合                                              |
| `M49`或profile命令    | profile登记命令          | profile登记命令    | profile登记命令           | `M49`                                        | 禁止进给/主轴倍率        | 与M48成对且程序结束恢复策略明确                                                 |
| `M98 P_ L_`           | `M98 P_ L_`或profile格式 | `M98 P_ L_`        | `L/CALL`静态调用          | 优先Program Open内联；必要时受控`O-call`     | 子程序调用               | 只允许冻结文件identity和有界次数；动态路径、递归失控或外部副作用拒绝            |
| `M99`                 | `M99`                    | `M99`              | `RET/M17`                 | 内联时消除；O-call时生成`o... return/endsub` | 子程序返回               | 必须与同一静态call graph配对；主程序M99语义不明拒绝                             |
| profile登记OEM M码    | profile登记OEM M码       | profile登记OEM M码 | profile登记PLC/aux命令    | `M62 P<signal>`                              | 与运动同步置位数字输出   | signal allowlist、极性、时序和native IO owner已登记；P0保留给RTCP               |
| profile登记OEM M码    | profile登记OEM M码       | profile登记OEM M码 | profile登记PLC/aux命令    | `M63 P<signal>`                              | 与运动同步复位数字输出   | 与M62相同                                                                       |
| profile登记OEM M码    | profile登记OEM M码       | profile登记OEM M码 | profile登记PLC/aux命令    | `M64 P<signal>`                              | 立即置位数字输出         | 仅非实时普通IO；P0只能由本节“RTCP开启”规则使用                                  |
| profile登记OEM M码    | profile登记OEM M码       | profile登记OEM M码 | profile登记PLC/aux命令    | `M65 P<signal>`                              | 立即复位数字输出         | 仅非实时普通IO；P0只能由本节“RTCP关闭”规则使用                                  |
| profile登记等待码     | profile登记等待码        | profile登记等待码  | profile登记wait命令       | `M66 P_/E_ L_ Q_`                            | 等待数字/模拟输入        | 输入allowlist、边沿/电平、timeout、返回值和取消合同完整                         |
| profile登记模拟输出码 | profile登记码            | profile登记码      | profile登记码             | `M67 E_ Q_`                                  | 与运动同步模拟输出       | channel、单位、范围、slew和safe default已登记                                   |
| profile登记模拟输出码 | profile登记码            | profile登记码      | profile登记码             | `M68 E_ Q_`                                  | 立即模拟输出             | 与M67相同且不进入伺服闭环                                                       |
| profile登记托盘M码    | profile登记托盘M码       | profile登记托盘M码 | profile登记托盘命令       | `M60`或已登记native action                   | 托盘交换并暂停           | 仅目标确有托盘机构、PLC sequence和fresh ack时允许；否则外部M30只转M2            |
| profile登记码         | profile登记码            | profile登记码      | profile登记码             | `M61 Q<resolved>`                            | 不执行换刀而设置当前刀号 | 仅恢复/特殊流程；tool owner actual和权限完整                                    |
| profile登记等效       | profile登记等效          | profile登记等效    | profile登记等效           | `M70/M71/M72/M73`                            | 保存、失效、恢复模态     | source scope和LinuxCNC modal集合完全一致；否则由translator内部保存/恢复，不输出 |
| 单个已登记M码         | 单个已登记M码            | 单个已登记M码      | 单个已登记M功能           | 精确LinuxCNC M码或native action              | OEM辅助功能              | 每个M码单独登记功能、参数、权限、IO、运动、安全、超时、ack和失败；禁止范围授权  |

已有 `M0/M1/M2/M3/M4/M5/M7/M8/M9` 继续由本节“基础代码”表直接等效。`M100-M199`仍不能从外部按号码直通；只有逐个绑定项目受信任入口的OEM辅助功能才能生成对应动作，并且不得生成shell或任意文件执行。

**文件包装**

parser可以接受但不得产生执行节点：

- 空行和普通空白；
- 成对 `%`；
- 只作主程序显示identity的十进制O号；
- 合法N行号；
- 普通源注释的惰性展示。

一旦O/N参与子程序、调用或跳转即拒绝。O-call、M98/M99、active comment、文件访问和日志命令不属于包装。

**整份程序放行条件**

1. 首个运动前明确G17、G20/G21、G90/G91、G94和一个已批准WCS；source依赖已登记默认WCS时，translator必须自动输出对应显式LinuxCNC WCS。
2. source profile明确I/J圆心语义；canonical显式建立目标圆心模式。
3. 基础规则只允许XYZ；RTCP程序只能通过本节“RTCP”规则使用active model登记的A/C或B/C。
4. RTCP程序必须冻结active model、geometry、tool/TLO、WCS和ruleset identity，运行时再由native owner fresh gate确认actual。
5. G31程序必须使用本节“G31探头探测”规则并绑定本机探头输入；任何结果引用也必须命中独立的结果读取规则。
6. 不存在未登记宏、子程序、坐标写入、刀补、fixed cycle、3+2、自定义IO或未批准探测/RTCP变体。
7. 每个source AST节点恰好命中一条白名单规则。
8. canonical重解析后只出现对应白名单规则声明的LinuxCNC节点、WCS选择、`M64/M65 P0`、`G38.3`和明确生成的TLO节点。
9. 任一条件失败时整份程序拒绝，不得部分运行。

<!-- AI_ONLY_BEGIN: rule_identity_map
以下映射仅供AI和实现工具把人类可读表格行绑定到稳定规则身份；不在人类阅读视图显示。
WL-V1-G17 :: FANUC=`G17` ; LinuxCNC=`G17` ; 功能=选择XY平面
WL-V1-G20 :: FANUC=`G20` ; LinuxCNC=`G20` ; 功能=选择inch单位
WL-V1-G21 :: FANUC=`G21` ; LinuxCNC=`G21` ; 功能=选择mm单位
WL-V1-G90 :: FANUC=`G90` ; LinuxCNC=`G90` ; 功能=绝对距离模式
WL-V1-G91 :: FANUC=`G91` ; LinuxCNC=`G91` ; 功能=增量距离模式
WL-V1-G94 :: FANUC=`G94` ; LinuxCNC=`G94` ; 功能=每分钟进给
WL-V1-G0 :: FANUC=`G00/G0` ; LinuxCNC=`G0` ; 功能=快速定位
WL-V1-G1 :: FANUC=`G01/G1` ; LinuxCNC=`G1` ; 功能=直线切削
WL-V1-G2 :: FANUC=`G02/G2` ; LinuxCNC=`G2` ; 功能=XY顺时针圆弧
WL-V1-G3 :: FANUC=`G03/G3` ; LinuxCNC=`G3` ; 功能=XY逆时针圆弧
WL-V1-F :: FANUC=`F_` ; LinuxCNC=`F_` ; 功能=G94模态进给
WL-V1-S :: FANUC=`S_` ; LinuxCNC=`S_` ; 功能=单主轴目标转速
WL-V1-M0 :: FANUC=`M00/M0` ; LinuxCNC=`M0` ; 功能=程序暂停
WL-V1-M1 :: FANUC=`M01/M1` ; LinuxCNC=`M1` ; 功能=可选暂停
WL-V1-M2 :: FANUC=`M02/M2` ; LinuxCNC=`M2` ; 功能=程序结束
WL-V1-M3 :: FANUC=`M03/M3`* ; LinuxCNC=`M3` ; 功能=主轴正转
WL-V1-M4 :: FANUC=`M04/M4`* ; LinuxCNC=`M4` ; 功能=主轴反转
WL-V1-M5 :: FANUC=`M05/M5`* ; LinuxCNC=`M5` ; 功能=主轴停止
WL-V1-M7 :: FANUC=`M07/M7`* ; LinuxCNC=`M7` ; 功能=雾冷开启
WL-V1-M8 :: FANUC=`M08/M8`* ; LinuxCNC=`M8` ; 功能=冷却液开启
WL-V1-M9 :: FANUC=`M09/M9`* ; LinuxCNC=`M9` ; 功能=冷却全部关闭
WL-V1-WORD-X/Y/Z :: FANUC=`X/Y/Z` ; LinuxCNC=`X/Y/Z` ; 功能=三个直线轴坐标
WL-V1-WORD-I/J :: FANUC=`I/J` ; LinuxCNC=`I/J` ; 功能=XY圆弧中心偏置
WL-V1-WORD-F :: FANUC=`F` ; LinuxCNC=`F` ; 功能=G94进给值
WL-V1-WORD-S :: FANUC=`S` ; LinuxCNC=`S` ; 功能=单主轴转速值
WL-V1-WORD-N :: FANUC=`N` ; LinuxCNC=可省略 ; 功能=source行号/line map identity
WL-WCS-MACHINE-G53 :: FANUC=`G53` ; LinuxCNC=`G53` ; 功能=本块使用机械坐标
WL-WCS-1 :: FANUC=`G54` ; LinuxCNC=`G54` ; 功能=选择第1加工坐标系
WL-WCS-2 :: FANUC=`G55` ; LinuxCNC=`G55` ; 功能=选择第2加工坐标系
WL-WCS-3 :: FANUC=`G56` ; LinuxCNC=`G56` ; 功能=选择第3加工坐标系
WL-WCS-4 :: FANUC=`G57` ; LinuxCNC=`G57` ; 功能=选择第4加工坐标系
WL-WCS-5 :: FANUC=`G58` ; LinuxCNC=`G58` ; 功能=选择第5加工坐标系
WL-WCS-6 :: FANUC=`G59` ; LinuxCNC=`G59` ; 功能=选择第6加工坐标系
WL-WCS-7 :: FANUC=`G54.1 P1` ; LinuxCNC=`G59.1` ; 功能=选择第7加工坐标系
WL-WCS-8 :: FANUC=`G54.1 P2` ; LinuxCNC=`G59.2` ; 功能=选择第8加工坐标系
WL-WCS-9 :: FANUC=`G54.1 P3` ; LinuxCNC=`G59.3` ; 功能=选择第9加工坐标系
WL-RTCP-ON :: FANUC=`G43.4 H_` ; LinuxCNC=`G43 H<resolved>`后`M64 P0` ; 功能=开启刀尖点控制
WL-RTCP-OFF :: FANUC=`G49` ; LinuxCNC=`M65 P0`，来源同时取消TLO时再输出`G49` ; 功能=关闭刀尖点控制
WL-RTCP-G0 :: FANUC=`G00/G0` ; LinuxCNC=`G0` ; 功能=RTCP开启状态下快速定位
WL-RTCP-G1 :: FANUC=`G01/G1` ; LinuxCNC=`G1` ; 功能=RTCP开启状态下协调直线进给
WL-RTCP-WORD-ROTARY :: FANUC=`A/B/C` ; LinuxCNC=active model的`A/C`或`B/C` ; 功能=RTCP姿态轴目标
WL-PROBE-G31 :: FANUC=`G31 X/Y/Z_ F_` ; LinuxCNC=`G38.3 X/Y/Z_ F_` ; 功能=朝工件直线探测；触发停止；未触发到终点不报警
WL-PROBE-RESULT :: FANUC=G31 skip位置/状态 ; LinuxCNC=`#5061-#5069`和`#5070` ; 功能=探测位置与成功状态
WL-G-DWELL :: FANUC=`G04 X_/P_` ; LinuxCNC=`G4 P<seconds>` ; 功能=translate
WL-G-PLANE-XZ :: FANUC=`G18` ; LinuxCNC=`G18` ; 功能=identity
WL-G-PLANE-YZ :: FANUC=`G19` ; LinuxCNC=`G19` ; 功能=identity
WL-G-ARC-CENTER-ABS :: FANUC=profile登记`G90.1` ; LinuxCNC=`G90.1` ; 功能=translate
WL-G-ARC-CENTER-INC :: FANUC=profile登记`G91.1` ; LinuxCNC=`G91.1` ; 功能=translate
WL-G-FEED-G93 :: FANUC=`G93` ; LinuxCNC=`G93` ; 功能=identity
WL-G-FEED-G95 :: FANUC=`G95` ; LinuxCNC=`G95` ; 功能=identity
WL-G-COMP-CANCEL :: FANUC=`G40` ; LinuxCNC=`G40` ; 功能=identity
WL-G-COMP-LEFT :: FANUC=`G41 D_` ; LinuxCNC=`G41 D<resolved>` ; 功能=translate
WL-G-COMP-RIGHT :: FANUC=`G42 D_` ; LinuxCNC=`G42 D<resolved>` ; 功能=translate
WL-G-TLO-ON :: FANUC=`G43 H_` ; LinuxCNC=`G43 H<resolved>` ; 功能=translate
WL-G-TLO-OFF :: FANUC=`G49` ; LinuxCNC=`G49` ; 功能=translate
WL-G-PATH-EXACT :: FANUC=profile登记exact-stop码 ; LinuxCNC=`G61`或`G61.1` ; 功能=translate
WL-G-PATH-CONTINUOUS :: FANUC=profile登记continuous码和容差 ; LinuxCNC=`G64 P_ Q_` ; 功能=translate
WL-G-CYCLE-CANCEL :: FANUC=`G80` ; LinuxCNC=`G80` ; 功能=translate
WL-G-CYCLE-DRILL :: FANUC=`G81` ; LinuxCNC=`G81`或展开`G0/G1` ; 功能=identity/expand
WL-G-CYCLE-DWELL :: FANUC=`G82` ; LinuxCNC=`G82`或展开`G0/G1/G4` ; 功能=translate/expand
WL-G-CYCLE-PECK :: FANUC=`G83` ; LinuxCNC=`G83`或展开基础运动 ; 功能=translate/expand
WL-G-CYCLE-TAP :: FANUC=profile刚性攻丝码 ; LinuxCNC=`G33.1`或已验证展开 ; 功能=translate
WL-G-CYCLE-BORE :: FANUC=`G85/G86/G89` ; LinuxCNC=对应LinuxCNC cycle或展开 ; 功能=translate/expand
WL-G-RETRACT-MODE :: FANUC=`G98/G99` ; LinuxCNC=`G98/G99` ; 功能=translate
WL-G-REFERENCE-RETURN :: FANUC=`G28/G30` ; LinuxCNC=展开为已登记中间点和`G53`目标 ; 功能=expand
WL-G-LOCAL-OFFSET :: FANUC=`G52` ; LinuxCNC=坐标预变换或登记native offset ; 功能=translate/expand
WL-G-G92 :: FANUC=`G92/G92.1`的精确profile ; LinuxCNC=项目登记`G92/G92.1/G92.2/G92.3`语义 ; 功能=translate
WL-G-ROTATE-2D :: FANUC=`G68/G69` ; LinuxCNC=Program Open静态旋转坐标后输出基础运动 ; 功能=expand
WL-G-SCALE :: FANUC=`G51/G50` ; LinuxCNC=Program Open静态缩放后输出基础运动 ; 功能=expand
WL-G-SPINDLE-SYNC :: FANUC=`G33`类profile ; LinuxCNC=`G33/G33.1` ; 功能=translate
WL-G-LATHE-DIAMETER :: FANUC=profile登记直径/半径模式 ; LinuxCNC=`G7/G8` ; 功能=translate
WL-G-CSS :: FANUC=`G96/G97` ; LinuxCNC=`G96/G97` ; 功能=identity/translate
WL-G-THREAD-CYCLE :: FANUC=profile登记thread cycle ; LinuxCNC=`G76`或展开`G33` ; 功能=translate/expand
WL-G-SUBPROGRAM-STATIC :: FANUC=`M98 P_ L_ / M99` ; LinuxCNC=Program Open内联展开或受控LinuxCNC O-call ; 功能=expand
WL-M-END-REWIND :: FANUC=`M30` ; LinuxCNC=默认`M2`；仅有托盘合同时`M30` ; 功能=程序结束/复位/回卷
WL-M-TOOL-SELECT :: FANUC=`T_` ; LinuxCNC=`T<resolved>` ; 功能=选择刀具
WL-M-TOOL-CHANGE :: FANUC=`M06/M6` ; LinuxCNC=`M6` ; 功能=执行换刀
WL-M-SPINDLE-ORIENT :: FANUC=`M19`及profile参数 ; LinuxCNC=`M19 R_ Q_ P_` ; 功能=主轴定向
WL-M-OVERRIDE-ENABLE :: FANUC=`M48`或profile命令 ; LinuxCNC=`M48` ; 功能=允许进给/主轴倍率
WL-M-OVERRIDE-DISABLE :: FANUC=`M49`或profile命令 ; LinuxCNC=`M49` ; 功能=禁止进给/主轴倍率
WL-M-SUBCALL :: FANUC=`M98 P_ L_` ; LinuxCNC=优先Program Open内联；必要时受控`O-call` ; 功能=子程序调用
WL-M-SUBRETURN :: FANUC=`M99` ; LinuxCNC=内联时消除；O-call时生成`o... return/endsub` ; 功能=子程序返回
WL-M-DIGITAL-SYNC-ON :: FANUC=profile登记OEM M码 ; LinuxCNC=`M62 P<signal>` ; 功能=与运动同步置位数字输出
WL-M-DIGITAL-SYNC-OFF :: FANUC=profile登记OEM M码 ; LinuxCNC=`M63 P<signal>` ; 功能=与运动同步复位数字输出
WL-M-DIGITAL-NOW-ON :: FANUC=profile登记OEM M码 ; LinuxCNC=`M64 P<signal>` ; 功能=立即置位数字输出
WL-M-DIGITAL-NOW-OFF :: FANUC=profile登记OEM M码 ; LinuxCNC=`M65 P<signal>` ; 功能=立即复位数字输出
WL-M-INPUT-WAIT :: FANUC=profile登记等待码 ; LinuxCNC=`M66 P_/E_ L_ Q_` ; 功能=等待数字/模拟输入
WL-M-ANALOG-SYNC :: FANUC=profile登记模拟输出码 ; LinuxCNC=`M67 E_ Q_` ; 功能=与运动同步模拟输出
WL-M-ANALOG-NOW :: FANUC=profile登记模拟输出码 ; LinuxCNC=`M68 E_ Q_` ; 功能=立即模拟输出
WL-M-PALLET :: FANUC=profile登记托盘M码 ; LinuxCNC=`M60`或已登记native action ; 功能=托盘交换并暂停
WL-M-SET-CURRENT-TOOL :: FANUC=profile登记码 ; LinuxCNC=`M61 Q<resolved>` ; 功能=不执行换刀而设置当前刀号
WL-M-MODAL-SAVE-RESTORE :: FANUC=profile登记等效 ; LinuxCNC=`M70/M71/M72/M73` ; 功能=保存、失效、恢复模态
WL-M-OEM-NATIVE :: FANUC=单个已登记M码 ; LinuxCNC=精确LinuxCNC M码或native action ; 功能=OEM辅助功能
AI_ONLY_END -->

## 7. 拒绝示例

本节非穷举；所有表外输入默认拒绝。

| 类别                  | 命令示例                                                  | 原因                                                                                 |
| --------------------- | --------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| 表外输入              | 统一白名单中没有对应条目的任意命令、地址字或结构          | 默认拒绝                                                                             |
| WCS写入/局部偏移      | G10、G52、G92及未登记扩展槽位                             | WCS选择已白名单化；写入、局部偏移和表外槽位仍需独立owner规则                         |
| 参考点未登记变体      | G27及无法证明中间点/目标的G28-G30                         | G53和可证明的G28/G30已白名单化；其它路径语义不明时拒绝                               |
| 刀补未登记变体        | G40-G44、H/D；RTCP专项rule除外                            | 只有RTCP ON/OFF声明的H解析和条件G49输出获准；其它刀补仍未闭合                        |
| fixed cycle未登记变体 | 参数或副作用无法匹配的G73-G89、G98/G99                    | 可等效cycle已进入统一白名单的“扩展G代码”表；只有无法证明参数/退刀/主轴语义的变体拒绝 |
| 路径模式未登记变体    | G09/G60及语义不明的G61/G64                                | 统一白名单的“扩展G代码”表只允许能证明exact/continuous和容差等效的profile             |
| 探测未登记变体        | G31.n、G31 P/L、多步skip、MEAW、下降沿、G38.2/G38.4/G38.5 | 只有统一白名单中G31的上升沿、未触发不报警语义获准                                    |
| 扩展坐标              | G54.1 Pn                                                  | 不能通用映射G59.1-G59.3                                                              |
| 坐标变换未登记变体    | 动态或含宏/探测/RTCP的G50/G51、G68/G69                    | 静态可证明部分按统一白名单的“扩展G代码”表展开；其余拒绝                              |
| 动态宏/子程序         | G65-G67、动态#、IF/GOTO/WHILE、运行期文件调用             | 静态M98/M99/CALL可有界内联；动态控制流和未知副作用拒绝                               |
| 自定义M/IO            | M10/M11、M31、M100-M199、OEM M码                          | OEM/PLC不通用；LinuxCNC M100-M199可执行文件                                          |
| 换刀/定向未登记变体   | 无tool/spindle/PLC映射的M6、M19、SPOS                     | 统一白名单的“扩展M代码”表允许owner、参数和fresh ack闭合的精确规则                    |
| RTCP未登记变体        | G43.5、G53.1、G68.2、TRAORI(n)、TRAFOON、CYCLE800         | 只有统一白名单中声明的精确ON/OFF和G0/G1姿态规则获准                                  |
| Siemens native        | TRANS/ROT/AROT/FRAME/CYCLE                                | 需要独立精确profile parser                                                           |
| 外部M30               | M30                                                       | LinuxCNC M30含托盘交换和模态复位                                                     |

## 8. 明确禁止的通用映射

不得建立以下品牌通用rule：

1. `G60 -> G61/G61.1/G64`；
2. `G31 -> 任意G38.x` 的通用映射；只允许统一白名单中的G31探测规则精确映射`G38.3`；
3. 未证明slot identity时把`G54.1 Pn/G54 Pn/G50x`映射为`G59.1-G59.3`；
4. 外部 `G44 -> LinuxCNC G44`；
5. 外部刚性攻丝 `G84 -> LinuxCNC G84`；
6. 外部 `G61.1/G64 -> LinuxCNC G61.1/G64`；
7. `G68/G69/G68.2/G53.1/G43.4/TRAORI`直接透传stock LinuxCNC；RTCP必须转换为统一白名单中的LinuxCNC目标；
8. 外部 `M30 -> LinuxCNC M30`；
9. FANUC-like H/D直接作为LinuxCNC tool number；
10. 外部M码范围整体映射 `M100-M199`；
11. 外部RTCP命令生成历史 `M128/M129`；
12. 仅凭拼写相同认定语义相同。

以后只能按“精确profile + 单rule + typed transform + target owner + 正反golden + 必要板端证据”逐项开放。

<!-- AI_ONLY_BEGIN: implementation_and_verification
## 9. 系统宏、remap和受信任入口

不得按目录、文件名前缀或M码范围授权。每个入口必须独立登记：

- entrypoint id/version；
- 文件/二进制hash或签名；
- 允许调用来源；
- 参数类型、单位、范围和默认；
- 完整调用图；
- 变量域和持久化范围；
- 允许运动/IO/坐标和owner；
- 递归、循环、指令数和时间上限；
- 失败、取消、超时和fresh readback；
- ruleset、target identity和证据hash。

外部 `M100-M199`、O-call、文件调用和active comment默认拒绝。外部程序不能伪造“系统宏来源”。

## 10. 输出AST白名单

canonical重新解析后，`G54-G59.3`只允许来自`WL-WCS-*`，`M64 P0/M65 P0`只允许来自`WL-RTCP-*`，`G38.3`只允许来自`WL-PROBE-G31`。除此之外默认禁止：

- 未登记O-call、外部子程序和文件搜索；
- `M100-M199` 和未登记可执行M码；
- active comment和文件/日志副作用；
- 未登记remap、系统宏、shell、HAL/INI写入；
- 原始#表达式、动态文件名或拼接token；
- 历史 `M128/M129`；
- 来源rule未声明的模态或持久副作用。

目标parser接受只证明语法成立，不能替代来源语义等价和板端证据。

## 11. Rule成熟度和晋级

### 11.1 成熟度

| maturity              | 证据                                        |
| —                     | —                                           |
| `specified`           | 手册和语义登记                              |
| `parser_verified`     | 正例、反例、边界和歧义解析                  |
| `translator_verified` | IR、emitter、line map和输出白名单           |
| `linuxcnc_verified`   | 精确目标版本重解析和副作用检查              |
| `board_verified`      | 硬件/运动规则完成operator和fresh actual闭环 |

成熟度不自动改变policy。只有显式批准才能从deny提升为allow。

### 11.2 每条allow rule最低测试

- 手册正例和边界；
- 同码异义profile反例；
- 缺字段、重复字段、非法组合和越界；
- 大小写、空白、前导零和注释注入；
- 小数、尾点、单位和精度；
- 模态前后和跨块状态；
- 零命中、多命中和规则冲突；
- target AST重解析；
- active comment、O-call、M100-M199和外部文件反例；
- source/canonical/error双向line map；
- profile/ruleset/target identity失效；
- 硬件/运动规则的板端闭环。

## 12. 白名单变更流程

每次扩大白名单只允许一个最小rule/profile切片：

1. 登记官方手册和精确profile identity；
2. 定义typed source pattern、allowed words和语义；
3. 定义canonical emitter和输出AST范围；
4. 补齐正例、反例、边界和歧义golden；
5. 通过parser、translator和LinuxCNC focused验证；
6. 涉及硬件/运动时完成板端operator闭环；
7. 更新ruleset version/hash；
8. 旧artifact自动STALE；
9. 同步本文唯一rule表；
10. 删除被新rule替代的旧fallback或shadow实现。

不得为了增加一个rule运行无关全量Linux/PetaLinux构建。
AI_ONLY_END -->

## 13. 人能看懂的报错提示表

报错的目的不是显示内部错误码，而是让操作员马上知道“哪里错、为什么错、系统有没有执行、应该怎么改”。禁止只显示“G代码错误”“格式错误”“打开失败”、内部英文异常或一串错误码。

### 13.1 弹窗固定格式

有准确代码位置时，弹窗必须按下面的口径显示：

> **{弹窗标题}**<br>
> 第{line}行，第{column}列，代码“{token}”：{原因与修改方法}<br>
> 系统未将该程序或MDI代码送入微内核执行。共发现{error_count}个问题，当前显示第{error_index}个。

文件级问题没有准确行列时，不得伪造第0行；弹窗直接说明整个程序存在什么问题以及需要选择或更新什么。

### 13.2 固定中文提示

下表中的标题和原因是必须使用的固定口径。实际弹窗把 `{token}`、`{line}`、`{column}`、`{minimum}`、`{maximum}`、`{unit}`、`{capability}` 等占位符替换为本次真实值，不得把占位符原样显示给用户。

| 出现的问题                 | 弹窗标题                 | 人能看懂的原因与修改方法                                                                                                                      | 系统已经做的保护                                               |
| -------------------------- | ------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------- |
| 无法识别程序来源           | 无法确定控制器类型       | 无法确定该程序来自哪种控制器、哪个版本或哪种机型。不同系统中相同代码可能含义不同，请选择准确的FANUC、三菱、新代、西门子或LinuxCNC型号与版本。 | 未猜测来源，未修改原程序，未生成可执行代码。                   |
| 同时匹配多个控制器类型     | 控制器类型不明确         | 该程序同时符合多个控制器类型，但其中至少一处代码含义不同。请根据原机床选择准确型号与版本。                                                    | 已列出候选类型和第一处差异，未替用户作选择。                   |
| 已选控制器与程序不符       | 控制器设置不匹配         | 当前选择的控制器版本、机型或选件与程序要求不一致。请选择匹配设置，或由维护人员更新受控配置。                                                  | 已标出不一致项目，未按错误设置继续翻译。                       |
| G代码行无法解析            | 本行代码格式有误         | 第{line}行代码“{token}”写法不完整、顺序不正确或含有无法识别的字符。请按所选控制器的格式修改本行。                                             | 已自动处理大小写、空格和允许的前导零；仍无法解析，因此未执行。 |
| 程序起止符不成对           | 程序起止符不完整         | 程序开头或结尾的“%”没有成对出现，或者出现在不允许的位置。请补齐成对的“%”或删除多余符号。                                                      | 未把错误的“%”当作普通字符忽略。                                |
| 注释没有结束               | 注释没有闭合             | 第{line}行开始的注释缺少结束符，后续代码可能被错误地当成注释。请补上结束符后重新打开程序。                                                    | 已在损坏位置停止分析，未猜测后续代码。                         |
| 注释会产生运行副作用       | 该注释不能执行           | 代码“{token}”不是普通说明文字，它可能写文件、输出日志或触发运行功能。请删除该功能，或改成普通注释。                                           | 保留原文，但未把该注释写入可执行程序。                         |
| 数值写法非法               | 数值格式有误             | 第{line}行代码“{token}”后面的数值含有非法字符，或不是可执行的有限数字。请输入明确的数字。                                                     | 未使用0、默认值或其它数字代替。                                |
| 数值存在两种解释           | 数值含义不明确           | 数值“{token}”在当前控制器设置下可能表示不同大小或单位。请选择准确控制器类型，或把数值写成带明确小数点的形式。                                 | 已显示可能的解释，但未任选一种继续。                           |
| 数值超出范围               | 数值超出允许范围         | 第{line}行代码“{token}”超出允许范围，应在{minimum}到{maximum}{unit}之间。请修改为机床允许的值。                                               | 未截断、取整或自动改成边界值。                                 |
| 同一地址字重复             | 本行参数重复             | 第{line}行重复写了“{token}”，系统无法确定应采用哪个值。请删除重复项，只保留一个明确值。                                                       | 未擅自采用第一个或最后一个值。                                 |
| 同一行出现互斥命令         | 本行命令互相冲突         | 第{line}行同时出现了不能一起使用的命令“{token}”。请只保留符合加工目的的一项。                                                                 | 已列出冲突命令，未自动删除任何一项。                           |
| 运动前缺少必要模式         | 运动条件没有说明完整     | 执行第{line}行运动前，单位、平面、绝对/增量或进给方式没有明确。请在运动前补上提示中列出的G代码。                                              | 只有来源设置能唯一确定时才自动补齐；存在歧义时不会猜测。       |
| 无法识别G码或M码           | 无法识别该命令           | 第{line}行代码“{token}”无法识别，可能是拼写错误、控制器类型选错或尚未登记的厂家命令。请先核对原程序和控制器型号。                             | 未透传、未删除，也未把未知命令当作无效行跳过。                 |
| 命令尚未获准执行           | 当前版本不支持执行该命令 | 系统认识第{line}行代码“{token}”，但当前版本还没有完成等效翻译和验证。请改用提示中的已支持写法，或等待该功能完成验证。                         | 已保留原代码和识别到的功能，未生成不确定的LinuxCNC代码。       |
| 当前命令不允许这个参数     | 该参数不能用于当前命令   | 第{line}行命令“{token}”带有不允许的参数。请删除该参数，或按提示改用支持这种参数的命令。                                                       | 已显示当前命令允许使用的参数，没有静默丢弃多余参数。           |
| 当前命令缺少必要参数       | 本行缺少必要参数         | 第{line}行命令“{token}”缺少完成动作所需的参数。请补上提示中列出的参数和明确数值。                                                             | 未使用历史值、0或默认值代替缺失参数。                          |
| 程序使用当前机型没有的轴   | 当前机型不支持该轴       | 第{line}行使用了轴“{token}”，但当前机型或运动模型没有登记这个轴。请选择正确机型，或删除错误轴命令。                                           | 未把该轴静默丢弃，也未把它映射到其它轴。                       |
| 同一代码有多种翻译方法     | 无法确定正确翻译方式     | 第{line}行代码“{token}”同时符合多种不同翻译方法，系统不能证明哪一种符合原加工意图。请选择准确控制器设置，或改成含义唯一的写法。               | 已列出冲突原因，未任选一种翻译。                               |
| 命令在白名单但条件不满足   | 该命令当前不能执行       | 第{line}行代码“{token}”属于支持范围，但参数、前置状态或使用位置不符合要求。请按提示补齐条件或修改本行。                                       | 已显示具体不满足的条件，未绕过条件继续。                       |
| 圆弧几何不成立             | 圆弧参数有误             | 第{line}行圆弧的终点、圆心或半径关系不成立。请检查X、Y、I、J以及当前平面和圆心模式。                                                          | 已计算并显示几何差值，未把圆弧自动改成另一种刀路。             |
| 螺旋圆弧链不连续           | 弹簧轨迹不连续           | 第{line}行圆弧与上一段的端点、切线、半径、螺距或姿态不连续。请检查本行X、Y、Z、I、J和A/C参数，确保五圈弹簧保持为同一连续加工段。               | 已指出首个断点和几何差值；未插入G1补线、停顿或快速移动掩盖断点。 |
| 螺旋圆弧能力未登记         | 当前机床不能运行此螺旋圆弧 | 第{line}行需要同步XYZ螺旋圆弧和RTCP姿态轴，但当前控制器profile或机床模型没有登记该能力。请选择匹配的控制器和机型，或联系维护人员补齐能力配置。 | 未把螺旋圆弧拆成G1小线段，也未忽略姿态轴后继续运行。           |
| 命令放错位置               | 命令使用位置不正确       | 第{line}行代码“{token}”不能在当前程序位置、MDI或子程序层级使用。请把它移到提示所允许的位置。                                                  | 未自动移动代码，也未改变程序执行顺序。                         |
| 子程序无法安全展开         | 子程序暂时不能执行       | 第{line}行调用的子程序无法找到固定版本、存在递归，或展开次数超过限制。请把子程序内容展开到主程序，或修正固定调用关系。                        | 未在运行时查找其它文件，也未执行不确定的子程序。               |
| 命令会调用未登记外部功能   | 外部功能未获准           | 第{line}行代码“{token}”会调用未登记的文件、脚本、厂家M码或外部设备功能。请改用已经登记的系统功能。                                            | 已阻止文件、脚本和外部动作执行。                               |
| 无法证明翻译前后含义一致   | 无法保证加工结果一致     | 系统认识第{line}行代码“{token}”，但无法保证翻译成LinuxCNC后刀路、坐标或副作用保持一致。请改用已验证的等效写法。                               | 已保留原文和可能解释，未生成可能改变加工目的的代码。           |
| 当前机床缺少所需能力       | 当前机床缺少所需功能     | 程序需要{capability}，但当前机床没有登记、没有启用或当前不可用。请选择匹配机床，或先完成该功能配置。                                          | 已识别缺少的能力，未用其它轴、IO或功能代替。                   |
| 翻译结果未通过LinuxCNC检查 | 程序翻译结果检查失败     | 原程序已经识别，但系统生成的LinuxCNC代码没有通过目标解释器检查。这属于翻译规则问题，请保留原程序并联系维护人员。                              | 未提交翻译结果，原程序未被改写，微内核待执行代码保持为空。     |
| 翻译结果出现未批准动作     | 翻译结果包含未批准动作   | 翻译后的程序出现了原规则没有允许的命令或副作用。请保留原程序并联系维护人员检查翻译规则。                                                      | 已阻止整个翻译结果进入执行链，不能由用户确认后绕过。           |
| 已翻译程序过期             | 程序需要重新翻译         | 原程序、控制器设置、白名单或机床配置已经变化，之前生成的执行程序不再有效。请重新打开程序，让系统重新翻译和检查。                              | 已撤销“可运行”状态并清空旧的待执行代码。                       |

### 13.3 用户点击“确定”后的固定动作

1. 有准确源代码位置的错误：自动打开手动输入/程序编辑页面，载入**原始G代码**，跳到对应行和列，并高亮错误代码；不能打开翻译后的缓存文件冒充原文。
2. 文件级错误没有准确行列：打开同一编辑页面并定位到程序顶部，同时显示需要选择的控制器类型、版本、机型或缺失能力；不得显示虚假的第0行。
3. 多个错误一次汇总，弹窗显示首个确定错误和“共N个问题”；用户选择列表中的任一问题时，编辑页面同步跳到该问题的位置。
4. “确定”只表示进入修改页面，不表示忽略错误、加入白名单或继续执行。修改后必须重新提交、重新翻译并重新检查。
5. 进入错误编辑页面前，程序运行缓存、MDI待执行缓存和微内核/Native待执行代码必须全部为空；UI不能显示一份代码而执行链残留另一份代码。
6. 系统自身翻译错误仍可打开原程序并定位来源行，但修改建议必须明确写“联系维护人员”，不得诱导用户随意改动本来正确的程序。

<!-- AI_ONLY_BEGIN: error_identity_map
以下稳定错误身份只供AI、实现和测试使用；人类弹窗必须使用上表中文标题与原因，不能用错误身份代替解释。
无法识别程序来源 :: GCODE_PROFILE_NOT_IDENTIFIED
同时匹配多个控制器类型 :: GCODE_PROFILE_AMBIGUOUS
已选控制器与程序不符 :: GCODE_PROFILE_MISMATCH
G代码行无法解析 :: GCODE_FORMAT_INVALID
程序起止符不成对 :: GCODE_PERCENT_UNPAIRED
注释没有结束 :: GCODE_COMMENT_UNCLOSED
注释会产生运行副作用 :: GCODE_ACTIVE_COMMENT_DENIED
数值写法非法 :: GCODE_NUMBER_INVALID
数值存在两种解释 :: GCODE_NUMBER_AMBIGUOUS
数值超出范围 :: GCODE_VALUE_OUT_OF_RANGE
同一地址字重复 :: GCODE_DUPLICATE_WORD
同一行出现互斥命令 :: GCODE_MODAL_CONFLICT
运动前缺少必要模式 :: GCODE_MODAL_REQUIRED
无法识别G码或M码 :: GCODE_COMMAND_UNKNOWN
命令尚未获准执行 :: GCODE_COMMAND_NOT_ALLOWLISTED
当前命令不允许这个参数 :: GCODE_WORD_NOT_ALLOWED
当前命令缺少必要参数 :: GCODE_WORD_REQUIRED
程序使用当前机型没有的轴 :: GCODE_AXIS_NOT_SUPPORTED
同一代码有多种翻译方法 :: GCODE_RULE_AMBIGUOUS
命令在白名单但条件不满足 :: GCODE_RULE_CONDITION_FAILED
圆弧几何不成立 :: GCODE_ARC_INVALID
螺旋圆弧链不连续 :: GCODE_HELIX_CHAIN_DISCONTINUOUS
螺旋圆弧能力未登记 :: GCODE_HELIX_CAPABILITY_MISSING
命令放错位置 :: GCODE_CONTEXT_INVALID
子程序无法安全展开 :: GCODE_SUBPROGRAM_DENIED
命令会调用未登记外部功能 :: GCODE_EXTERNAL_EFFECT_DENIED
无法证明翻译前后含义一致 :: GCODE_SEMANTIC_UNPROVEN
当前机床缺少所需能力 :: GCODE_TARGET_CAPABILITY_MISSING
翻译结果未通过LinuxCNC检查 :: GCODE_OUTPUT_REPARSE_FAILED
翻译结果出现未批准动作 :: GCODE_OUTPUT_NODE_DENIED
已翻译程序过期 :: GCODE_ARTIFACT_STALE
AI_ONLY_END -->

任何错误都不得降级为忽略后继续运行。
