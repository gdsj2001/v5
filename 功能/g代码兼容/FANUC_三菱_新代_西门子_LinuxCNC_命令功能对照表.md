# G 代码兼容翻译与系统命令功能对照方案（讨论稿）

更新时间：2026-07-15

当前状态：`方案讨论稿`。本轮只在 `功能/g代码兼容` 目录内收敛方案，没有修改其它需求 owner、源码、构建配置或板端行为。本方案在完成跨文档合并前不代表项目运行合同已经生效；与现有 owner 冲突时，现有 owner 仍优先。

本目录的执行白名单唯一规范为：[G代码执行白名单](./G代码执行白名单.md)。本文只维护兼容架构、翻译链、跨系统对照、错误映射和程序级合同，不再复制命令、地址字、rule_id或放行条件表。

## 0. 本轮收敛的方案方向（待确认）

1. 底层只运行项目认可的 LinuxCNC canonical 语义，不让微内核、UI 或 bridge 同时解释 FANUC、三菱、新代和西门子多套方言。
2. 外部程序必须绑定精确来源 profile；系统应优先依据已登记客户/机床/program metadata和唯一规则自动绑定，只有无法唯一确定时才要求用户选择。
3. 采用默认拒绝白名单。未知、歧义、未验证、超出参数范围或缺少目标能力的任一语句，都会使整份程序禁止运行。
4. 翻译不是字符串替换，而是“来源语法解析 → typed semantic IR → 目标能力校验 → LinuxCNC canonical 生成 → 目标侧重新解析和输出白名单复核”。
5. 原程序永久保留；建议在 Program Open 阶段生成不可编辑、可复现、身份冻结的 canonical artifact。Start 只使用已经冻结的 artifact，不在启动热路径重新识别或翻译。
6. 任何转换都必须保留完整数值精度、模态语义、源行映射和来源证据；禁止删掉不认识的内容后继续运行。
7. 品牌对照表只供人工理解，不是执行白名单。执行资格只能来自单条、精确、版本化的规则。
8. 第一阶段只开放低歧义的三轴铣削子集；宏、子程序、探测、刀补、固定循环、坐标写入、RTCP、3+2、西门子原生语法和自定义 M 码以后逐项证明，不能按代码段或品牌整包放行。
9. 兼容性目标是“能无歧义自动处理的全部自动处理”。大小写、空白、前导零、换行、成对包装、已登记同义写法和确定性语义转换不得反复询问用户；只有可能改变加工目的或无法唯一判断时才阻断并报错。

## 1. 目标、边界和非目标

### 1.1 目标

操作员可以保留原控制器的常用编程习惯和已有程序资产。系统在运行前把已证明等价的外部方言转换为 LinuxCNC canonical 程序，最终仍由 LinuxCNC interpreter、motion、HAL、kinematics 和 native owner 执行。

本兼容层只负责：

- 自动匹配并绑定唯一来源profile，或在无法唯一匹配时使用受控选择结果；
- 词法和语法解析；
- 构建带模态状态的 typed semantic IR；
- 执行静态白名单、语义和目标能力校验；
- 生成 LinuxCNC canonical 程序；
- 建立双向 source map；
- 输出结构化拒绝原因和证据身份。

### 1.2 不属于本方案的 owner

本方案不拥有：

- 插补、轨迹、前瞻、小线段融合或降点；
- 急停、使能、软限位、回零、stillness 和实时安全；
- WCS、G92、刀具表、TLO、RTCP、active model 的 actual；
- Program Open、Start、Resume 的最终 UI 行为；
- 错误弹窗布局、程序预览渲染和大文件 UI 策略；
- LinuxCNC/HAL/native owner 已有的运行时状态裁决。

兼容层不得为了“支持更多代码”自建第二套运动、坐标、刀补、探测或 RTCP 算法。

### 1.3 需要后续合并裁决的冲突

本方案建议执行“从原程序确定性生成的 canonical artifact”，而现有项目文档仍可能规定 LinuxCNC 直接打开原始文件。两者必须在整体方案确认后由相应 owner 统一裁决；本轮不修改那些文档，也不实施代码。

canonical artifact 只有在同时满足以下条件时才不构成第二源码真源：

- 原文件和 `source_sha256` 永久保留；
- artifact 不允许人工编辑；
- artifact 可由固定的 source/profile/ruleset/target identity 完全复现；
- artifact 只在原子生成完成并通过全部校验后取得执行资格；
- 任一输入 identity 改变都会使旧 artifact 失效；
- 运行报警能准确反查原文件行、列和 token。

## 2. 总体架构

```text
操作员原程序
  + 自动唯一匹配或受控选择的 source_profile_id
  + 当前 machine_profile_id
             |
             v
来源 lexer/parser（整份程序）
             |
             v
来源 AST + 完整模态状态 + source_span
             |
             v
来源规则白名单（必须恰好命中一条）
             |
             v
Typed Semantic IR（不含原始字符串模板）
             |
             v
目标能力和静态 owner 合同校验
             |
             v
LinuxCNC canonical AST / emitter
             |
             v
目标 LinuxCNC 精确版本重新解析
             |
             v
输出 AST 白名单 + side-effect 检查
             |
             v
原子冻结 canonical artifact + line map + identity
             |
             v
Program Open 标记 ready_to_run
             |
             v
Start/Resume -> LinuxCNC -> native/microkernel actual
```

### 2.1 为什么需要 typed semantic IR

相同代码在不同控制器、机型和参数下可能含义不同。只做正则替换无法处理：

- 模态前置和后置状态；
- 车床与铣床同码不同义；
- 圆弧中心绝对/增量、长短弧和多圈规则；
- 无小数点数值的最小输入单位；
- G0 是直线快速还是各轴独立快速；
- H/D 是独立补偿寄存器还是 LinuxCNC tool number；
- G31 的触发边沿、未触发行为和连续高速 skip；
- 子程序、宏变量和控制流；
- 生成目标中的 active comment、外部文件调用或可执行 M 码副作用。

因此规则先生成明确的语义操作和 typed operands，再由 emitter 输出目标程序。禁止用自由字符串模板拼接用户 token。

### 2.2 非实时边界

方言识别、整程序解析、转换和目标重解析都在 Program Open/import 阶段完成，不进入 CPU0 或运动实时周期。微内核只接收项目已登记的 LinuxCNC/native 请求和 actual，不感知外部品牌方言。

## 3. 精确 profile 与身份

### 3.1 Source profile

每个可执行来源必须有唯一 `source_profile_id`，至少绑定：

| 字段 | 含义 |
| --- | --- |
| `controller_family` | FANUC、Mitsubishi、Syntec、Siemens ISO、Siemens native、LinuxCNC |
| `controller_model` / `series` | 精确型号和系列，不能只写品牌 |
| `software_version` / `firmware` | 解释器版本或控制器软件版本 |
| `machine_class` | mill、turn、mill-turn 等 |
| `axis_schema` | 允许的线性轴、旋转轴和直径轴规则 |
| `option_set` | 宏、五轴、探测、高速加工、循环等已安装选件 |
| `parameter_profile_hash` | 会改变语义的参数集合 identity |
| `oem_plc_mcode_profile` | 机床厂 PLC/PMC、自定义 M 码映射版本 |
| `decimal_mode` | 无小数点和尾点数值的解码规则 |
| `unit_mode_contract` | 默认单位和显式单位要求 |
| `arc_mode` | IJK 绝对/增量、R 长短弧、全圆、多圈规则 |
| `rapid_path_mode` | G0 直线插补或各轴独立快速 |
| `tool_offset_schema` | T/H/D 的编号、寄存器和值映射 |
| `manual_identity` | 手册名称、版本、页码和文件 hash |

### 3.2 Target machine profile

`machine_profile_id` 至少绑定：

- LinuxCNC 精确版本/build identity；
- 当前机型、active model、轴与运动学；
- INI/HAL/remap registry identity；
- 本机 WCS、刀具表、TLO、主轴、冷却、探测、换刀和 IO 能力；
- 当前兼容 `ruleset_id/version/hash`；
- 允许的系统宏/remap manifest identity。

运行时动态安全仍由 native owner fresh actual 裁决。翻译器只能声明“目标具备此能力”，不能用缓存状态冒充实时放行。

### 3.3 Profile 选择规则

1. Program Open 先按已登记的客户/机床默认profile、程序metadata、导入来源、控制器标识和规则集自动匹配。
2. 当且仅当匹配结果唯一，且精确到型号、版本、机型、选件和参数hash时，系统自动绑定该profile，不弹确认框。
3. 自动语法特征只能用于排除不匹配项，不能把“像 FANUC”当成精确身份；文件扩展名、注释、O号和少量公共ISO代码不能单独授权。
4. 没有唯一结果时才要求用户选择；候选列表必须说明各profile差异和造成歧义的源行，不能只显示品牌名。
5. 用户选择也不能绕过版本、参数hash和白名单；仍不匹配时程序只能查看并报错。
6. 运行中不得切换profile；自动或人工切换都必须重新打开、重新翻译并形成新identity。

## 4. 执行白名单唯一入口

所有执行策略、三层白名单、rule schema、V1命令、地址字、文件包装、拒绝条件、受信任系统入口和规则晋级要求，统一由 [G代码执行白名单](./G代码执行白名单.md) 裁决。

本文中的跨品牌命令说明和功能对照只能帮助识别来源语义，不能直接授予执行资格。translator必须按精确source profile解析为typed AST，再让每个节点恰好命中独立白名单中的一个有效rule；零命中或多命中都拒绝整份程序。

## 5. 自动兼容与规则消费

1. 系统优先自动完成格式归一、唯一profile匹配、确定性转换、目标能力校验、canonical生成、目标重解析和输出白名单。
2. 能够唯一证明等价的差异自动处理，不要求用户介入。
3. 单个白名单rule只能消费其声明的source profile、参数、上下文和target capability，不能按品牌或代码范围模糊放行。
4. 静态白名单只决定“语义能否进入可执行artifact”，运行时急停、使能、模式、stillness、轴映射、IO和安全条件仍由各自native owner fresh gate裁决。
5. 不得存在未知透传、警告后继续、删除不支持行、用户确认绕过或翻译失败后raw source fallback。
## 6. Program Open、artifact 与 MDI 合同建议

### 6.1 Program Open

建议流程：

1. 只读打开原程序并计算 `source_sha256`。
2. 自动完成无语义格式归一并记录每项变换。
3. 自动匹配唯一source profile；只有零匹配或多匹配时才要求用户处理。
4. 固定source profile、machine profile、ruleset和target interpreter identity。
5. 整份解析并构建source AST、模态状态和调用图；在可安全恢复解析边界内一次收集全部确定错误。
6. 自动完成三层白名单、确定性翻译和所有静态校验。
7. 生成canonical artifact、双向line map和translation report。
8. 用目标LinuxCNC版本重新解析artifact并检查输出白名单。
9. 对source、artifact、line map、report和全部identity做原子提交。
10. 只有原子提交完成后才返回 `ready_to_run=true`。

翻译、预览或重新解析仍在后台进行时，不得先显示“可运行”。

成功路径不弹逐行确认。若存在多个错误，界面应一次显示可确定的错误总数和列表，避免用户每修一行再重新遇到下一行；解析结构已经破坏、无法可靠定位后续块时才停止继续收集。

### 6.2 Artifact identity

建议冻结：

- `source_path/source_sha256`；
- `source_profile_id/hash`；
- `machine_profile_id/hash`；
- `ruleset_id/version/hash`；
- `target_linuxcnc_identity`；
- `canonical_artifact_path/sha256`；
- `line_map_sha256`；
- `translation_report_sha256`；
- `program_open_generation`。

Start/Resume 不重新读原文件、不重新翻译、不修改 canonical artifact。任何 identity 改变都要求重新 Program Open。

### 6.3 MDI

MDI 也必须经过同一个 profile、parser、typed IR 和三层白名单。它可以形成单块、短生命周期的 canonical command identity，但不得绕过规则直接向 LinuxCNC、Command Gate 或 native owner发送外部方言文本。

## 7. 首版白名单范围

首版可执行命令、地址字、文件包装、程序级条件和拒绝示例全部见 [G代码执行白名单](./G代码执行白名单.md)。本文不再复制这些清单。

首版仍以低歧义三轴铣削为安全起点；任何新增命令必须先在独立白名单中取得精确rule_id、profile、参数约束、目标能力、正反测试和失效条件，随后才能由translator消费。跨系统对照表中出现但未进入独立白名单的功能只能识别和报错，不能执行。
## 8. 非规范性跨系统功能对照

本章只帮助人工理解，不能决定执行。控制器型号、软件、参数、选件和 OEM PLC 可能改变任一行。

| 功能 | FANUC 常见 | 三菱常见 | 新代/Syntec 常见 | Siemens 常见 | LinuxCNC 目标注意 |
| --- | --- | --- | --- | --- | --- |
| 快速/直线 | `G0/G1` | `G0/G1`，G0路径可参数化 | `G0/G1` | ISO `G0/G1` | 同码不保证 G0 路径相同 |
| 圆弧 | `G2/G3 IJK/R` | `G2/G3 IJK/R` | `G2/G3 IJK/R` | ISO G2/G3及原生变体 | 必须固定平面、圆心和R规则 |
| 单位 | `G20/G21` | `G20/G21` | 常见 `G20/G21` | ISO 模式相关 | canonical preamble须明确 |
| 绝对/增量 | `G90/G91` | `G90/G91` | `G90/G91` | ISO G90/G91，原生有AC/IC | 只能按精确profile |
| 准确停止 | `G09` 等 | `G61`/高精模式依系列 | 依系列 | ISO与原生模式不同 | `G61.1/G64` 不能跨品牌直通 |
| 单向定位 | `G60` 依系列 | `G60` 常见 | 依系列 | 无统一同义 | 不得映射为 LinuxCNC G61 |
| 工件坐标 | `G54-G59/G54.1` | 系列相关 | FANUC-like或扩展bank | frame/TRANS体系 | 必须绑定本机owner和值 |
| 刀补 | `G41/42 D`、`G43 H` | 系列相关 | FANUC-like常见 | 常用D刀沿/原生体系 | H/D含义不保证等于tool number |
| 固定循环 | `G73-G89` | 同码但参数/机型差异 | FANUC-like常见 | ISO或CYCLE | 逐条profile，不按代码段放行 |
| 探测/skip | `G31/G31.n` | G31系列 | G31或测量宏 | MEAS/测量cycle | 不得通用映射G38.x |
| 宏 | Custom Macro B | 宏体系依选件 | #、@、R及控制结构依版本 | R参数/命名变量/程序结构 | 首版拒绝，未来逐入口manifest |
| RTCP/TCP | `G43.4` 依选件 | 五轴选件 | 五轴选件 | `TRAORI` | 只通过项目当前RTCP owner |
| 3+2/倾斜面 | `G68.2/G53.1` 依选件 | 系列相关 | 系列相关 | `CYCLE800`/frame | 无通用一对一映射 |
| 程序结束 | `M2/M30` | `M2/M30` | `M2/M30` | M2/M30或原生流程 | LinuxCNC M30托盘行为须单独处理 |
| 自定义IO | PMC M码 | PLC M码 | PLC/厂商M码 | PLC/NC同步动作 | 只允许精确登记的项目入口 |

## 9. 禁止映射与受信任系统入口

禁止的通用映射、宏/subprogram/remap边界、受信任系统入口要求及输出AST拒绝项，统一见 [G代码执行白名单](./G代码执行白名单.md)。

兼容层不得把“常见写法”当作语义证明，也不得把FANUC Macro B、三菱宏、新代控制语法、西门子变量/循环、LinuxCNC O-code或项目自定义M码互相猜测转换。只有精确profile和白名单rule能够放行；受信任入口还必须绑定固定身份、参数schema、权限、owner、readback和失败合同。
## 11. 自动处理边界、错误映射与 readback

### 11.1 能自动处理的格式差异

下列差异在精确profile和白名单允许时自动处理，不弹错误、不要求确认，但必须写入translation report：

| 输入差异 | 自动处理 |
| --- | --- |
| 大小写 | `g01 x1` 归一为 `G1 X1` |
| G/M前导零 | `G00/M03` 归一为 `G0/M3` |
| 多余空格、Tab、CRLF/LF | 归一为空格和canonical换行 |
| 合法N行号 | 保留source map，可不输出到canonical |
| 成对 `%` | 按文件包装处理，不进入运动语义 |
| 已登记普通注释 | 保留在source map或转成惰性展示文本 |
| profile已明确的无小数点/尾点规则 | 按精确profile无损解码并记录原值与canonical值 |
| 唯一命中的 `allow_translate` | 自动生成typed IR和canonical输出 |

以下情况不能自动猜测：单位不明、profile不唯一、数值可能有两种解释、同码在铣/车或不同选件中语义不同、未知G/M码、未批准地址字、宏副作用、坐标/刀补/RTCP语义不确定。它们必须进入错误映射并使 `ready_to_run=false`。

### 11.2 结构化错误合同

兼容层只输出结构化结果，具体弹窗和UI归相应UI/error owner。禁止只显示“G代码错误”“格式错误”或LinuxCNC原始英文异常。

统一用户提示模板：

`[{error_id}] 第{line}行，第{column}列，{token}：{problem}。系统已执行：{auto_action}。处理建议：{next_action}。`

每个失败至少返回：

- `error_id/severity/phase`；
- `source_path/source_sha256`；
- `source_profile_id` 和自动匹配证据；
- `source_span`：原始行、列、长度和token；
- `source_excerpt`：错误行及有限上下文；
- `rule_id` 或冲突的候选rule IDs；
- `modal_snapshot`；
- `problem`：明确说明哪里错、期望什么；
- `auto_action`：已自动归一、已尝试的规则或“未修改原文”；
- `next_action`：用户应修改的内容或缺少的项目能力；
- `normalized_preview`：仅在无歧义时提供；
- `ruleset/target identity`；
- `error_count/error_index`。

### 11.3 固定错误提示映射表

| error_id | 触发条件 | 明确用户提示 | 系统自动处理 | 处理建议 |
| --- | --- | --- | --- | --- |
| `GCODE_PROFILE_NOT_IDENTIFIED` | 无法唯一匹配来源profile | 无法确定该程序对应的控制器型号、版本或机型 | 已分析metadata和语法特征，未修改程序 | 选择已登记的精确profile或补充导入来源 |
| `GCODE_PROFILE_AMBIGUOUS` | 同时匹配多个profile | 该写法在多个控制器profile中含义不同，不能自动选择 | 列出候选profile及首个差异行 | 选择正确profile；不得按品牌笼统选择 |
| `GCODE_PROFILE_MISMATCH` | profile版本、选件或参数hash不符 | 已选profile与程序要求或当前登记参数不一致 | 标出不匹配字段 | 使用匹配profile或更新受控登记 |
| `GCODE_FORMAT_INVALID` | 行结构无法解析 | 本行G代码格式不完整或词序非法 | 已执行大小写、空白和前导零归一，仍无法解析 | 按提示补齐或修改该行 |
| `GCODE_PERCENT_UNPAIRED` | `%` 未成对或位置非法 | 程序起止符%没有成对出现 | 未修改原文 | 补齐或删除错误的%包装 |
| `GCODE_COMMENT_UNCLOSED` | 注释括号未闭合 | 注释没有正确结束，后续内容无法可靠解析 | 停止跨越损坏边界继续猜测 | 闭合注释后重新打开程序 |
| `GCODE_ACTIVE_COMMENT_DENIED` | 出现LOG/DEBUG/PRINT/MSG等active comment | 该注释会产生文件、日志或运行副作用，不允许执行 | 保留原文并阻止输出到canonical | 删除该副作用或改为普通注释 |
| `GCODE_NUMBER_INVALID` | 数值含非法字符、NaN、Inf等 | 地址字后的数值格式无效 | 未修改该数值 | 输入有限合法数值 |
| `GCODE_NUMBER_AMBIGUOUS` | 无小数点/尾点存在多种解释 | 该数值在当前profile中无法确定实际单位或倍率 | 显示所有可能解释，不选择其一 | 选定精确profile或写出明确小数值 |
| `GCODE_VALUE_OUT_OF_RANGE` | 数值超出规则或机床范围 | 该数值超出允许范围 | 显示允许最小值、最大值和单位 | 修改为允许范围内的值 |
| `GCODE_DUPLICATE_WORD` | 同一块重复出现不允许重复的地址字 | 本行重复定义了同一个地址字，目标值不唯一 | 未选择最后一个或第一个值 | 删除重复项并保留唯一值 |
| `GCODE_MODAL_CONFLICT` | 同一块出现同模态组冲突命令 | 本行同时包含互斥的模态命令 | 列出冲突命令，不自动选取 | 只保留加工意图对应的一项 |
| `GCODE_MODAL_REQUIRED` | 运动前单位/平面/距离/进给模式不明确 | 运动所需的模态状态未明确 | 若profile有唯一固定值则自动生成；否则不猜测 | 在运动前明确所缺G码 |
| `GCODE_COMMAND_UNKNOWN` | 来源parser不认识G/M码或结构 | 无法识别命令，不能确定其加工含义 | 未透传、未删除 | 检查拼写或补充经过验证的兼容规则 |
| `GCODE_COMMAND_NOT_ALLOWLISTED` | 已识别但不在独立执行白名单 | 命令已识别，但当前版本未批准执行 | 标出命令和识别到的功能，未生成可执行输出 | 改用白名单命令或等待该功能完成验证 |
| `GCODE_WORD_NOT_ALLOWED` | 命令带有未批准地址字 | 当前命令不允许使用该地址字 | 列出该命令允许的地址字 | 删除错误地址字或改用已验证写法 |
| `GCODE_WORD_REQUIRED` | 缺少必填地址字 | 当前命令缺少必要参数 | 列出缺少字段和用途 | 补齐明确参数 |
| `GCODE_AXIS_NOT_SUPPORTED` | 出现A/B/C/U/V/W或目标不存在的轴 | 当前首版profile不允许该轴 | 未将该轴静默丢弃 | 使用匹配机型/profile或移除错误轴 |
| `GCODE_RULE_AMBIGUOUS` | 一个AST节点命中多条规则 | 多条翻译规则同时匹配，系统无法证明哪条正确 | 列出冲突rule_id，未选择其一 | 修正规则集或选择能消除歧义的profile |
| `GCODE_RULE_CONDITION_FAILED` | 命令在白名单但参数/上下文不满足 | 命令属于白名单，但本行参数或前置状态不符合放行条件 | 显示失败条件 | 按白名单条件修改程序 |
| `GCODE_ARC_INVALID` | G2/G3圆弧几何、I/J或终点不成立 | 圆弧中心、终点或半径关系无效 | 计算并显示几何误差，不修改刀路 | 修正X/Y/I/J；首版不改用R自动猜测 |
| `GCODE_CONTEXT_INVALID` | MDI/主程序/结束块等上下文错误 | 该命令不能在当前位置或当前程序层级使用 | 未移动命令位置 | 将命令放到允许上下文 |
| `GCODE_SUBPROGRAM_DENIED` | O-call、M98/M99或子程序结构 | 当前首版不允许子程序或外部程序调用 | 建立调用线索但不打开或执行外部文件 | 展开为白名单主程序或等待子程序功能验证 |
| `GCODE_EXTERNAL_EFFECT_DENIED` | M100-M199、外部文件、shell或未登记remap | 命令会调用未登记的外部功能 | 已阻止生成和执行 | 使用精确登记的系统入口 |
| `GCODE_SEMANTIC_UNPROVEN` | 来源和LinuxCNC含义无法证明等价 | 系统认识该命令，但无法保证转换后加工目的不变 | 保留原文和候选解释，不生成执行输出 | 使用已验证等价写法或新增专项规则和golden |
| `GCODE_TARGET_CAPABILITY_MISSING` | 当前机型无所需主轴、冷却、轴或owner能力 | 程序要求的机床能力当前未登记或不可用 | 已识别所需能力 | 更换匹配机型/profile或完成能力配置 |
| `GCODE_OUTPUT_REPARSE_FAILED` | canonical产物不能被目标LinuxCNC解析 | 转换结果未通过目标LinuxCNC语法检查 | 保存诊断，不提交artifact | 修复转换规则；用户原程序不被改写 |
| `GCODE_OUTPUT_NODE_DENIED` | 输出AST产生未批准节点或副作用 | 转换结果包含白名单外目标命令 | 标出来源rule_id和新增节点 | 修复emitter或规则，不允许用户绕过 |
| `GCODE_ARTIFACT_STALE` | source/profile/ruleset/target identity变化 | 已生成的执行程序已失效 | 自动撤销ready_to_run | 重新Program Open并自动转换 |

### 11.4 多错误汇总和界面要求

1. Program Open应在可安全恢复的语法边界内一次收集全部确定错误，按源行排序，并显示“共N个问题”。
2. 首条P0错误在主提示中显示，其余错误可展开；点击任一项必须跳到准确源行、列并高亮token。
3. 同一根因引起的级联错误应合并，避免几十条重复提示。
4. 成功自动处理的格式差异放在“已自动处理”列表，不和错误混在一起。
5. 每条提示必须使用本节映射表，不允许临时拼接含糊文案。
6. 用户修正并重新打开后，旧error generation立即失效，不得继续显示旧行号。
7. 错误不得降级为“已忽略并继续运行”，也不得提供绕过白名单按钮。

## 12. 白名单成熟度与程序级验收

rule成熟度、最低测试、晋级和失效由 [G代码执行白名单](./G代码执行白名单.md) 唯一维护。兼容方案只消费当前有效ruleset，不能在本文建立第二套成熟度或放行表。

程序取得 `READY` 还必须同时满足：

1. source profile唯一且identity冻结；
2. 所有source AST节点均命中有效白名单rule；
3. typed IR完整且无未决语义；
4. canonical artifact生成完成并由精确目标LinuxCNC版本重新解析；
5. 输出AST白名单通过且没有新增副作用；
6. source、artifact、line map、report、ruleset、profile和target identity原子冻结；
7. Program Open、Start、Resume、First Point、preview和MDI消费同一canonical identity；
8. 任一identity变化立即撤销旧artifact的 `READY`。
## 13. 本轮删除或延期的旧内容

本方案不再在本文件裁决：

- 50MB文件读取、流式缓存、预览LOD；
- 小线段识别、G64 P/Q推荐、连续路径和降点；
- UI弹窗布局和按钮行为；
- Syspro目录整体信任；
- 外部绝对路径输入；
- M码段整体授权；
- 直接字符串runtime模板；
- “尽量直通”为默认目标；
- 自动方言识别直接放行；
- 无法证明时仍生成部分可执行程序。

这些内容要么属于其它 owner，要么必须等整体方案确认后再合并，不能继续混在命令白名单里形成第二套运动或运行合同。

## 14. 后续整体合并清单（本轮不执行）

方案确认后再按唯一 owner 逐项合并：

1. 在需求索引登记唯一 G-code兼容白名单 owner。
2. 裁决“原始文件直运行”与“不可编辑 canonical artifact”的 Program Open/Start合同。
3. 合并并物理退役平行的旧白名单文档或规则源，保证一个策略真源。
4. 把 UI错误展示、程序打开、Start/Resume行为同步到各自 owner。
5. 把大文件预览和小线段/前瞻内容留在其现有 owner，不回填本文件。
6. 把WCS/G92、刀具/TLO、RTCP、探测、换刀、IO和系统宏逐项接到已有native owner。
7. 方案和 owner 文档稳定后再设计机器可读规则投影、源码和focused tests。

## 15. 参考资料边界

对照和规则必须引用精确型号的官方手册，不能用品牌常识代替。当前技术校核可参考：

- [LinuxCNC 2.9 G-code Overview](https://www.linuxcnc.org/docs/2.9/html/gcode/overview.html)
- [LinuxCNC 2.9 G-code Reference](https://www.linuxcnc.org/docs/2.9/html/gcode/g-code.html)
- [LinuxCNC 2.9 M-code Reference](https://www.linuxcnc.org/docs/2.9/html/gcode/m-code.html)
- [FANUC CNC Function Catalog](https://www.fanucamerica.com/docs/default-source/cnc-files/cnc-function-catalog.pdf)
- [Mitsubishi M800/M80/C80 Programming Manual](https://dl.mitsubishielectric.com/dl/fa/document/manual/cnc/ib1501278/ib1501278engd.pdf)
- [Siemens ISO Dialects Function Manual](https://support.industry.siemens.com/cs/attachments/56727098/IH_en_en-US.pdf)
- [Syntec官方资料入口](https://www.syntecamerica.com/index.php?ws=download)

官方资料仍只说明候选语义。真正允许规则还必须绑定本项目target identity、owner、正反golden和必要板端证据。
