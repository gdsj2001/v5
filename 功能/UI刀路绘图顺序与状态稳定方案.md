# UI 刀路绘图顺序与状态稳定方案

引用需求真源：`REQ-DOC-SINGLE-SOURCE`、`REQ-NATIVE-OWNER-FIRST`、`REQ-LINUXCNC-COMMAND-GATE`、`REQ-PARAM-MEMORY-LIGHTWEIGHT-SAVE`、`REQ-G92-NATIVE-RUNTIME`、`REQ-WCS-NATIVE-OWNER`、`REQ-ROTARY-UNWRAP-ABSOLUTE-PROOF`、`REQ-ROTARY-REBASE-NATIVE-GATE`、`REQ-RTCP-G53-NATIVE-ACTUAL`、`REQ-RTCP-NATIVE-STATUS-SOURCE`、`REQ-GCODE-RUN-HOT-PATH`。

## AI 阅读入口

- 本文定位：刀路显示顺序、同帧 actual、RTCP/G53 UI 消费、红/绿点和 30ms UI 显示动态层。
- 先读 `REQ-RTCP-G53-NATIVE-ACTUAL`；RTCP native 状态源和无 fork 回读先跳到 `功能/微内核.md` 的 `REQ-RTCP-NATIVE-STATUS-SOURCE`。
- 刀路 UI 可以用 SHM 消费 30ms UI 显示投影：编码器反馈机械坐标、运动学理论坐标、`spindle_speed_rpm`、`linear_velocity_mm_per_min`、`feedrate_override`、`spindle_override`；当前模态、当前刀号、对应刀长、WCS、TLO/tool、A/C geometry、G53、RTCP 动作证明、安全、诊断和其它 actual 仍直连微内核/native 内存，不得用 request、dirty、设置文件、helper cache、旧 SHM 字段或旧快照拼 active。
- 本文消费 `功能/0开机参数入内存.md` 的最高启动内存架构：UI 刀路代码、资源、投影器、图层表和 provider 必须随 UI 运行闭包开机进 RAM；程序文件只允许在 Program Open/Load 阶段读取并生成只读程序模型/运行内存副本，Start/Resume 和动态绘图不得重新读程序文件、扫描脚本、懒导入模块或读取硬盘 JSON/result。


## 1. 边界

UI 刀路层只能做显示：

- 读取同一帧 `ReV3StatusView` / typed SHM UI 显示投影。
- 在 Program Open/Load 阶段解析并缓存只读 G-code 程序模型；运行态只消费该内存模型。
- 把程序刀路、WCS/MCS、A/C 姿态、刀尖/刀柄和必要状态文字投影到屏幕；前台刀路画布不显示内部诊断 token。
- 处理缩放、平移、视角切换、图层显示和手势。
- 通过正式控制链发送用户命令。

UI 刀路层禁止做运动真相：

- 不修改 G-code，不为了显示效果改 `多格式g代码/cc.ngc` 或用户程序。
- 不计算 RTCP 真实运动学，不替代 LinuxCNC/HAL/EtherCAT/native owner。
- 不把 G53、回零、等效角、软限位、驱动 preset、rebase 或参数写入逻辑塞进绘图层。
- 不恢复 `/run` JSON、FIFO、临时 response JSON、短生命周期 Python fallback 或 UI 热路径 `fork/exec`。
- 不用绘图修正掩盖 Broker、SHM、LinuxCNC 或 HAL 状态错误。

状态链固定为：

```text
微内核/native 坐标内存 -> 常驻 v3_state_publisher.py / 登记 native publisher -> /dev/shm/v3_status_shm -> UI SHM reader -> ReV3StatusView -> toolpath view
微内核/native 其它状态内存/API -> UI 状态 provider / toolpath view
```

控制链固定为：

```text
UI C -> Command Broker UDS framed socket -> product action -> LinuxCNC/HAL/EtherCAT
```

## 2. 数据来源

刀路显示必须使用同一 generation 的状态。SHM 只提供 30ms UI 显示投影：编码器反馈机械坐标、运动学理论坐标、`spindle_speed_rpm`、`linear_velocity_mm_per_min`、`feedrate_override`、`spindle_override`；该投影必须由常驻 State Publisher 或登记 native publisher 从微内核/native 内存采样维护，不得由周期短脚本、硬盘 JSON/result 或一次性 helper 搬运状态。当前模态、当前刀号、对应刀长、WCS、TLO/tool、A/C geometry、G53、RTCP 动作证明、安全、诊断和其它 actual 必须由 UI 状态 provider 直连微内核/native 内存或登记 API 读取。每一帧先把 SHM 显示投影和直连微内核状态按 generation 收敛为 scene/model，再刷新各图层；不得一个图层读 SHM、另一个图层读缓存或 JSON，也不得在动态帧里重新读取 G-code 文件、资源文件或运行结果文件。

| 数据 | 来源 | 用途 | 缺失处理 |
| --- | --- | --- | --- |
| `mcs[5]` | 编码器/关节 actual 经 30ms SHM UI 显示投影 | X/Y/Z 大字机械坐标、绿色机械点、UI 轨迹实际点；A/C 只作为坐标显示输入之一 | 动态 actual 图层隐藏或 degraded |
| `cmd_mcs[5]` | 运动学/插补器 commanded/theory 归一到同一 UI 机械域后经 30ms SHM UI 显示投影 | 红色 commanded 刀尖点、UI 轨迹理论点、跟随误差显示 | 红点和跟随误差隐藏 |
| `wcs_offset` / `wcs_offsets[9][5]` | native/WCS owner 直连微内核/native 内存或原生 API | WCS 原点、程序 WCS offset | 缺全表时只允许 active WCS 降级；非 active 不猜 |
| `runtime_modal_text` / `current_tool` / `display_tool_length_mm` | 微内核/native modal/tool 状态块或等价原生 API；State Publisher 只能从该 owner 采样到 `/run` native block | 当前模态文字、当前刀号、对应刀长和 RTCP 图层显示 | 缺失则当前模态显示 `--`、刀号/刀长显示 `T-- L--`，RTCP unknown/degraded；不得回退到 SHM modal、tool.tbl 磁盘读、设置表、JSON、旧缓存或 request/dirty |
| `spindle_speed_rpm` / `linear_velocity_mm_per_min` / `feedrate_override` / `spindle_override` | 微内核/LinuxCNC/HAL runtime actual 经 30ms SHM UI 显示投影发布 | 转速、进给速度、进给倍率和主轴倍率显示 | 缺失则对应显示 degraded；不得作为运动证明、动作成功或门禁 |
| `rtcp_actual_enabled` | 只能从直连微内核/native modal actual 派生；不是独立真源 | RTCP 图层布尔显示投影 | modal 缺失时 invalid；不得由 SHM modal、request/dirty/设置文件、非同源 helper/cache 或旧快照派生 |
| `tool_offset` / `display_tool_length_mm` | LinuxCNC/tool native owner 直连微内核/native 内存或原生 API | 刀长、刀柄线和必要的红点 Z 计算 | 缺失则隐藏刀柄线等相关动态图层 |
| A/C geometry 与 raw/unwrapped/display phase | rotary/native owner 直连微内核/native 内存或登记 native memory block | 生成唯一的 UI A/C 显示投影；供坐标面板、刀路 A/C 姿态、标签和诊断共同消费 | 缺 geometry 时隐藏相关几何/状态文字或进入 degraded；前台不得显示 `geom--` 等内部 token；display phase/count-domain 无效时不得显示默认 0 冒充真实 A/C |
| `status_epoch` / seqlock / CRC | SHM UI 显示 ABI | 只证明允许的 SHM 显示字段同帧一致性和 freshness | 冲突丢帧或 stale grace；结构错误 fail-closed |

RTCP/G53/A/C 的动作证明和几何真源统一命名为 same-generation native actual，不同步到 SHM。RTCP 的控制因果是：G-code/M-code 进入微内核/LinuxCNC，微内核 runtime modal 成为 actual owner；State Publisher 只可把该 owner 的当前模态、当前刀号和对应刀长发布到 `/run` native modal/tool 状态块或等价原生 API，UI/provider 必须直连回读该 owner 并与 SHM 坐标 generation 收敛。WCS、TLO、A/C geometry 和 G53 仍必须来自微内核/native 内存或登记 API，不允许 UI 从旧 SHM 字段、多个缓存或 JSON 自行拼帧。`rtcp_actual_enabled` 如保留，只能是从该 modal actual 派生的显示字段。RTCP request 只能在界面显示 pending；只有后续 native modal actual 确认 active 后，RTCP 图层才能切 active。

RTCP/switchkins actual 的 native 状态源、HAL pin/native component、native memory block 和无 fork 回读规则由 `功能/微内核.md` 的 `REQ-RTCP-NATIVE-STATUS-SOURCE` 维护。本文只规定 UI/刀路消费 30ms SHM 显示投影 + 直连微内核状态的显示语义；不得在本文重复维护底层状态源形态。

重建边界固定如下：

总规则：开机读内存建 WCS/MCS/A/C 轴线并 fit；打开 G-code 建黄线并 fit；RTCP/模态文字、当前刀号和对应刀长必须直连 native modal/tool owner；之后只机械坐标值、native 模态/tool 状态和速度/倍率显示投影驱动动态层，不得重建。

- 开机顺序固定为：先读到有效 30ms SHM UI 显示投影，同时直连读到必要微内核/native WCS、A/C geometry 和动作证明状态，再建立 WCS/MCS、A/C 轴线状态层结构，再生成第一次 fit。
- 打开 G-code 顺序固定为：Program Open/Load 读取程序文件并生成只读程序模型/运行内存副本；成功后只重建黄色程序线，再为黄色线生成一次 fit。
- RTCP 显示状态只能从 G-code/M-code 控制后的直连微内核 runtime modal actual 派生；产品动作证明必须直连 native actual。运行 G-code 时可以复用上一帧有效 RTCP modal actual 缓存；RTCP 开关、模态变化、异常、无有效缓存或缓存过期时必须重新读，不能用缓存假装正常。
- 开机结构和黄色线建立完成后，后续只由同一帧内存里的机械坐标值做整张图形的显示投影驱动；模态文字、转速、进给速度和倍率只更新动态显示，不得触发 WCS/MCS、A/C 轴线、黄色程序模型或 fit 重建。
- fit 只有开机/页面创建后首次读到有效 SHM UI 显示投影并直连读到必要 native actual 建立状态层时自动重建一次，以及打开 G-code 成功并显示黄色程序线时自动重建一次。
- WCS/MCS 原点和轴线、A/C 轴线和中心点，只在开机/页面创建后首次读到有效内存 actual 时建立结构；后续状态变化只更新同一结构的投影坐标。
- 黄色程序线只在 Program Open/Load 成功时解析、缓存或重建；之后只能由同一帧内存里的机械坐标值驱动已缓存黄线投影，不得重新读文件或重新建黄线。
- rtcp直接去读内存里的模态；后续只作为内存 actual gate 参与机械坐标投影，不得触发 fit、WCS/MCS、A/C 轴线或黄色程序模型重建。

`cmd_mcs` 只能用于显示和诊断，不能替代 `mcs` 通过 Home、Start、Jog、RTCP 或设零门禁。

当前主页面可见坐标必须同时显示当前模态、机械坐标系、加工坐标系和 A/C。当前模态文本必须由微内核/native modal 状态块或等价原生 API 提供，并显示常用 LinuxCNC modal group 的 active G 码，至少覆盖运动 `G0/G1/G2/G3`、平面 `G17/G18/G19`、单位 `G20/G21`、刀补 `G40/G41/G42`、刀长 `G43/G43.1/G49`、当前 active WCS `G54-G59.3`、路径控制 `G61/G61.1/G64`、固定循环 `G80-G89`、距离模式 `G90/G91`、圆弧距离 `G90.1/G91.1`、进给模式 `G93/G94/G95`、主轴速度模式 `G96/G97` 和返回模式 `G98/G99`；显示顺序必须稳定，不能因 LinuxCNC `stat.gcodes` 原始顺序或固定 10 项截断而丢常用模态。当前模态区域必须在轨迹区左侧显示成单列：`当前模态` 标题之后先显示当前刀号和该刀号对应刀长，随后每个 active G 码独立一行，不得横向长串截断。当前刀号/刀长来源必须同为微内核/native tool owner 回读，不得从 SHM modal、设置页、磁盘 `tool.tbl`、JSON 或旧缓存补齐。加工坐标数值列必须遵循 `REQ-WCS-NATIVE-OWNER`：使用同源当前 `mcs - active_wcs_offset` 计算，并且始终只与机械坐标相差当前 active WCS offset；不得显示固定 `0,0,0`，不得把 `cmd_mcs`、驱动表、设置页表格、profile、旧缓存、默认 0 或非同源快照当作加工坐标系真源。当前 active WCS 坐标系在轨迹区里的原点必须来自 native 当前 WCS offset 在 G53/MCS 空间中的固定位置；机械坐标点移动时，轨迹区加工坐标系结构原点不得跟随当前 `mcs` 或刀尖位置移动，只有 active WCS 或其 offset 的 native readback 变化才允许改变 WCS 原点。`cmd_mcs` 继续只用于红色 commanded 点、理论轨迹和跟随误差诊断。

**跟随误差与坐标统一消费红线**：同一帧内所有机械与理论坐标值（包括 A/C 的等效折算角和跟随误差数据），**必须在状态收敛阶段统一进行一次性计算**并写入 scene 模型。
- **跟随误差** 统一计算并保存为浮点数：
  ```text
  following_error[i] = mcs[i] - cmd_mcs[i]
  ```
- **坐标大字控件、3D 刀路轨迹渲染、以及跟随误差面板（3个消费者）** 必须同时直连消费 scene 模型中已算好的浮点数值，严禁任何消费者在本地进行重复计算，也严禁读取 UI 控件文本进行反向解析。

操作员可见 actual 坐标、绿色机械点、MCS/WCS 坐标系、A/C 轴线、A/C 标签、刀柄/动态姿态、刀路姿态以及红色 commanded 刀尖点，都必须先在 UI 状态层收敛成同一条显示投影链路，再提供给坐标面板和刀路视图。禁止出现“坐标面板用 `rotary_phase`，刀路视图用 `mcs[3]` / `mcs[4]`”的双路径显示。若 `rotary_phase`、count-domain、unwrap 或对应有效标志不可用，所有 actual 显示消费者必须同帧进入 degraded/不可用显示或同帧回到受控 raw actual 显示；红色 commanded 点缺少同帧 `cmd_mcs`/刀长/有效 MCS 门禁时也必须隐藏，不得用默认 0 位姿或种子点补画。

### 2.1 状态层与程序层的绘图门禁

UI 刀路视图分为两类图层，不能再用同一个“是否已打开程序”条件一起控制：

- 状态层：MCS 机械坐标系、当前/程序 WCS 加工坐标系、A/C 轴线和标签、绿色机械点、刀柄线、红色 commanded 刀尖点、坐标读数和 degraded 文本。
- 程序层：黄色 G-code 进给/切削轨迹。

主页面刀路对象必须按 v3 `v3_ui_toolpath_layers.c` / `v3_ui_toolpath_view*.c` 的直观层级恢复：左侧刀路区先创建透明 `toolpath_clip_layer`，黄色进给线作为 clip layer 内的 `lv_line`，红色 commanded/microkernel 点使用 v3 的 `toolpath_microkernel_marker_dot` 样式（7x7、`rgb(255,64,64)`、边框 `rgb(255,230,230)`），绿色机械/holder 点使用 v3 的 `toolpath_holder_marker_line` 样式（7x7、`rgb(68,221,144)`、边框 `rgb(220,255,235)`）。v5 不得在主页面外层另造 `cmd_point_marker`、`mcs_point_marker`、说明标签或另一套颜色近似替代 v3 图层；缺少有效 `cmd_mcs` 或 `mcs` 时对应红/绿点必须隐藏，不得用默认点补画。

v5 主页面轨迹区域的前台对象集合必须逐项等同 v3 `ReV3ToolpathLayerHandles`，不得自创另一套近似对象：当前模态标签、summary/detail/view 标签、透明裁剪层、黄色 G-code 进给线及分段线、MCS 原点锚和三轴线、当前 WCS 原点锚和三轴线、program WCS 原点/三轴线/标签对象、A 轴红线和标签、C 轴青线和标签、A/C 中心点、刀柄线、红色 commanded/microkernel 点、绿色 actual/holder 点、MCS/WCS 小标签、XY/XZ/YZ/3D 视图按钮都必须按 v3 的对象层级、坐标、颜色、线宽、点大小、标签位置、按钮默认/选中态建立。加工坐标系可见层只显示当前模态/当前 active WCS；其它非当前 G54-G59.3 program WCS 对象必须保持隐藏，不得在开机骨架或运行态同时铺出 9 组加工坐标系。黄色 G-code 轨迹是唯一允许开机为空的主轨迹对象；除此之外，轨迹区结构对象在主页面创建/开机首帧就必须显示 v3 同款视觉骨架，包括 MCS 原点锚、三轴线、当前 WCS 原点锚、当前 WCS 三轴线/标签、A/C 轴线、A/C 中心点、A/C 标签、标签锚点、红/绿点样式的屏幕锚点和视图按钮，不能因为 SHM/modal 仍为 `UNAVAILABLE` 或程序未打开而让轨迹区域整块空白。该开机骨架只属于 UI 结构显示基线，不得作为机械坐标、WCS、A/C、tool 或 commanded/actual 真值被控制链消费。没有 native tool/geometry/program WCS actual 时，对应动态数值、刀柄线、真实 A/C 姿态和非当前 program WCS actual 必须保持隐藏，不能用默认 A/C=0、设置页表格、驱动 profile、旧缓存或 v3 后端计算补齐。样式和对象层级必须来自 v3 UI；但 G-code 解析、A/C 姿态、WCS/G53/RTCP、投影 fit、状态收敛和运行期刷新逻辑必须继续按本文和对应 `REQ-*` owner 实现，不得照搬 v3 后端逻辑、旧缓存、JSON/result、helper 或 fallback 路径。
轨迹区元素尺寸必须参考 v3 源码常量，不允许自定小号骨架：clip 区域为 `388x378`，clip 内 `lv_line` 对象铺满 clip；普通结构线宽 `1px`，MCS 原点线宽 `2px`，刀柄线宽 `5px`；红/绿锚点为 `7x7`，A/C 中心点为 `2x2`；标签与 XY/XZ/YZ/3D 按钮位置和尺寸按 v3 创建。A/C 开机骨架和运行态几何方向必须按机械坐标系投影：A 红线沿 MCS X 方向；C 青线始终平行于机械 YZ 平面，初始沿 MCS Z 方向，MCS 有效后按同帧 A 机械值围绕 A/X 轴旋转后再投影，不能产生 X 方向分量；A/C 直线总长度固定为 `80.0mm`，即以中心点向两端各投影 `40.0mm`。A 轴中心点必须来自 G53 机床坐标目标位置表的 A中心 native readback（X 取 A 轴机械中心约束，Y/Z 取 `g53_A_center_y/z`）；C 轴中心点必须来自 G53 机床坐标目标位置表的 C中心 native readback（X/Y 取 `g53_C_center_x/y`，Z 取 C 轴机械中心约束），不得用手工屏幕坐标、默认 0、设置页显示文本反推或 v3 后端缓存替代。不得用手工屏幕斜线、短线或与机械坐标系不匹配的替代线。
加工坐标系方向必须来自同一机械坐标投影基向量；A/C 为 0 的姿态下，当前 WCS 的 X/Y/Z 方向必须分别与 MCS X/Y/Z 方向一致，只允许原点位置因当前模态 WCS offset 改变，不允许用另一套手写屏幕坐标造成加工坐标系与机械坐标系方向不一致。非当前加工坐标系保持隐藏。
MCS/WCS 坐标系三轴线的世界长度固定为 v3 `RE_V3_TOOLPATH_POSE_MCS_AXIS_LEN = 40.0mm`；开机骨架、无 G-code 状态和运行态都只能投影这 40mm 轴线，不得为了填满轨迹区域把坐标系线段手工放大、拉长或改成屏幕像素固定长线。屏幕显示长度只能来自统一 projector、fit、手势缩放的结果。

Program Open/Load 成功并返回主页面时，主页面显示缓存必须先用当前页面对象重新渲染并刷新 resident cache，不能直接 blit 开机旧主页面缓存覆盖新黄色轨迹、红点、绿点或程序预览。普通页面切换仍可使用 resident cache，但任何会改变主页面可见内容的 Program Open、MDI 载入或同类动作必须标记主页面 cache dirty 并在返回主页面时回写新缓存。

状态层必须以同一帧 SHM UI 显示投影/`ReV3StatusView` 的有效机械坐标作为实际坐标绘图闸门，并以直连微内核/native 的 WCS、A/C geometry、RTCP 动作证明和 tool 状态作为结构参数。只要 native 运行内存/API 能读到 actual 坐标，即使尚未打开 G-code、程序为空或 machine off，State Publisher 也必须把当前模态、MCS、A/C 和速度/倍率投影到 `/dev/shm` 或 `/run` 这类 RAM 路径，UI 必须用真实状态覆盖开机骨架并显示状态层；不得因为尚未打开 G-code、黄色轨迹未解析、程序缓存为空或 Program Open 失败而隐藏 MCS/当前 WCS/A/C/坐标读数。该路径禁止读取硬盘文件、设置表、驱动表、profile 或旧缓存补坐标。`status_epoch == 0`、SHM stale、`mcs[0..2]` 非 finite 或仍为不可用哨兵值时，UI 仍必须保留包含当前 WCS 与 A/C 对象的开机结构骨架、红/绿屏幕锚点和 degraded/UNAVAILABLE 文字，但不得绘制种子图、假坐标数值、默认 0 位姿、真实 actual 刀柄线或真实 A/C 姿态。
UI init/relay 启动脚本不得在启动 `v5_lvgl_shell` 前阻塞等待 SHM 坐标、`cmd_mcs`、WCS、LinuxCNC 或其它 native readiness；这些状态只能在 shell 已接管 framebuffer 后以 degraded/UNAVAILABLE、后台 health 重启或后续 fresh 帧覆盖。开机/重启后屏幕停留在内核企鹅或空 framebuffer 等待坐标 ready 属于不合格启动路径。
UI relay 不得因为“坐标 ready”事件重启已经运行的 `v5_lvgl_shell`。坐标从 degraded 到 fresh 只能通过常规 status/native readback 刷新覆盖当前页面；反复 stop/start shell 会造成主页面坐标在有效值和 `--` 之间闪烁，属于不合格启动/刷新路径。
UI native readback 刷新必须按 owner 独立合并：WCS、modal/tool、RTCP 和安全状态互不作为彼此显示门禁。安全/急停 probe 失败时只能让安全状态 degraded，不得丢弃本轮已经读到的 WCS offset、当前模态、刀号、刀长或 RTCP actual。

黄色程序层只在 Program Open/Load 成功后增加。打开 G-code 前不得画黄色默认轨迹、示意弹簧、假两点路径或旧程序残影；打开 G-code 成功后，黄色轨迹可与同一帧状态一起投影。黄色轨迹解析失败、Broker 返回失败或程序帧过大导致 open 未确认时，只能显示状态层和明确原因，不得用旧黄色轨迹冒充当前程序。

红点不是 MCS ready 门禁的例外，不得为了让界面看起来完整而提前绘制默认 0 位姿、种子点或旧帧红点。程序几何可以在 Program Open/Load 阶段提前解析和缓存，但屏幕实际绘制必须等待 MCS ready 后再把程序几何与同一帧状态投影到同一个 scene。

`empty_status_scene`、假两点背景、初始种子坐标、默认 A/C=0 或把开机骨架当成真实 WCS/MCS 坐标都属于退役路径；缺少有效 MCS 时必须保留 v3 同款结构骨架、红/绿屏幕锚点和 degraded 文本，同时隐藏刀柄线、真实 A/C 姿态和其它强依赖动态真值，不能用伪造几何让界面看起来“已就绪”。
## 3. 绘图语义

绘图按状态分层推进。所有可见元素必须先在各自真实空间中生成几何点，再通过统一 projector 进入屏幕坐标；某个元素已经投影成屏幕点后，不得再单独按屏幕方向补偿来冒充几何正确。

### 3.1 红色 commanded 刀尖点

红点表示理论 commanded/microkernel 刀尖坐标，按 v3 `re_v3_toolpath_microkernel_kinematic_xyz` 语义由同帧 `cmd_mcs` 和当前显示刀长计算：

```text
red_tip.x = cmd_mcs[0]
red_tip.y = cmd_mcs[1]
red_tip.z = cmd_mcs[2] - tool_len

tool_len = display_tool_length_mm
           if display_tool_length_valid && finite(display_tool_length_mm)
           else 0.0
```

红点要求：

- `cmd_mcs[0..2]` 全部 finite，且本帧非 stale；否则隐藏。
- 不消费 `tool_holder_tip_xyz`、`tool_tip_xyz`、`actual_tip_ac_pose` 或绿色机械点。
- 只用于刀路可视化、诊断和跟随误差，不参与任何控制门禁。

### 3.2 绿色机械点和刀柄线

绿色点/刀柄上点必须直接等于同一帧 `mcs[0..2]` 的 actual 机械反馈点，且与机械坐标大字、轨迹绿色 actual 点使用同一消费端，不得另读参数表、旧缓存、UI 文本或 `cmd_mcs`。刀柄线从该机械坐标点向当前显示刀长方向绘制：同源世界坐标起点为 `mcs[0], mcs[1], mcs[2]`，终点为 `mcs[0], mcs[1], mcs[2] - display_tool_length_mm`，投影和手势 transform 必须与绿色点同帧一致；屏幕 3D 默认视角下表现为从刀柄上点向下的蓝色 5px 粗直线。刀柄线只做显示，不反推 RTCP 真值；缺少 fresh MCS 或有效刀长时隐藏刀柄线但保留绿色点。

### 3.3 黄色主刀路

黄色只表示进给/切削路径。G0/G53 不得进入黄色主刀路，可隐藏、灰线或只做诊断。程序模型是只读 G-code 几何；黄色程序几何只在 Program Open/Load 成功时解析、缓存或重建。之后只由同一帧内存里的机械坐标值驱动整张图的显示投影，不重写源程序、不重新创建黄色程序模型。

RTCP actual 未开启时，黄色轨迹保持当前程序/WCS 投入 MCS 后的非 RTCP 预览，不跟随 A/C 机械显示值旋转；这是操作员在 RTCP OFF 下看到的正确模型。RTCP actual 已开启且同 generation 的 native A/C 几何与坐标 SHM A/C 显示机械值有效时，黄色轨迹必须按坐标面板同源的 A/C 显示机械值，围绕直连微内核/native 的 G53 A/C 几何轴做显示投影刷新；不得使用另一套 raw、多圈、commanded、滤波或种子角度。该旋转只影响 UI 投影输出，不得使黄色程序模型或 fit 失效，不得修改 G-code、HAL、LinuxCNC actual 或运动门禁真值。

黄色主刀路是 Program Open/Load 成功后的增量层，不是 UI 轨迹视图的基础层。主页面进入、回零后、开机后或尚未打开程序时，MCS/WCS/A/C/坐标读数等状态层必须照常显示；黄色层保持空。Program Open/Load 成功并写入当前可见程序元数据后，黄色层才解析并显示当前 G-code。若 Program Open 失败，必须保留状态层、弹出明确失败原因，并清除或拒绝刷新当前黄色层，避免操作员误认为失败程序已经加载。

#### 3.3.1 大程序预览抽样

刀路预览可以为了板端内存和绘制开销对大程序做显示层抽样，但必须满足以下边界：

  - 抽样只影响黄色预览模型和屏幕绘制，不影响 G-code 文件、LinuxCNC 执行、运动门禁、坐标真值、WCS/RTCP/TLO actual 或任何控制链请求。
  - Program Open 的黄色程序层必须使用独立程序预览容量，参考 v3 `RE_V3_TOOLPATH_MAX_POINTS = 512`；不得把 `/dev/shm/v3_status_shm` 的 30ms 动态轨迹点数上限 `V5_STATUS_TRAJECTORY_POINT_COUNT = 16` 当作 G-code 程序预览容量。SHM 16 点只属于动态显示投影 ABI，不能导致 `cc.ngc` 这类 5 圈弹簧程序被截成 5 点或 16 点方形/折线。
  - 抽样不能完全静默。程序缓存、诊断文本或等价状态必须标出 `preview_decimated=true`、原始候选段数、保留点数、抽样策略、微位移阈值、分段数量和是否触达点数上限。只因 `RE_V3_TOOLPATH_MAX_POINTS` 满而停止追加时，必须显示为预览降级，不得冒充完整路径。
- 抽样策略必须优先保留首尾点、切削段端点、WCS/模态变化点、A/C 大跳边界、明显拐点和 fit 所需包围盒关键点。微位移过滤只允许删除显示上不可分辨的连续小段，不得删除会改变视觉拓扑、WCS 归属、A/C wrap 边界或黄色/G0/G53 分类的点。
- 大程序必须按显示层 LOD/分段抽样，不能只依赖固定 `RE_V3_TOOLPATH_MAX_POINTS` 数组原地 compact。触达容量上限时，不得把最后一个程序点塞入当前连续折线尾部形成跨大段的终点长拉线；必须在截断/抽样断点处切分 segment，或者显式隐藏不可证明连续性的尾部连接。
- 点数上限不能作为功能正确性的唯一保护。提高 `RE_V3_TOOLPATH_MAX_POINTS` 前，必须先证明静态黄线投影和 `lv_line_set_points()` 不在 30ms 坐标/轨迹动态路径执行，否则放宽点数只会放大 UI 主循环卡顿。
- 如果无法证明抽样后的预览仍保留关键几何和分类边界，UI 必须显示 degraded/preview incomplete，而不是输出看似正常的完整轨迹。

### 3.4 WCS/MCS 与 fit

- 所有图形基础框架是 MCS。
- MCS 原点和轴线可见，但不显示 `MCS`、`MX/MY/MZ` 文字标签。
- MCS、当前 WCS、程序 WCS 的 X/Y/Z 轴线世界长度固定 40mm。
- A 标签必须显示在 A 轴端点，C 标签必须显示在 C 轴端点。
- A/B/C 中心点用 2px 中心标记，只表示几何中心，不替代轴线或刀尖点；A/B/C 轴中心 marker 必须按同一份 scene 几何，不得用屏幕红线、标签位置或 UI 像素坐标反推。设置页 G53 表的显示顺序必须为 A 中心、B 中心、C 中心、对刀仪、5 方向检测仪；B 中心位于 A/C 中间，坐标体系参考 A/C 中心，B 中心 X/Z 来自 `G53_B_X/G53_B_Z` 或等价 G53/native geometry readback，B 中心 Y 来自当前模态加工坐标系 Y 偏移值。
- pending WCS 只允许作为主页面 WCS 按钮/命令已发出的短时视觉提示；actual 为 G54 但刀路底部仍显示 `WCS pending: G55` 是错误状态。G54 OFS `X+030.000 Y+020.000 Z-050.000` 时，G54 原点就是 `(30,20,-50)`。
- 开机默认视角必须是 3D，`view_3d` 进入 3D；3D 视角投影 owner 是 `lvgl_app/src/v3_ui_toolpath_3d_view.c`，相机方向为 X+ Y- Z+ 的低角度正交视角，正交投影基向量必须与 v3 一致：screen right = `(0.7071067811865476, 0.7071067811865476, 0.0)`，screen up = `(-0.4082482904638631, 0.4082482904638631, 0.8164965809277261)`。在默认 3D 屏幕上 X 轴应向右下，Y 轴应向右上，Z 轴应竖直向上；v5 不得用近似经验公式、按钮选中态、手工屏幕坐标或其它 3D 斜投影替代这组向量，也不得让开机骨架使用另一套手写斜率。
- fit 只允许在两类入口自动重建：开机/页面创建后首次读到有效 SHM UI 显示投影和必要 native actual 并建立 WCS/MCS、A/C 状态层结构时；Program Open/Load 成功并出现黄色 G-code 程序线时。运行、暂停、WCS/RTCP/TLO、A/C 姿态、红点/绿点移动不得触发重新 fit 或完整场景重建。
- 最终 fit 使用 `RE_V3_TOOLPATH_WINDOW_FIT_RATIO = 0.80`，候选包含黄色刀路、WCS/MCS、A/C 线、红点、绿点和刀柄线；图形安全区必须排除左侧模态文字、底部状态文字和右侧视角按钮。
- 手势显示态 owner 是 `lvgl_app/src/v3_ui_toolpath_gesture_view.c`；`manual_scale`、`manual_screen_rotate_deg`、`manual_wcs_x_rotate_deg`、`manual_wcs_z_rotate_deg` 只影响显示 transform。按 XY/XZ/YZ/3D 视角按钮只回到按钮对应的默认视角投影，不得重建 fit、WCS/MCS、A/C 轴线或黄色程序模型。

### 3.5 RTCP 与 A/C

- RTCP 图层只按 G-code/M-code 控制后的微内核 runtime modal actual 显示；UI 侧必须通过状态 provider 直连读取 native modal/RTCP actual。`rtcp_actual_enabled` 或等价布尔字段只能是该 modal actual 的派生显示投影。request、dirty、按钮状态、SHM `runtime_modal_text`、设置文件和非同源 helper/cache 只能显示 pending 或诊断，不得提前切 active；State Publisher 内部上一帧有效 RTCP 缓存只允许作为运行期回读优化，开关/异常/无有效缓存时必须重读。
- A/C 姿态源必须来自同一 generation 中已经收敛的唯一 UI A/C 显示投影。该投影只能以 SHM UI 显示投影的 `mcs[3]` / `mcs[4]` 或等价编码器反馈机械坐标为动态输入，以直连微内核/native 的 count-domain/unwrap/degraded 状态为解释输入，且只能在一个 owner 中决策一次；坐标面板、刀路视图和 A/C 标签必须复用同一结果。等效角计算（多圈累计折叠为 [0, 360) 浮点数）必须在数据刚进入 UI 进程的状态收敛模块（如 `v3_ui_toolpath_scene.c`）中统一计算一次，产出纯浮点数并存入当前帧 scene 模型中。大字坐标控件仅读取此浮点数并格式化为 `%+.3f` 字符串进行文字显示；3D 渲染层必须直接读取同一个浮点数进行矩阵相乘，绝对禁止通过获取 Label 文本（如 `lv_label_get_text`）反向解析来驱动图形，也绝对禁止在渲染层重复进行等效角计算。
- `rotary_phase` 只有在对应 count-domain/profile/unwrap 有效且错误码为 0 时才能覆盖操作员主显示值；无效时不得把有限默认值 `0.0` 当成真实 A/C 显示值。
- 刀路视图不得绕过坐标面板显示投影直接读取另一套 A/C 角度。若刀路需要 raw、多圈或 `tool_tip_ac_pose` 做诊断，必须单独标注为诊断字段，不得混入主 A/C 显示链路。
- A 轴红线表示物理 A 旋转轴，不随 A/C 角度转动。
- C 轴蓝线以 A 轴为旋转中心，按同一条 UI A/C 显示投影中的 A 值旋转；不得按另一套 C 值、raw 多圈值或 commanded 值摆动 C 轴线。
- C 蓝线中点必须是 `(G53_C_X, G53_C_Y, 当前模态加工坐标系 Z 偏移值)`。显示层 A/C 投影变换必须使用 `ac_center_x = G53_C_X`、`ac_center_z = 当前模态加工坐标系 Z 偏移值`；不得把 C 中心 Z 固定为 `G53_A_Z`、UI 默认值或自建参数表值。
- MCS ready 后的首次有效绘图中，A 红线中心必须是 `(当前模态加工坐标系 X 偏移值, G53_A_Y, G53_A_Z)`。A 中心 X 来自当前模态加工坐标系 X 偏移值，不得写入固定 G53 几何常量或自建参数表。C 蓝线的空间位置由直连微内核/native 的 A/C 几何生成，初始方向与 MCS Z 平行，后续只能按同一条 UI A/C 显示投影中的 A 值围绕 A 轴红线做空间旋转。开机首帧没有有效机械坐标时不得提前绘制 A/C 轴、MCS/WCS 坐标系或红点。
- C 机械值只表示绕自身轴的相位/诊断，不得改变 C 蓝线轴线方向、端点方向或屏幕方向。XY/XZ/YZ/3D 四个实际绘图视角都只是这套空间位置关系的投影快照；XY/XZ/YZ/3D 四个实际绘图视角只是同一份 MCS world scene 的不同角度快照，不能按视角重新创建或重算 A/C 几何关系。
- 屏幕红线和 UI 像素坐标不得作为 A/C 线显示中心替代。A/C 几何只允许来自直连微内核/native 几何 provider，没有有效 native 几何时不得从设置页表格内存或旧 SHM 字段拼出显示几何；运动中的 A/C 姿态变化只由同一 SHM UI 显示帧的 A/C 机械坐标驱动。
- XY 视图中只有同一条 UI A/C 显示投影中的 A 值为 0 度等效姿态时，C 轴可显示为点；A 显示值非 0 时不得显示这个退化点，C 轴必须按同一份三维 scene 的正常投影线随 A 姿态变化。XZ/YZ 视图始终按实际 C 轴三维线段显示，不能为 A/C 线、黄色路径、WCS/MCS 轴线或刀柄/刀尖创建另一套独立绘图逻辑。
- `cmd_mcs[3]` / `cmd_mcs[4]` 只用于跟随误差和诊断。
- 连续旋转解包必须有明确策略或 manifest；unknown 时不猜，不把 `G90 C0` 自动解释成 `C360`。
- A/C 或程序点相邻样本出现接近 360 度整倍数的大跨度、unwrap 边界或等价屏幕投影大跳时，只允许在显示线段上断开连接，形成多个绘制 segment；不得修改程序点、不得重写 A/C actual/commanded、不得用滤波后的姿态替代坐标面板同源 A/C 显示机械值。断线原因应进入诊断或测试可见的 segment metadata，避免后续误判为丢点。

### 3.6 SHM 视觉稳定

SHM 结构错误继续 fail-closed；短暂 seqlock 冲突允许显示层使用上一帧 grace：

| 状态 | UI 行为 | 控制门禁 |
| --- | --- | --- |
| `last_good_age_ms <= 300` | 继续显示上一帧并标 `SHM stale` | 不得复用 stale epoch 放行 |
| `300 < age <= 500` | 隐藏强依赖动态图层，保留静态程序刀路和 degraded | fail-closed |
| `age > 500` | 显示 `SHM unavailable`，动态图层隐藏 | fail-closed |

### 3.7 显示刷新节流

刀路 view 可以做显示层 FPS 自适应节流，但不得影响 SHM 读取、状态应用、命令 freshness、Broker 请求、State Publisher 采样或任何安全/运动门禁。

- UI 刀路热路径分为动态层和静态/结构层。动态层目标是 30ms UI 显示投影刷新；动态层只包含红色 commanded/theory 点、绿色编码器反馈机械点、刀柄/刀尖动态线、模态文字、转速/进给/倍率显示、必要跟随误差或运行中当前位置提示。
- 静态/结构层不得固定 30ms 重建。WCS/MCS 原点和轴线、A/C 轴线和中心点、标签锚点等状态层对象在主页面创建/开机首帧先按 v3 视觉骨架显示；首次读到有效 SHM UI 显示投影和必要 native actual 后，只用真实状态覆盖这套对象的投影，不另建第二套对象。静态 G-code 黄线只在 Program Open/Load 成功时解析、缓存或重建；screen fit 只在首次有效状态层建立和黄色程序线出现时自动重建。视角、手势和窗口变化只能更新显示 transform 或裁剪后的投影输出，不得触发 fit、状态层结构或 G-code 重建。RTCP actual 开启后，A/C 显示机械值变化只允许按已缓存黄色程序点驱动投影刷新；RTCP OFF 时 A/C 变化不得触发黄色轨迹重投影。
- 主页面轨迹区域必须支持 v3 操作员视图同类手势：raw evdev 层至少收集两个同帧触点，触点均在轨迹图形区内时由刀路 view 消费；双指距离变化只更新显示缩放，双指中点移动只更新显示平移，双指角度变化只更新屏幕显示旋转或在当前 v5 投影不支持旋转时保持无害忽略。手势 transform 必须同帧作用到黄色 G-code 进给线、红色 commanded 点、绿色 actual/holder 点、MCS/WCS 原点和轴线、A/C 几何及标签；不得只缩放黄线或只移动动态点。手势不得读取 G-code 文件、不得重算 screen fit、不得重建状态层结构、不得用设置表/profile/旧缓存补真值，离开轨迹图形区或手指释放时只结束手势状态，不得清空已确认图形。
- 状态层已经由有效 SHM UI 显示投影和必要 native 状态绘出后，短暂 `status_stale`、单帧 `mcs_ready=false`、单帧投影失败或单帧 A/C geometry 无效不得调用清空函数把上一帧线对象置空。UI 只能保留上一帧已确认图形并更新正式 degraded 状态，等待下一帧有效状态覆盖；不得在刀路画布底部显示 `diag:`、`rtcp--`、`geom--`、`holder--` 等内部诊断 token。
- 前台状态文字遵循同一保留规则：WCS offset 摘要、A/C 几何摘要、runtime modal/RTCP 文本和标签锚点只能由有效 SHM 显示投影或 native 状态 provider 覆盖；开机首帧、单帧缺字段、WCS offset 无效/非有限值、WCS 全表未 ready、RTCP geometry 未 ready 或 runtime modal 未 ready 时，必须保留上一帧有效文字或保持空白，不得显示模板值、默认 0、`A/C geom --` 或内部诊断占位冒充真实数据。
- 清空状态层只允许发生在明确的对象销毁，或长期 SHM 显示投影不可用并进入明确 fail-closed 空场景且保留开机结构骨架的路径；页面重新创建后必须立即显示 v3 结构骨架，再等待首次有效 SHM UI 显示投影和必要 native actual 按开机规则覆盖 WCS/MCS、A/C 轴线和 fit。更换程序只允许清理黄色轨迹，不得清空或重建状态层。清空状态层不得出现在 30ms 普通刷新路径里。
- G-code 运行时，主页面无遮挡的坐标、红点/绿点、当前刀路位置和必要运动 overlay 目标是 30ms 坐标/轨迹变化显示；除这些可见运行显示和 `REQ-GCODE-RUN-HOT-PATH` 指定的安全 actual 外，刀路层不得触发 G-code 重解析、全量 fit、全量 scene 重建、诊断落盘、慢状态校验或后台维护刷新。
- 静态 G-code 黄线必须在 Program Open/Load 后台任务完成并写入当前程序元数据后，由主页面后台缓存生成；程序页双击前台成功路径只做本地文件名/路径存在性校验，写入当前程序元数据并立即切回主页面，且不得使用 `program_open_cc` 中介，不得同步等待运行副本生成、LinuxCNC `program_open`、黄线解析、投影缓存、fit、MDI 全量读取或其它大 G-code 预览缓存完成。黄色程序点必须保留打开程序自身解析出的 G54-G59.3 模态；显示时按该程序点所属 WCS 的 native/interpreter offset 把程序 XYZ 放入机械坐标显示层，再与 MCS/WCS/A/C/刀柄对象一起投影，不得把运行时 active WCS 二次套到已带 WCS 模态的程序点上。没有对应 WCS offset 的点不得用默认 G54、UI 缓存或当前 active WCS 乱画。在大型 G-code 文件解析和刀路加载期间，UI 应当显示一个加载指示器（如 LVGL Spinner 或 Loading 进度动画），解析及首次 fit 缓存完成后自动将其隐藏，以提供对大型文件加载进度的良好交互反馈。30ms SHM 显示路径不得重新解析 G-code、不得重新读取程序文件、不得全量重建黄色路径模型。屏幕 fit 只在首次有效状态层建立和黄色程序线出现时自动重建；后续同一帧 SHM 显示投影 + native 状态只驱动已有 WCS/MCS、A/C 轴线和黄色程序线投影。RTCP actual 开启且 A/C 显示机械值变化时，只允许按已缓存程序点刷新投影和必要的 `lv_line_set_points()`；RTCP OFF、无 A/C 投影变化或缓存命中时不得全量重投影黄线、不得全量重写黄色 `lv_line_set_points()`。
- A/C 坐标显示和刀路 A/C 姿态的 30ms 输入固定为 SHM UI 显示投影中的编码器反馈机械坐标；unwrap/count-domain/display phase 解释从微内核/native 直连读取，不得作为独立 SHM 字段扩展。若只更新 X/Y/Z 而不同步 A/C 坐标，坐标面板和刀路视图会继续显示旧 A/C 值，这属于不合格的低频 full-frame 依赖。
- 无 G-code 或程序层为空时，WCS offset、holder、A/C geometry 等内存字段变化只能作为投影参数或诊断文本输入，不能成为状态层结构或 fit 的重建签名。坐标微抖、对刀点动、单帧 SHM 数值抖动只能更新动态点或后台诊断状态，不得触发全量 scene/signature 重建。
- 机床空闲、无触摸/手势、程序模型未变化时，静态/结构层必须复用缓存；动态层可按机械坐标值变化或 30ms 目标刷新。没有动态变化时，允许跳过实际 LVGL 对象重写，但不得跳过 SHM fresh 坐标进入 UI model。
- 检测到触摸/手势、视角切换、机械坐标值变化、RTCP actual gate 显示变化、红点/绿点动态变化或 degraded 状态变化时，只刷新投影输出、动态 overlay 或状态文字；只有 Program Open/Load 成功能重建黄色程序线并生成对应 fit，只有开机/页面创建后的首次有效内存 actual 能建立状态层结构并生成第一次 fit。
- 节流只跳过重复绘制，不跳过 SHM fresh 状态进入 UI model；动态图层可按状态变化触发刷新，不能因为画面空闲而发布旧 actual 或隐藏 stale/fail-closed。
- 节流状态应可诊断，例如记录当前 toolpath refresh mode、目标刷新间隔、最近一次触摸/运动/scene 变化原因、静态 cache hit/miss、黄线点数、program WCS/A/C 投影刷新次数和本帧是否调用了 `lv_line_set_points()`。

### 3.8 与 State Publisher / SHM 热路径的联动

State Publisher 性能治理不能替代 UI 刀路热路径收敛。即使 `mmap.flush()`、进程 nice 或 Python 打包开销被优化，UI 仍必须满足本文件的动态/静态分层要求。

- RTCP actual 缓存是 native/provider 内部的同源回读优化，不是 UI 真源。运行 G-code 时可复用上一帧有效 RTCP modal actual；RTCP 开关、状态异常、无有效缓存、缓存过期或读回失败时必须重新采样微内核 modal owner，并返回 invalid/unknown/degraded，不得沿用缓存冒充正常。RTCP native 状态源以 `REQ-RTCP-NATIVE-STATUS-SOURCE` 为准，本文不重复定义 helper/HAL pin/adapter 形态。
- `mmap.flush()` 是优先 A/B 排查点；板端证据支持关闭时必须删除旧路径并补当前 canonical 实现，不得保留配置开关或默认关闭分支。不得删除 seqlock、dual-page header、CRC、retry 或 degraded/fail-closed 逻辑。关闭 steady-state flush 前后必须采集 State Publisher 返回样本的 `timings_ns`、UI stale/degraded 计数、SHM epoch/CRC 异常、坐标刷新和 Broker 急停响应；不得要求周期性 status/perf JSON 写盘。
- `RE_V3_STATE_PUBLISHER_NICE` 可先从 `10` A/B 到 `0`。只有板端证据证明 LinuxCNC/HAL/EtherCAT 实时线程无新增 overrun、Broker 急停/停止延迟不恶化、每核 CPU 分布合理时，才允许讨论 `-2`。nice/affinity 不能作为掩盖 UI 30ms 全量重绘的方案。
- Python struct 打包或 native publisher 属于结构优化。只有完成 flush/nice A/B、UI 静态/动态分层、大程序 LOD/分段缓存后，仍有板端证据证明 publisher 打包是主瓶颈，才进入 SHM hot/cold ABI 或 native publisher 任务。
- 性能结论必须同时包含 publisher timing、UI perf/flush area、toolpath cache hit/miss、LVGL line rewrite 次数、每核 CPU、Broker ready/急停响应和最终安全态；不能只凭截图帧率或单个耗时字段宣称完成。

## 4. Owner 文件

| 职责 | owner |
| --- | --- |
| LVGL 对象、图层顺序、事件入口、刷新 glue | `lvgl_app/src/v3_ui_toolpath_view.c` |
| 公开 API | `lvgl_app/include/v3_ui_toolpath_view.h` |
| 旧入口兼容壳 | 禁止；不得恢复 `lvgl_app/src/v3_ui_toolpath.c` 空壳 |
| G-code 读取、词法解析、主循环 | `lvgl_app/src/v3_ui_toolpath_parser.c` |
| A/C raw target 解析与大跳断线诊断 | `lvgl_app/src/v3_ui_toolpath_parser_target.c`；不得在 UI parser 中重新实现 rotary unwrap、RTCP 五轴变换或连续角平铺 |
| 点缓存、线段、XY 圆弧采样 | `lvgl_app/src/v3_ui_toolpath_path_builder.c` |
| 大程序预览抽样、关键点保留和降级诊断 | `lvgl_app/src/v3_ui_toolpath_parser.c`、`lvgl_app/src/v3_ui_toolpath_path_builder.c` |
| 显示 transform、WCS offset、A/C/manual 程序点变换 | `lvgl_app/src/v3_ui_toolpath_transform.c` |
| 屏幕 projector、fit-lock、公开投影 API | `lvgl_app/src/v3_ui_toolpath_projector.c` |
| 3D 视角投影向量 | `lvgl_app/src/v3_ui_toolpath_3d_view.c` |
| scene 状态收敛 | `lvgl_app/src/v3_ui_toolpath_scene.c` |
| WCS/MCS 与投影 | `lvgl_app/src/v3_ui_toolpath_projection.c` |
| fit-lock 与 80% 视窗 | `lvgl_app/src/v3_ui_toolpath_screen_fit.c` |
| A/C 姿态、轴线、程序点姿态 | `lvgl_app/src/v3_ui_toolpath_pose.c` |
| 刀柄线和 overlay 模型 | `lvgl_app/src/v3_ui_toolpath_overlay_model.c` |
| 状态文字格式化与离线诊断签名 | `lvgl_app/src/v3_ui_toolpath_diagnostics.c` |
| 手势显示态 | `lvgl_app/src/v3_ui_toolpath_gesture_view.c` |
| 显示刷新节流和重绘决策 | `lvgl_app/src/v3_ui_toolpath_view.c`、`lvgl_app/src/v3_ui_toolpath_view_refresh.inc` |
| 通用纯数学 helper | `lvgl_app/src/v3_ui_toolpath_math.c` |

`v3_ui_toolpath_view.c` 只保留 UI 对象、图层顺序、事件入口和 owner 调用 glue。新增 scene/projection/screen-fit/pose/overlay/diagnostic/gesture 逻辑必须落到对应 owner。

## 5. 图层顺序

实际管理 LVGL 对象层级的 owner 必须有集中顺序表或等价机制，测试输出必须能验证。

| 顺序 | 图层 | 含义 |
| --- | --- | --- |
| 0 | `base_background` | 背景 |
| 10 | `mcs_axes` | MCS 轴线和原点锚点 |
| 20 | `ac_axis_a` | A 轴 |
| 30 | `ac_axis_c` | C 轴 |
| 40 | `workpiece_aux` | 工件辅助线 |
| 50 | `wcs_origin_axes` | 当前/程序 WCS |
| 60 | `feed_path_yellow` | 进给/切削路径 |
| 80 | `microkernel_kinematic_point` | 红色 commanded 刀尖点 |
| 90 | `tool_holder_line` | 刀柄线和绿色机械点 |
| 110 | `labels_status` | 前台状态标签；不得显示内部诊断 token |

退役对象不得再出现在测试证据中：`current_segment_marker`、`actual_tip_marker`、`actual_tip` 或等价白色实际刀尖点。

## 6. 实现检查点

实现或审查时按以下顺序检查：

1. 一帧状态输入是否先收敛为 scene，再刷新所有图层。
2. G0/G53 是否被排除出黄色主刀路。
3. WCS 切换是否只更新 display transform，不 reparse G-code。
4. RTCP 是否由 G-code/M-code 控制微内核 runtime modal，并由 UI/provider 直连微内核/native modal actual 状态显示；request/dirty/非同源 helper/cache/旧 SHM 字段是否只显示 pending 或诊断；上一帧有效 RTCP 缓存是否在开关、异常、无有效缓存或缓存过期时强制重读。
5. A/C raw、unwrapped、display phase 是否分离，unknown 策略不自动解包。
6. fit-lock 是否只在开机首次有效 SHM 状态层和新程序黄线出现时更新；视角、手势、窗口、RTCP、WCS/TLO、A/C 或坐标 actual 变化不得更新 fit。
7. seqlock 短冲突是否只影响显示 grace，不影响控制 epoch。
8. 30ms 坐标/轨迹动态路径是否只更新动态层和投影输出；静态黄线程序模型、WCS/MCS、program WCS、A/C 轴线和 fit candidate 是否不会因任何运行状态被重建。
9. 无 G-code/空程序状态下，WCS/holder/A/C 浮点微抖是否只影响投影/诊断，不会改变结构 signature 并触发全量 scene 或 fit 重建。
10. 大程序预览抽样是否保留关键点、按 LOD/segment 断开不可证明连续的连接、标出 decimated 诊断，并且不修改 G-code 或运动真值。
11. A/C wrap、大角度和屏幕大跳是否只断开显示线段，不改程序点和 actual/commanded。
12. FPS 节流是否只跳过显示重绘，不跳过 SHM fresh 状态和控制 freshness。
13. 若同时调整 State Publisher `mmap.flush()` 或 nice，是否有 A/B 证据证明 SHM 一致性、Broker 急停和实时线程没有退化。
14. 新逻辑是否落到对应 owner，而不是继续堆到大 view 文件。

## 7. 本地门禁

源码修改后至少运行：

```powershell
python -m pytest lvgl_app\tests\test_toolpath_ui_contract.py lvgl_app\tests\test_state_shm_reader_contract.py lvgl_app\tests\test_state_shm_runtime_init_contract.py -q
python -m pytest lvgl_app\tests\test_toolpath_rotary_microkernel_owner_contract.py lvgl_app\tests\test_state_shm_stage8_field_batches.py -q
git diff --check
```

必须检查：黄色 G0/G53 为 0、图层顺序可验证、WCS 切换不重解析、stale SHM 正确 degraded、RTCP pending/rejected 不提前切图、A/C 连续角不拉线、大程序抽样有诊断且保留关键点、FPS 节流不影响 SHM freshness、当前刀号/刀长正确、fit 只在开机首个有效状态层和黄色程序线出现时自动重建且不压文字。

涉及性能热路径时还必须检查：30ms 坐标路径不重建静态黄线程序模型、不重算全量 fit candidate、不因运行状态重写黄色 `lv_line_set_points()`；无 G-code 微抖不改变结构 signature；大程序抽样不会产生跨截断终点长拉线；`mmap.flush()`/nice 只作为配置 A/B 证据，不作为 UI 刀路分层验收替代。

## 8. 板端闭环

只改本文档不执行板端。修改 toolpath 源码后，声明 `fixed`、`done`、`verified` 或 `board_verified` 必须按 `AGENTS.md` 执行板端可见功能规则。

最低板端证据：

- 产品 UI 构建产物和部署身份。
- 原始 UI/operator 路径截图，截图必须来自 remote relay/LVGL flush framebuffer 或同等级已验证颜色路径。
- 截图中能看到黄色主刀路、红色 commanded 刀尖点、绿色机械点、WCS/MCS、A/C 或 RTCP 相关显示。
- 若涉及性能热路径，必须同时采集 publisher timing、UI perf/flush area、toolpath cache hit/miss、LVGL line rewrite 次数、每核 CPU、Broker ready/急停响应；G-code 运行中不得出现 publisher/刀路互相争抢导致的明显 5Hz 卡顿。
- 若涉及运动，跑 `nc/cc.ngc` 黄金运动闭环，并保留截图、对齐分析 JSON、黄金测试结果 JSON 和最终安全停机状态。

cc.ngc 对齐口径：机器空间误差不超过 `1.0mm`，屏幕投影误差不超过 `5px`。该对齐限额为静态几何与投影计算公式的正确性校验门禁（防软件 Bug 导致轨迹长期漂移），非运行时重绘的实时像素精度限制；不得通过 UI 吸附、样本中位偏移、手工估计或修改 G-code 让它看起来重合。

## 9. 一票否决

- 修改 G-code 来修显示。
- G0/G53 进入黄色主刀路。
- UI 自算 RTCP 真值并当 actual。
- RTCP request、dirty 或按钮状态刚发出就切 active 图层。
- A/C 显示归一化写回 HAL、INI、驱动或 G-code。
- unknown 连续策略下擅自 unwrap。
- RTCP OFF 时黄色轨迹跟随 A/C 机械显示值旋转，或 RTCP ON 时黄色轨迹不围绕同源 A/C 几何轴随坐标面板 A/C 显示机械值旋转。
- display phase、滤波姿态、raw 多圈值或 commanded 值被当成黄色轨迹主显示几何。
- stale-cache 的 `status_epoch` 用于控制请求。
- 大程序预览抽样无诊断、无关键点保留证明，或把抽样结果冒充完整轨迹。
- 大程序触达点数上限后把终点塞回当前连续折线，产生跨截断长拉线。
- 30ms 坐标路径全量重投影静态黄线、WCS/MCS、program WCS、A/C 静态几何或全量重写黄色 `lv_line_set_points()`。
- 用 `mmap.flush()`/nice 调参结果替代 UI 刀路动态/静态分层修复。
- FPS 节流影响 SHM 读取、状态应用、命令 freshness 或安全/运动门禁。
- 恢复旧 JSON/FIFO/native daemon fallback。
- 在 `v3_page_main.c` 或 `v3_ui_toolpath_view.c` 继续堆几何、投影、fit、诊断或手势状态机。

## 10. 关闭条件

关闭 UI 刀路问题时必须说明：

- 修改范围：文档、源码、板端和 G-code 是否被改动。
- 图层证据：黄色主刀路、红点、绿点、WCS/MCS、A/C、RTCP。
- 状态来源：同帧 typed SHM/`ReV3StatusView` actual、program model、WCS/TLO/runtime modal RTCP 字段和 degraded 分支；RTCP request/dirty/非同源 helper/cache 是否只作为 pending 或诊断；State Publisher 内部 RTCP 缓存是否有开关/异常/无效/过期强制重读证据。
- 性能证据：动态层 30ms、静态层 cache hit/miss、大程序 LOD/segment、无 G-code 微抖 signature 稳定性、publisher flush/nice A/B 结果若本轮涉及。
- 验证结果：本地 pytest/smoke/截图；若上板，还要有构建产物、部署、截图、日志和最终安全态。
- 未闭合事项只记录真实缺口，不写过程日志或聊天摘录。
