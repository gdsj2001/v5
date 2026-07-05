# AGENTS.md

AI_LOAD_ONLY=true
PROJECT_ROOT=.
RULE_SOURCE=AGENTS.md
HIGHEST_FEATURE_DOCS_SUPREMACY_RULE=ABSOLUTE_RULE: The indexed owner documents under `功能/` are the highest project-local product requirements. `AGENTS.md` defines workflow, cleanup, verification, and conflict-handling rules; it must not override settled product behavior, runtime truth, owner boundaries, field lists, button semantics, or acceptance boundaries recorded in `功能/需求真源索引.md` and the single owner document named there. If code, tests, `待做工作/`, legacy notes, architecture docs, old chat assumptions, or AGENTS wording conflict with the current `功能/` owner, treat the non-owner text as stale and update it to match `功能/` before implementation continues.
HIGHEST_DEPRECATED_CODE_RETIREMENT_RULE=FIRST_RULE_DELETE_FORK_LEGACY_CODE: The first cleanup action in every slice is to delete forked, legacy, deprecated, retired, dead, duplicate, bypass, shadow, unsupported, or split-brain code/scripts/configs/tests/docs/switches/APIs/modes/hooks/paths/runtime branches when no supported runtime path still depends on them. The default bias is "宁可错杀，不放过": when a code path or branch is suspected deprecated/unsupported and no current `功能/` owner explicitly requires it, delete it first, let compile/build expose the real missing dependency, then补全 the canonical path from the `功能/` owner. Do this before adding, repairing, shielding, disabling, commenting out, hiding, rename-only preserving, or keeping fallback/compatibility survivors. If a supported runtime path still depends on the old branch, migrate that dependency to the canonical owner first, then delete the old branch in the same slice.
HIGHEST_NO_FALLBACK_SURVIVOR_RULE=ABSOLUTE_RULE: Product/runtime/test/docs must not retain any fallback, compatibility, bypass, disabled, env-gated, rename-only, shadow, "temporary", rollback-insurance, or just-in-case survivor path after the canonical path exists or the old path is unsupported. This rule is a highest project-local cleanup rule and has priority over convenience, local-only green tests, old examples, compatibility promises, rollback comfort, or finishing a slice quickly. If a supported runtime path still depends on the old path, first migrate that dependency to the canonical owner, then delete the old path in the same slice. If migration cannot be completed safely, keep status honest as source_only, local_verified_only, or blocked; do not preserve or add a second route to appear unblocked.
HIGHEST_DELETE_AI_MADE_BRANCH_RULE=ABSOLUTE_RULE: Except for real product/process branches required by the indexed owner, hardware state, native/LinuxCNC/HAL/VPS safety, or an explicitly supported operator workflow, delete every AI-made branch. "AI-made branch" includes invented compatibility routes, diagnostic-only product routes, alternate command paths, shadow state paths, temporary bypasses, guard-only refusals, fake success/failure modes, disabled-code survivors, rollback-insurance branches, and split-brain implementations. If a real runtime path still depends on one, migrate the dependency to the canonical real flow first, then delete the AI-made branch in the same slice.
HIGHEST_NO_FALLBACK_RECORD_GATE=Any task that discovers a fallback/compatibility survivor must record the highest rule by deleting the survivor in the same slice, or by recording an honest blocker with the exact remaining supported dependency and next deletion condition. A TODO, disabled branch, environment gate, test-only entrypoint, renamed wrapper, doc note, or repo-local backup in a callable path is not a valid closure.
HIGHEST_FEATURE_OWNER_REQUIRED_RULE=功能/ is mandatory settled product memory and the highest product requirement source, not optional reference material. `待做工作/` markdown files are work notes, investigation inputs, active cards, and progress records; after a requirement is settled into the indexed owner documents under `功能/`, `功能/` becomes the authority. All feature behavior, runtime truth, ownership, acceptance boundaries, and AI implementation decisions must follow `功能/需求真源索引.md` and the indexed owner documents under `功能/`. When code, tests, backlog, old notes, current assumptions, architecture docs, AGENTS wording, or `待做工作/` text conflict with the relevant `功能/` owner, the non-owner text is stale; update the stale text or owner first when the requirement changes, then synchronize implementation and the work note only as needed.
HIGHEST_FILE_CLASSIFIED_PLACEMENT_RULE=Use REQ-FILE-CLASSIFIED-PLACEMENT in 功能/需求真源索引.md. Files must be classified before creation or movement: source, docs, tools, screenshots, evidence, backups, scratch, caches, runtime JSON, and logs each have an owner directory. Temporary/runtime outputs must go under `repo_ignored/temp/` or a more specific ignored evidence/scratch directory, not scattered through `待做工作/`, source folders, root, or nearby business directories. Fix the generator path when a scattered temporary file is found; do not merely hide it with `.gitignore`.
V5_REBUILD_SOURCE_TRUTH_RULE=按 `功能/` owner 文档重建一条干净 v5 主干，不再把 `D:/v3` 作为继续修补的源码主线。源码放置遵守“哪边编译、编辑、验证方便就放哪边”的总原则，不人为规定所有源码必须在 Windows 或 VM 单侧闭包；但同一个源码域只能有一份真源，严禁 Windows/VM 双边镜像、互相同步副本或保留备用副本。当前分工是：Linux/native/LVGL/运行服务源码真源放在 VM `/root/Desktop/v5`；Markdown 文档和必须用 Windows/Vivado 编辑的 FPGA 工程真源放在 Windows `D:/v5`。任何新版本运行、编译、部署、测试或脚本引用都不得依赖 `D:/v3` 的源码路径。
V5_D3_REFERENCE_ONLY_RULE=`D:/v3` 是冻结只读旧工程，只能作为人工参考或一次性素材来源；不得修改、同步、审计写入、复制覆盖或把状态回写到 `D:/v3`。从 `D:/v3` 搬入的工具或素材必须落到 v5 当前真源位置，改成 v5 自有入口/命名/路径，并删除对 `D:/v3` 的运行时依赖；不得保留 Windows/VM 双边镜像或备用副本。
V5_NEW_NAME_NO_Z20_RULE=v5 新增工具、脚本、服务、日志、运行目录、文档入口、部署 manifest 条目和对外命令名称不得再使用 `z20` 作为新命名。旧工程只读引用、已有 SSH host、硬件 hostname、udev 真实设备路径等外部既成事实可以作为诊断事实出现，但从旧工程移植到 v5 的可维护入口必须改为 `v5_*` 或其它 v5 owner 命名，不得把旧 `z20_*` 文件名/目录名照搬成 v5 新主线。
V5_MANUAL_VM_BACKUP_PROTECTION_RULE=`D:/v5/vm-v5` 是用户手动备份目录，不属于当前运行、编译、部署、测试或编辑真源；AI 禁止删除、移动、清理、格式化、改写、同步覆盖、批量重命名或把新源码改动写入该目录。该目录只能作为人工备份存在；任何自动化、构建、部署、测试、搜索替换、真源同步和 cleanup slice 都必须跳过它。若发现该目录与 VM `/root/Desktop/v5` 或 Windows 文档不一致，不得修它，只记录“手动备份可能过期”并继续以 VM `/root/Desktop/v5` 为 Linux/native/LVGL/运行服务真源。
V5_BOARD_UNREACHABLE_RECOVERY_RULE=如果 `v5_board` MCP、`z20-board` SSH 或配置板端 IP 不能连接开发板，下一步不是等待或新增报错提示，而是先打开 COM 串口监控，再使用 v5 项目内继电器工具对开发板断电重启并记录证据。默认工具为 `D:/v5/tools/v5_board_power_cycle.py`，默认目标 `z20-board`/`192.168.1.221`，默认 USB 继电器 CH4；COM 日志必须覆盖 U-Boot、kernel、网卡、IP、SSH、init.d、LinuxCNC 相关启动输出，并把状态/证据更新到当前工作文档或 `待做工作/遗留.md`。

P0_ENTRY_RULES:
1. First delete forked, legacy, fallback, duplicate, shadow, bypass, split-brain, or unsupported code/docs/tests/configs when no supported runtime path still depends on them. Apply "宁可错杀，不放过": delete suspected retired branches first, expose missing dependencies by local/VM compile, then补全 only the canonical `功能/` owner path. If still depended on, migrate to the canonical owner first, then delete in the same slice.
1a. Only real owner-approved product/process branches may remain. Delete AI-made branches that do not correspond to an actual supported runtime, board, VM, VPS, safety, LinuxCNC/HAL, microkernel, or operator workflow; migrate any still-used dependency to the real flow before deletion.
2. Classify every slice before editing: P0 for native/SHM/State Publisher/Broker control/motion/safety/parameters/wcheckpoint/segment boundary/stillness/fallback survivors; P1 for ordinary product behavior, non-motion UI, feature docs, local refactors, and non-realtime config; P2 only for spelling, comments, formatting, links, test names, and non-semantic annotation/import compatibility.
3. If uncertain, choose the higher class. If a P1/P2 slice touches ownership, ABI/schema, resident daemon lifecycle, control IPC, native helper allowlists, board-visible behavior, or fallback policy, escalate immediately.
4. `功能/` files are mandatory settled product memory and the highest product requirement source. Work notes may describe active tasks and evidence, but behavior, runtime truth, owner boundaries, and acceptance conditions must obey `功能/需求真源索引.md` and the indexed `功能/` owner; behavior or requirement changes update that owner first, using `功能/需求真源索引.md` only to find or assign the single `REQ-*` owner before source/config/tests/backlog.
5. Runtime truth has one owner. UI and Broker may display/request/consume typed state, but must not invent native, microkernel, SHM, parameter, motion, safety, wcheckpoint, segment boundary, or stillness truth.
6. Finish status must be honest: use `source_only` for source/doc-only work, `local_verified_only` for local gates without board proof, `board_verified` only after the original board/operator/motion path is proven, and `blocked` only for a real blocker with the next acceptance condition recorded.
7. In parallel AI work, do not take over another active owner/card or broad shared ABI/lifecycle file. If a shared file must change, make the smallest patch and state which owner semantics were not changed.

WORKDOC_TO_FEATURE_FLOW:
- `待做工作/` is the user's active requirement input, investigation note, implementation card, and progress/evidence record. It may propose or correct requirements, but it is not the final execution truth after those requirements are settled.
- When the user asks to implement a specific `待做工作/*.md`, read that workdoc first, then locate the related `功能/` owner through `功能/需求真源索引.md`.
- If the specified workdoc changes, corrects, or clarifies product behavior, runtime truth, owner boundaries, acceptance conditions, or forbidden paths, update the indexed `功能/` owner first. Then implement code/tests/configs against the updated owner, not against stale copied text.
- If the workdoc and the current `功能/` owner conflict, treat the specified workdoc plus the latest user message as the requirement-change input; do not bypass `功能/`. Convert the stable requirement into the owner, then synchronize implementation.
- After implementation, update the workdoc only with status, evidence, blocker, next acceptance condition, or deletion condition. Do not duplicate volatile owner policy text in the workdoc unless it is clearly marked as a pointer to the `REQ-*` owner.

PROJECT_PATH_RULE=All project-local paths must be relative to PROJECT_ROOT so the whole v5 folder can be moved intact
SOURCE_FILE_MAX_LINES_RULE=The 500-line guardrail applies to v5 self-written business/control-flow source, not to microkernel/native/upstream-owned files. Self-written business/control-flow source under PROJECT_ROOT should stay at or below 500 lines and must not grow past that boundary for new business ownership. The exception is an atomic, genuinely indivisible file whose content cannot be reasonably split without breaking its natural owner, such as generated files, schema/ABI declarations, parameter files/schemas, pure parameter tables, pure data tables, constants/enums/maps, style/assets, tests, docs, or pure config with no business/control-flow logic. A single parameter file or parameter table that is one runtime/config truth must not be split merely because it is long. Microkernel/native files should not be modified or split just to satisfy this line-count rule; touch them only when the indexed owner requires a minimal native change. If an exempt or atomic file gains branching business logic, owner decisions, fallback behavior, runtime state transitions, parser/algorithm logic, parameter-migration write paths, or command/control flow, it loses the exemption and must be split before accepting more behavior.
MARKDOWN_LINK_RELATIVE_EXCEPTION=Markdown clickable links are an exception to mechanical PROJECT_ROOT-relative prefixes: link hrefs must be written relative to the markdown file's own directory so VS Code/Git viewers resolve them correctly. The target must still stay inside PROJECT_ROOT unless it is an explicitly external reference.
HIGHEST_PARAMETER_REQUIREMENT=功能/0开机参数入内存.md
REQUIREMENT_SOURCE_INDEX=功能/需求真源索引.md
HIGHEST_REQUIREMENT_DOC_FIRST_RULE=Any new or changed product requirement must update the indexed owner document under `功能/` first, before editing backlog docs, test plans, code, scripts, configs, or other documents. If the relevant owner does not exist, update `功能/需求真源索引.md` to assign exactly one `REQ-*` owner under `功能/`, then edit that owner.
DOC_SINGLE_SOURCE_RULE=Cross-feature or fast-changing requirements must have exactly one REQ-* owner recorded in 功能/需求真源索引.md. Non-owner docs may cite REQ IDs and local implementation details, but must not duplicate volatile policy text as an independent requirement.
DOC_SINGLE_SOURCE_PRECEDENCE=When a non-owner feature doc, legacy doc, old note, or test comment conflicts with the current REQ owner named in 功能/需求真源索引.md, treat the duplicate text as stale and update the owner requirement first. A demand change should modify the owning document only; references are updated only when they are missing or misleading.
PARAMETER_DOC_PRECEDENCE=For microkernel-owned parameter/state truth, 功能/0开机参数入内存.md is the highest feature requirement under AGENTS.md; docs/rules/code that conflict with it must be updated to that requirement before implementation.
HIGHEST_PARAMETER_TABLE_MEMORY_RULE=Use REQ-PARAM-MEMORY-LIGHTWEIGHT-SAVE in 功能/需求真源索引.md and its owner 功能/0开机参数入内存.md. AGENTS.md keeps precedence and workflow gates; the owner keeps the volatile parameter-table save rule text. Microkernel/native parameters and data must be consumed from microkernel/native memory/API whenever they exist, except for the SHM display whitelist. Do not create a second V3 parameter table for them. The drive parameter table is the explicit drive-only custom-schema exception.
PARAMETER_TABLE_MEMORY_PRECEDENCE=The parameter-table memory rule remains highest under AGENTS.md through REQ-PARAM-MEMORY-LIGHTWEIGHT-SAVE. If another doc/code path conflicts, update the indexed owner first, then implementation.
HIGHEST_NATIVE_TRUTH_RULE=Use REQ-NATIVE-OWNER-FIRST and REQ-LINUXCNC-COMMAND-GATE in 功能/需求真源索引.md; owner text lives in 系统代码架构硬边界守则.md.
NATIVE_TRUTH_PRECEDENCE=Native-first remains a highest architecture rule under AGENTS.md. AGENTS.md points to the owner instead of duplicating the volatile native boundary text.
NATIVE_GAP_EXCEPTION_RULE=Native-gap exceptions must be recorded in the indexed owner context: exact native gap, minimal adapter/gate, fail-closed boundary, microkernel/native memory readback, board/motion evidence, and deletion/upstream condition.
DEPRECATED_CODE_NO_REVIVAL_PRECEDENCE=If a supported runtime path still depends on the old path, it is not ready to be declared retired; migrate that dependency to the canonical replacement first, then delete the old path immediately. Reintroducing a retired path is blocked unless the latest user explicitly reverses the retirement and the owning rule/doc is updated first.
DELETE_OVER_ADD_PRECEDENCE=Deleting retired, duplicate, bypass, confusing, or shadow code is higher priority than adding new code. A change is not complete if it makes a new working branch while leaving an old misleading branch alive.
CANONICAL_PATH_OR_BLOCKED_RULE=Prefer one canonical path that may temporarily fail and then be repaired over multiple paths that appear to work but confuse future maintenance. If the canonical path does not work, fix that path or record an honest blocker; do not create a side path, fallback, or temporary fork to get around it.
DELETE_BIAS_FOR_RETIRED_CODE=When the choice is between keeping a retired/unclear/possibly-unused path "just in case" and deleting it, delete it. If later evidence proves it is needed, reintroduce the needed behavior deliberately through the current canonical owner; do not preserve stale code in mainline as insurance.
ARCH_BOUNDARY_DOC=系统代码架构硬边界守则.md
PROJECT_GUIDE_DOC=项目软硬件架构和后期修改指导说明.md
CMV_GUIDE_DOC=CMV自动化体系.md
NO_TASK_BOARD=true
TASK_BOARD_FILE=deleted
LEGACY_FILE=待做工作/遗留.md
DO_NOT_RECREATE=AI_并行任务看板.md,MULTI_AI_MODULES.md,FILE_STRUCTURE.md,AI自动作业指导书.md

## P0_RULE_NAVIGATION

RULE_READING_ORDER:
- Any task starts from `AGENTS.md` top `P0_ENTRY_RULES`, then uses `功能/需求真源索引.md` only to locate the relevant `REQ-*` owner, then reads that single owner document and the active work card or legacy entry if one exists.
- If the latest user message names a specific `待做工作/*.md`, read that workdoc as the active requirement-change input before implementation, then use `WORKDOC_TO_FEATURE_FLOW` to settle the stable requirement into the relevant `功能/` owner.
- Do not bulk-merge every rule, backlog, old note, or chat summary before acting. If a non-owner document repeats a volatile rule, treat the repeated text as a reference only and follow the indexed owner.
- Do not create a new rule document to resolve confusion until the existing index, owner document, `待做工作/拆分.md`, `待做工作/改进.md`, and `待做工作/遗留.md` have been checked for the same scope.
- Human-facing backlog or work-instruction documents must stay short: what is wrong, how to close it, current status/evidence, blocker if any, and next acceptance condition. Do not add command logs, AI reasoning logs, chat history, or duplicate policy text.
- After context loss, compaction, or handoff, restart from this order: `AGENTS.md` -> `功能/需求真源索引.md` -> relevant owner document -> `git status`/`git diff` -> active work card or `待做工作/遗留.md`. Do not rebuild plans from memory or old chat.

## P0_HUMAN_FEEDBACK_PRECEDENCE

HUMAN_CHAT_FEEDBACK_PRECEDENCE=Within this project, the current human user's explicit chat feedback is the highest requirement-change input, but settled product truth must be written into `功能/需求真源索引.md` and the relevant `功能/` owner before implementation continues. If AGENTS.md, feature docs, legacy docs, tests, code comments, old AI notes, or prior chat assumptions conflict with the latest explicit user feedback, update the conflicting `功能/` owner or stale non-owner document first, then implement from the indexed `功能/` owner.
LATEST_USER_FEEDBACK_WINS=When multiple user feedback items conflict in the same scope, the newest explicit user feedback wins. Preserve earlier requirements only when they do not conflict with the newest instruction.
HUMAN_FEEDBACK_BOUNDARY=This project-local precedence rule does not override system/developer/tool safety instructions, legal/safety constraints, destructive-operation approval requirements, or physical machine safety. Project-local lock files are paused by the current user instruction and are not higher-priority blockers.
LOCKS_PAUSED_BY_USER=true
LOCKS_PAUSED_SCOPE=All `repo_ignored/locks/` source, VM, board, deploy, operator, motion, and resource-lock coordination requirements are paused. Do not create new lock files, do not wait on existing lock files, and treat existing lock files as historical records only. This pause does not remove live process/status checks, board idle checks, artifact identity checks, physical machine safety checks, incremental UI verification, or the requirement to stop/diagnose real running process/resource contention.

## P0_DOC_SINGLE_SOURCE

REQUIREMENT_ID_RULE:
- `功能/需求真源索引.md` is the index for cross-feature and fast-changing product requirements.
- Current high-volatility entries include `REQ-DOC-SINGLE-SOURCE`, `REQ-PARAM-MEMORY-LIGHTWEIGHT-SAVE`, `REQ-NATIVE-OWNER-FIRST`, `REQ-SETTINGS-RUNTIME-DRIVE-ONLY`, `REQ-SCALE-CHAIN-HISTORY-POLLUTION`, `REQ-LINUXCNC-COMMAND-GATE`, `REQ-G92-NATIVE-RUNTIME`, `REQ-WCS-NATIVE-OWNER`, `REQ-ROTARY-UNWRAP-ABSOLUTE-PROOF`, `REQ-ROTARY-REBASE-NATIVE-GATE`, and `REQ-RTCP-G53-NATIVE-ACTUAL`.
- Every volatile requirement must have one `REQ-*` entry and one owner document. Change that owner document when the requirement changes; do not chase every non-owner feature doc just to restate the same policy.
- Non-owner docs should cite the relevant `REQ-*` IDs and keep only local implementation detail, owner cards, field mappings, and verification requirements.
- If copied text in a non-owner doc conflicts with the indexed owner, the copied text is stale. Update the owner requirement first, then implementation; update references only when missing or actively misleading.
- New or edited feature docs that mention native owner, `settings_runtime.json`, G92, WCS, RTCP/G53, rotary unwrap/checkpoint, or LinuxCNC command gate must cite the corresponding `REQ-*` entry.

## P0_FINISH_LINE_READ_FIRST

DEFAULT_GOAL=Reach the required verification endpoint, not merely edit files.

USER_ISSUE_SOLVING_RULE:
- Treat every user-raised problem/question as a request to solve or advance the real issue to the required verification endpoint, not as a request to merely report the first error.
- A user-reported defect is not closed by adding a new error prompt, popup, warning text, disabled button, guard rejection, or friendlier failure message. Those are only acceptable when the user explicitly asks for messaging, or when they are the minimal safety surface while the real root cause remains honestly open.
- If the symptom is a wrong behavior, failed action, missing result, motion/UI/runtime mismatch, or operator-visible failure, the default work is to fix the underlying path and then re-run the same user-visible scenario. Do not convert the requested fix into a designed refusal, preflight block, or explanatory error state unless the latest user requirement says that behavior is the product requirement.
- Existing error prompts, popup warnings, guard rejections, disabled states, and "not supported" branches found on the reported path must be treated as implementation gaps, not product acceptance. Implement the missing behavior so the original path no longer reports the error, then delete the obsolete prompt/guard branch in the same slice unless it is still required as a real safety interlock by the indexed owner.
- Do not preserve old error text as compatibility, diagnostics, or rollback insurance after the underlying issue has been implemented. If the error still appears in the original scenario, the issue remains open; continue fixing or record an honest blocker with the exact unsupported dependency and next deletion condition.
- When fixing a reported path, keep only branches that represent actual product/process choices, real hardware/runtime states, real safety interlocks, or owner-approved operator workflows. Delete AI-made branches that exist only to explain, route around, simulate, special-case, or preserve an invented alternate behavior.
- A tool, command, build, test, deploy, board, or runtime failure is diagnostic evidence. Investigate the cause, try a reasonable fix-forward or alternate allowed path, and continue toward closure when safe.
- Do not treat "command failed", "tool errored", "cannot access", "test failed", "board unavailable", or "stopped here" as complete work. These can only be intermediate findings or blockers.
- Stop as `blocked` only when solving would be unsafe/destructive without user approval, required external state is unavailable, user input is genuinely required, or multiple meaningful attempts have hit the same concrete blocker.
- Multiple meaningful attempts means at least three distinct relevant attempts, or three consecutive turns/tries ending at the same blocker after diagnosis. When this threshold is reached, append/update LEGACY_FILE with the blocker, attempts made, missing evidence, next acceptance condition, and strongest honest status before final reply.
- A LEGACY_FILE entry is not a substitute for attempting the solution. It is the record for unresolved work after the agent has made reasonable progress attempts or hit a real safety/external blocker.

CONTINUOUS_PROGRESS_RULE:
- The default execution mode is continuous progress. If a requested workstream has a known next safe action, continue to that action without waiting for user confirmation, intermediate approval, status acknowledgement, or a new chat prompt.
- Do not stop after a plan, partial inventory, one field migration, one local gate, one tool run, one source edit, one doc update, or `local_verified_only` for a field. Use that result as input for the next field/slice, missing-tool build, cleanup scan, integration manifest, or final board slice as applicable.
- If a needed migration/test tool is missing, build the smallest reusable tool or task-local diagnostic helper allowed by TOOLING, verify that tool, then continue the migration. Missing convenience tooling is not a reason to stop.
- Pause only for unsafe/destructive operations needing approval, a real external blocker, VM/board contention, a rule conflict that cannot be resolved from local context, or user input that is genuinely required. Otherwise keep advancing source, docs, tools, tests, cleanup, and verification toward the required endpoint.
- When context is compacted or previous chat detail is unavailable, reload AGENTS.md, the controlling feature docs, current git status/diff, and existing tool outputs, then continue from current source truth instead of restarting or waiting.

INCREMENTAL_UI_AUTOMATION_GATE:
- Before any full automation flow, full operator pipeline, full motion pipeline, regression matrix, or equivalent end-to-end automation, close the relevant single issue/function in isolation first.
- For UI/operator paths, the isolated check must proceed by screenshot/remote-frame confirmation first: confirm the real screen, color/state, and next click target before sending input.
- Simulated screen clicks or remote-input clicks must be executed one small action at a time, with screenshot/remote-frame evidence after each step. If the screen is wrong, the click target is unclear, or one small action fails, stop and fix that single function before continuing.
- Do not start or continue the full automation flow while the single function is still unclosed. Full automation is final regression evidence, not the primary debugging tool for an unclosed single problem.
- For batch improvement work that explicitly requires local-first closure, complete each single item with its local focused gate first; after all local single items pass, sync the full current PROJECT_ROOT once, run one full VM/ARM build, deploy once, then perform per-function board checks before final full automation.
- Under CONTINUOUS_PROGRESS_RULE, this gate defines the next safe action for UI/operator/motion work. Full automation is not a next safe action until the relevant single-function checks have passed.
- Running the full flow before per-function screenshot/click verification is a P0 workflow violation because it hides the real failing step and wastes VM/board/motion time. Report `blocked_by_incremental_ui_automation_gate` instead of bypassing this gate.

PROGRESS_DOC_SYNC_RULE:
- Any material progress on an improvement, legacy item, owner migration, board/operator step, or blocker must update the corresponding location in the active improvement document, work instruction, work card, and/or LEGACY_FILE before stopping or switching context.
- "Material progress" includes a source fix, test added, local gate result, VM build, board deploy, screenshot/click proof, failed verification, new blocker, changed root cause, changed close condition, or evidence that a legacy description is stale.
- Update the existing matching section, work card, or legacy item in place. Do not append loose notes at the bottom, create duplicate improvement documents, or leave only chat/process logs for future agents to reconstruct.
- Rule owner documents record rule or close-condition changes only. Do not turn owner rule files into process logs; put execution progress in the active work instruction/work card or LEGACY_FILE.
- Keep the update concise and human-readable: current status, strongest evidence, blocker if any, and next acceptance condition. Do not write command流水, long logs, or AI-only reasoning into human docs.

RISK_GRADED_EXECUTION_RULE:
- This is the detailed gate expansion for `P0_ENTRY_RULES` item 2. The top entry rules decide the first path; this section defines the full gate expectations after classification.
- Before choosing gates, classify the slice as P0, P1, or P2. If uncertain, choose the higher risk class. If a low-risk slice discovers native ownership, SHM ABI, Broker/control routing, motion safety, parameter truth, fallback survivor, or board-visible behavior, escalate immediately and use the higher class from that point.
- P0 high-risk slices include any change to native/LinuxCNC/HAL/EtherCAT ownership, microkernel parameter truth, typed SHM ABI or State Publisher ownership, Broker/product control paths, runtime motion, safety gates, wcheckpoint, segment boundary, stillness gate, settings-runtime truth, board-visible behavior, deploy/startup scripts for resident owners, or any supported fallback/compatibility survivor. P0 keeps the full doc-first, owner, no-fallback, ABI, focused test, VM/board/motion gates required elsewhere in this file.
- P1 medium-risk slices include ordinary product behavior, non-motion UI behavior, feature documentation, local refactors, non-realtime configuration, and tests that can affect a supported runtime path but do not change P0 ownership or safety semantics. P1 requires the relevant owner document when behavior changes, scoped no-fallback cleanup for touched paths, targeted compile/unit/contract tests, and honest `local_verified_only` if board gates are not run.
- P2 low-risk slices include spelling, comments, formatting, markdown link fixes, test name cleanup, and source annotation/import compatibility changes that do not alter runtime semantics, public contracts, owners, schemas, generated ABI, deployment, or board-visible behavior. P2 uses status/diff review, backup before edit, `git diff --check`, and the smallest syntax/import/focused gate that can catch breakage from the touched file; it must not expand into full VM/board/motion gates unless the change is reclassified.
- Low-risk changes must not update high-volatility owner requirements, SHM schemas, native helper allowlists, resident daemon lifecycle, product control IPC, or fallback policy text as a side effect. If those files must change, the slice is not P2.
- Board/operator/motion verification is mandatory only for P0/P1 slices whose behavior is board-visible or motion-capable. Do not require board gates for P2 cleanup or doc/rule-only edits unless the edit itself changes a board-visible requirement.

FINISH_LINE_MATRIX:
- doc_or_rule_only: update requested document/rule -> run text sanity such as `git diff --check` -> final may say doc/rule updated when the edited text is internally consistent.
- non_board_local_code: update doc first if behavior changed -> edit source/config -> run targeted compile/unit/contract/static gates -> final status `local_verified_only` unless board closure also ran.
- board_visible_function_code: update doc first if behavior changed -> edit source/config -> sync/build full current PROJECT_ROOT -> deploy canonical artifact -> trigger the original UI/operator path automatically -> collect the minimal verification outputs needed for the claim -> final status `board_verified` only after the verification actually ran.
- motion_capable_code: complete board_visible_function_code plus `nc/cc.ngc` golden motion run result; no golden loop means no pass/fixed/done claim.
- blocked_or_unsafe: keep source state honest -> record exact blocker and missing test in final reply and append/update LEGACY_FILE -> final status `blocked` or `source_only`; never claim fixed/done/verified.
- release_or_push: Git push is backup/sync only unless the user explicitly asks for a formal release; a push can verify remote refs for backup integrity but never replaces code review, build, tests, VM/board deploy, or board closure.

GIT_PUSH_BACKUP_ONLY:
- The user's Git push workflow is used as an off-machine/manual backup, similar to a network drive snapshot.
- Git commits and pushes during ordinary work are allowed recovery checkpoints: preserve the current tree so later risky edits can be rolled back or compared.
- Git commit/push/tag/remote-ref alignment is never a finish line for writing code, fixing bugs, finishing a split, closing a board function, or claiming release readiness.
- If the user explicitly asks to push, do the push accurately and verify remote refs, then report it only as backup/sync state.
- A pushed commit with failing, missing, or unrun required gates remains `source_only`, `local_verified_only`, or `blocked` according to the actual verification endpoint.
- Do not add "push completed" as a required step to ordinary code tasks; only push when explicitly requested by the user.
- Do not lecture or repeatedly remind the user that Git is not completion. Keep Git wording brief: recovery point, backup/sync state, and remote alignment only when directly relevant.

DO_NOT_STOP_AT:
- source edits only
- docs/rules/legacy records only
- local compile/unit/mock tests only when board closure is required
- direct UDS diagnostics when the user-facing defect is a UI button/operator path
- replacing the reported failure with a new popup/error/guard/disabled-state instead of fixing the requested behavior
- keeping an existing popup/error/guard/disabled-state on the reported path after implementation work claims the behavior is supported
- keeping an AI-made branch, alternate route, compatibility mode, or shadow path that is not an actual supported product/process flow
- VM build without board deploy when board-visible behavior changed
- plan/checklist/legacy record without executing required tests
- reporting the first command/tool/test error without diagnosis and fix-forward attempts
- writing LEGACY_FILE before meaningful attempts unless the blocker is unsafe, destructive, external-state unavailable, or requires user input

PASS_WORDS_REQUIRE_VERIFICATION=fixed, repaired, done, passed, verified, board_verified, release_ready, works on board, live, closed-loop complete, operator_says_ok

FINAL_STATUS_REQUIRED:
- Always state changed files, verification actually run, and blocker if any.
- If board closure was required, final must include `board_closure_state=board_verified|local_verified_only|source_only|blocked`.
- If only local gates ran, say `local_verified_only`; do not imply board behavior is fixed.
- If tests did not run, say `source_only` or `blocked` and name the missing test.

## P0_ARCH_NO_POLLUTION

CONTROL_PATH_ONLY=UI C -> Command Broker UDS framed socket -> product action -> LinuxCNC/HAL/EtherCAT
STATE_PATH_ONLY=microkernel/native resident memory/API truth -> state publisher display/diagnostic projection -> /dev/shm/v3_status_shm typed SHM -> UI SHM reader
UI_ROLE=display,input,state_view,command_sender
MOTION_TRUTH=LinuxCNC/HAL/EtherCAT
ARCH_BOUNDARY_OWNER=系统代码架构硬边界守则.md
ARCH_BOUNDARY_INDEX=功能/需求真源索引.md REQ-NATIVE-OWNER-FIRST, REQ-LINUXCNC-COMMAND-GATE, REQ-PARAM-MEMORY-LIGHTWEIGHT-SAVE, REQ-SETTINGS-RUNTIME-DRIVE-ONLY
ARCH_FAST_FAIL=Any new direct LinuxCNC/HAL control path, UI disk/default parameter truth, file/JSON/FIFO product control IPC, product fallback to old scripts, env-gated compatibility survivor, or V3 private truth for native-owned state is blocked unless the architecture owner explicitly allows the narrow diagnostic exception.
ARCH_EXCEPTION_RULE=Temporary diagnostic exceptions must stay outside product source/runtime/deploy paths, must not become hidden fallbacks, and must be converted back into canonical source fixes before delivery; see 系统代码架构硬边界守则.md for the full exception contract. When the canonical source fix exists, the temporary exception path must be deleted in the same slice; it cannot remain disabled, env-gated, renamed, or test-only as insurance.

## P0_WORKFLOW

BEFORE_EDIT:
1. Read AGENTS.md
2. Run git status/diff for relevant paths
3. Find relevant existing doc under 功能/ when behavior changes
4. If the latest explicit user feedback conflicts with any project doc/rule/test/code comment/old AI note: treat the old project text as stale, edit the conflicting doc/rule first, then source/config
5. Backup edited files to repo_ignored/<short_task>/backup/
6. Do not create or update process files (`process.md` or `过程.md`)

PROCESS_FILE_DISABLED:
- Do not create or update `process.md`, `过程.md`, or equivalent per-task process markdown.
- Use git status/diff, final reply, generated verification outputs, and LEGACY_FILE for required traceability.
- Low-level scratch logs may be generated only when required by a tool or verification output; do not create a separate process markdown just to narrate work.

LEGACY_RECORD:
- If any unfinished, blocked, deferred, fail-closed, not-board-verified, or follow-up item remains at the end of a task, append/update `待做工作/遗留.md`.
- If board closure was required but not executed or failed, append/update LEGACY_FILE before final reply with the exact missing test, blocker, and acceptance condition.
- If multiple meaningful attempts cannot resolve a user-raised problem, append/update LEGACY_FILE before final reply with: exact blocker, attempts made, files or commands checked, missing verification, next concrete action, and status `blocked`, `source_only`, or `local_verified_only` as appropriate.
- Do not use LEGACY_FILE as a way to stop early. First diagnose, retry with allowed alternatives, and fix forward unless blocked by safety, missing user input, external unavailability, or repeated same-blocker attempts.
- `待做工作/遗留.md` is a legacy backlog record only; do not use it as a task board, file lock, deploy lock, or replacement for disabled process files.

USER_FEEDBACK_NOISE_FILTER:
- Chat updates must be high-signal only: current action, material finding, meaningful risk, blocker, verification result, or user decision needed.
- Do not describe self-recovered command/tool failures, shell quoting mistakes, bad search regex, retry mechanics, or other operational trial-and-error in the chat.
- Record useful low-level failure/retry details only in scratch logs when they are necessary for verification; otherwise omit them.
- Surface a tool/command failure in chat only when it changes the outcome, blocks progress, risks source/board state, or requires user input.

AI_LOCKS_SINGLE_MAINLINE:
- This project has one authoritative mainline only: current `main` plus the current PROJECT_ROOT working tree after synchronizing with `origin/main`.
- Project-local lock files are paused by the current user instruction. Do not create new `repo_ignored/locks/` files and do not treat existing lock files as blockers.
- Before editing, committing, syncing, building, deploying, or verifying, still inspect `git status` and `git diff`; account for dirty files and do not overwrite unrelated user work.
- Existing files under `repo_ignored/locks/` are historical records only while `LOCKS_PAUSED_BY_USER=true`.
- Do not create side branches, per-AI module forks, shadow product truths, stale VM copies, or alternate runtime owners to bypass lock conflicts.
- Formal delivery and Git push must report local commit and remote ref alignment because they define the single backup/sync state.

VM_BOARD_SINGLE_OPERATOR:
- VM sync, VM/ARM build, package, board deploy, board UI restart, operator probe, and motion closure are global single-operator resources.
- Do not acquire or wait on `repo_ignored/locks/vm_board_*` while locks are paused. Before starting any VM sync/build, package, board deploy, board UI restart, operator probe, or motion closure, confirm the VM and board are idle by live process/status probes.
- If a real VM, board, build, deploy, UI restart, operator, or motion process is busy, diagnose it. The current user authorizes ending other blocking processes when it is safe and non-destructive; otherwise stop the VM/board step and report the real process/resource blocker.
- Never sync while another real build is running, build while another real sync is running, deploy while another real build/deploy is running, or run board verification while another real operator/motion process is controlling the board.
- NAS/off-machine backup rsync jobs that only copy existing deliverables or backup folders are not VM build/deploy/operator resources. Do not wait for them, do not stop them, and do not treat them as blockers for VM build, ARM build, board deploy, or board verification unless they are actively modifying the current owner source/build/deploy directory.
- Every VM/ARM build must use a task-specific staging/build directory and must not reuse unknown old artifacts.
- Board deployment must use the full current VM_SOURCE_ROOT runtime source and freshly built VM/ARM artifacts from that source; partial deploys or stale artifacts cannot close board delivery.

BOARD_UNREACHABLE_RECOVERY_RULE:
- If the development board is unreachable by ping/SSH/remote relay during a board deploy, restart, recovery, or verification slice, treat the failure as a recoverable board-access fault before declaring `blocked`: use the existing tools relay power-cycle path for the development board plus COM/serial boot monitoring when available, then re-probe ping, SSH, port 18080, `/remote/info`, and board relay/input readiness.
- Relay power-cycle and COM monitoring are recovery/diagnostic steps only. They must not patch board product files, bypass full-current PROJECT_ROOT sync/build/deploy, replace original UI/operator-path verification, or become a hidden runtime fallback.
- Use bounded attempts and preserve evidence: record the relay tool/command, COM port or reason COM was unavailable, boot/network readiness observation, and final probes in the final reply or generated verification output. If relay control or COM monitoring cannot restore access after meaningful attempts, append/update `待做工作/遗留.md` through LEGACY_FILE with the exact remaining blocker, attempts made, missing verification, and next acceptance condition.

SOURCE_TRUTH:
- Runtime source truth is split by owner: Linux/native/LVGL/runtime-service source belongs in VM_SOURCE_ROOT; Markdown, Windows tools, and Windows/Vivado-owned sources belong in PROJECT_ROOT. Board `/opt/8ax/...`, staging bundles, deploy outputs, `bak/`, `repo_ignored/`, `artifacts/`, `deploy_tmp/`, old builds, and temp clean copies are not source truth.
- Source fixes must be made in the owning truth location. If a board copy or temporary artifact is inspected or patched for diagnosis, reproduce the change in VM_SOURCE_ROOT or PROJECT_ROOT according to owner, rebuild from that owner, and redeploy before any source/fix claim.
- Every VM/board build/package/deploy must use the full latest VM_SOURCE_ROOT runtime source after current diffs and relevant untracked source files are understood. Partial copies, partial builds, side branches, stale VM/board copies, stale artifacts, or single-file deploys are diagnostic only and cannot close delivery.
- Destructive mirror delete/overwrite options are forbidden unless the source is the full current worktree and excluded files are recorded as generated/ignored/non-source.

DOC_FIRST:
- Any new or changed product requirement starts in `功能/`: update `功能/需求真源索引.md` only to locate or assign the single `REQ-*` owner, then update that owner document before changing code, tests, plans, backlog, legacy notes, or other docs.
- Latest explicit user feedback is the highest requirement-change input; when it conflicts with current docs/rules/tests/comments/old notes, settle the change into `功能/需求真源索引.md` and the relevant `功能/` owner first, then source/config.
- Parameter/native truth changes use `功能/需求真源索引.md` and `功能/0开机参数入内存.md`/architecture owners before source edits.
- Required order is owner doc/rule -> canonical source/config -> local gates -> VM/board original operator-path closure when required -> result record. Git push is optional backup only when explicitly requested and is not a verification endpoint.
- Board-facing behavior cannot close at doc/source/local-test level; without board/operator evidence, report only `source_only` or `local_verified_only`.

MICROKERNEL_PARAM_MIGRATION_WORKFLOW:
- Scope source of truth: 功能/0开机参数入内存.md is the highest feature requirement for boot-into-memory parameters, microkernel/native truth, and V3 second-truth retirement.
- New or retained V3-made parameters are allowed only after inventory proves the microkernel/LinuxCNC/HAL/EtherCAT/native owner has no existing field, no registrable native helper/gate, no extensible runtime-memory/API readback, and no equivalent persistent owner for that semantic. Record that proof in 功能/0开机参数入内存.md before adding the parameter.
- If the microkernel/native owner already has or can own the semantic, every V3-made duplicate parameter is migration debt. Do not expand its callers, add write paths, use it as config generation input, runtime gate, save-success evidence, fallback, or diagnostic crutch. When a slice touches such a field, migrate the supported dependency to the microkernel/native owner first, then delete every now-unused V3-made entry in the same slice; leave only an honest blocker when a supported dependency still prevents deletion.
- Except for UI coordinate-value display projection, every microkernel/native-owned status, parameter, actual, gate, save-success fact, action result, and safety fact must be consumed from microkernel/native resident memory status data, direct memory/API readback, or a registered native memory block. Do not use `/dev/shm/v3_status_shm`, typed SHM, State Publisher projection, Broker read-only fields, product-action snapshots, JSON, `/run` results, or cached UI state as the source for those facts; touching such a dependency means migrate it to microkernel/native memory first and delete the SHM/V3 duplicate path when unused.
- Classify every touched parameter as one of: migrate to LinuxCNC/runtime INI, migrate to PARAMETER_FILE/linuxcnc.var, migrate to tool.tbl/native tool owner, migrate to registered non-Python native gate/helper, keep in Settings Drive private domain, or keep only as read-only diagnostic/UI dirty state.
- Implementation order is fixed: inventory owner card -> sync affected docs -> microkernel/native memory readback -> Broker/product action write path through registered native gate/helper or controlled apply/restart -> restart/reload/readback proof -> retire old V3 owner -> board/operator/motion evidence when required.
- UI may show dirty values and send Broker requests, but save success must wait for microkernel/native memory readback from the owner. UI must not directly edit runtime INI, linuxcnc.var, tool.tbl, HAL, or settings_runtime.json.
- status_epoch only proves the UI sample freshness. Long flows and real execution must reread current microkernel/native safety facts through the registered owner/gate; SHM is display/diagnostic projection only and cannot replace the lower-level fact.
- G92 is allowed only as explicit LinuxCNC native policy with readback. It must not replace Work Zero, RTCP geometry, Rebase, machine zero, or coordinate-base owners.
- Startup/default writes and runtime actual writes are separate domains. A controlled apply may update native startup/default configuration for persistent settings, but a running G-code, MDI, button action, RTCP toggle, override change, or G92/modal actual change must only affect current runtime memory/actual state and SHM readback, not disk defaults or V3 parameter files.
- current_tool_state.json, linuxcnc_modal_defaults.var, V3 WCS/G92/tool/RTCP/modal/coordinate patch files, config_cache, Broker snapshots, /run results, UI dirty values, and diagnostic JSON are not final truth. Once replacement owner/readback/board evidence exists, retire the old product dependency instead of renaming it.

BUTTON_FUNCTION_FLOW:
- New or changed button/operator functions must prove the backend/script behavior before UI wiring.
- For single-step buttons, first run the script/product-action/Broker contract path successfully with focused local checks, then connect the UI button, popup, and state display.
- For multi-step buttons, first run each step-level process script/action successfully in isolation, then run the full chained process script/action sequence successfully, and only after that connect the UI button flow.
- UI code must not hide missing backend behavior behind visual-only success, placeholder popups, or client-side-only state changes.
- After UI integration, board-facing buttons still require original UI/operator-path verification before any fixed/done/verified claim.

PROTECT_USER_WORK:
- Never revert unrelated dirty files
- Work with existing edits
- If switching scope, final/notes must state changed files, deploy state, verification state, rollback state

DEPRECATED_CODE_RETIREMENT:
- Deletion is a first-class delivery requirement, not cleanup after the fact. Removing retired/duplicate/confusing paths is often more important than writing replacement code, because extra branches make later agents and operators lose the canonical route.
- Prefer accidental over-deletion of retired/unclear paths, repaired later through a deliberate canonical change, over leaving deprecated code in mainline as a permanent source of confusion. This bias applies only to retired, duplicate, bypass, shadow, or unsupported paths; it does not authorize deleting active supported behavior without migration.
- When a path, script, compatibility switch, API, config key, UI mode, test hook, command example, deploy hook, package entry, environment variable, or document section is declared retired/deprecated and no supported runtime path still depends on it, delete it in the same slice.
- Do not keep bypasses, "temporary" compatibility, deferred cleanup, disabled branches, dead wrappers, alias macros, rename-only survivors, hidden fallbacks, stale tests, stale docs, or examples that can lead later AI or operators back to the retired path.
- No fallback survivor is allowed after migration: do not leave old behavior behind an environment variable, config switch, test hook, CLI flag, importable helper, package entry, deploy script, disabled branch, documentation snippet, or "for rollback" wrapper. If rollback is needed, use source control and the archived non-runtime backup under `repo_ignored/<short_task>/`, not a callable product path.
- Finding and leaving a fallback survivor is incomplete work. The slice must either remove the survivor from source, runtime, config, tests, docs, packaging, deploy inputs, and callable tools, or explicitly record `blocked` with the active supported dependency that prevents deletion.
- Do not convert a fallback survivor into a comment, TODO, disabled flag, renamed file, compatibility macro, ignored test, hidden CLI, environment switch, package alias, or doc-only warning. These are still survivor paths unless they are non-callable archive/reference copies under `repo_ignored/<short_task>/`.
- If any supported runtime path still depends on the old path, do not declare it retired yet. First move that dependency to the canonical replacement, prove the replacement path, then delete the old path immediately in the same workstream.
- Retired code must not remain importable, compilable, packageable, deployable, callable by Broker/UI/scripts, readable as product truth, or referenced by tests as an accepted path.
- Archive/reference copies may exist only under `repo_ignored/<short_task>/` or explicit non-runtime history/reference locations, with filenames and notes that mark them non-source and non-callable. They must never be product source, tests, deploy inputs, fallback scripts, or operational instructions.
- Reintroducing a retired path is blocked unless the latest user explicitly reverses the retirement and the owning rule/doc is updated first; otherwise treat any re-addition, bypass, or "temporarily parked" branch as rule pollution to delete.
- If the canonical path is temporarily broken, keep the status honest as `source_only`, `local_verified_only`, or `blocked` and repair the canonical path. Do not add a second route merely to make the task appear unblocked.
- Before adding a new helper, adapter, fallback, mode, command, or test entrypoint, search for an existing retired/duplicate/confusing path in the same scope and delete or merge it first. Net branch count should go down unless a genuinely new owner is required.

TOUCH_OWNER_DOWNSHIFT:
- Deleting obsolete owners, branches, shims, and split candidates has priority over adding another owner. When the current path is confusing, simplify toward one canonical owner before introducing new code.
- Split ownership by page, protocol, business flow, algorithm, write/readback proof, and hardware boundary; line count is a hard guardrail, not the only design signal.
- V5 self-written business/control-flow source owners should stay at or below 500 lines. New business ownership must not be added past 500 lines unless the file is atomic and genuinely indivisible; convenience, habit, or avoiding a split is not an exception.
- Self-written business/control-flow files over 500 lines must not accept a new feature owner, new business helper, state machine, parser, algorithm, parameter-migration write path, UI command category, or cross-layer decision.
- Exempt files include tests, generated files, docs, pure config, parameter files/schemas, pure parameter tables, pure data tables, constants/enums/maps, style/assets, schema/ABI declarations, and other atomic declarative files with no business/control-flow logic.
- Atomic indivisible means the file is naturally one declaration/data/schema/config artifact and cannot be reasonably split without making the owner less clear. A parameter file or parameter table that represents one runtime/config truth is atomic for this rule and should stay whole. Once it gains branching business logic, owner decisions, fallback behavior, runtime state transitions, parser/algorithm logic, parameter-migration write paths, or command/control flow, it is no longer atomic for this rule and must be split.
- Microkernel/native/upstream-owned files are outside the self-written 500-line cleanup target. Do not split, reformat, or churn them for line-count compliance; only make the smallest owner-required change when native behavior must change.
- Oversized business/control-flow legacy files may receive only deletion, migration out, or a narrow blocker-removal fix that does not add new behavior; record the remaining split condition when they cannot be made compliant in the same slice.
- New business helpers, state machines, parsers, algorithms, table-driven blocks, parameter-migration write paths, UI command categories, and cross-layer decisions must land in an existing suitable owner or a new owner/sub-owner.
- Business action owners should target 300-500 lines and must not exceed 500 lines. Complex business flows must split into sub-owners such as precheck, plan, runtime, verify, and result before reaching the ceiling.
- Registries must stay small and only register or look up handlers; they must not own business logic.
- Broker cores keep socket, frame, peer credentials, jobs, idempotency, and dispatch. Business handlers must be split by domain, for example drive, program, jog, settings, home, and fault/estop.
- C/LVGL page files keep layout, event entry, and light forwarding. Dirty/readback state, request construction, command execution flow, parsing, algorithms, projection, and state decisions must downshift to owners.
- Algorithm-heavy files must split by responsibilities such as parser, projection, rotary, cache, and verify.

TOOLING:
- Repetitive work should be automated with small task tools/scripts when practical
- Prefer reusable repo tools under tools/ for project-wide checks
- Use repo_ignored/<short_task>/scratch/ for task-only scratch tools
- Record useful tool command/path in final reply or generated verification output when it is needed for handoff
- Do not install, enable, configure, or depend on WSL. Linux build/test/runtime-source work must use `VM_SOURCE_ROOT` or existing board/VM tools only.

TOOL_GAP_FIRST:
- If the required local, VM, board, SHM, Broker, ABI, artifact-identity, or motion evidence cannot be produced by existing tools, build or extend the smallest appropriate tool before changing product behavior further.
- Reusable evidence tools belong under `tools/` with focused tests or self-checks. Task-only probes belong under `repo_ignored/<short_task>/scratch/`; temporary board probes belong only under `/tmp/v5_test_tools` or `/run/v5_test_tools` and must never become product runtime paths or hidden fallbacks.
- New or changed tools must be verified before their output is trusted: Python tools need `python -m py_compile` plus focused tests/self-test where practical; shell/C helpers need the nearest syntax/build/contract gate. Tool output must be redacted before persistence.
- A missing tool is not a reason to skip verification or declare success. Until the tool exists and produces usable evidence, keep the affected slice at `source_only`, `local_verified_only`, or `blocked` according to the evidence actually available.

## P0_VERIFICATION

CODE_CHANGE_TEST_REQUIRED:
- After modifying product/test/tool source code, run tests targeted to the modified part before stopping; do not end after code edits only.
- Writing or updating docs, rules, plans, checklists, or legacy records is never a substitute for executing tests after a code/config change.
- Targeted tests mean the smallest meaningful compile/unit/contract/static gate that directly covers the touched module, behavior, protocol, or regression; broaden only when the change crosses shared APIs, motion/control/state boundaries, UI behavior, or runtime packaging.
- For touched Python, include `python -m py_compile` on touched Python files plus focused pytest/contract tests where available.
- For touched C/C++/UI code, include the applicable build/compile gate and the focused UI/protocol/state simulation or board-facing test required by the change scope.
- If no focused automated test exists, add or update a focused test when practical; otherwise run the nearest available gate and state the test gap in the final reply.
- Doc-only or rule-only edits do not require product tests, but still require `git diff --check` or an equivalent text sanity gate for touched files.
- A code/config change with required tests not executed is incomplete unless testing is genuinely blocked; record the blocker and do not claim passed, verified, board_verified, release_ready, fixed, usable, or done beyond source-level completion.

BOARD_FUNCTION_CLOSURE_REQUIRED:
- Any code/config/script change intended to implement, repair, or change a board-facing operator function must run real board closed-loop verification before any pass/fixed/done claim.
- Board-facing operator functions include main-page buttons, setting-page buttons, popups/error reasons, Home, Jog, Start, Stop/Pause, E-stop, program open/run, MDI, axis zero, drive/profile actions, coordinates, following error, toolpath, RTCP/rotary display, remote input, Broker actions, SHM publisher/reader, state freshness, and any UI-visible status derived from the board.
- Local compile/unit/mock tests are required but not sufficient for board-facing operator functions; they are only pre-board gates.
- The required board sequence is: sync/build from full current PROJECT_ROOT -> deploy canonical artifact -> trigger the original operator path automatically -> collect the minimal Broker result, SHM/status, events/touch logs, UI screenshot/framebuffer, or service logs needed to prove the specific claim -> run motion golden verification when motion-capable.
- Writing documentation, adding a legacy item, showing a plan, or running only local tests cannot close a board-facing code change.
- If the board is unavailable, a live board/VM/operator/motion process is active, hardware is unsafe, or a required precondition is missing, retry or work around when practical; if still blocked, mark the task blocked/source-only, append/update `待做工作/遗留.md` through LEGACY_FILE, and do not claim the feature is fixed or board-verified.

LOCAL_GATES:
- compile/build where applicable
- python -m py_compile for touched Python
- focused pytest/contract tests
- UDS/SHM simulation for protocol/state changes
- rg/static scans for forbidden paths
- for microkernel-owned parameter work, run `python tools/v3_microkernel_param_audit_runner.py --repo-root . --json-out repo_ignored/<work-id>/microkernel_param_audit.json` when the tool exists, then scan/classify settings_runtime.json/current_tool_state.json/linuxcnc_modal_defaults.var/config_cache/Broker snapshot/SHM snapshot usages, direct linuxcnc.command/halcmd access, V3 private parameter files, and stale unnumbered boot-parameter doc references before claiming the rule/doc is aligned
- git diff --check for touched files

CODE_CHANGE_BATCH_CADENCE:
- Default code/config cadence is local-tool-first, then one final full-current VM/board/operator/motion slice when required. Detailed batch, field, integration manifest, probe, and rerun rules live in `LOCAL_FIRST_FAST_PROGRESS`; do not maintain a second copy here.

LOCAL_FIRST_FAST_PROGRESS:
- For split, Broker, UI, SHM, settings, drive, tool, or board-visible code work, start with the existing focused local gates for the touched path: touched-file `python -m py_compile`, focused pytest/contract tests, `python tools/v3_microkernel_param_audit_runner.py --repo-root . --json-out repo_ignored/<id>/microkernel_param_audit.json` when microkernel/native truth is relevant, and `git diff --check`.
- Prefer the remaining focused local checks before any board use: touched-file `python -m py_compile`, focused pytest/contract tests, and microkernel-specific tools such as `tools/v3_microkernel_param_audit_runner.py` when relevant.
- Default cadence is batch-local-then-one-board: keep iterating source fixes against local/static/unit/contract gates until they are clean, then run one final full-current VM build, board deploy, and operator/motion closure for the accumulated slice. Do not repeatedly build/deploy/operate the board for every small local edit unless the current blocker is board-only or the user explicitly asks for incremental board probing.
- Field-level work must stop at `local_verified_only` until it joins an integration board slice. For each field or field group, first run `python tools/v3_microkernel_param_audit_runner.py --repo-root . --json-out repo_ignored/<id>/microkernel_param_audit.json` when microkernel/native truth is relevant, touched-file `python -m py_compile`, focused pytest/contract tests, and `git diff --check`. Until the integration board slice runs, the only allowed positive conclusion is `local_verified_only`; do not write fixed/done/verified/board_verified/works on board/live/board usable equivalents.
- If existing tools do not cover the needed local/VM/board evidence, build the missing tool before changing product behavior: reusable project tools go under `tools/` with focused tests; one-off diagnostic probes stay under `repo_ignored/<short_task>/` and must never become product runtime paths or hidden fallbacks.
- For C/LVGL or board-shipped runtime changes, use the existing VM/full-current tooling instead of ad hoc copies: `tools/v3_vm_full_current_sync.py --repo . --work-id <short_task> --upload --build-lvgl`, followed by `tools/v3_board_deploy_product_ui.py` and the focused operator or motion probe required by the owner.
- A failed final VM/board/operator/motion pipeline should be treated as evidence: fix the canonical source, rerun the relevant local gates first, and only rerun the required final pipeline stage after the local failure class is closed.
- After multiple fields reach `local_verified_only`, review the accumulated current worktree directly before the final integration board slice: full current worktree sync/build, deploy the canonical artifact, trigger the original UI/operator path, collect SHM, Broker result, events/logs, screenshot or relay evidence, and run `nc/cc.ngc` for motion-related claims.
- If dry-run or touched-path classification requires `operator` or `motion`, do not run the final pipeline without an explicit probe. Provide `--operator-probe` and a task-local `--operator-probe-config` under `repo_ignored/<short_task>/` unless the selected pipeline mode already has a built-in probe.
- For Python shipped to the board, local Python may be newer than board Python; avoid runtime-evaluated modern type aliases or syntax in product modules unless target import/build proves compatibility.

BOARD_CLOSURE_ENTRY:
- Do not cite a missing orchestration tool as a required or optional closure path. A new orchestration entry may be named only after a real tool is added in the same slice with tests.
- Board-visible closure uses the existing direct tools: full-current VM/ARM build through `tools/v3_vm_full_current_sync.py`, deploy through `tools/v3_board_deploy_product_ui.py`, then the focused operator or motion probe required by the owner.
- stdout/stderr/result files must be redacted before persistence. Tokens, passwords, authorization headers, private key material, cookies, and secret-like environment values must not be written to `repo_ignored/` evidence.
- Before any board deploy, operator probe, or motion probe, perform a live board/VM busy check. Do not create lock files while `LOCKS_PAUSED_BY_USER=true`; stop as `blocked` only for real process/resource contention, not historical lock files.
- For driver profile, driver parameter, authorization, or VPS-downloaded map changes, evidence must include local SHA256, uploaded/board SHA256, and runtime-loaded identity where applicable. A mismatch is fail-closed and cannot support a pass claim.
- Board deploy artifacts must be ARM/Linux ELF from the VM/ARM toolchain. Verify `file`/`readelf -h` before upload; x86/x86-64/Windows/host-built artifacts fail with `ARTIFACT_ARCH_MISMATCH` and must not overwrite the board.
- Status words such as `relay_ready`, `board_deployed`, or `board_runtime_ready` do not override `PASS_CLAIM_GATE`; motion claims still require original operator path plus `nc/cc.ngc`.

BOARD_REQUIRED_WHEN:
- Use `BOARD_FUNCTION_CLOSURE_REQUIRED`, `INCREMENTAL_UI_AUTOMATION_GATE`, and `PASS_CLAIM_GATE` to decide board need; this block is only a fast reminder.
- Board closure is required for user-visible pass/verified/board_verified/release_ready claims, board-facing button/function behavior, and motion/state/control path behavior.

BOARD_CLOSURE:
- Board closure follows `BOARD_FUNCTION_CLOSURE_REQUIRED`: full current PROJECT_ROOT sync/build/deploy, canonical artifact, original UI/operator path, and minimal SHM/Broker/log/screenshot or relay evidence.
- Direct UDS is diagnostic unless the original defect is UDS/API; UI issues require simulated screen tap/button path.
- Motion-capable closure requires the `nc/cc.ngc` golden motion loop; simulated UI input is the trigger path, not a substitute.
- Do not use direct `/dev/fb0` dump/capture scripts for screenshots; use product remote relay/LVGL flush framebuffer or another verified color-correct path.

- Do not leave `/run/8ax_v3_product_ui/disable_remote_relay` or `/run/8ax_v3_product_ui/disable_remote_input` present at handoff unless the user explicitly asked for remote access to remain disabled.

PASS_CLAIM_GATE:
- Words or equivalents such as fixed, repaired, done, passed, verified, board_verified, release_ready, works on board, live, closed-loop complete, or operator_says_ok require matching executed verification.
- If board closure was required but not run, final status must explicitly say `not_board_verified` or `source_only`; do not bury this after summaries.
- If only local gates ran, final status is `local_verified_only`; it is not a substitute for board closure.
- Output paths are required only when files are actually generated or needed to substantiate the verification; do not create separate basis files just to satisfy process.

MOTION_OR_REAL_MACHINE:
- Home/Jog/Start/run/E-stop/program/axis/rotary validation requires cc.ngc golden motion verification before any pass claim
- Motion-related "passed/verified/board_verified" requires UI original-path trigger plus cc.ngc golden loop verification
- cc.ngc is validation input; do not modify nc/cc.ngc or cc programs unless user explicitly requests
- If cc loop fails, fix code/config/build/deploy/probe/SHM/UI/toolpath, not G-code

## P0_DELIVERY

FINAL_REPLY_MIN:
- changed files
- fix summary
- verification result
- output path only if generated, or blocker
- next step only if needed
- If board closure was required, include board_closure_state as one of: board_verified, local_verified_only, source_only, blocked.

HASH_RULE:
- Include hashes only for deployed/release artifacts or source/board identity proof

BOARD_DELIVERY_RECORD:
- board backup path
- deployed target path
- direct functional output path when generated
- process/log output when relevant

GIT_BACKUP_AND_RELEASE:
- Git push is normally only backup/sync; it does not prove code correctness, deployment, runtime behavior, or board readiness.
- Formal release is separate from backup push and must be explicitly requested as a release.
- Formal release must be reviewed, buildable, verified, and board-verified where applicable before claiming release readiness.
- Git push/tag/release/rebuildable delivery must include tracked 8ax-win source when the user explicitly requests that release scope.
- publish/, bin/, obj/ are artifacts, not source truth

ARCHIVE:
- Long-lived verification outputs/history -> repo_ignored/<short_task>/
- Do not create process files. Do not place verification outputs, backups, or task scratch outside PROJECT_ROOT; the project folder must remain self-contained for whole-folder moves

## P1_PATHS

MAIN_BRANCH=main
PRODUCT_UI=app
WIN_CLIENT=8ax-win
VM_SSH_TARGET=z20-vm
VM_PATH_USER=root
VM_SOURCE_ROOT=/root/Desktop/v5
ARCHIVE_ROOT=repo_ignored

VM_ACCESS:
- Preferred target is the existing VM SSH/MCP access for `/root/Desktop/v5`; do not invent credentials or require a different user unless that is already configured.
- First probe before any VM build/runtime-source edit: `test -d /root/Desktop/v5 && readlink -f /root/Desktop/v5` through the VM access tool or existing SSH alias.
- If SSH alias is unavailable, use the existing VM console/hypervisor network setup or documented `vm-bak/` recovery notes; do not install/enable WSL as a workaround
- Treat `VM_SOURCE_ROOT` as the canonical Linux/native/LVGL/runtime-service source. Treat Windows `PROJECT_ROOT` as the canonical Markdown/Vivado/Windows-tool source. Do not mirror, bulk sync, or keep backup source copies across Windows and VM; move one-time materials into the owning truth location, then edit/build/verify only from that owner.

## P1_FEATURE_DOCS

requirements_index=功能/需求真源索引.md
boot_params_memory=功能/0开机参数入内存.md
microkernel=功能/微内核.md
settings_bus_pulse=功能/3设置页总线模式设置项作用说明.md
bus_pulse_difference=功能/总线脉冲区别.md
settings_buttons=功能/3设置页每个按钮作用和目的.md
settings_set_drive=功能/4设置页设置驱动按钮作用和目的.md
axis_zero=功能/4轴设0软件偏置方案.md
home=功能/8回零按钮开机强制回零与双模式真实回零方案.md
wcs_work_offset=功能/2加工坐标系偏移值保存与读取实现.md
drive_command_map=功能/1驱动命令映射表命令需求.md
drive_profile_adapter=功能/1驱动命令映射表命令需求.md
vps_auth=功能/VPS登录与项目对接说明.md
toolpath=功能/UI刀路绘图顺序与状态稳定方案.md
main_buttons=功能/7主页面按钮功能.md
estop=功能/7主页面按钮功能.md
start=功能/7主页面按钮功能.md
rotary_equivalent=功能/6旋转轴等效角目标与解决方案.md
native_helper_whitelist=功能/native_helper白名单与验收说明.md
popup=功能/跳窗提示方案.md
touch_input=功能/-触摸校准与输入链路说明.md
auto_closed_loop=功能/自动闭环测试方式.md
red_point=功能/UI刀路绘图顺序与状态稳定方案.md

## P1_COMMAND_STYLE

SEARCH=rg first
POWERSHELL:
- keep commands simple
- avoid fragile heredoc/pipeline/nested quote commands
- put complex scratch scripts under repo_ignored/<short_task>/scratch/
- Python text IO/logs use UTF-8; errors=replace/ignore when needed

## P2_UI_FRONTEND

SCREENSHOT_REQUEST=reply must include inline image, not only path
UI_HOT_PATH_NO_JSON_POLL=true
UI_NO_SYSTEM_POPEN_FORK_EXEC=true
TOOLPATH_DISPLAY_ONLY=true
