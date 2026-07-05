# G-code 白名单与转译边界

本文定义 v3 后续支持发那科、三菱、新代、西门子等操作习惯时的白名单原则。`AGENTS.md` 仍是唯一规则入口；本文只约束 G-code/MDI 方言兼容层的目标、边界和验收口径。

## 目标

让操作员可以按过去习惯输入 MDI，或打开常见控制系统生成的 G-code 文件；v3 在启动前把可证明等价的内容转成 LinuxCNC 可执行格式，再交给现有安全链路运行。

统一执行链路必须是：

```text
屏幕 MDI / 打开程序
  -> G-code 方言白名单检查
  -> 安全转译为 LinuxCNC runtime copy
  -> rotary equivalent filter
  -> v3 start_run safety gate
  -> LinuxCNC AUTO / MDI
```

禁止直接把不同系统方言透传给 LinuxCNC 后再观察是否报错。禁止静默猜测宏、循环、坐标变换、刀补或机床专用 M 码语义。

## 白名单优先级

第一优先级是不会出错：宁可少支持、阻塞运行，也不能错译、误跑、误动。只有能证明与 LinuxCNC 执行语义等价的内容，才允许放行或转译。

第二优先级是尽量多覆盖：在第一优先级成立的前提下，尽量覆盖发那科、三菱、新代、西门子操作员常用写法，包括程序外壳、行号、普通运动 block、常见坐标系和常见主轴/冷却/程序结束指令。

未列入白名单的 G/M 码、宏、变量、循环、子程序、固定循环、刀补、坐标变换、控制器专用指令和机床厂自定义语义，默认 `block`。后续只能逐项登记、逐项转译、逐项验证后加入白名单。

## 基本规则

- 源文件不改；只生成 `/run/8ax_v3_product_ui/` 下的运行副本和 manifest。
- MDI 与程序文件共用同一套白名单和转译逻辑；不能出现 MDI 能跑、打开程序不能跑的双头语义。
- 能证明与 LinuxCNC 等价的语法才允许放行或转译。
- 不能证明等价时必须阻塞运行，给出行号、原文、原因和建议。
- 语法合法不等于机床动作已验证；主轴、冷却、换刀、刀长补偿等实际动作必须另有板端证据。
- 方言转译必须在 rotary equivalent filter 前执行，避免 A/C 轴目标绕过现有保护。
- 转译层只处理程序文本语义；真实运动、坐标、RTCP、回零、执行仍走 LinuxCNC/HAL/EtherCAT 原生链路。
- 每次运行必须记录 manifest：原始 hash、转译 hash、检测到的方言、改写行、阻塞行、未覆盖语法。

## 处理状态

| 状态 | 含义 | 是否允许运行 |
| --- | --- | --- |
| pass | LinuxCNC 原生兼容，无需改写 | 是 |
| rewrite | 已按白名单转译为 LinuxCNC 等价语义 | 是 |
| block | 发现不能证明等价的语法 | 否 |
| ignore_envelope | 程序包裹符、行号或注释被移除/忽略 | 是 |
| unsupported | 识别到品牌特性但未实现映射 | 否 |

## v1 允许白名单

### 程序外壳

| 输入 | 处理 | 说明 |
| --- | --- | --- |
| `%` 程序包裹 | ignore_envelope | 仅程序文件允许；MDI 中出现时阻塞 |
| `O1234` 程序号 | ignore_envelope | 仅作为文件标识，不作为 LinuxCNC O-word 子程序执行 |
| `N10` 行号 | ignore_envelope | 去掉或保留均可，但不得改变后续 word |
| 空行 | ignore_envelope | 不执行 |
| 括号注释 `( ... )` | pass | 保留或剥离均可，但不能解析注释内的轴 word |
| 分号注释 `; ...` | pass | LinuxCNC 可接受 |

### 模态与坐标

| 输入 | 处理 | 说明 |
| --- | --- | --- |
| `G90` / `G91` | pass | 增量模式下 A/C 旋转轴仍由 rotary filter 决定是否阻塞 |
| `G20` / `G21` | pass | 英制/公制由 LinuxCNC 解释 |
| `G17` / `G18` / `G19` | pass | 平面选择 |
| `G54` - `G59` | pass | LinuxCNC 标准工件坐标系 |
| `G59.1` - `G59.3` | pass | LinuxCNC 支持时允许 |
| `G54.1 Pn` | block | 扩展坐标系映射未定义 |
| `G92` / `G92.1` | block | 临时坐标偏置风险高，需单独设计 |

### 运动

| 输入 | 处理 | 说明 |
| --- | --- | --- |
| `G0` / `G00` | pass | 快移；A/C 大角度仍受 rotary filter 限制 |
| `G1` / `G01` | pass | 直线进给 |
| `G2` / `G02`、`G3` / `G03` | pass | 只允许 LinuxCNC 可直接解释的 I/J/K/R 圆弧；品牌私有圆弧模式、极坐标圆弧、复合圆弧或未知 arc center mode 必须阻塞 |
| `X Y Z A C` 数字轴 word | pass | A/C 继续交给 rotary filter 判断 |
| `I J K R` 圆弧参数 | pass | 不做品牌私有圆弧重解释 |
| `F` 进给 | pass | 默认 LinuxCNC `G94` 语义 |
| `G4 P...` | block | 不同系统 dwell 时间单位容易不同，先阻塞 |
| `G5.x` 样条/高级插补 | block | 未定义等价语义 |
| `G40` / `G41` / `G42` | block | 刀具半径补偿方向、入口线和控制器差异风险高，先阻塞 |
| `G80` - `G89` | block | 固定循环参数在不同系统间差异大，先阻塞 |
| `G93` / `G95` | block | 反时间/每转进给会改变进给语义，未验证前阻塞 |

### 主轴、刀具、冷却与程序结束

| 输入 | 处理 | 说明 |
| --- | --- | --- |
| `S` | pass | 主轴转速设定；语法可放行，实际主轴动作需板端验证 |
| `M3` / `M03`、`M4` / `M04`、`M5` / `M05` | pass | 主轴正转/反转/停止；语法可放行，实际主轴动作需板端验证 |
| `M7` / `M07`、`M8` / `M08`、`M9` / `M09` | pass | 冷却控制；语法可放行，实际输出需板端验证 |
| `Tn`、`M6` / `M06` | pass | 仅 LinuxCNC 工具表语义；实际换刀策略、互锁和 UI 状态需板端验证 |
| `G43 Hn`、`G49` | pass | 标准刀长补偿；需保持现有 tool holder/TCP 语义，并经板端状态验证 |
| `M0` / `M00`、`M1` / `M01` | pass | 程序暂停语义由 LinuxCNC 处理 |
| `M2` / `M02`、`M30` | pass | 程序结束 |
| 其他 `M` 码 | block | 机床专用 M 码必须逐个登记映射 |

## 品牌差异处理

### 发那科 / Fanuc

可优先支持的安全子集：

- `%`、`O` 程序号、`N` 行号。
- `G00/G01/G02/G03`、`G17/G18/G19`、`G20/G21`、`G90/G91`、`G54-G59`。
- `M03/M04/M05/M08/M09/M30`、`Tn M06`、`G43 Hn`。

必须阻塞：

- Macro B：`#` 变量、`IF`、`WHILE`、`GOTO`、`G65`、`G66`、`G67`。
- 坐标旋转/缩放/镜像：`G68`、`G69`、`G51`、`G50`。
- TCP/五轴私有语义：`G43.4`、`G43.5`、`G68.2`。
- 刀具半径补偿和固定循环：`G40/G41/G42`、`G80-G89`，除非后续逐项验证。
- 进给模式变更：`G93`、`G95`。
- 扩展坐标：`G54.1 Pn`。
- 子程序：`M98`、`M99`，除非后续实现明确映射到 LinuxCNC O-word。

### 三菱 / Mitsubishi

按 Fanuc-like 子集处理普通 G-code。必须阻塞：

- 变量、宏、条件跳转、固定循环参数扩展。
- 三菱专用 M 码、PLC/机床厂自定义 M 码。
- 高速高精、平滑、坐标变换、刀尖控制等专用 G-code。
- 子程序调用，除非已有明确映射。

### 新代 / Syntec

按 Fanuc-like 子集处理普通 G-code。必须阻塞：

- `@`、`#`、变量、条件、循环、宏调用。
- 机床厂自定义 M 码。
- 极坐标、坐标旋转、刀尖控制、复合循环等非标准语义。
- 控制器专用的参数写入或系统变量读取。

### 西门子 / Siemens

只允许 ISO 风格的普通 G-code 子集。必须阻塞：

- `CYCLE...` 固定循环。
- `TRAORI`、`TRANS`、`ROT`、`AROT`、`MIRROR`、`SCALE`。
- `R` 变量、`DEF`、`MSG`、`GOTOF/GOTOB`、标签与流程控制。
- `L` 子程序调用、`M17`。
- Siemens 专用刀具、坐标系、框架变换语义。

## MDI 特殊规则

- MDI 允许一行或多行普通 block。
- MDI 不允许 `%`、`O` 程序号、子程序定义、循环或宏。
- MDI 输入为空、只有注释、只有行号时必须阻塞。
- MDI 转译后必须与屏幕可见文本做 hash/trajectory 一致性检查，再进入启动链路。
- MDI 中出现 `M30/M2` 可允许，但不应清空屏幕 MDI 文本。

## 转译示例

### 可运行

```gcode
%
O1001
N10 G90 G54 G21
N20 G0 X0 Y0 Z5
N30 G1 X10.0 F200
N40 M30
%
```

转译为：

```gcode
G90 G54 G21
G0 X0 Y0 Z5
G1 X10.0 F200
M30
```

### 必须阻塞

```gcode
N10 #100 = 10
N20 G1 X#100 F200
```

阻塞原因：`macro_variable_not_supported`。

```gcode
CYCLE81(10, 0, 2, -20)
```

阻塞原因：`siemens_cycle_not_supported`。

```gcode
G54.1 P12
```

阻塞原因：`extended_work_offset_not_mapped`。

## manifest 最小字段

```json
{
  "ok": true,
  "source_kind": "mdi_or_program",
  "source_name": "operator input or file path",
  "dialect_detected": ["fanuc_like"],
  "source_sha256": "...",
  "generated_sha256": "...",
  "generated_program_path": "/run/8ax_v3_product_ui/gcode_whitelist_active.ngc",
  "rewrites": [
    {
      "line": 1,
      "reason": "strip_program_percent",
      "before": "%",
      "after": ""
    }
  ],
  "blocked": []
}
```

阻塞时：

```json
{
  "ok": false,
  "blocked": [
    {
      "line": 20,
      "reason": "macro_variable_not_supported",
      "text": "G1 X#100 F200"
    }
  ],
  "generated_program_path": ""
}
```

## 后续实现验收

- 同一段文本从 MDI 输入和程序文件打开，白名单结果一致。
- LinuxCNC-native 程序不被无意义改写。
- Fanuc-like 普通程序能生成 runtime copy 并继续进入 rotary filter。
- 宏、Siemens cycle、自定义 M 码、扩展坐标等不确定语义不会进入 LinuxCNC。
- manifest 足够复盘每一行为什么放行、改写或阻塞。
- UI 弹窗显示第一条阻塞行号和原因，完整详情保留在 evidence/result JSON。
