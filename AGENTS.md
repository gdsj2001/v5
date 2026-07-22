# AGENTS.md

AI_LOAD_ONLY=true
PROJECT_ROOT=.
RULE_SOURCE=AGENTS.md

## P0_ENTRY_KERNEL

HIGHEST_RULE_1:
- 项目真实可交付结果和用户当前目标是项目内最高目标；规则、文档、门禁、identity、构建梯级和状态词只用于减少错误与证明受影响闭环，不得成为独立工作目标。与当前 owner/输入/target/风险无关的流程必须短路；若通用工具强制执行无关全树扫描、无关子系统验证或重复证明，视为工具/规则缺陷，立即改用或补出最小入口后继续推进，不得让“遵守规则”阻塞项目。
- 日常开发默认目标是最快把当前需求落实到 canonical source 并取得受影响闭环。任何会进入板端产物或影响板端运行的代码、配置、脚本、服务、recipe 或部署项修改，在 Windows focused gate 后必须继续完成最小受影响 target/package 编译、必要 canonical artifact 生成与部署、板端启动就绪检查，以及受影响原始 operator/control 路径的最小测试和 fresh owner readback；`local_verified_only` 只允许作为进行中的中间状态，不得作为这类修改的正常关闭点。只改文档/规则、真正不进入板端且不依赖板端协议的独立主机工具、用户明确要求仅本地修改或真实安全/外部 blocker 才可不做上板；rootfs/image 仅在最小受影响部署确实需要时生成，不得把本规则扩成无关全量重建。
- Windows 是默认执行主机。能在 Windows 正确完成的 compile/check/codegen/package/manifest/schema/test 步骤必须留在 Windows；VM 只承担 Windows 无法提供所需 Linux ABI、PetaLinux/Yocto、ARM 交叉环境、Linux 内核行为或 vendor 能力的最小步骤。
- 正式构建输入必须落到 Windows `D:\v5` 的唯一 canonical owner，并有固定 identity/hash/provenance。VM/BitBake 发现缺项时必须输出 inventory 绑定的机器可读报告并停止；只允许 Windows importer 下载报告中已登记、checksum/许可证齐全的包到 canonical owner，复核后再让 VM 只读消费。VM、BitBake、代理、sstate 或构建脚本不得自行联网补下载，未知输入必须 `blocked`。
- 每份源码只允许 Windows `D:\v5` 内一个持久 owner。VM 只允许 `${VM_BUILD_ROOT}/temp_source/current` 一套由 Windows identity 派生、不可编辑的加速投影；identity 一致可跨任务复用，不一致只按文件 identity/hash 原地更新新增、修改、删除集合，未变文件必须保持 inode/mtime。禁止因总 identity 变化删除重建整棵投影、全量强制复制、第二投影、VM checkout、手工 patch、反向覆盖 Windows 或把投影当 owner。
- 日常工作默认 `incremental-current`：Windows focused gate -> 受影响 target/recipe compile/install/package -> 必要 rootfs/manifest -> 一次 current 镜像。缺项补齐后只重跑报告登记的原失败层并保留有效 sstate/tmp/work/stamp；不得未过单包就跑最终镜像，不得无证据清 kernel/sstate/tmp/downloads/fetch stamp。空 build/downloads/sstate 的断网全量构建只用于用户明确要求、发布/灾难恢复里程碑或离线能力重新认证。
- 日常 focused 修复只校验改动文件和当前 target 实际读取的直接输入，不运行完整源码 inventory、全仓 hash、未触碰 owner identity 或离线闭包认证。仓库中与当前 target 无关的缺项推迟到 full-current、发布或离线全量构建时统一发现并由 Windows importer 补齐；只有当前 target 实际请求的输入缺失才停止当前失败层。
- 只保留一个 mainline、一个实现和一个 owner。开发过程中一旦确认发现退役/弃用/重复/fallback/shadow/bypass 的代码、条件分支或入口，必须立即纳入当前 slice；真实依赖迁移完成后当场物理删除，不得推迟到后续清理，也不得通过改名、disabled、env gate、TODO、测试入口或备份继续保留。

STRICT_STATUS_WORDS=[source_only, local_verified_only, board_verified, blocked]

## P0_ROUTING_AND_PRECEDENCE

AI_READ_SEQUENCE:
1. 读最新用户消息并判断任务类型。
2. 读本文件入口核，不要先全文漫游项目文档。
3. 产品功能/行为先读 `功能/需求真源索引.md` 的“AI 任务阅读包”，定位唯一 `REQ-*` owner。
4. 先读 owner 的 `AI_FAST_READ_BEGIN` 到 `AI_FAST_READ_END`；只继续读命中的 `detail_sections`。字段级实现、冲突或验收时再扩展正文。
5. 读相关源码、当前 `git status` 和 focused diff；不得覆盖用户已有改动。
6. 用户需求、contract 或产品行为发生变化时，先改索引登记的 owner 正文并同步同文件快读卡，再改 source/config/tests。若 owner 已经正确、只是实现没有兑现现有要求，读取并引用 owner 后直接修 canonical source/config/tests，不重复改写文档或把错误现状写回需求。纯提问/审查默认只读。

CONFLICT_PRIORITY:
1. system/developer/tool safety instructions
2. 最新明确用户反馈，作为需求变更输入
3. `功能/需求真源索引.md` 与其中登记的唯一 owner
4. 本文件的流程、放置、锁、删除、验证和交付规则
5. `待做工作/` 的活动输入
6. 旧聊天、旧笔记、代码注释、测试、生成物、VM/板端副本

TASK_ROUTER:
| 任务 | 第一读取 | 第一修改 | 最低关闭 |
| --- | --- | --- | --- |
| 产品行为/需求 | 需求索引 -> 唯一 owner 快读卡/命中章节 | 需求变化先改唯一 owner；现有需求的实现缺陷直接改 canonical source | source/config + focused gates；涉及板端代码或运行行为必须完成最小编译、部署、上板测试和 readback |
| 指定 `待做工作/*.md` | 指定 workdoc -> 需求索引 -> owner | 行为变化先改 owner | 按 owner 实施；workdoc 不替代真源 |
| 规则/文档结构 | 本文件对应节、需求索引 | 唯一 canonical 文档 | 文档路由校验 + `git diff --check` |
| 板端源码 | owner + Windows canonical source | Windows owner | focused -> affected target -> current artifact -> deploy -> operator path |
| 本地非板端代码 | 相关 source/owner | canonical local source | 最近的 compile/unit/contract/static gate |
| 提问/审查/诊断 | 相关 owner/source/运行证据 | 默认不改 | 证据化回答；诊断不自动扩成修复 |

DOC_OWNER_RULES:
- 项目根目录只允许 `AGENTS.md` 这一份 Markdown；其余 canonical 项目 Markdown 全部放在 `功能/`，活动输入继续放在 `待做工作/`。
- `功能/` 保存 settled product truth、跨功能架构和项目指导；`功能/需求真源索引.md` 只登记 REQ、owner、路由和跨功能 owner；一个易变需求只能有一个 owner。
- `AGENTS.md` 只保存每个任务都必须自动加载的执行核，不复制功能字段表、按钮语义、公式、完整错误表或专项验收正文。
- `待做工作/` 只保存活动输入；需求稳定后必须落入 owner。除非用户明确指定，不把进度、命令输出、截图路径、板端结果或完成摘要写入 human Markdown。
- 快读卡只保存 `owner_reqs/read_when/truth/forbidden/readback/impact/acceptance/detail_sections` 指针；正文裁决，禁止平行摘要、JSON 规则库或第二真源。
- 文档结构修改后运行 `python board/tools/docs/verify_v5_document_routes.py --strict-details`。工具只读当前 Markdown，不生成需求副本。

NO_TASK_BOARD=true
FORBIDDEN_PROCESS_DOCS=[AI_并行任务看板.md, MULTI_AI_MODULES.md, FILE_STRUCTURE.md, AI自动作业指导书.md, process.md, 过程.md]

## P0_SOURCE_AND_BUILD_KERNEL

CANONICAL_SOURCE_OWNERS:
- `D:\v5\linux`：Linux kernel/PREEMPT_RT；对应 identity 文件随 owner 保存。
- `D:\v5\linuxcnc`：完整 LinuxCNC 与 V5 native 改动；`board/linuxcnc` 只允许 HAL/INI/runtime/recipe 集成，不得成为第二源码树。
- `D:\v5\board`：LVGL、runtime services、HAL/INI 集成、PetaLinux project、部署和项目工具。
- `D:\v5\vivado_hw_project`：当前产品/SD 使用的 FPGA 功能。
- `D:\v5\new-vivado\z20_v1_5_hw_project`：另一 FPGA 功能，不是当前产品 XSA/bitstream 输入；两项目互不备份、互不 fallback。
- `D:\v3` 只读参考；不得成为 V5 build/runtime owner。
- 根目录是唯一 Git；`v5` 内禁止嵌套 `.git`、第二 worktree、源码压缩包、NAS 可编辑镜像、backup/bak/source copy。

SOURCE_AND_PATH_RULES:
- 产品源码只能在 owning Windows path 修改。VM 投影、VM build tree、板端 `/opt/8ax`、SD、部署目录和 generated output 都不是 source truth。
- 源码/config/script/test/manifest 中的项目内地址必须从 project/resource root 或 caller-relative path 解析；禁止硬编码 `D:\v5`、`C:\...`、VM home、临时目录或板端项目目录。真实 OS/device/API 绝对路径只在 owner 明确登记且 fail-closed 时允许。
- Markdown 链接相对当前 Markdown 文件；最终回复引用真实本地文件时使用绝对可点击路径。
- `.gitignore` 只排除可再生 build/cache/log/evidence/runtime/scratch；必要 source/hardware/recipe/config/schema/recovery input 不得只在 ignored、VM、板端或旧 SD。
- 源码/文档编辑前备份禁止；根 Git 历史是唯一回退。Git 用于恢复和协作，不作为工程质量评分；未获用户明确授权不得 blanket `git add/commit/push`。

OFFLINE_FULL_SD:
- `REQ-V5-OFFLINE-FULL-SD-REPRODUCTION` 的 owner 是 `功能/全局通用配置需求.md`；可执行重建方法由 `功能/SD卡重建.md` 裁决。
- 完整 `D:\v5` 是唯一可携带灾难恢复单元：在支持的断网电脑上，不依赖原机、预装工具链、历史 VM、旧 downloads/sstate、远程 Git/HTTP、NAS、旧 SD 或 ready image，从空缓存生成、写入并全量回读空白 SD，最后板端冷启动。
- proprietary/restricted 工具必须有合法、版本化、校验过的离线介质及激活/许可证步骤在 `v5` 内；法律/厂商边界无法闭合时状态必须是 `blocked`，不得写“以后下载”。
- `origin/main` 加全部可达 Git LFS 对象是唯一 off-host recovery history；恢复声明还需 `HEAD == origin/main`、无必要 untracked/ignored input、Git/LFS/source identity gates 与真实离线生产证明。两者不能互相替代。

VM_BUILD_BOUNDARY:
- `VM_SOURCE_MOUNT_ROOT=/mnt/v5-source` 必须是 Windows owner 的 live read-only mount；`VM_SOURCE_PROJECTION_ROOT=/root/v5-build/temp_source/current` 是唯一派生投影；build output 只能写到 canonical external build directories。
- 日常 focused 切片只验证当前受影响 owner 的文件级差量和 target 直接输入，不预扫其它 owner。需要完整 identity 的 full-current、发布或离线构建中，每个 owner 一轮只验证一次；源码未变时跨 package/rootfs/image 复用，不重复全树 hash、重投影或 remount。identity 变化时只同步机器可读差量并保持未变文件 mtime；只有投影缺失/损坏且严格校验失败或明确空缓存离线认证才允许整棵重建。
- VM 可以保留一套 current image 集合方便普通复核/重写卡；新集合替换旧集合。它是产物副本，不是离线从零证明。
- VM 不得安装/使用 WSL。VM 长期外网访问能力不算第二源码/构建 owner；允许通过正常虚拟网卡直接联网，直连受限时也允许一套常驻 Linux 代理客户端服务，但代理地址、凭据或订阅不得进入项目源码、recipe、镜像、普通文档或构建配置。
- VM 外网只用于系统维护、非构建诊断和用户明确要求的普通联网操作。source import、Git/HTTP source acquisition、BitBake fetch、远程 sstate、configure/compile/package/rootfs/image 和离线认证仍禁止借外网补输入；正式构建前清除大小写 proxy 变量并执行 network-disabled gate。

BOARD_AND_QSPI_BOUNDARY:
- 禁止用 SSH/SCP/sed/tee/vi/nano/重定向直接修改板端产品文件来实现修复；板端访问只用于 inspection/log/runtime probe/safe recovery 和部署由 Windows source 构建的 artifact。
- 临时板端诊断 mutation 不能关闭任务；必须在 Windows owner 重现、从 source 重新部署并清除临时变更。
- QSPI 只作为独立最小 SSH 恢复入口，用于更新/校验 SD；不得扩展为产品 rootfs、功能 fallback 或依赖待修 SD 才能登录。

## P0_ARCHITECTURE_KERNEL

ARCH_OWNER=`功能/系统代码架构硬边界守则.md`
CONTROL_PATH=UI C -> framed UDS native Command Gate -> LinuxCNC/HAL/EtherCAT/native owner
STATE_PATH=microkernel/native resident memory/API -> allowed display projection -> typed SHM -> UI

ARCH_RULES:
- native/microkernel 能拥有的参数、actual、运动、安全、WCS/G92、RTCP、active model、stillness、segment/wcheckpoint 和动作结果必须由 native owner/readback 裁决；UI/Broker/provider 只请求、路由、显示和诊断。
- SHM 只承载 `REQ-SHM-DISPLAY-PROJECTION` 白名单显示投影。SHM epoch/cache/UI dirty/JSON/result/default 不能成为保存、控制、安全、运动或动作成功真值。
- UI、自写脚本、microkernel/runtime service、owner mapping 和热路径资源按 `REQ-PARAM-MEMORY-LIGHTWEIGHT-SAVE` 在开机或 canonical reload 进入 RAM；运行期除明确保存、Program Open、限频事件日志和受控重启外不得读盘、扫描、懒加载或用 wrapper fallback。
- CPU0 专属 microkernel/native realtime motion。UI、Broker、Publisher、provider、日志、构建/部署和诊断不得 pin/affinity 到 CPU0；CPU0 ownership/scheduling 是 P0。
- 控制/状态 owner、ABI、resident lifecycle、service privilege/capability、remote relay exposure 的完整边界只读索引登记 owner，不在本文件复制。

PARAMETER_RULE:
- 微内核/native 已有或可承载的字段先进入 native resident owner/API；drive mapping 已有的驱动私域字段进入 drive owner；只有两者都没有的设置字段才进入自建参数表。
- UI 可持有 edit/dirty 并发请求，但保存成功必须等唯一持久 owner 源位置回读；运行生效必须等 controlled reload 后 native/drive actual。UI 不直接写 runtime INI、linuxcnc.var、tool.tbl、HAL 或 settings_runtime.json。
- `settings_runtime.json` 只允许 drive-only schema/证据；不得回流 WCS/G92/TLO/tool/RTCP/modal/status/runtime INI 真值。

SELF_WRITTEN_CODE_SIZE:
- 500 行限制只约束 V5 自写的产品业务代码和 business/control-flow owner。新增业务 owner、状态机、产品算法、业务 parser/write path 原则上必须少于 500 行；达到 500 行前优先按 page/protocol/domain/precheck/runtime/verify/result 拆分。只有职责单一、边界完整且拆分会破坏原子语义的真正原子级业务实现才允许超过 500 行，并必须能从源码结构直接证明不可安全拆分。
- tools、tests、构建/部署/迁移/验证脚本、docs、generated、pure config/schema/parameter/data/table/constants/ABI 及真正原子声明文件放宽行数，不受 500 行机械限制；仍应保持职责清晰，若其中混入产品业务 owner 或控制真值，必须先把业务部分拆回其 canonical owner。microkernel/native/upstream 文件不为行数规则重排，只做 owner 所需最小改动。

## P0_WORKFLOW_AND_LOCKS

RISK_CLASS:
- P0：native/LinuxCNC/HAL/EtherCAT owner、CPU0、SHM ABI/Publisher、control IPC、motion/safety、参数真值、WCS/G92/RTCP、wcheckpoint/segment/stillness、resident startup、board-visible deploy、fallback survivor。
- P1：普通产品行为、非运动 UI、feature docs、local refactor、non-realtime config。
- P2：不改变语义/contract/owner/schema/deploy 的拼写、注释、格式、链接、test name 和 import annotation。
- 不确定选更高；低风险任务触碰 owner/ABI/lifecycle/control/fallback/board behavior 立即升级。

BEFORE_EDIT:
1. 读取路由到的 owner 和当前 source。
2. `git status` + focused diff；保留用户/其它任务已有改动。
3. 原子获取精确文件锁；同文件已被活跃 owner 锁定时不覆盖、不复制绕路，只做无关未锁工作。
4. 需求变更先 owner，随后 canonical source/config/tests。

FILE_LOCK_PROTOCOL:
- 位置：`repo_ignored/locks/files/<project-relative-path>.lock`。
- 内容只能有 `lock_version=1`、`thread_id`、`file`、`created_at`。
- 只在编辑和立即 focused text/syntax check 期间持有，完成即删除；不得等待 unrelated test/VM/build/deploy/handoff。
- 锁只防同文件并发，不是 source truth；禁止 lock daemon/database/heartbeat/TTL/history/approval queue/repository-wide lock。

FAST_INCREMENTAL_SLICE:
- 先推进当前可交付闭环，再做证明该闭环所必需的最小流程。任何 gate 必须说明其失败如何影响当前 target；不能说明直接影响的 gate 本切片跳过。通用入口若强制验证未触碰的 Linux/kernel/PetaLinux/其它 owner，必须修出 target-only 路径或调用已登记的更低层入口，不得重复运行错误的大入口。
- owner 和直接源码入口一旦明确，下一步必须是编辑或最近 focused command；同一问题在首次具体动作前最多允许两轮只读定位，不得持续扩展架构分析、全仓分类或方案比较。focused gate 通过后，真正非板端代码可交付当前阶段；任何会进入板端产物或影响板端运行的修改必须继续使用现有最小 build/deploy/operator 入口完成受影响上板闭环，不为追求全量 `board_verified` 新建无关 deploy bundle、服务、协议或镜像路径。缺少必要部署能力、板端不可达或存在安全 blocker 时，必须保留实际最高状态并报告未闭合条件，不得把 `local_verified_only` 当作已关闭。
- 日常修复默认一个切片只关闭一个具体缺陷、一个 owner 链和一个最小可部署 target；用户一次提出多个问题时按依赖顺序逐个关闭、逐个验证、逐个交付，除非源码证明它们是同一不可拆原子变更。不得把多个 P0/P1、架构整理、顺手重构或文档清扫打包成几十文件的大切片。
- 进入 VM 前冻结当前缺陷、owner、允许修改文件集和最小 build target。进入 VM 后只修改当前明确失败层直接依赖的 owner/source/config/test/build tool；新发现的独立问题留到下一切片。受影响单包未通过前禁止 rootfs/image。
- 耗时 gate 绑定“受影响输入 identity + gate 实现 identity + 命令参数”。三者未变时复用已通过的 source identity/hash、唯一投影、文档路由、runtime policy 和上游构建层；变化时只重跑受影响 gate 及其下游，不得重复全树扫描制造进度。
- focused gate 以当前 target 能否正确编译/安装/运行所需的最小证据为止；不得先执行完整 source-package inventory、全部 owner identity、全项目 policy 或无关许可证扫描。上述全局闭包只在 full-current、发布、灾难恢复或离线认证时执行。
- 每次跨阶段立即更新可见 plan。活跃命令连续 120 秒没有新输出时必须检查 PID/CPU/子进程/日志/资源锁：仍活跃就报告真实阶段并继续，已退出或锁与进程不一致就立即收集失败证据、停止等待并释放锁。

VM_BOARD_SINGLE_OPERATOR:
- mount/projection refresh、VM native/ARM compile、package、board deploy/restart/operator/motion 是单 operator resource。实际操作前获取 `repo_ignored/locks/resources/vm_board.lock` 并 live-check 进程；命令结束/停止立即释放。
- VM native 和 ARM 编译不得并发；发现活跃 lock/process 时继续本地无关工作，不启动第二构建，不 remount，不强杀/重启 active build。
- canonical paths 固定：`VM_LINUX_BUILD_DIR=/root/v5-build/linux`，`VM_BOARD_BUILD_DIR=/root/v5-build/board`；不得按 AI/task/date 创建第二 source/build tree。

MAINLINE_AND_RETIREMENT:
- 只在当前 Windows mainline 工作；禁止 Git side branch/worktree/per-AI source tree/task source copy。不同 AI 可编辑不同已锁文件，不能交叉覆盖。
- 删除只针对已确认 retired/duplicate/fallback/shadow/bypass 且依赖已迁移的路径；不得借“删除优先”破坏无关用户工作或尚在支持的行为。
- 退役路径不得以 wrapper/alias/env/CLI/test/package/doc example/archive/repo_ignored copy 留存；canonical 暂坏时诚实报状态并修主线，不加第二路。
- 不创建源码/文档 backup、过程文档或任务板；临时 output 进入 `repo_ignored/temp/` 或更具体 ignored evidence/scratch owner。

PROGRESS_AND_BLOCKERS:
- 有当前最小切片内的已知安全下一步就持续推进，不停在 plan、inventory、单字段或首次错误；命令/工具失败是诊断证据，应 fix-forward。不得把“持续推进”解释成自动提高验证等级、扩建部署基础设施或追逐与当前阶段交付无关的 board/image 闭环。
- 只有安全/破坏性审批、真实 external state、活跃 VM/board contention、无法从 owner 裁决的规则冲突或必要用户输入才暂停。
- 同一具体 blocker 经至少 3 次有意义诊断/尝试仍无法推进，报告 attempts、缺失验证、下一接受条件和最强诚实状态；不自动写 `待做工作/遗留.md`。
- commentary 只报告当前动作、关键发现、风险、阻塞和验证结果；省略已自行恢复的 shell quoting/regex/命令试错。

TOOLING:
- 搜索先 `rg`；重复审计/迁移/验证优先现有 focused tool。缺失时先建最小工具并验证，再执行重复工作。
- reusable 项目工具只放 `board/tools/`；task-only probe 放 `repo_ignored/<task>/scratch/`；临时板端 probe 只放 `/tmp/v5_test_tools` 或 `/run/v5_test_tools`，不得进入产品 runtime。
- Python 工具至少 `python -m py_compile` 加 focused test/self-check；shell/C 用最近 syntax/build/contract gate。证据输出持久化前去除 token/password/header/private key/cookie/secret-like env。

## P0_VERIFICATION_AND_DELIVERY

LOCAL_FIRST:
- doc/rule：严格文档路由校验（涉及文档路由时）+ `git diff --check`。
- Python：touched `py_compile` + focused pytest/contract/self-test。
- C/CMake/LVGL：Windows 能执行的 syntax/static/CMake target 先在 Windows；板端 ABI/ARM/PetaLinux 最小不可替代步骤再进 VM。
- owner/native/SHM/parameter 变化先跑现有 `board/tools/` focused audit；工具缺口不能用猜测代替。

BUILD_LADDER:
1. 日常 focused 只证明 Windows 受影响 owner diff、当前 target 直接输入和 VM read-only/唯一 projection 可用；完整 source identity/inventory 留给 full-current、发布或离线构建。
2. 构建最小受影响 target/recipe/package；失败先修该层。
3. 任何会进入板端产物或影响板端运行的修改都必须部署；只生成该最小部署所需的 manifest/rootfs/current artifact，已有 package/hot-deploy 闭包足够时不得追加整张镜像。
4. 需要 current artifact 时只生成一套；不得清未受影响缓存，也不得把最小上板扩成无关 owner、全量 rootfs/image 或发布级认证。
5. 检查 ARM/Linux ELF/ABI（`file`/`readelf -h`）；host/x86/Windows artifact 不得覆盖板端。
6. 原子部署 canonical artifact，确认 source/artifact/deploy identity 和受影响服务/runtime readiness。
7. 走受影响的原始 UI/operator/control path 做最小上板测试，取得 fresh owner actual/readback；运动/安全修改还必须按 owner 完成对应运动证据并恢复 ESTOP/safe state。

FINISH_LINE:
以下表格定义各状态词需要的证据。纯文档和真正非板端代码不强制上板；任何会进入板端产物或影响板端运行的修改默认必须完成最小编译、部署、受影响原始路径测试和 fresh readback，不能以 `local_verified_only` 关闭。完整回归、整张镜像和发布认证仍只按受影响 owner 或里程碑触发。
| Slice | 必须执行 | 允许的正向状态 |
| --- | --- | --- |
| doc/rule only | focused 文档结构/文本检查 | 文档已更新；不是板端功能通过 |
| local non-board code | nearest compile/unit/contract/static gate | `local_verified_only` |
| board program/runtime/visible behavior | local gates + 最小受影响 build + 必要 artifact + deploy/readiness + 受影响 original operator/control path + fresh readback | owner 验收满足后为 `board_verified`；仅到 `local_verified_only` 是未关闭中间状态 |
| motion/Home/Jog/Start/MDI/E-stop/axis/rotary | board closure + fresh native actual + active model 对应 `cc-ac.ngc` 或 `cc-bc.ngc` golden motion | `board_verified` |
| 缺验证/真实 blocker | 明确缺失 test、原因和下一条件 | `source_only` 或 `blocked` |

OPERATOR_AND_UI_PROOF:
- UI 页面与操作流程测试默认使用项目现有抓屏工具取得 fresh board frame，并使用模拟鼠标点击走真实 UI operator path；不得只凭源码、日志、后台接口或旧截图判断页面行为。点击前必须从当前抓屏确认页面、状态和目标控件，点击后必须重新抓屏并核对可见结果。
- UI/operator defect 不得用 direct UDS/linuxcncrsh/SSH backend 直跑替代。模拟点击前先抓 fresh screenshot/remote frame，确认页面/状态/target；每次只做一个小动作并检查后帧/事件。
- 模拟点击不能证明真实触摸校准、real-finger ergonomics、motion-capable closure 或 owner 明确要求的真实输入。
- 先关闭单功能，再跑 full automation/regression；full flow 不是调试未关闭单点的第一工具。
- 缓存/首帧、弹窗、刷新率、dirty rect 与 remote relay 按需求索引对应 owner 验收；禁止用整屏兜底掩盖 dirty 缺口。

PASS_CLAIM_GATE:
- fixed/repaired/done/passed/verified/board_verified/release_ready/works/live 等词必须有对应已执行证据。
- source edit、doc、smoke、dry-run、VM build、board_deployed、direct UDS、旧 result、错误提示或新 guard 都不能替代用户报告路径的真实修复。
- board-visible change 未部署/未走原始 operator path，只能 `source_only` 或 `local_verified_only`；motion 没有 model-matched golden loop 不能通过。
- 所有 motion/board acceptance 结束后保持 ESTOP/safe state，并证明 `estop_active=1`、`machine_enabled=0` 或 owner 等价安全回读。

BOARD_UNREACHABLE_RECOVERY:
- 板端在 deploy/restart/verification 中 SSH 不通或不可达时，必须使用 canonical `board/tools/v5_board_power_cycle.py` 的继电器控制执行开发板 power-cycle，并配合 COM/serial monitoring；随后按顺序 probe ping、SSH、18080、`/remote/info` 和 input readiness。不得把人工断电、反复空等或绕过工具的临时启动方式作为默认恢复路径。
- recovery 只诊断和恢复访问，不能改产品文件或绕过 build/deploy/operator proof；bounded 尝试后才报告 blocker。

FINAL_REPLY_MIN:
- changed files
- verification actually run and result
- board closure state when relevant
- exact blocker/missing test/next condition
- generated artifact/evidence path only when it真实存在且有交接价值

## P1_PATHS_AND_COMMAND_STYLE

PROJECT_ROOT=D:\v5
RUNTIME_SOURCE_ROOT=D:\v5\board
LINUX_KERNEL_SOURCE_ROOT=D:\v5\linux
LINUXCNC_SOURCE_ROOT=D:\v5\linuxcnc
VM_SOURCE_MOUNT_ROOT=/mnt/v5-source
VM_BUILD_ROOT=/root/v5-build
VM_SOURCE_PROJECTION_ROOT=/root/v5-build/temp_source/current
VM_LINUX_BUILD_DIR=/root/v5-build/linux
VM_BOARD_BUILD_DIR=/root/v5-build/board
EVIDENCE_ROOT=repo_ignored

COMMAND_RULES:
- 文件搜索优先 `rg`/`rg --files`。PowerShell 多关键词用多个 `-e`，不要把含 `|` 的 quoted regex 交给 cmd 误解析。
- 本地文件修改使用 `apply_patch`；机械 formatter/bulk rewrite 可用对应 formatter。不要用 shell 重定向、`cat`、Python 随意覆盖 source/doc。
- Windows 递归 delete/move 前解析并验证 absolute target 留在明确范围内；不要跨 shell 拼路径后删除。禁止 `git reset --hard`、`git checkout --` 覆盖用户工作。
- 不把命令用 `echo ====;` 等分隔符串成噪声；长逻辑优先已有 script/tool。长操作每 60 秒内给高信号进展。
- VM/board 首选现有 MCP execution entry；SSH 只在 MCP 不足时使用既有 alias，不发明凭据。不可达时按 recovery rule，不恢复旧 source mirror，不用 WSL。

## P2_UI_FRONTEND

UI_LAYOUT=v3视觉参考；V5 source 与 native owner 是唯一实现真源
UI_REFRESH=按 `REQ-UI-FIRST-FRAME-CACHE`、`REQ-UI-RUNTIME-REFRESH-RATE`、`REQ-UI-POPUP-RESULT-POLICY` 和具体页面 owner 实施，不在 AGENTS 重复专项规则
