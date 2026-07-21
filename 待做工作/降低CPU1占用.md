# 降低 CPU1 占用（全运动模型）作业指导书

状态：`board_deployed_operator_test_running`（C01-C04 已实现、ARM/QEMU 与板端非运动闭环通过；用户已明确授权通过板端抓屏与pointer/mouse工具执行当前模型Golden Motion）

用途：交给 AI 按卡修改代码、完成本地构建和板端部署；用户负责最终真实加工体验确认。每次只执行一张卡，当前卡未通过不得进入下一张。

产品语义以以下 owner 为准，本文只规定整改顺序，不另建第二套需求：

- `功能/3-4UI刀路绘图顺序与状态稳定方案.md`
- `功能/0-1开机参数入内存.md`
- `功能/0-4共享内存.md`
- `功能/需求真源索引.md`
- `功能/自动闭环测试方式.md`

## 1. 当前已确认的问题

2026-07-20 板端运行 `cc-bc.ngc`、RTCP ON、一个 remote stream 时，5 秒窗口实测：

| CPU1 任务 | 占用 |
|---|---:|
| `v5_lvgl_shell` | 42.8% |
| `v5_remote_ui_relay` | 21.5% |
| Position Publisher | 12.7% |
| Native HAL Owner | 4.4% |
| WCS Publisher | 4.0% |
| State Publisher | 3.8% |
| `milltask` | 2.4% |
| CPU1 总占用 | 97.0% |

同一窗口还确认：

- `PROGRAM_STATUS=RUNNING`，`motion_queue=20`。
- G-code 文件句柄位置保持在 `2601`，5 秒内没有继续读文件。
- `fit_generation=92` 保持不变，不是持续 auto-fit。
- `build_count`、`project_count` 随位置帧持续增长。
- 当前主因是 RTCP/active-model 场景重复全量投影、UI 大面积重画和 remote payload build，不是该窗口内的磁盘读取。

以上数值只作为本轮基线。修改后必须重新采集，不得把旧数值冒充新证据。

### 1.1 基于当前代码的架构结论

| 提议 | 当前代码事实 | 本文处理 |
|---|---|---|
| 通用 `memcmp` 会被旋转姿态击穿 | 成立。`V5ProgramSceneModel` 同时保存 topology、中心和本帧 `sin/cos`；`v5_program_scene_static_key_same()` 比较整份结构。AC/BC 的 `resolve()` 都会按当前旋转轴重写 `sin/cos` | C01 必须从共享模型接口拆开 topology 与 pose，禁止针对 BC 写特例 |
| 已对 AC/BC/AB/三轴全部实现 | 不成立。当前 motion registry 和 scene `model_ops[]` 只登记 `XYZAC_TRT`、`XYZBC_TRT`；现有 descriptor 还强制两个旋转轴，不能表达纯 XYZ | 当前板端功能保证范围只能是所有**已登记且有独立 scene ops 的模型**，即 AC/BC；C01 同时建立未来模型的强制接入合同。AB、XYZ、双摆头等必须先有 descriptor、独立 projector/kinematics branch 和测试，才能进入保证集合 |
| 姿态变化后 `build_count` 变成 0 | 表述错误。该计数是累计值，首次静态构建后至少为 1 | 验收改为：稳定 topology 下，首帧后 `build_count` **不再增长** |
| 全部动态更新都可降为 `<10` 点、O(1) | 只对刀尖、刀柄、轴线、中心和 marker 成立。owner 要求 RTCP ON 时黄色程序轨迹随同源模型姿态变化；可见轨迹发生像素变化时仍需变换/投影所有保留点，或者由具备矩阵变换能力的 renderer 等价处理 | 静态解析/拓扑构建从热路径移除；marker 路径 O(1)。黄色程序层采用一次融合矩阵和一次 `O(N_visible)` 遍历，并用安全的屏幕位移上界跳过不可见变化。禁止通过冻结黄线伪造 O(1) |
| UI 目前完全没有 dirty 机制 | 不成立。当前 scene 已有一个 dirty 包围框，UI 会对旧、新区域分别 `lv_obj_invalidate_area()`；问题是单一包围框可能覆盖很大区域，且对象重写仍可能过宽 | C02 在现有机制上增加图层分类和有界局部失效，不重造第二套 dirty 系统 |
| Relay 尚无 33ms 合并和 Latest-Wins | 不成立。当前 `SharedDirtyPayloadProducer` 已有 coalesce、共享 payload、有界历史、backpressure 和慢客户端 repair | C03 先用计数证明现有实现是否闭环，只修仍发生的重复 build、过大 dirty 或积压追赶，不重复实现已有功能 |
| WinRemote 必须“无损30Hz”且拥塞时 Latest-Wins | 两个要求不能同时作为逐帧硬合同。当前 owner 允许 CPU1/网络过载时合并或丢弃陈旧显示帧，但必须保留最新可恢复画面、协议base连续性和远程输入可用 | 正常负载目标为不低于30Hz的最新增量观察；拥塞时允许丢中间显示帧，不允许像素内容算错、旧帧回放、输入丢失或靠频繁full-frame修复 |
| Position和State分别重复读取同一批HAL，合计浪费24.5% | 不成立。当前Python Position Publisher是坐标/倍率的HAL sampler和256字节Position writer；C State Publisher通过mmap读取该Position块，再合并RTCP/WCS/G53/modal等resident native块和scene，不会再次逐pin采同一批Position HAL数据 | 现架构已经是“Position采样一次、State消费一次”。C04只评估把Python Position等价替换为C Position writer，禁止以“消除重复HAL采样”为理由直接揉并两个生命周期 |
| 用 C 合并 Publisher 可直接减少 12.7% | 只能作为上限假设。HAL采样、显示投影、抗抖、CRC、SeqLock、共享内存发布和总线低频采样仍必须执行；删除Python不等于删除全部12.7%工作 | C04必须先profile，再判断CPython/对象构造占比；不得未经同窗口实测承诺固定收益 |
| Program Runtime尚未把G-code读入RAM | 不成立。当前`v5_program_runtime_open_file()`已在Program Open时把最多2MiB原文件完整读入匿名mmap，生成preview/hash/first-point；但`prepare_start()`仍把原始`source_path`交给LinuxCNC `Set Open` | UI resident副本已经存在，却不是LinuxCNC解释器执行源。执行预缓冲必须单独改解释器/lookahead/queue-feed边界，不能只改`v5_program_runtime.c`或再造临时运行副本 |
| `dirty_events=42854`、payload 1.81GB可直接证明当前30秒负载 | 这些指标若是进程启动后的累计值，不能直接归因到同一CPU窗口；当前仓库证据中也没有本次原始起止快照 | 纳入基线前必须提供同一30秒窗口的counter delta、进程identity、remote连接数和full-repair次数 |
| 32位ARM上的CPython 33ms循环必有6%～10%固定底噪 | 当前12.7%实测说明Position值得继续观察，但不能把它拆成未经profile的固定解释器底噪；单线程主循环也不能笼统归因于GIL切换 | C04以Position进程自身同窗口占比和函数级热点为门槛，不以CPU1总占用或经验百分比决定是否重写 |
| 最终 CPU1 必然为 5.3%～8.0% | 无法由进程占用简单相减推出。各进程百分比、等待、relay 流量和渲染开销会相互影响 | 删除该绝对承诺。先以第2节阶段目标验收，后续按新瓶颈逐卡收敛 |

### 1.2 “全模型 100% 保证”的可执行定义

本文中的“100%”只表示功能覆盖和门禁覆盖，不表示预先承诺某个 CPU 百分比：

1. 测试必须枚举当前 registry 的全部条目；任何条目缺 topology resolver、pose resolver、geometry/projector branch 或等价测试时直接失败。
2. shared cache、fit、projection、renderer、relay 和 sampler 不得出现固定 AC/BC、固定 `mcs[3]/mcs[4]`、固定 primary/child 两轴公式。
3. 每个具体模型只在自己的 branch 计算姿态矩阵和动态几何；shared 层只消费 registry id、能力描述和模型无关矩阵/向量。
4. 新模型只有同时增加唯一 descriptor、独立 scene ops、独立 kinematics/projector 和正确性/性能测试后，才自动加入“全部已登记模型”验收矩阵。
5. 未登记、ops 缺失、generation 不新鲜或 descriptor/branch 不一致时保持现有 fail-degraded/fail-closed，禁止回退 AC、BC、identity 或上一模型。

## 2. 本轮目标

使用同一程序、同一视图、RTCP状态、remote连接数和采样窗口，做到：

1. 当前 registry 全部模型（现为 AC/BC）连续旋转轴运动时，首帧后的静态程序模型 `build_count` 不再增长。
2. 未真实越界时 `fit_generation` 不变。
3. RTCP OFF 的黄色程序层变换次数为 0。
4. RTCP ON每个实际发布的scene最多生成一次模型姿态并完成一次融合`O(N_visible)`变换/投影；C01第一阶段不以子像素位移上界跳过作为完成条件。
5. UI 只刷新发生变化的图层，不因任意 `native_generation` 改变而执行整页动态刷新。
6. relay只构建最新可恢复dirty状态；积压的陈旧中间帧不逐帧补算，必要时所有慢订阅者共享一次latest full repair后继续增量；订阅者增加不重复生成scene或payload。
7. AC 和 BC 分别在模型匹配程序、同一3D视图、RTCP ON、一个 remote stream 条件下，CPU1 30 秒平均值不高于 70%，P95 不高于 80%，不得连续 1 秒保持 100%。该目标是第一阶段板端门槛，不是对未实现模型或任意程序规模的预估承诺。
8. CPU0 affinity、servo周期、EtherCAT OP/WKC/DC、安全链和运动实际值不得改变。
9. 刷新分层保持当前owner：坐标/轨迹动态层30Hz，普通按钮和急停显示10Hz，其它普通状态/诊断5Hz或更低；相同generation的周期tick必须cache hit，不得把30Hz解释成每33ms强制重算。
10. WinRemote正常负载下持续消费最新coalesced增量帧；拥塞时允许丢弃陈旧中间显示帧，但恢复后的画面必须与同一最新本地framebuffer逐像素一致，远程输入不得随显示丢帧一起丢弃。

若第 7 项暂未达到，但前三张卡的计数已正确闭合，停止并报告剩余进程占比，不得靠降到 10Hz、修改 nice 或迁移 CPU0 冒充通过。

## 3. 固定执行规则

1. 开始每张卡前重读上述三个 owner 和当前代码。
2. 只修改当前卡白名单文件；确需扩大范围时先说明原因并更新本文白名单。
3. 保留用户和其他 AI 的现有修改，不覆盖无关 diff。
4. 每张卡先加/改 focused test，再修改实现。
5. focused executable 必须实际运行，不能只证明编译成功。
6. 前三张卡不得修改 `V5StatusShmFrame`/`V5StatusDisplayScene` 大小或版本；优先使用现有 `flags` 空余位表达图层 dirty mask。
7. 所有显示、Publisher、relay仍固定在 CPU1，不得进入 CPU0，不得提升为实时线程。
8. AI 默认不直接启动真实运动；用户明确放行运动自动化后，按 `功能/自动闭环测试方式.md` 通过合规operator面启动active model匹配的`cc-ac.ngc`或`cc-bc.ngc`并完成fresh native/UI/motion回读。本轮用户已明确放行，不再逐步请求确认。
9. CPU、dirty、payload、build/project等计数一律取同一窗口起止delta；累计总数、`ps %CPU`长周期均值和单张截图不得用于计算单卡收益。
10. 每卡先证明数学/像素/协议等价，再看CPU；任何“CPU下降但画面冻结、丢关键状态、降低本地刷新率或远程输入不可用”均判失败。

## 4. 卡 C01：Scene Producer 静态/动态分层

### 目标

停止“任一旋转姿态变化就重建 resident 程序拓扑”的共享层错误路径，同时保持当前全部已登记模型、RTCP ON/OFF和黄色轨迹物理语义不变，并让未来模型通过统一接入合同自动进入门禁。

### 文件白名单

- `board/services/state_publisher/v5_program_scene_model.h`
- `board/services/state_publisher/v5_program_scene_model.c`
- `board/services/state_publisher/v5_program_scene_model_internal.h`
- `board/services/state_publisher/v5_program_scene_model_ac.c`
- `board/services/state_publisher/v5_program_scene_model_bc.c`
- `board/services/state_publisher/v5_program_scene_cache.c`
- `board/services/state_publisher/v5_program_scene_producer.c`
- `board/services/state_publisher/v5_program_scene_producer.h`
- `board/services/state_publisher/v5_program_scene_projection.c`
- `board/services/state_publisher/v5_program_scene_projection.h`
- `board/services/command_gate/v5_motion_model_registry.h`
- `board/app/src/v5_program_scene_producer_smoke.c`

### 必须修改

1. 把当前 `V5ProgramSceneModel` 的职责显式拆为：
   - `Topology`：registry id、能力/活动轴描述、G53中心来源与已提交中心、固定结构参数、geometry epoch。
   - `Pose`：本帧同源实际旋转轴、模型 branch 算出的融合刚体矩阵、pose generation。
   - 不要求沿用上述类型名，但缓存 key 中不得再出现本帧角度、`sin/cos` 或 pose matrix。
2. 将 scene ops 改为能力驱动接口：每个模型 branch 独立提供 topology resolve、pose resolve/matrix 和动态 geometry；shared 层不得知道 A/B/C 旋转顺序。纯三轴未来接入时应使用零旋转/identity 能力，而不是伪造 primary/child 两轴。
3. 静态 key 只允许包含 program source identity/generation、point count、WCS归属/offset epoch、registry id、模型 geometry epoch等结构状态；不得包含本帧姿态或 `rtcp_enabled`。RTCP切换只改变投影模式，不重新解析或重建resident程序拓扑。
4. resident程序点、WCS归属、segment break和未施加实时姿态的base world points只在Program Open或真实结构变化时缓存。当前实现把RTCP变换后的点写入 `world_points`，必须改为保存未变换base点。
5. 随姿态变化的模型轴线/中心、红绿点、刀柄线进入动态几何层，点数有界且按frame为O(1)。
6. RTCP OFF直接复用静态黄色程序屏幕缓存，黄色程序RTCP变换计数为0。
7. RTCP ON由唯一模型branch为同一generation只生成一次融合矩阵：
   - 必须用该矩阵对全部保留程序点执行一次融合transform+project，复杂度为`O(N_visible)`；当前scene容量上限为512点，不得把它描述成无界几千点热循环。
   - 正常fit未改变时不得先transform再project形成两次全量遍历；真实越界导致本次fit提交时允许有明确计数的一次必要重投影，但持续越界不得重复。
   - 不能只更新少于10个marker后冻结黄色程序线，也不能用抽样点证明整条线没有变化。
8. producer处理中收到更新sample时只保留最新完整generation，不建立待追赶队列。
9. 最终像素、三位文字桶和图层均未变化时，不推进 `scene_generation`。
10. registry测试必须枚举全部已登记模型，并验证descriptor与唯一scene ops一一对应；以后新增模型而未补scene branch/test时focused gate必须失败。

### 非阻塞二次优化

只有C01正确版本完成板端profile，且一次512点融合遍历仍是显著热点时，才允许另开C01B评估“矩阵delta + program半径 + frozen fit scale”的保守屏幕位移上界。C01B必须满足：

- 判定本身为严格O(1)，不得扫描程序点。
- 使用double中间量，覆盖XY/XZ/YZ/3D、缩放/旋转/平移和极端半径，溢出或非finite一律退回完整遍历。
- 只有能证明全部点最坏位移小于0.5px时才复用；边界相等、证明不完整或真实越界时必须完整遍历。
- 必须用黄金逐点结果证明跳过帧与完整投影的整数像素输出一致，并以板端CPU delta证明收益大于判定和分支成本。

C01B不是C01的阻塞门禁，也不得与C01首版同批实现。

### 禁止做法

- 只删除模型 `memcmp` 后冻结黄色轨迹。
- 修改G-code、运动actual或commanded值迎合UI。
- 把任一具体模型公式放入shared层、放入另一模型branch，或固定假设旋转轴总是slot 3/4。
- 为了宣称O(1)而冻结RTCP ON黄色轨迹、只变marker或降低几何精度。
- 用10Hz替代静态/动态分层。

### focused验证

```bash
cmake --build <linux-build> --target v5_program_scene_producer_smoke v5_state_publisher_smoke
<linux-build>/app/v5_program_scene_producer_smoke
<linux-build>/app/v5_state_publisher_smoke
python board/tools/docs/verify_v5_document_routes.py --strict-details
git diff --check
```

新增smoke必须从registry枚举当前全部条目；每个条目连续输入至少300帧合法姿态，并断言：

- 静态结构不变时首帧之后 `build_count` 不再增长。
- RTCP OFF变换次数为0。
- RTCP ON每个已发布scene最多生成一次模型矩阵、正常fit下最多执行一次融合全点遍历；真实越界重投影必须单独计数且同一越界事件只发生一次。
- 未越界时 `fit_generation` 不增长。
- 每个模型的黄线、WCS/MCS、轴线、中心、红绿点和刀柄线输出与修改前正确物理公式在允许误差内一致。
- identity/零旋转能力用合成模型测试覆盖；它不代表当前产品已经支持XYZ三轴active model。

完成后停止并交付本卡结果。

## 5. 卡 C02：UI分类刷新与刀路renderer

### 目标

在当前已有单dirty包围框基础上，消除 `scene_generation` 任意变化导致坐标、倍率、刀路和整页对象一起重写的路径；对所有模型只按图层与真实像素变化失效。

### 文件白名单

- `board/app/include/v5_status_shm.h`：只允许新增不改变ABI大小的scene flag位。
- `board/services/state_publisher/v5_program_scene_producer.c`：只允许发布图层dirty flag和屏幕像素等价减点，不得改变scene ABI。
- `board/services/state_publisher/v5_program_scene_projection.c`
- `board/app/src/v5_ui_shell_internal.h`
- `board/app/src/v5_ui_shell_refresh.c`
- `board/app/src/v5_main_page.h`
- `board/app/src/v5_main_page_view.c`
- `board/app/src/v5_main_page_toolpath_geometry.c`
- `board/app/src/v5_main_page_toolpath_program.c`
- `board/app/src/v5_ui_shell_refresh_smoke.c`
- `board/app/src/v5_main_page_smoke.c`

### 必须修改

1. 将统一的 `V5_MAIN_PAGE_REFRESH_DYNAMIC` 拆成坐标、速度倍率、黄色程序层、动态几何和CPU指标刷新原因。
2. scene flag增加图层dirty mask，但不得改变SHM结构大小、字段偏移和版本；C02默认方案不新增多rect数组或tile bitset。
3. 只变scene时不得重新格式化所有坐标和倍率；只变坐标时不得遍历黄色程序点。
4. 黄色程序点在producer完成world-to-screen后只做屏幕像素等价减点：第一阶段仅删除投影到完全相同整数像素、且不跨`break_before`的连续重复点；保留关键点和segment break。更广泛的`0.5px`曲线简化只有在另有最坏误差证明和板端profile收益时才允许进入后续卡，不能用潜在`O(N^2)`简化阻塞C02。
5. renderer不得为每个未变化点重复建立LVGL line descriptor；使用有界批量1px折线绘制或等价专用rasterizer。
6. 1px防抖必须作用于该图层全部会改变的screen primitive或由producer给出的安全图层hash/位移上界，不能只比较刀尖：
   - RTCP OFF时黄色程序层可驻留独立静态canvas/offscreen buffer，只更新红绿点、刀柄和必要动态几何。
   - RTCP ON且黄线发生可见姿态变化时，黄色程序层属于动态图层，必须正确重绘全部受影响线段；不能只擦旧/新刀尖两个16×16区域。
7. 黄色程序层、active-model轴线/中心、红绿点和刀柄层分别失效；dirty必须覆盖每个图层旧位置与新位置的并集，但不得无条件失效整个主页面。`16x16`只适合有界marker候选，且必须用8/16/32或等价实测选择；程序折线和长刀柄按实际覆盖tile/rect处理。
8. 隐藏页面只保存immutable snapshot，不调用renderer、fit或cache invalidate。
9. scene ABI当前只有一个dirty包围框。C02必须优先使用现有flags图层分类和scene内旧/新screen points，在UI侧生成marker或图层的有界局部失效区域；本卡禁止改变`V5StatusDisplayScene`大小、偏移或版本。
10. 只允许结构边界、明确视角切换或真实越界事件进行大面积重绘；稳定运行态必须按实际变化图层和区域增量失效。不得预先承诺每帧固定512像素，也不得把UI `<5%`写成未测通过值。

若现有scene信息无法在不漏画的前提下表达程序折线多rect，当前卡必须停止并报告`blocked_by_scene_dirty_geometry`；后续另开C02B做完整ABI设计，同时升级C/Python writer、reader、CRC、version、boot-ready、relay和兼容测试。不得在C02中偷塞数组、复用保留字节或用不完整dirty区域换CPU下降。

### focused验证

```bash
cmake --build <linux-build> --target v5_ui_shell_refresh_smoke v5_ui_display_models_smoke v5_ui_shm_refresh_smoke v5_main_page_smoke
<linux-build>/app/v5_ui_shell_refresh_smoke
<linux-build>/app/v5_ui_display_models_smoke
<linux-build>/app/v5_ui_shm_refresh_smoke
<linux-build>/app/v5_main_page_smoke
git diff --check
```

smoke必须证明scene-only、coordinate-only、scalar-only三种输入不会互相触发对象重写；隐藏主页面的renderer计数为0；RTCP OFF静态黄线只在结构边界重绘，RTCP ON黄线姿态变化与修改前黄金像素/几何结果等价；marker-only变化不得产生整幅刀路dirty。

完成后停止并交付本卡结果。

## 6. 卡 C03：Remote relay 最新帧与背压

### 目标

保留本地30Hz动态显示；审查并补齐现有shared payload/coalesce/backpressure路径，使remote积压时不重复构建或追赶旧显示帧。该卡不是从零重写relay。

### 文件白名单

- `board/services/ui/v5_remote_ui_relay.py`
- `board/services/ui/v5_remote_ui_support.py`
- `board/services/ui/v5_remote_ui_state.py`
- `board/services/ui/v5_remote_ui_shared_payload.py`
- `board/services/ui/v5_remote_ui_dirty_geometry.py`
- `board/services/ui/v5_remote_ui_relay_coalesce_smoke.py`

### 必须修改

1. 先运行现有coalesce smoke并读取板端计数；已经满足的共享payload、历史repair和backpressure行为只保留，不做无收益改写。
2. 保持relay纯镜像：静态依赖检查必须覆盖relay及其全部运行时support模块，并证明它们都不include/import Position/State codec、`v3_status_shm` reader、scene model、RTCP或world-to-screen实现；CPU等允许的remote metrics直接读取`/proc`等系统接口，只读取UI提交的framebuffer mmap和dirty FIFO，不得通过support模块间接实例化`V5StatusShmReader`。
3. 无stream订阅者时，dirty reader可以记录最新frame id，但payload build次数必须为0；无dirty时生产线程阻塞/等待，不得轮询framebuffer或形成busy loop。
4. 有积压时只允许保留最新可恢复状态；不得逐帧重新抓取/打包。协议需要连续base时使用一次共享latest full repair，再从新base继续。
5. 同一generation只构建一次payload，多订阅者共享immutable结果；订阅者数量不得线性增加framebuffer读取和压缩次数。
6. 正常网络下目标是33ms窗口内的最新coalesced增量，不要求发送窗口内每一张中间UI帧；CPU1超过85%或TCP背压时允许显示降帧并保留最新可恢复帧，不得降低本地SHM/UI freshness，不得破坏remote协议base连续性或远程输入通道。
7. 避免把稀疏1px轨迹无条件合并为整块画布payload；沿用现有bounded min-extra merge并增加板端面积/字节门槛。
8. dirty/payload归因必须使用同一30秒窗口的起止delta；进程启动以来的累计`dirty_events`或字节数不能作为单次修复收益。

### focused验证

```bash
python board/services/ui/v5_remote_ui_relay_coalesce_smoke.py
python -m py_compile board/services/ui/v5_remote_ui_relay.py board/services/ui/v5_remote_ui_support.py board/services/ui/v5_remote_ui_state.py board/services/ui/v5_remote_ui_shared_payload.py board/services/ui/v5_remote_ui_dirty_geometry.py
git diff --check
```

必须覆盖0、1、2个订阅者、慢订阅者、30Hz连续dirty和CPU1过载模拟；producer build次数不得随订阅者数量线性增长。每个已发送增量经`base_frame_id + rect payload`重建后必须与对应本地framebuffer逐像素一致；丢弃中间帧后客户端必须经共享repair或连续delta收敛到最新frame，不能停在旧画面。

完成后停止并交付本卡结果。

## 7. 卡 C04：全模型统一Sampler与Position Publisher降耗（条件卡）

只有C01-C03已板端通过，并且同条件30秒窗口内Position Publisher自身仍持续超过8%，或函数级证据证明它是CPU1未达标的主要剩余热点时才执行。不能仅因CPU1总占用高于40%就进入本卡。

当前触发证据（2026-07-20，C01-C03 focused bundle 已上板，active model=`XYZBC_TRT`，Machine Off，保持1个真实`/remote/stream`长连接，30个1秒样本）：CPU1平均42.85%、P95 44.79%；Position Publisher平均9.47%、P95 11.00%、最大12.00%。同窗UI平均2.70%、relay平均3.67%、State平均2.43%、milltask平均1.87%。该窗口没有执行运动，不能冒充AC/BC加工态最终验收；但Position进程在更轻的静止条件下已经持续超过8%，已满足本卡明文触发条件，允许进入C等价替换。

当前事实：只有Python Position Publisher逐pin读取坐标/倍率HAL，C State Publisher只mmap读取Position块，再读取RTCP/WCS/G53/modal resident块并生成最终scene。因此本卡不是“消除两次HAL采样”，而是评估把现有唯一Position sampler从Python等价迁移到C是否值得。

12.7%旧快照不能当成全部可节省开销。实现和复测时仍须按HAL pin读取、Python对象/投影、稳定器、CRC/pack、mmap publish和bus采样分别归因；`6%~10% CPython固定底噪`只允许作为待验证假设。当前30秒实测已经用进程自身持续超过8%触发C化，不再把取得函数级采样器结果作为阻塞门槛；最终只以C实现前后同条件窗口的实际差值报告收益。

目标架构保持“一次Position HAL sample生成一个immutable Position snapshot，State Publisher只消费该snapshot”。当前owner明确规定Position和State为独立30Hz publisher及lineage门槛；本卡优先新增C Position writer并等价替换Python实现，不授权把Position writer直接揉进State进程。若确需合进单进程，必须先修改`功能/0-4共享内存.md`的独立publisher/startup合同并单独做ABI/lifecycle设计。

本卡固定选择独立native C实现，并必须同时保持：

1. Position v3的256字节ABI、SeqLock、CRC、writer identity、generation、freshness和reader重连语义。
2. 全部轴的三位一脉冲、未使能轴有界滤波、旋转轴360度等效/环形距离、display digits、unit-per-count和following error同源结果逐位或容差等价。
3. bus status原有低频节奏与错误降级，不得把总线轮询扩大到30Hz。
4. startup readiness、PID/lock、部署manifest、rootfs安装、State Publisher position fixture以及C writer等价smoke的闭包。
5. active axes/slots来自fresh model descriptor；不得固定AC/BC、固定A/B/C或固定slot 3/4。
6. 先并行保留旧Python实现只用于C/Python golden对照；C smoke、ARM构建、原子bundle部署、板端Position/State lineage和同窗CPU均通过后，在同一卡内删除`v5_position_status_publisher.py`、其import型测试、Python启动argv、安装/校验专用分支及所有fallback，最终仓库和板端只能剩一个native Position writer。

本卡文件范围固定为：`board/services/state_publisher/`下新C writer、init与focused smoke，`board/app/CMakeLists.txt`，`board/config/deploy/v5_runtime_deploy_manifest.tsv`，以及直接识别Position executable/argv/ABI原子域的`board/tools/deploy/`安装、核验、冷启动与policy测试。若`rg v5_position_status_publisher.py`命中其它有效启动或部署owner，必须一并改为native路径；仅是历史说明的文档引用可保留并明确为旧实现，不得留下可执行fallback。完成后必须比较C化前后同一窗口CPU，不能把原12.7%整项当成已节省。

## 8. G-code执行预缓冲（独立任务）

当前`v5_program_runtime_open_file()`已经在Program Open时把最多2MiB原始G-code一次性读入匿名RAM，供preview、hash、首点和UI resident使用；这里不存在“再加一次全文件读取就完成预缓冲”的缺口。但`v5_program_runtime_prepare_start()`仍把原始`source_path`交给Command Gate/LinuxCNC `Set Open`，LinuxCNC task/interpreter不会消费UI的`gcode_text`缓冲。

因此真正缺口是LinuxCNC执行侧的resident parse/lookahead/有界queue-feed，不是`v5_program_runtime.c:490`少传了一个内存指针。当前实测窗口中G-code文件句柄offset五秒不动且`milltask=2.4%`，没有证据表明磁盘读取是97%满载首因；page cache命中也不能证明解释器解析和queue-feed已退出CPU1。

必须在C01-C04之后且只有执行侧profile证明`milltask`、解释器、缺页/读取或queue补给成为新瓶颈时单独立项，不与显示链同批修改。目标是Program Open阶段由LinuxCNC/微内核执行owner完成resident parse和足量lookahead；CPU0只允许消费无文件、无日志、无文本解析的最小有界队列。不得把普通LinuxCNC task/interpreter整体迁入CPU0，不得创建执行用临时G-code副本、sidecar或改变原始program identity。

## 9. 板端验收

前三张卡本地通过后，使用当前完整Windows源码做VM/ARM构建并部署对应runtime；最终交付前执行full-manifest一致性核验。禁止只同步单个源码或复用旧对象后宣称完成。

部署后：

1. 保持安全实际状态，确认runtime DAG、UI、Position/State/WCS Publisher和relay ready。
2. 当前注册表逐模型执行：AC打开原始 `cc-ac.ngc`，BC打开原始 `cc-bc.ngc`；使用同一3D视图、RTCP ON并保持一个remote stream。不得用轴字替换或同一个程序冒充跨模型测试。
3. 用户启动程序，或用户已明确授权时由AI通过合规operator面启动程序；随后分别采集30秒：每进程CPU、每核CPU、scene/static-build/pose-matrix/full-point-project/fit计数、UI rewrite/dirty、relay payload和Task State。
4. 同窗口核对servo overrun、EtherCAT OP/WKC/DC、编码器连续性和安全actual。
5. 同时记录30Hz坐标/轨迹、10Hz按钮/急停、5Hz其它状态/诊断的实际apply/rewrite计数；相同generation不得因timer tick重复工作。
6. 使用`capture_v5_board_ui.sh`或`collect_v5_remote_input_evidence.py capture`抓取板端画面，并仅用`collect_v5_remote_input_evidence.py click/drag`做鼠标模拟；不得用WinRemote等屏幕软件自动操作。对窗口末尾最新frame做本地/远程逐像素或hash一致性检查，并确认稳态full-frame只用于initial/repair。
7. 用户停止程序并保持最终安全态。

任何功能、运动、DC/WKC或安全退化，立即回滚当前卡产物并停止；不得继续下一卡掩盖问题。

### 9.1 收益口径

- 可以承诺的结构收益：所有当前已登记模型不再因姿态变化重建/重拷贝resident程序拓扑；RTCP OFF黄线不做姿态变换；RTCP ON同generation不重复生成矩阵或重复全点遍历；消费者数量不增加producer计算次数。
- 必须板端实测的性能收益：UI、relay、Position Publisher和CPU1总占用。不得把`-30%/-35%/-14%/-12.7%`线性相加，也不得在四卡完成前写`5.3%~8.0%`。
- 若AC和BC任何一个模型未通过几何等价、轨迹连续、计数或CPU门槛，不能用另一个模型的结果声明“全模型通过”。
- AB、XYZ三轴、双摆头等尚未登记的模型只证明接入门禁存在，不声明产品功能或板端CPU已通过；首次登记时必须自动加入本节矩阵。

## 10. 交接模板

```text
卡号：C01 | C02 | C03 | C04
状态：source_only | local_verified_only | blocked | board_deployed_wait_user_test | board_verified
修改文件：
focused测试及结果：
是否部署：未触发 | 成功 | 失败
板端同条件CPU1：平均 / P95 / 最大连续100%时长
static build / RTCP transform / project / fit计数：
UI rewrite / dirty面积：
relay build / payload字节：
CPU0、EtherCAT和安全状态：
剩余问题：
下一步：停止 | 用户测试 | 进入下一卡
```

## 11. 2026-07-21 当前代码与板端实测

### 11.1 当前完成度

- C01：`board_verified`（当前 BC 模型）。AC/BC branch 只负责生成本模型的 3x4 affine pose matrix；normal-fit 路径把模型变换、平面变换和屏幕投影融合为一次全点遍历。`world_points`、`v5_program_scene_model_transform()`和`v5_program_scene_project_static_cache()`旧双遍历入口已删除。Windows smoke、ARM build 和 ARM QEMU smoke 均通过；BC 真实运动 30 秒内 `build_count 3 -> 3`、`transform_count 7325 -> 8260`、`project_count 7376 -> 8311`，935 个 fresh scene 各增加一次 transform/project，静态 build 未增长。
- C02：`board_verified_target_narrow_miss`。分层 dirty、scene 直接消费、局部失效、旧逐点 LVGL 对象路径删除、黄色轨迹 generation raster cache、`16x16` tile XOR、clip 内非零 bit 绘制和 unchanged visibility/state setter 门禁均已落地；BC 的 3D/RTCP ON 黄线、轴线、红绿点、坐标和刀柄连续正确且无残影。相同真实运动窗口中固定全刀路框 `0,50,399,396` 已从 dirty FIFO 消失，LVGL UI 平均由47.89%降到25.94%，CPU1平均由66.15%降到44.54%；但UI仍高于本文件25%失败上限0.94个百分点，因此不得写成完全通过，也不得靠降30Hz或冻结黄线凑数。
- C03：`board_verified`。relay及support模块已不再导入或实例化`V5StatusShmReader`；`/remote/info`的CPU数据直接从`/proc/stat`按请求间隔取delta，画面只消费UI framebuffer和dirty FIFO。coalesce smoke、`py_compile`、静态禁用依赖检查和板端两次`/remote/info`读回通过；真实运动窗口 relay 平均2.57%。
- C04：`board_verified`（当前 BC 模型）。唯一 Position owner 是 native C writer；板端 Position v3 256字节 ABI、SeqLock、CRC、writer identity、lineage和freshness读回通过，真实运动窗口 Position 平均0.16%。旧 Python writer、旧测试和旧 init Python ABI status分支已物理删除。
- 尚未登记的AB、XYZ三轴或其它模型不进入已验证集合；AC仍须切换真实active model后运行原始`cc-ac.ngc`，不能用BC结果代替。

### 11.2 已执行验证

- Windows：`v5_program_scene_producer_smoke.exe`实际运行通过；relay相关`py_compile`和`v5_remote_ui_relay_coalesce_smoke.py`通过；严格文档路由与`git diff --check`通过。
- ARM：canonical `/root/v5-build/board`重编`v5_program_scene_producer_smoke`、`v5_state_publisher_smoke`、`v5_state_publisher`、`v5_lvgl_shell`和`v5_position_status_publisher`；两个ARM smoke均经QEMU实际运行通过。
- 部署：15项Position/State/UI ABI原子bundle读回`V5_POSITION_ABI_READBACK_OK`、`V5_STATE_ABI_READBACK_OK`、`V5_SHM_ABI_ATOMIC_RESTART_OK`；relay support按UI scope单独部署。板端当前binary hash与canonical ARM artifact一致。
- Operator路径：只使用`collect_v5_remote_input_evidence.py click/capture`完成取消急停、打开原始`cc-bc.ngc`、机械全轴Home、Start和最终急停；没有使用WinRemote、direct UDS或SSH命令冒充按钮操作。运行截图为`截图/跑cc/28_running_after_sample/v5_board_capture_20260721T021548Z.bmp`。
- 真实运动readback：`motion.current-vel=3.75`、`motion.program-line=30`、`motion.distance-to-go=53.98515`；画面为3D、RTCP ON且黄色轨迹连续。最终安全readback为`v5-safety-estop-active=TRUE`、`v5-safety-actual-valid=TRUE`、`v5-machine-enabled=FALSE`，最终截图为`截图/跑cc/29_final_estop/v5_remote_input_20260721T021638Z_after.bmp`。

### 11.3 同窗CPU1实测

测量方法：板端直接按0.5秒间隔读取`/proc/stat`和各PID的`/proc/<pid>/stat`，不在采样循环中调用`halcmd`，避免诊断进程污染CPU1。active窗口为`XYZBC_TRT`、原始`cc-bc.ngc`、主页3D、RTCP ON、1个remote stream、30.339秒。

| 项目 | BC真实运动 | 最终急停静稳 |
| --- | ---: | ---: |
| CPU1平均 | 66.15% | 15.58% |
| CPU1 P95 | 68.00% | 18.37% |
| CPU1最大 | 82.02% | 33.87% |
| LVGL UI平均 | 47.89% | 2.75% |
| remote relay平均 | 2.57% | 0.07% |
| native Position平均 | 0.16% | 0.17% |
| State Publisher平均 | 3.23% | 2.45% |
| WCS Publisher平均 | 3.03% | 3.02% |
| milltask平均 | 2.08% | 1.72% |

BC总CPU1平均和P95已进入第一阶段`平均<=70%、P95<=80%`门槛，且没有连续1秒100%；但C02不能关闭，因为UI自身仍是第一热点。急停后scene仅`10670 -> 10672`，`build/transform/project`分别保持`3/10253/10309`不变，说明高占用随真实动态投影停止，没有残留程序重建循环。

### 11.4 C02收口前预计占用（历史）

以下是基于本次同窗归因的工程目标，不是提前承诺。只有保持相同程序、视角、RTCP和remote stream复测后才能替换为完成值。

| 项目 | 当前BC实测 | C02收口后预计 | 失败上限 |
| --- | ---: | ---: | ---: |
| CPU1总平均 | 66.15% | 28%～42% | >70% |
| CPU1 P95 | 68.00% | 35%～55% | >80%或连续1秒100% |
| LVGL UI | 47.89% | 10%～20% | >25% |
| remote relay（1 stream） | 2.57% | 1%～4% | >12%或持续full repair |
| native Position | 0.16% | 0.1%～1.0% | >1.5% |
| State Publisher | 3.23% | 2%～5% | >8% |
| WCS Publisher | 3.03% | 2%～5% | >7% |
| milltask/interpreter | 2.08% | 2%～5% | >20% |

本次数据已经排除“G-code文件解析是97%首因”：真实运动中`milltask=2.08%`，而UI为47.89%。下一步先修UI program layer raster/flush，不进入第8节执行侧预缓冲，也不做线性百分比相减承诺。

### 11.5 C02最新上板结果

同一测量脚本按0.5秒采样60次，窗口为`XYZBC_TRT`、原始`cc-bc.ngc`、主页3D、RTCP ON、1个remote stream、30.224秒；scene在窗口内保持30Hz freshness，`build_count 3 -> 3`、`transform_count 545 -> 1467`、`project_count 550 -> 1473`，证明静态程序未重建且每个fresh scene只有一次transform/project。最终保留版本撤回了会把坐标区拆成10个rect的负优化：该试验曾使UI升到29.13%、relay升到4.03%，不进入产品代码。

| 项目 | C02前BC真实运动 | C02最新BC真实运动 | 当前结论 |
| --- | ---: | ---: | --- |
| CPU1平均 | 66.15% | 44.54% | 通过第一阶段`<=70%` |
| CPU1 P95 | 68.00% | 46.94% | 通过第一阶段`<=80%` |
| CPU1最大 | 82.02% | 65.79% | 未出现连续1秒100% |
| LVGL UI平均 | 47.89% | 25.94% | 距`<=25%`还差0.94个百分点，C02不可冒充全通过 |
| remote relay平均 | 2.57% | 2.58% | 稳定，保留既有33ms合并/Latest-Wins/shared payload |
| native Position平均 | 0.16% | 0.17% | 稳定 |
| State Publisher平均 | 3.23% | 3.28% | 稳定 |
| WCS Publisher平均 | 3.03% | 3.04% | 稳定 |
| milltask平均 | 2.08% | 1.89% | 不是当前首要瓶颈 |
| rtapi_app平均 | 未单列 | 18.53% | 属运动实时链，禁止为显示目标迁移或降周期 |

板端`strace`脏区证据已证明运行时不再出现固定`0 50 399 396`整刀路框，黄线只提交实际变化tile/rect；最终两帧抓屏位于`repo_ignored/evidence/c02_final_visual/v5_board_capture_20260721T034905Z.bmp`和`v5_board_capture_20260721T034914Z.bmp`，可见黄色轨迹随BC姿态连续变化且旧位置无残影。最终急停截图为`repo_ignored/evidence/c02_final_visual/v5_remote_input_20260721T034951Z_after.bmp`；HAL读回`estop-active=TRUE`、`machine-enabled=FALSE`，runtime DAG ready，backend为`BACKEND_MOTION_READY`、5/5 OP、WKC 10/10、DC valid。
