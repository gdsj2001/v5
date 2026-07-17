# G代码运行 CPU1 优化 AI 直接作业指导书

记录日期：2026-07-17

性能历史样本对应源码：85834b210。本文重写时 HEAD：37fcd8b80。每次实施必须重新绑定当前 HEAD、focused diff、构建 identity 和板端 identity，不得把本文历史数字当作当前事实。

文件性质：待做工作中的活动实施输入。本文用于让 AI 按固定作业卡执行，不是需求真源、架构真源、运行状态真源或板端通过证明；禁止把完成进度、命令日志和板端结果持续回写本文。

~~~text
AI_DIRECT_EXECUTION=true
ENTRY=第3节
ONE_JOB_PER_SLICE=true
RETIREMENT_POLICY=确认退役后同一切片物理删除并清到零正向残留
DEFAULT_FINISH=local_verified_only
BOARD_ONLY_WHEN=用户明确要求立即上板、任务本身是板端验收、集成里程碑或发布
~~~

## 1. 唯一目标、裁决顺序和读取包

本文唯一目标是在不降低功能、不迁移普通工作到 CPU0、不牺牲 native actual、安全、30Hz 显示和远程功能的前提下，降低 CPU1 固定负载与 G 代码运行放大负载，并对真正的 dense-G1 LinuxCNC 热点设置证据门。

每张作业卡开始前必须按顺序读取：

1. [AGENTS.md](../AGENTS.md) 的入口核、锁、构建梯级、退役和状态词规则。
2. [功能/需求真源索引.md](../功能/需求真源索引.md) 的 AI 任务阅读包，定位本卡唯一 REQ owner。
3. owner 的 AI_FAST_READ_BEGIN 至 AI_FAST_READ_END，再读本卡命中的 detail_sections。
4. 当前 canonical source、当前 git status、focused diff、相关构建/安装/服务入口和运行证据。

本问题常用 owner：

- [功能/0-1开机参数入内存.md](../功能/0-1开机参数入内存.md)：REQ-GCODE-RUN-HOT-PATH、REQ-CPU0-MOTION-REALTIME-DOMAIN。
- [功能/0-4共享内存.md](../功能/0-4共享内存.md)：REQ-SHM-DISPLAY-PROJECTION、REQ-REMOTE-DISPLAY-RELAY-DECOUPLING。
- [功能/3-1主页面按钮功能.md](../功能/3-1主页面按钮功能.md)：REQ-START-RUN-HOT-PATH、主页面刷新边界。
- [功能/3-4UI刀路绘图顺序与状态稳定方案.md](../功能/3-4UI刀路绘图顺序与状态稳定方案.md)：30Hz 刀路、冻结 Fit、active-model 投影。
- [功能/微内核.md](../功能/微内核.md)：100 行等价前瞻、native owner、运行期无周期落盘。

裁决规则：

- 用户改变产品行为或 contract：先改唯一 owner 正文和同文件快读卡，再改 canonical source/config/tests。
- owner 已正确、代码未兑现：引用 owner，直接修 canonical source/config/tests，不把错误现状写回需求。
- owner 冲突、索引与正文冲突、无法证明唯一替代 owner：停止该卡并报告 blocked，不得自行选择第二实现。
- 本文与 owner 冲突时 owner 胜出；应先修本文，再实施，不得用本文覆盖 owner。

## 2. 不可破坏合同

所有卡必须同时满足：

1. 坐标、轨迹动态点、红绿点、刀柄刀尖、速度和倍率显示保持 30Hz 目标；按钮状态 10Hz；急停显示和 actual 回读 10Hz；普通诊断 5Hz。
2. 只允许跳过未变化对象、合并尚未发布的同窗 dirty、丢弃过期显示工作；不得降低 native/State freshness，不得用缓存、默认值或重写时间冒充 actual。
3. 运行期禁止周期整屏刷新、整页重建和 full-frame polling；full frame 只允许 initial 或明确 repair。
4. UI 只写 remote BGRA mmap 和 dirty FIFO；18080、HTTP、WebSocket、编码和发送由独立 relay 进程承担。
5. UI、relay、Publisher、网络、日志、诊断和普通 LinuxCNC task/interpreter 不得迁入 CPU0，不得提升为 SCHED_FIFO。
6. CPU0 只属于有界 deadline 的 servo、motmod、kinematics、HAL realtime、EtherCAT、safety 和必要的最小有界 queue-feed 闭集。
7. 即使 CPU1 满载或 UI 卡死，CPU0 仍须无 queue starvation、servo overrun、DC/WKC 异常、编码器不连续和安全失效。
8. 不得通过停 relay、减少客户端、停 Publisher、降低 servo/安全周期、修改产品 G 代码或关闭证据来制造低 CPU。
9. UI/刀路不得改原始 G 代码；dense 诊断样本只进 repo_ignored/cpu1_optimization/scratch，不得替代 cc-ac.ngc、cc-bc.ngc 或原始 MPF。
10. 急停 edge 必须直通 native safety owner，不能被性能节流、debounce、日志或普通命令队列延迟。

当前非暂停 Start 的唯一序列必须保持：

~~~text
Set Task_Plan_Init
Set Mode Auto
Set Open <原始 runtime_program_path>
Set Run 0
~~~

Program Open 只登记原始路径、identity 和显示 metadata，不提前执行 native Set Open。不得删除 Start 中唯一 Set Open，不得改成 sidecar、过滤文件、预览产物或运行副本。

## 3. AI 唯一执行入口和状态机

AI 只能沿下面状态机前进；任一步失败都留在当前状态修正，不得跳到镜像、部署或下一卡。

~~~text
START
  -> P0_AUTHORITY       读取 AGENTS、需求索引、唯一 owner
  -> P1_IDENTITY        记录 HEAD、git status、focused diff、相关构建 identity
  -> P2_SELECT          按第6节选择第一个满足进入条件的作业卡
  -> P3_PROVE           用源码检测谓词证明缺陷仍存在
  -> P4_LOCK            获取所有待编辑精确文件锁
  -> P5_EDIT_DELETE     编辑；确认退役项在同卡物理删除
  -> P6_LOCAL_GATE      Windows 最近 focused gate；POSIX/ABI 才进 VM 最小 target
  -> P7_SURVIVOR_SWEEP  扫源码、构建、安装、服务、测试、文档正向残留
  -> P8_OPTIONAL_BOARD  仅在当前目标要求时部署并走原始 operator 路径
  -> P9_CLEANUP         清本线程锁、探针、临时进程和 task-only scratch
  -> P10_HANDOFF        按第11节报告唯一结果和下一卡进入条件
END
~~~

硬停止条件：

- 需求 owner 冲突或没有唯一替代 owner。
- focused 文件存在无法归属的用户改动，且本卡会覆盖同一行或同一语义。
- 精确文件锁或 vm_board.lock 被活跃任务持有。
- 当前 target identity 与待验证源码不一致，且无法完成最小合法投影/构建。
- 退役路径仍有未迁移的真实消费者。
- 安全运动条件不成立，或板端验收结束无法回到 ESTOP/Machine Off。

## 4. 通用前检、锁、证据和收尾

### 4.1 Windows 前检

每卡先复制执行，任何非零退出码都先处理：

~~~powershell
Set-Location D:\v5
$ErrorActionPreference = 'Stop'

$RepoRoot = (Get-Location).Path
$BuildDir = Join-Path $RepoRoot 'repo_ignored\build\windows-mainline'
$PycacheDir = Join-Path $RepoRoot 'repo_ignored\cpu1_optimization\pycache'
$env:PYTHONPYCACHEPREFIX = $PycacheDir

git rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'git rev-parse failed' }

git status --short
if ($LASTEXITCODE -ne 0) { throw 'git status failed' }

Get-ChildItem -LiteralPath 'repo_ignored\locks\files' -Filter '*.lock' -Recurse -ErrorAction SilentlyContinue |
  Select-Object FullName, LastWriteTime
~~~

随后只对本卡允许文件执行 focused diff：

~~~powershell
git diff -- <本卡允许文件逐项列出>
if ($LASTEXITCODE -ne 0) { throw 'focused diff failed' }
~~~

若 Windows CMake 缓存不存在，才配置一次：

~~~powershell
if (-not (Test-Path -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt'))) {
    cmake -S board -B $BuildDir -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE -ne 0) { throw 'Windows CMake configure failed' }
}
~~~

### 4.2 精确文件锁

每个待编辑文件都必须有：

~~~text
repo_ignored/locks/files/<project-relative-path>.lock
~~~

锁内容只能是：

~~~text
lock_version=1
thread_id=<当前任务标识>
file=<项目相对路径>
created_at=<当前 ISO 时间>
~~~

必须用 apply_patch 创建和删除锁；禁止用 PowerShell 重定向生成锁。锁只覆盖编辑及紧邻的文本/语法检查，之后立即删除；不得持锁等待 VM、构建、部署或用户。

### 4.3 证据目录

只把有复用价值的机器可读证据放到：

~~~text
repo_ignored/cpu1_optimization/evidence/<JOB_ID>/<HEAD>/
~~~

固定文件名：

- precheck.txt：HEAD、status、owner、allowed files、identity。
- before.tsv 和 after.tsv：相同字段、相同时钟、相同场景窗口。
- gates.txt：命令、退出码、成功标记。
- retirement.tsv：旧符号/文件/入口、删除位置、零残留结果、允许保留的负向门禁。
- board.tsv：只有真实板端验证时创建。

不得创建进度 Markdown、第二任务板、源码备份、板端源码副本或 VM 可编辑副本。

### 4.4 Windows C/Python/文档固定门禁

Windows Visual Studio 是多配置生成器；必须指定 Release，构建后必须执行 exe：

~~~powershell
cmake --build $BuildDir --config Release --target <TARGETS> --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'focused build failed' }

& (Join-Path $BuildDir 'app\Release\<TARGET>.exe')
if ($LASTEXITCODE -ne 0) { throw '<TARGET> failed' }
~~~

当前确认可在 Windows 直接构建并运行：

- v5_coordinate_digits_smoke
- v5_ui_display_models_smoke
- v5_main_page_model_projector_smoke
- v5_state_publisher_cadence_smoke

以下含 POSIX mmap、Unix socket、unistd 或 m.lib 链接边界，禁止冒充 Windows gate：

- v5_ui_shm_refresh_smoke
- v5_main_page_smoke
- v5_program_runtime_smoke
- v5_state_publisher_smoke
- v5_state_publisher

Python 固定形式：

~~~powershell
python -m py_compile <本卡修改的全部 Python 文件>
if ($LASTEXITCODE -ne 0) { throw 'py_compile failed' }

python <本卡 focused test>
if ($LASTEXITCODE -ne 0) { throw 'focused Python test failed' }
~~~

改需求/规则文档时：

~~~powershell
python board/tools/docs/verify_v5_document_routes.py --strict-details
if ($LASTEXITCODE -ne 0) { throw 'document route verification failed' }
~~~

所有 tracked 修改收尾：

~~~powershell
git diff --check
if ($LASTEXITCODE -ne 0) { throw 'git diff --check failed' }
git status --short
~~~

### 4.5 VM/板端单 operator 门

只有 Windows 无法提供必要 POSIX/ARM/Linux ABI，或当前卡明确要求板端验收时，才进入 VM/板端。先检查：

~~~powershell
$VmBoardLock = Join-Path $RepoRoot 'repo_ignored\locks\resources\vm_board.lock'
if (Test-Path -LiteralPath $VmBoardLock) {
    Get-Content -LiteralPath $VmBoardLock
    throw 'vm_board.lock is already held'
}

Get-CimInstance Win32_Process |
  Where-Object {
    $_.CommandLine -match 'bitbake|petalinux-build|build_v5_linuxcnc|run_v5_board_acceptance|push_v5_runtime|write_v5_sd_card'
  } |
  Select-Object ProcessId, ParentProcessId, Name, CommandLine
~~~

取得 vm_board.lock 后，仍须通过现有 VM 执行入口检查：

~~~sh
pgrep -af 'bitbake|petalinux-build|cmake --build|build_v5_linuxcnc|run_v5_board_acceptance|push_v5_runtime'
~~~

发现活跃 build、deploy、operator 或 motion 时释放自己的锁并停止；不得强杀或并发。VM 只消费 /mnt/v5-source 的 Windows 只读 owner，唯一投影是 /root/v5-build/temp_source/current，唯一 board build 是 /root/v5-build/board。

## 5. 退役路径物理删除协议

### 5.1 一句话硬规则

退役不是改名、禁用、停止调用或留兼容壳。只有唯一替代 owner 已覆盖输入、输出、readback、安全、构建、安装和部署依赖后，才可确认退役；一旦确认，必须在迁移真实依赖的同一作业卡内物理删除并清理干净，不能留到下一卡。

### 5.2 退役确认门

删除前逐项满足：

1. 唯一替代 owner、ABI、readback 和启动闭包已由需求 owner 与源码证明。
2. 用 Graphify 找消费者和跨模块路径，再逐行核对当前源码；图中无边不等于无依赖。
3. 旧符号、文件名、service 名、CLI 名、SHM/PIDFILE 名、安装路径的真实消费者已全部迁移。
4. 替代路径 focused compile/unit/contract gate 通过。
5. build/CMake、manifest、installer、init/service、SD、package、verifier、policy、tests 和正向文档引用已列入删除清单。
6. 若需要板端关闭，当前进程、打开文件、SHM、PIDFILE、init/rc 链接和安装文件均已纳入清残证明。

任一项不满足：不得删，也不得加 fallback；报告 blocked 及缺失的唯一接受条件。

### 5.3 同卡必须物理删除的范围

- 旧源码文件、声明、定义、调用、include 和只为旧路存在的常量/状态。
- CMake/Make/recipe/manifest/package/install 条目。
- init/service/rc 链接、daemon 启动、CLI 参数、环境开关和测试入口。
- wrapper、alias、兼容转发、disabled 分支、编译开关、空函数和 TODO。
- _old、legacy、bak、backup、archive、repo_ignored 副本、VM 副本、板端手工副本。
- 把旧路径描述为现役 owner 的文档正向引用和示例。

删除只能用 apply_patch 的 Delete File 或精确行删除；禁止宽泛递归删除，禁止先备份，禁止恢复旧 fallback 让 canonical 暂时“能跑”。

### 5.4 正向残留和负向清残必须分开

零残留要求针对能安装、导入、执行、调用、注册或宣传旧实现的正向路径，它们必须为 0。

为清除已部署旧板而保留的单向 rm -f、test ! -e 和 source-absence policy 是负向迁移门禁，不是旧实现。只有 owner 明确要求兼容旧镜像升级时才可逐项 allowlist；它们不得安装、启动、import 或调用旧代码。升级支持窗口结束时，负向 tombstone 也在同卡物理删除。

不得为了追求文本零命中而提前删除唯一旧板清残门，导致现场继续残留旧进程或旧文件。

### 5.5 板端删除边界

禁止 SSH/SCP/sed/tee/vi/nano 直接改板端产品文件。板端清残必须来自 Windows canonical source 构建的 installer/package/rootfs；验证只读检查 process、file、service、link、SHM 和 readback。

### 5.6 每卡固定零残留检查

精确旧符号检查：

~~~powershell
rg -n --encoding utf-8 -F '<OLD_SYMBOL_OR_PATH>' -- <本卡精确目录>
if ($LASTEXITCODE -eq 0) { throw 'retired positive survivor remains' }
if ($LASTEXITCODE -gt 1) { throw 'rg survivor scan failed' }
~~~

文件存在性、manifest 和 service 检查必须逐项列出，不能只搜索一个函数名。最终 retirement.tsv 必须记录：

- 删除的文件和符号。
- 删除的 build/install/service/test/doc 正向入口。
- 仍保留的每个负向清残门及 owner 理由。
- focused gate 与源码、package、板端 survivor 计数。

## 6. 作业卡选择表

先完成 J00。本次 checkout 已知确认退役项优先于性能重构，因此 R01、R02 必须先处理；以后发现新的确认退役路径，也立即插到当前未开始性能卡之前。

| 顺序 | 卡号 | 唯一缺陷 | 进入条件 |
| ---: | --- | --- | --- |
| 1 | J00 | 身份与可比基线 | 每次新实施必做 |
| 2 | R01 | 无 caller 的 UI cache-sync 符号 | 重新证明仅声明+定义、无 caller |
| 3 | R02 | 已删除 Python owner 的现役正向文档残留 | 当前 owner 已由 native/HAL 替代 |
| 4 | J10 | fresh-SD Position 启动闭包 | 静态检查仍缺 service link |
| 5 | J11 | verifier 写 canonical block | 当前脚本仍能启动第二 writer |
| 6 | J12 | 轴设零重启错误 publisher | reload_position_publisher 仍重启 WCS |
| 7 | J13 | publisher singleton/writer identity | PIDFILE 丢失、PID 复用、orphan 未覆盖 |
| 8 | J14 | 升级双 writer 与旧 owner 退役 | installer 仍可能先启新后停旧 |
| 9 | J20 | DYNAMIC 文本无条件升级 POSE | 当前源码检测谓词成立 |
| 10 | J21 | 最终显示字符串重复写控件 | 同显示字符串仍触发 set_text/invalidate |
| 11 | J30 | relay payload 多次整块复制 | payload 生命周期可被测试锁定 |
| 12 | J31 | UI dirty FIFO 高频重连与锁范围 | relay 缺席时 UI 约 100Hz 重试 |
| 13 | J32 | 慢客户端反压/无界积压 | 两客户端测试能复现 |
| 14 | J40 | Publisher 超期零等待追赶 | 连续 40/50ms 注入可复现 |
| 15 | J41 | State freshness laundering | source 时间未端到端透传 |
| 16 | J50 | 每帧 open/rename/mmap/msync | VM strace 证明 steady-state 系统调用 |
| 17 | J60 | 64 个全窗 line 对象重复遍历 | J20 已关闭且绘制仍为主增量 |
| 18 | J70 | dense-G1 只读归因 | 独立样本且同窗可比 |
| 19 | J71 | ack/eager 低开销计数缺口 | perf 无法裁决 J70 |
| 20 | J72 | usrmot ack polling 热点 | J70/J71 证明热点 |
| 21 | J73 | emcTaskEager 无界占用 | J72 后仍是主要热点 |
| 22 | J90 | 板端综合验收 | 用户明确要求或集成/发布 |

不得按“看起来收益大”跳卡。前卡已由当前源码和 focused gate 证明正确时，在 precheck.txt 写明证据并跳到下一卡；不得为了制造修改而重复改。

## 7. 作业卡固定模板

每张实施卡必须在 precheck.txt 填完以下字段后才能编辑：

~~~text
job_id=
risk=P0|P1|P2
single_defect=
owner_req=
owner_file=
owner_sections=
head=
focused_diff=
predecessor=
entry_predicate=
source_detection_result=
allowed_files=
required_locks=
required_deletions=
forbidden_files_behaviors=
windows_gates=
vm_gate_and_trigger=
board_gate_and_trigger=
success_markers=
failure_action=
status_ceiling=
next_job=
~~~

通用生命周期固定为：

1. 检测：证明缺陷仍存在，不能凭本文历史描述直接改。
2. 锁：只锁 allowed_files，发现重叠用户改动或活跃锁就停。
3. 测试先行：新增能在旧代码失败、在新代码通过的 focused 断言。
4. 编辑：只改一个 owner 链，不顺手整理下一缺陷。
5. 删除：本卡确认退役的旧路同卡物理删除。
6. Windows gate：运行最近可用 compile/unit/contract/static gate。
7. VM gate：只有 Windows 不可替代时运行最小 target；单包未过不进 rootfs/image。
8. survivor sweep：源码、构建、manifest、安装、服务、测试、文档分层扫。
9. 可选板端：只在授权条件成立时执行原始 operator/readback。
10. 清理与交付：释放本线程锁和探针，按第11节报告。

## 8. 可直接执行的作业卡

### J00：绑定身份并建立可比窗口

- 风险：P0，只读。
- 允许修改：产品源码无；task-only sampler 只进 repo_ignored/cpu1_optimization/scratch。
- 必做：执行第4.1节；记录 HEAD、status、focused diff、Graphify graph identity、相关 source/build/deploy identity。
- 本地结束：若当前卡只做源码/文档修复，不得把板端运动基线当作前置阻塞。
- 板端触发：用户要求性能实测、立即上板、集成或发布时，才采 10 秒同窗。
- 固定场景：Machine Off+两个远程端；原始 cc-ac 打开暂停；真实 UI Start 运行 cc-ac；独立 deterministic dense-G1。
- 固定字段：每核 busy、进程/线程 ticks、affinity/last CPU、relay dirty/payload/full/repair/backlog、UI pose/projector/draw/dirty/full-screen、Position/State generation/source age/missed slots、milltask/perf、queue/underflow、servo overrun、DC/WKC、编码器连续性。
- 排除：SSH/dropbear、verify、one-shot、halcmd、sampler、启动瞬态。
- 通过：各场景字段、时间基准、客户端数和 identity 一致，探针退出且无残留 PID。
- 失败：identity 不一致、无法隔离诊断负载、安全运动条件不足时停止；不修改产品源码。
- 状态上限：只读基线不构成功能 verified。
- 下一卡：R01。

### R01：物理删除无 caller 的 shell cache-sync 残留

- 风险：P1。
- owner：UI 首帧/page cache owner；先从索引重新定位。
- 当前检测谓词：shell_sync_current_page_cache_if_dirty 只在 board/app/src/v5_ui_shell_navigation.c 定义、在 board/app/src/v5_ui_shell_internal.h 声明，调用者为 0。
- allowed_files：
  - board/app/src/v5_ui_shell_navigation.c
  - board/app/src/v5_ui_shell_internal.h
- 编辑前命令：

~~~powershell
$Hits = rg -n -F 'shell_sync_current_page_cache_if_dirty' -- board/app/src
if ($LASTEXITCODE -ne 0) { throw 'R01 pre-scan failed' }
$Hits
if (($Hits | Measure-Object).Count -ne 2) { throw 'R01 predicate changed; re-analyze callers' }
~~~

- 动作：用 apply_patch 删除完整函数定义、声明和只为该函数存在的依赖；不得留空函数、wrapper、alias、#if 0、disabled 或 TODO。
- 新测试：无需为死函数新增行为测试；必须证明调用为 0，并编译真实消费者 target。
- focused gate：git diff --check；取得 vm_board.lock 后在 canonical VM 构建 v5_lvgl_shell，因为该 target 含 POSIX 依赖。
- 删除后命令：

~~~powershell
rg -n -F 'shell_sync_current_page_cache_if_dirty' -- board/app/src
if ($LASTEXITCODE -eq 0) { throw 'R01 survivor remains' }
if ($LASTEXITCODE -gt 1) { throw 'R01 survivor scan failed' }
~~~

- 通过：符号零命中，v5_lvgl_shell 最小构建通过，无新入口。
- 失败：出现真实 caller 时停止删除，回到 owner/consumer 迁移分析。
- 状态上限：local_verified_only。
- 下一卡：R02。

### R02：清除退役 Python owner 的正向残留

- 风险：P0 文档/owner 校正。
- 唯一替代：
  - 开机 resident 登记：board/services/microkernel/v5_microkernel_manifest.c。
  - G53/RTCP native actual：linuxcnc/src/hal/user_comps/v5_native_hal_owner.comp。
  - safety：linuxcnc/src/hal/components/v5_safety_latch.comp。
  - HAL 装载：board/linuxcnc/hal/v5_bus_2ms.hal。
- 退役文件：
  - v5_rtcp_status_publisher.py
  - v5_g53_geometry_memory_owner.py
  - v5_native_safety_latch_owner.py
- 进入谓词：三个实现文件已不存在，但以下 owner 文档仍把它们写成现役路径：
  - 功能/需求真源索引.md
  - 功能/0-3native_helper白名单与验收说明.md
  - 功能/3-4UI刀路绘图顺序与状态稳定方案.md
- allowed_files：上面三个文档；行为语义冲突时加真实 owner 文档，先更新 owner 再更新索引。
- 动作：把现役正向引用改成当前 native/HAL owner；不是在旧名字前加“退役”继续当入口。若任何退役实现、service、manifest、test、fallback 文件重新出现，使用 apply_patch 直接删除。
- 负向 allowlist：
  - write_v5_sd_card.sh 中 staged-rootfs rm -f。
  - install_v5_runtime.sh 中 stop/remove/rm -f。
  - verify_v5_board_runtime.sh 中 test ! -e。
  - check_v5_runtime_policy.py 中 source absence 断言。
- 负向项只能证明或执行单向清残；不得安装/启动/import 旧 owner。是否继续保留由升级支持 owner 裁决；支持窗口结束时也物理删除。
- focused gate：

~~~powershell
python board/tools/docs/verify_v5_document_routes.py --strict-details
if ($LASTEXITCODE -ne 0) { throw 'R02 document route failed' }

python board/tools/deploy/check_v5_runtime_policy.py
if ($LASTEXITCODE -ne 0) { throw 'R02 runtime policy failed' }

git diff --check
if ($LASTEXITCODE -ne 0) { throw 'R02 diff check failed' }
~~~

- 通过：现役正向引用为 0；实现/service/manifest/test/fallback 为 0；负向 allowlist 逐项有迁移理由。
- 状态上限：文档已更新；不等于板端功能通过。
- 下一卡：J10。

### J10：fresh-SD Position 启动闭包

- 风险：P0 lifecycle。
- allowed_files：
  - board/tools/petalinux/write_v5_sd_card.sh
  - board/config/deploy/v5_runtime_deploy_manifest.tsv
  - board/services/state_publisher/init.d/v5-position-status-publisher
  - board/tools/deploy/check_v5_runtime_policy.py
  - 新增的 focused fresh-rootfs service-link test。
- 检测：对 staged rootfs 运行静态模拟，证明文件安装但启动链接缺失时旧测试失败。
- 动作：让 fresh-SD 与 canonical manifest/install owner 生成同一启动链接；不得复制第二 init 脚本。
- 删除：删除任何只为旧 WCS/Position 重复启动路存在的 manifest/link 条目。
- gate：sh -n write_v5_sd_card.sh；focused staged-rootfs test；runtime policy；git diff --check。
- 通过：空 staged rootfs 只产生一个 Position service 文件和一组 canonical rc 链接。
- 失败：缺链接、重复链接、引用不存在脚本即停止。
- 下一卡：J11。

### J11：verifier 只读 canonical 状态

- 风险：P0 owner/writer。
- allowed_files：
  - board/tools/deploy/verify_v5_board_runtime.sh
  - 对应 focused verifier contract test。
- 检测：测试证明 verifier 当前能启动 --once writer 或写 canonical WCS/operator-error block。
- 动作：verifier 对 canonical block 只读；需要 writer 自测时使用独立临时路径并在退出时删除，不得复用产品 SHM/file。
- 删除：删除 second-writer 启动、canonical 输出参数和只为它存在的 cleanup。
- gate：sh -n；focused contract；runtime policy；git diff --check。
- 通过：verifier 前后 canonical generation/writer identity 不变，临时路径清空。
- 下一卡：J12。

### J12：轴设零不再重启错误 publisher

- 风险：P0 actual/readback。
- owner：参数保存/drive action 与 Position actual owner。
- allowed_files：
  - board/services/drive_profile/v5_settings_action_runtime.py
  - board/services/drive_profile/v5_settings_restart_smoke.py
  - owner 明确要求的新 focused readback test。
- 唯一设计：轴设零动作不重启 Publisher。删除 reload_position_publisher 函数及调用，动作成功等待 canonical Position generation/readback 前进并验证 actual。
- 若当前 owner/API 没有可等待的 Position generation/readback：停止并先更新唯一 owner；不得改成重启 WCS、sleep 固定时间或兼容壳。
- gate：py_compile；v5_settings_restart_smoke.py；新增 readback success/timeout/stale test；runtime policy。
- 通过：没有 restart helper；成功只由 fresh Position actual/readback 裁决；超时明确失败。
- 下一卡：J13。

### J13：Position 全生命周期 singleton 和 writer identity

- 风险：P0 owner/lifecycle。
- allowed_files：
  - board/services/state_publisher/v5_position_status_publisher.py
  - board/services/state_publisher/v5_position_status_publisher_test.py
  - board/services/state_publisher/init.d/v5-position-status-publisher
  - board/tools/deploy/check_v5_runtime_policy.py
- 动作：进程启动即获取全生命周期独占锁；canonical 输出携带可核对 writer identity。PIDFILE 只作诊断，不能作唯一 singleton 证明。
- 测试：PIDFILE 丢失、陈旧 PID、PID 复用、orphan、并发启动；第二实例必须 fail-closed，第一实例继续。
- 删除：删除仅依赖 kill -0/PIDFILE 判定唯一性的旧分支和 fallback。
- gate：py_compile；Position focused test；新增 singleton test；runtime policy。
- 通过：一个进程、一个 lock owner、一个 writer identity；异常退出后可受控恢复。
- 下一卡：J14。

### J14：升级先停旧 writer，再启新 owner

- 风险：P0 deploy。
- allowed_files：
  - board/tools/deploy/install_v5_runtime.sh
  - board/config/deploy/v5_runtime_deploy_manifest.tsv
  - board/tools/deploy/check_v5_runtime_policy.py
  - focused upgrade-order test。
- 动作：安装前先停止并确认旧 writer 消失，再替换文件/链接，最后只启动新 owner；升级中断必须保持无双 writer 的 fail-closed 状态。
- 删除：确认退役的旧 WCS/Position service、manifest、rc link、CLI 和 package 条目同卡物理删除；现场清残 tombstone 按第5.4节处理。
- 测试：旧进程活跃、PIDFILE 丢失、stop timeout、安装中断、重试安装。
- gate：sh -n；upgrade-order test；runtime policy；git diff --check。
- 通过：任意时刻最多一个 canonical writer，安装完成仅新 owner 存活。
- 下一卡：J20。

### J20：DYNAMIC 与 POSE 分类解耦

- 风险：P1 UI。
- allowed_files：
  - board/app/src/v5_ui_shell_refresh.c
  - board/app/src/v5_ui_shell_internal.h
  - board/app/src/v5_ui_shell_refresh_smoke.c
  - board/app/CMakeLists.txt
- 检测谓词：当前 v5_ui_shell_refresh_once 在主页面把任意 V5_MAIN_PAGE_REFRESH_DYNAMIC 无条件附加 V5_MAIN_PAGE_REFRESH_POSE。
- 动作：文本、倍率、modal、following-error 和慢诊断只置 DYNAMIC；只有实际姿态、active model、RTCP/model geometry 或真正影响投影的字段置 POSE。fresh SHM 帧仍进入 model。
- 测试先行：新增 v5_ui_shell_refresh_smoke，直接覆盖仅文本变化、MCS/CMD_MCS 姿态变化、active-model/RTCP 变化、hidden page。
- 删除：删除无条件 DYNAMIC→POSE 分支和只为旧耦合存在的标志传播。
- Windows 辅助 gate：v5_ui_display_models_smoke。
- VM focused gate：取得 vm_board.lock 后构建并运行新 v5_ui_shell_refresh_smoke 和 v5_lvgl_shell；现有 v5_ui_shm_refresh_smoke、v5_main_page_smoke 不得冒充覆盖 v5_ui_shell_refresh.c。
- 通过：仅文本变化时 pose/projector 计数不增长；真实姿态/模型变化仍刷新；30Hz actual 未降频。
- 下一卡：J21。

### J21：最终显示字符串去重

- 风险：P1 display-only。
- allowed_files：
  - board/app/src/v5_coordinate_panel.c
  - board/app/src/v5_main_page_view.c
  - board/app/src/v5_coordinate_digits_smoke.c
  - board/app/src/v5_ui_display_models_smoke.c
- 动作：actual 保留原值和原采样率；只比较最终显示字符串，字符串不变时不调用 lv_label_set_text、不 invalidate。诊断仍按 5Hz 到期，但显示桶不变不写控件。
- 禁止：修改 SHM/native actual、控制容差、动作判断，或通过降低 30Hz 采样达标。
- Windows gate：

~~~powershell
cmake --build $BuildDir --config Release --target v5_coordinate_digits_smoke v5_ui_display_models_smoke --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'J21 build failed' }
& (Join-Path $BuildDir 'app\Release\v5_coordinate_digits_smoke.exe')
if ($LASTEXITCODE -ne 0) { throw 'coordinate digits failed' }
& (Join-Path $BuildDir 'app\Release\v5_ui_display_models_smoke.exe')
if ($LASTEXITCODE -ne 0) { throw 'display models failed' }
~~~

- VM gate：若修改 v5_main_page_view.c，运行 v5_main_page_smoke。
- 通过：正负边界、零附近、真实跨显示桶变化均正确；同字符串不产生 dirty。
- 下一卡：J30。

### J30：relay 一个 generation 只固化一次 payload

- 风险：P1 relay。
- allowed_files：
  - board/services/ui/v5_remote_ui_state.py
  - board/services/ui/v5_remote_ui_shared_payload.py
  - board/services/ui/v5_remote_ui_relay_coalesce_smoke.py
- 动作：消除 mmap→bytearray→bytes 中可证明多余的整 payload copy；一个 generation 只产生一个不可变共享 payload。
- 保持：frame/base-frame、rect、initial/full repair 协议和全部诊断计数。
- 测试：历史 payload 不被后续 mmap 写入污染；两个消费者共享同 generation；撕裂/越界失败。
- gate：

~~~powershell
python -m py_compile board/services/ui/v5_remote_ui_state.py board/services/ui/v5_remote_ui_shared_payload.py
if ($LASTEXITCODE -ne 0) { throw 'J30 py_compile failed' }
python board/services/ui/v5_remote_ui_relay_coalesce_smoke.py
if ($LASTEXITCODE -ne 0) { throw 'J30 smoke failed' }
python board/tools/deploy/check_v5_runtime_policy.py
if ($LASTEXITCODE -ne 0) { throw 'J30 policy failed' }
~~~

- 通过：shared build/generation=1，协议不变，无新增 full repair。
- 下一卡：J31。

### J31：UI dirty FIFO 有界重连与最小锁区

- 风险：P1 UI/IPC。
- allowed_files：
  - board/app/src/v5_lvgl_remote_display.c
  - board/app/src/v5_lvgl_remote_display.h
  - 新增 board/app/src/v5_remote_dirty_fifo_smoke.c
  - board/app/CMakeLists.txt
- 动作：relay 无 reader 时不在 UI 主循环约 100Hz 重复 mkdir/mkfifo/open；使用单调时钟的有界退避，成功后复位。物理 framebuffer 格式转换在 remote mmap 锁外完成，锁只覆盖共享帧写入和 generation 提交，解锁后通知 FIFO。
- 禁止：等待网络、阻塞 open、无限退避、把发送搬回 UI。
- 测试：无 reader、reader 晚到、EPIPE、ENXIO、恢复、连续 dirty；断言重试上限和恢复延迟。
- VM gate：构建/运行 v5_remote_dirty_fifo_smoke 和 v5_lvgl_shell。
- 通过：无 relay 时重试有界，relay 恢复后自动重连，UI loop 不阻塞。
- 下一卡：J32。

### J32：慢客户端 latest-only 背压

- 风险：P1 relay protocol。
- allowed_files：
  - board/services/ui/v5_remote_ui_relay.py
  - board/services/ui/v5_remote_ui_shared_payload.py
  - board/services/ui/v5_remote_ui_relay_coalesce_smoke.py
- 动作：每客户端队列有界；只丢弃尚未发送的中间显示帧，保留最新可恢复帧和 initial/full repair。输入事件不能丢，慢客户端不能反压 producer 或正常客户端。
- 测试：一个正常客户端+一个人工慢客户端；断言 backlog 上限、drop 计数、正常客户端延迟和 repair 不循环。
- gate：py_compile；relay coalesce smoke 的 normal、shared-producer、slow-client 三种模式；runtime policy。
- 通过：内存/队列有界，正常客户端不被拖慢，无高频 repair。
- 下一卡：J40。

### J40：Publisher 绝对 deadline 和 latest-wins

- 风险：P1 cadence。
- allowed_files：
  - board/services/state_publisher/v5_polling_cadence.py
  - board/services/state_publisher/v5_position_status_publisher_test.py
  - board/services/state_publisher/v5_state_publisher_service.c
  - board/services/state_publisher/v5_state_publisher_cadence_smoke.c
- 动作：以绝对 deadline 推进 30Hz；超期跳到下一个未来 slot，丢弃尚未发布的陈旧显示工作并主动 sleep/yield；禁止连续零等待追赶。
- 测试先行：连续注入 40ms/50ms work，旧实现应失败；断言无连续零等待、generation 单调、正常负载仍 30Hz。
- gate：

~~~powershell
python -m py_compile board/services/state_publisher/v5_polling_cadence.py
if ($LASTEXITCODE -ne 0) { throw 'J40 py_compile failed' }
python board/services/state_publisher/v5_position_status_publisher_test.py
if ($LASTEXITCODE -ne 0) { throw 'J40 position test failed' }
cmake --build $BuildDir --config Release --target v5_state_publisher_cadence_smoke --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'J40 C build failed' }
& (Join-Path $BuildDir 'app\Release\v5_state_publisher_cadence_smoke.exe')
if ($LASTEXITCODE -ne 0) { throw 'J40 cadence smoke failed' }
~~~

- 通过：不 busy-loop、不补历史帧、不降正常 30Hz、不提 realtime/CPU0。
- 下一卡：J41。

### J41：source timestamp 端到端透传

- 风险：P0 ABI/freshness。
- owner：[功能/0-4共享内存.md](../功能/0-4共享内存.md) 及索引命中的 native actual owner。
- allowed_files：
  - board/services/state_publisher/v5_position_status_publisher.py
  - board/services/state_publisher/v5_machine_status_projection.py
  - board/services/state_publisher/v5_machine_status_projection_test.py
  - board/services/state_publisher/v5_state_publisher_service.c
  - board/services/state_publisher/v5_status_shm_mmap.c
  - board/app/src/v5_status_shm_reader.c
  - 若 ABI/语义变化，先修改唯一 owner 文档和快读卡。
- 唯一设计：source acquisition timestamp 与 generation 从 Position 透传到 State/SHM/UI；State 自己的 publish 时间只能是独立字段，不能覆盖 source 时间。
- 测试：旧 Position+新 State 组合必须 stale/degraded；时间回拨、generation 重复、CRC/seq 错误均 fail-closed。
- 删除：删除用 current time 洗新 freshness 的赋值和兼容 fallback。
- gate：Python projection tests；Windows cadence smoke；POSIX SHM tests 在 VM。
- 通过：source age 可追溯，旧数据不被标 fresh。
- 下一卡：J50。

### J50：Position/State 常驻 fd/mmap 原子迁移

- 风险：P0 ABI/lifecycle。
- 前置：J13、J14、J41 已通过；若 ABI 变化先改 owner。
- allowed_files：
  - board/services/state_publisher/v5_position_status_publisher.py
  - board/services/state_publisher/v5_wcs_status_codec.py
  - board/services/state_publisher/v5_native_sample.c
  - board/services/state_publisher/v5_status_shm_mmap.c
  - board/services/state_publisher/v5_status_shm_mmap.h
  - board/app/src/v5_status_shm_reader.c
  - 对应 ABI/projection/smoke tests。
- 唯一设计：固定 inode，启动时常驻 fd/mmap，SeqLock 提交，reader 有限重试。不得并存 temp+rename 与常驻 mmap 两套 writer。
- 测试：writer crash、奇数 seq、CRC 错、reader 超限、重启、inode identity、单 writer、source timestamp。
- 退役删除：替代路径通过后，同卡删除每帧 temp file/rename/open/ftruncate/mmap/msync/munmap/close 旧路及相关测试入口。
- VM gate：

~~~sh
cmake --build /root/v5-build/board \
  --target v5_state_publisher v5_state_publisher_smoke v5_ui_shm_refresh_smoke \
  -j2

/root/v5-build/board/app/v5_state_publisher_smoke \
  --path /tmp/v5_state_publisher_smoke.bin \
  --frames 3 \
  --interval-ms 1 \
  --unlink-after

/root/v5-build/board/app/v5_ui_shm_refresh_smoke
~~~

- 性能证明：steady-state strace -f -c 不再出现每帧 open/ftruncate/mmap/msync/munmap/rename。
- 失败：reader 持有旧 inode、无限自旋、旧缓存补真值、控制路径开始消费显示 SHM。
- 下一卡：J60。

### J60：单一 retained custom-draw 刀路 owner

- 风险：P1 UI renderer。
- 前置：J20 已关闭；同窗仍证明 renderer 为主要增量。
- allowed_files：
  - board/app/src/v5_main_page_toolpath_primitives.c
  - board/app/src/v5_main_page_toolpath_geometry.c
  - board/app/src/v5_main_page_toolpath_program.c
  - board/app/src/v5_main_page_model_projection.c
  - board/app/src/v5_main_page_model_projector_smoke.c
  - board/app/src/v5_main_page_smoke.c
  - board/app/CMakeLists.txt
- 唯一设计：一个 retained custom-draw 对象拥有投影后的 polyline；每帧遍历有效边一次，只画与 clip/dirty tile 相交的 segment。
- 保持：黄色 1px、G0/G53 断线/排除、图层顺序、AC/BC、RTCP-WCS follow、冻结 Fit、四视角、最多 512 点。
- 退役删除：64 个全窗口 bbox line 对象数组、创建/更新路径、只为旧对象存在的常量和测试入口；不得 disabled 保留。
- Windows gate：

~~~powershell
cmake --build $BuildDir --config Release --target v5_main_page_model_projector_smoke v5_ui_display_models_smoke --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'J60 build failed' }
& (Join-Path $BuildDir 'app\Release\v5_main_page_model_projector_smoke.exe')
if ($LASTEXITCODE -ne 0) { throw 'projector smoke failed' }
& (Join-Path $BuildDir 'app\Release\v5_ui_display_models_smoke.exe')
if ($LASTEXITCODE -ne 0) { throw 'display models failed' }
~~~

- VM gate：构建并运行 v5_main_page_smoke、v5_program_runtime_smoke。
- 测试：当前约 361 点 cc-ac、最大 512 点、clip 边界、wrap 断线、hidden page、30 帧旋转、dirty count/overflow/full-screen。
- 失败：整屏 canvas、轨迹降频、黄色轨迹语义变化、修改 G 代码。
- 下一卡：J70。

### J70：dense-G1 只读归因门

- 风险：P0，只读。
- 允许修改：产品源码无；样本和 probe 仅 repo_ignored/cpu1_optimization/scratch。
- 场景：保持与 cc-ac 相同页面、remote 客户端数、Machine 状态、采样字段和窗口；只替换为独立 deterministic dense-G1。
- 可用证据：perf 函数/调用栈采样、milltask 线程 ticks、commands/s、queue depth/underflow、servo/DC/WKC。
- 进入 LinuxCNC 的必要结果：dense 相对 cc-ac 的主要新增持续负载位于 milltask/usrmot 或 emcTaskEager，且 UI/relay/Publisher 增量已分离。
- 当前仓库没有 ack polls/command 和 wait-time 计数器；不得伪造。perf 已能裁决则直接到 J72；不能裁决才到 J71。
- 不满足：关闭 dense 分支，不改 LinuxCNC。
- 下一卡：J71 或 J72。

### J71：最小低开销 ack/eager instrumentation

- 风险：P0 LinuxCNC。
- allowed_files：
  - linuxcnc/src/emc/motion/usrmotintf.cc
  - linuxcnc/src/emc/motion/usrmotintf.h
  - 最小 focused instrumentation test。
- 动作：只增加 poll count、wait ns、timeout、command count 的低开销计数/readout；不改变等待、queue、timeout 或调度语义。
- 禁止：同时改 emctaskmain.cc、CPU0、servo 或产品日志热写盘。
- gate：最小受影响 LinuxCNC target/package；不能用 build_v5_linuxcnc_petalinux.sh --focused，因为它继续 rootfs。使用已登记 package-only 层，且先核对 projection identity。
- 成功标记：V5_LINUXCNC_PACKAGE_ONLY_COMPILE_OK、INSTALL_OK、PACKAGE_OK、V5_LINUXCNC_PACKAGE_ONLY_OK。
- 通过：计数开销有界、默认无高频日志、能裁决 J72。
- 下一卡：J72。

### J72：usrmot acknowledgment 有界等待

- 风险：P0 motion。
- 前置：J70/J71 证明 usrmotWriteEmcmotCommand polling 是主要热点。
- allowed_files：
  - linuxcnc/src/emc/motion/usrmotintf.cc
  - 最小 queue/ack/abort/timeout focused test。
- 唯一设计：短暂有界自旋后进入 sequence/event/futex 类有界等待；保持 command number、receipt、Abort、Pause、E-stop、timeout 语义。
- 禁止：同卡改 emctaskmain.cc、servo 周期、CPU0 affinity 或整个 milltask 调度。
- gate：最小 LinuxCNC compile/install/package；queue length、queue-buster、abort/timeout、高速 command mock。单包未过禁止 rootfs/image。
- 板端触发：只有用户要求时；dense 下 poll/CPU 降、queue 无 underflow、servo overrun=0、DC/WKC 稳定、cc-ac 无回归。
- 下一卡：J73。

### J73：emcTaskEager 有界让出

- 风险：P0 motion。
- 前置：J72 已通过，dense 同窗仍证明 emcTaskEager 为主要热点。
- allowed_files：
  - linuxcnc/src/emc/task/emctaskmain.cc
  - 对应 task/queue focused test。
- 唯一设计：有界 command batch/时间片；只在 queue 高水位安全时 yield，低水位立即恢复投喂。
- 保持：100 行等价前瞻/时间余量、连续进给、queue-buster、Pause/Resume、E-stop。
- 禁止：再次修改 ack 实现、降低 lookahead/INTERP_MAX_LEN、迁入 CPU0。
- 失败：任一 underflow、运动停顿、Pause/E-stop 变慢、CPU0 抖动上升即停止。
- 下一卡：J90。

### J90：板端综合验收

- 风险：P0 board/motion。
- 进入：用户明确要求立即上板、当前任务本身是板端验收、集成里程碑或发布；所有相关本地/包层卡已通过。
- 前置阻塞：需求索引要求一次 Home/Open 后连续三轮 Start→1秒→急停，轮间只取消急停；自动闭环测试方式正文仍有每轮重新 Home/Open 的冲突。进入三轮前先同步唯一 owner 正文；不得并行执行两套流程或自造第三套。
- 矩阵：
  1. Windows source identity 与板端 ELF/script/hash 一致。
  2. remote 0/1/2 对比，正式窗口保持两个客户端和输入可用。
  3. Machine Off 空闲 10 秒。
  4. 原始模型匹配 cc-ac 打开暂停 10 秒。
  5. 主页面可见，真实 UI Start 运行 cc-ac。
  6. 同运动隐藏主页面，分离 renderer 成本。
  7. 一个正常客户端+一个慢客户端。
  8. 独立 dense-G1；不能替代 model-matched golden motion。
  9. CPU1 压力 30至60秒，证明 CPU0 连续、无 underflow/overrun/DC/WKC/encoder 异常。
  10. 最后真实急停并 fresh 回读 estop_active=1、machine_enabled=0。
- 通过：原始 operator/control path、fresh native actual、model-matched golden loop、安全回读齐全，才可 board_verified。
- 否则：最多 local_verified_only 或 source_only，必须列缺失证据。

## 9. 建议性能预算

以下只用于优化判断，不是 owner 需求真源：

| 场景/指标 | 建议目标 |
| --- | --- |
| 两个远程客户端、Machine Off 空闲 | CPU1 低于约 35%至40% |
| cc-ac 连续运动 | CPU1 p99 低于约 80%至85%，不得持续 100% |
| display frame work | p99 小于 33ms，无历史补帧积压 |
| relay shared producer | 建议低于约 10% |
| runtime full frame | initial/真实 repair 外为 0 |
| LVGL invalidation | frame 内不超过 32 项，无 overflow 退整屏 |
| Publisher | 每个 owner 恰好一个实例、锁和 writer identity |
| source freshness | 正常不超过约 2至3 个显示周期，超限明确 stale |
| CPU0 运动域 | servo overrun=0、DC/WKC 稳定、queue 无 underflow |

CPU 数字未达预算但功能、安全和 CPU0 隔离全部满足：报告真实数据并继续定位，不得降功能。CPU 下降但 actual、remote、30Hz 或安全回退：优化失败。

## 10. 全局一票否决

- 确认退役后不物理删除，改成 wrapper、alias、disabled、环境开关、编译开关、旧文件改名、TODO、test-only 入口或 backup。
- 恢复退役 Python safety/RTCP/G53 owner、第二 publisher 或 canonical 暂坏时的旧 fallback。
- 把 UI、relay、Publisher、网络、日志、诊断或整个 milltask 迁入 CPU0。
- 降低 servo、安全、EtherCAT、坐标/刀路 30Hz、按钮/急停 10Hz、诊断 5Hz。
- 停 relay、断远程、减少正式验收客户端或改手动刷新。
- 周期整屏、full-frame polling、整页重建或整屏 canvas 兜底。
- 用旧 SHM、UI cache、dirty、默认值或 State 重写时间冒充 fresh actual。
- 修改 cc-ac/cc-bc、把 G3 离散成 G1、运行过滤副本掩盖热点。
- 未过 J70 就改 LinuxCNC；同卡同时改 usrmot 和 emctaskmain。
- 删除 Start 中 owner 要求的唯一 Set Open。
- focused gate 未过就 rootfs/image/deploy 或宣称 fixed/verified。
- verifier、测试、one-shot 写 canonical block。
- sampler、临时 daemon、本线程锁或测试进程在交付后残留。
- 用 SSH 直接删板端产品文件作为修复。

## 11. 强制交付模板

每卡结束必须按下面顺序报告：

~~~text
job_id:
closed_defect:
owner_req_and_file:
head_and_identity:
changed_files:
physically_deleted_files_symbols_entries:
retained_negative_cleanup_gates_and_reason:
focused_commands_and_exit_codes:
success_markers:
before_after_same_window:
survivor_scan_source_build_manifest_install_service_test_doc:
vm_package_rootfs_deploy_operator_motion_not_run:
locks_probes_temp_processes_cleaned:
board_safe_state:
honest_status=source_only|local_verified_only|board_verified|blocked
exact_blocker_or_missing_test:
next_job_and_entry_condition:
~~~

不得写“已修复”“已通过”“板端可用”而没有对应已执行证据。达到 local_verified_only 后默认交付，不自动进入下一 P0/P1 卡、镜像或板端。

## 附录 A：2026-07-17 历史只读基线

以下是 Machine Off、速度为零、program_line=0、约 5 秒窗口的历史样本，只能用于规划重新测量：

| CPU1 项目 | 历史值 | 解释 |
| --- | ---: | --- |
| CPU1 总忙碌 | 约 56.3% | 空闲余量不足一半 |
| remote relay | 约 26.9% | 空闲第一负载 |
| Position Publisher | 约 8.6% | 30Hz/heartbeat、HAL 读取与发布链 |
| UI | 约 4.4% | 空闲非第一主因，运动时被刀路放大 |
| WCS Publisher | 约 3.2% | 5Hz 慢状态仍有 Python/VFS 成本 |
| milltask | 约 1.8% | 该空闲窗口不是主因 |
| State Publisher | 约 1.0% | 每帧文件/mmap 链的一环 |

当时有两个远程订阅端，约 15.5 frame/s、19.7 dirty event/s，平均 dirty payload 约 50KB/frame；没有 large/full repair。A/C following-error 在千分位显示桶附近跳动。未发现重复 Position/WCS/State 实例，也未发现退役 RTCP/G53/Python safety owner 正在运行。

旧稿中 CPU1 约 25.7%、无 remote dirty、Python safety 应固定 CPU1 已过时。当前 safety owner 是 CPU0 realtime 闭集中的 v5_safety_latch，禁止恢复旧 Python owner。

## 附录 B：根因与场景边界

空闲固定负载：

1. relay 的 Python shared producer、行拷贝、payload 固化和两个客户端发送。
2. Position→State→UI 快链的临时文件/rename、fopen/fread、open/ftruncate/mmap/msync/munmap/close。
3. Publisher 超期后的零等待追赶反馈。
4. following-error/坐标最终字符串变化持续产生 dirty；这不是旧 G 代码脏数据。

cc-ac 是运动放大器：当前 cc-ac_siemens.mpf 为 20 个连续长 G3，按 5 度预览约 361 点。任意 DYNAMIC 被升级 POSE 会重复投影；64 个全窗 line 对象与最多 12 个 dirty rect 的源码上界约 4320 次 draw/帧。随后像素继续经过 LVGL→g_frame→physical/remote mmap→relay payload→socket。

dense-G1 是条件分支：只有同窗证明 milltask/usrmot/emcTaskEager 是相对 cc-ac 的主要新增负载，才允许修改 LinuxCNC。不能用 20 个长 G3 的 cc-ac 证明 dense-G1 ack polling。

“脏数据”判断必须分层：

- live duplicate/writer：必须有进程、锁、writer identity、generation 证据。
- stale source 被洗 fresh：由 source timestamp 链证明。
- 已确认退役代码：不会仅因文件存在自动耗 CPU，但必须按第5节清除，避免错误入口、双 owner 和未来复活。
- 无 caller 死函数：不产生现役 CPU，但确认退役后仍须物理删除，保持唯一实现。

## 附录 C：Graphify 的正确用法

Graphify 有价值的地方：

- 快速查 UI 按钮到 Broker、native、HAL、EtherCAT、FPGA 的跨模块调用。
- 查某函数修改影响哪些消费者。
- 找大文件、循环依赖、重复入口和孤立代码。
- AI 接手新模块时先取局部调用图，减少全仓盲搜。
- 对比 BUS、Pulse、Home、Jog、急停等跨层链路。

Graphify 不能替代：

- 功能/需求真源索引.md 和对应 owner。
- 实际源码、INI、HAL、FPGA 寄存器定义。
- 编译、单元测试和板端运行证据。
- native actual/readback 与安全状态。
- Git 历史和人工架构判断。

固定步骤：

1. 优先使用当前 graphify-out；V5 board 外部图当前位于 D:\graphify\v5-board\graphify-out。
2. reflect --if-stale，记录 graph identity。
3. query 先窄后宽；退役审计扩展 delete、remove、retired、legacy、manifest、install、runtime、verify、service、relay、publisher、toolpath。
4. path/explain 只定位消费者；回源码、manifest、installer、service、tests 逐项复核。
5. 每次查询结束 save-result，记录 expanded_tokens、node_ids、outcome。

本问题图谱用于支持下列分卡，不用于裁决需求：

~~~text
UI shell refresh
  -> dynamic/pose
  -> toolpath projection/render
  -> LVGL dirty/flush
  -> remote framebuffer
  -> relay shared payload

native/HAL position
  -> Position Publisher
  -> State Publisher/SHM
  -> UI refresh

retirement
  -> source/declaration/caller
  -> build/manifest/installer/service
  -> verifier/policy
  -> board process/file/link survivor
~~~

## 附录 D：当前已知命令成功标记

- V5_POSITION_STATUS_PUBLISHER_TEST_OK
- V5_WCS_STATUS_PUBLISHER_TEST_OK
- V5_MACHINE_STATUS_PROJECTION_TEST_OK
- V5_STATE_PUBLISHER_CADENCE_OK
- v5 remote ui relay coalesce smoke: dirty coalescing ok
- v5 main page model projector smoke ok
- runtime policy 成功时可能无 stdout，以退出码 0 为准。
- LinuxCNC package-only：V5_LINUXCNC_PACKAGE_ONLY_COMPILE_OK、INSTALL_OK、PACKAGE_OK、V5_LINUXCNC_PACKAGE_ONLY_OK。

若测试实现或成功标记发生变化，以当前测试源码和退出码为准，并同步本活动指导书；不得只匹配旧字符串。
