# AGENTS.md

AI_LOAD_ONLY=true
PROJECT_ROOT=.
RULE_SOURCE=AGENTS.md

## P0_AI_FAST_PATH

THIS_SECTION_IS_INDEX_ONLY:
- 本节是给 AI 的快读入口，不替代后文详细规则。
- 若本节和后文 P0 细则冲突，以更具体的后文 P0 细则为准；若项目规则和系统/开发者/工具安全规则冲突，以系统/开发者/工具安全规则为准。

FIRST_90_SECONDS:
1. 先读最新用户消息，判断任务类型：规则/文档、功能需求、代码修复、板端验证、VM/部署、纯提问。
2. 读本节，然后只跳到匹配的详细章节，不要全文漫游后才行动。
3. 先看 `git status` 和相关 diff；不要覆盖用户已有改动。
4. 如果是产品功能/行为变化，先走 `功能/需求真源索引.md` 找单一 `REQ-*` owner，再改 owner 文档，然后改代码。
5. 如果只是 `AGENTS.md`、规则、说明文档整理，备份被改文件后直接改目标文档，跑 `git diff --check`。
6. 如果改 `D:\v5\board` 下板端可见代码，必须从 Windows source truth 同步到 VM、完整构建、部署并走原始 UI/operator 路径验证；没有板端证据只能报 `source_only` 或 `local_verified_only`。
7. 最终回复只讲改了什么、验证了什么、缺什么；不要写命令试错过程。

TASK_ROUTER:
| User request kind | Read first | Edit first | Required closure |
| --- | --- | --- | --- |
| Product behavior / feature requirement | `功能/需求真源索引.md` then one indexed owner | indexed `功能/` owner | code/config + targeted tests; board-visible changes need board closure |
| Specific `待做工作/*.md` request | named workdoc, then indexed `功能/` owner | indexed `功能/` owner if behavior changes | implementation against owner, not stale workdoc text |
| Rule/workflow cleanup | `AGENTS.md` matching section | `AGENTS.md` only unless user names another file | `git diff --check` |
| Board runtime source change | `AGENTS.md`, indexed owner if behavior changes, relevant `D:\v5\board` source | Windows `D:\v5\board` source truth | sync to VM, full build, deploy, original operator-path proof |
| Local non-board code/tool change | relevant source + focused owner docs | canonical local source/tool | nearest compile/unit/contract/static gate |
| Pure question / review | relevant docs/source only | no edit unless requested | answer with evidence and status limits |

CONFLICT_PRIORITY:
1. System/developer/tool safety instructions.
2. Latest explicit user message as requirement-change input.
3. `功能/需求真源索引.md` plus the single indexed owner for product truth.
4. `AGENTS.md` for workflow, placement, cleanup, verification, and delivery.
5. `待做工作/` as active input/progress only, not settled product truth.
6. Old notes, comments, tests, generated files, VM/board copies, and chat summaries are stale when they conflict with the above.

SOURCE_AND_DELIVERY_SHORTCUTS:
- Source truth: docs/Windows-owned files under `D:\v5`; runtime/Linux/native/LVGL/tools under `D:\v5\board`; VM and board copies are generated/deployed copies only.
- Source-first board delivery is mandatory: fix source under `D:\v5` / `D:\v5\board`, then sync/build/package/deploy to the board. Do not SSH/SCP/edit files directly on the board as the way to implement a change.
- SD-card rebuild invariant: the board SD card is disposable and may fail at any time. The system must be rebuildable from Windows source truth plus the controlled VM build workspace; never make the SD card or any board-side file the only place a source change exists.
- Deletion beats fallback: remove retired/shadow/duplicate/bypass paths instead of adding wrappers or compatibility branches.
- SHM is display projection only unless an indexed owner explicitly says otherwise; native/microkernel actuals must come from native owner/readback.
- Board-facing behavior is not fixed until the real board/operator path is proven. Local tests are pre-board gates, not closure.
- Status words are strict: `source_only`, `local_verified_only`, `board_verified`, `blocked`.

## P0_AI_READ_FIRST

READ_ORDER:
1. Latest user message in this chat.
2. `AGENTS.md` top rules and the matching section below.
3. If the user message is a feature requirement, first check `功能/需求真源索引.md` and the corresponding document under `功能/` for an existing record.
4. If the feature requirement is recorded but conflicts with the latest user message, update the indexed `功能/` owner first; if it is not recorded, add or assign the `REQ-*` entry and owner document under `功能/` first.
5. `功能/需求真源索引.md` to find the single `REQ-*` owner.
6. The one indexed owner document under `功能/`.
7. Relevant work note under `待做工作/`, then `git status` and focused diff.

ONE_SENTENCE_MODEL:
- `功能/` owns settled product truth.
- `AGENTS.md` owns workflow, cleanup, placement, and verification.
- `待做工作/` is active input/progress, not final truth after settlement.
- Source truth is split by owner: Windows `D:\v5\board` is the Linux/native/LVGL/runtime-service and project-tool source truth; Windows `D:\v5` remains the Markdown, screenshots, and Windows/Vivado owner; `D:\v3` is read-only reference only.

DEFAULT_ACTION:
- Implement/fix/verify to the required endpoint.
- Delete retired/fallback/shadow paths before adding another path.
- Keep status honest: `source_only`, `local_verified_only`, `board_verified`, or `blocked`.
- For user-raised feature changes, edit only the indexed `功能/` owner document for requirement text, then modify code, build/deploy, and test on the board when required. Do not write implementation results, board evidence, screenshots, or progress summaries into `功能/`, `待做工作/`, backlog, legacy, or process documents unless the user explicitly asks for that specific document update.

## P0_NON_NEGOTIABLES

FEATURE_OWNER_SUPREMACY:
- Highest product truth is `功能/需求真源索引.md` plus the single owner document named there.
- If code, tests, notes, AGENTS wording, or old chat conflicts with the indexed owner, treat the non-owner text as stale.
- Latest explicit user feedback is requirement-change input; settle it into the indexed owner before implementation.
- For any feature requirement proposed by the user, AI must first check the corresponding `功能/` record. If a record exists and is inconsistent, update that owner before source/config/tests/backlog; if no record exists, record it by adding or assigning the `REQ-*` entry and owner document under `功能/` before implementation.

DELETE_FIRST_NO_FALLBACK:
- First cleanup action is deleting unsupported forked, legacy, duplicate, bypass, shadow, fallback, split-brain, or AI-made paths.
- No disabled branch, env gate, wrapper, alias, renamed survivor, test-only entry, TODO, or doc warning may remain as rollback insurance.
- If a real supported path still depends on old code, migrate that dependency to the canonical owner first, then delete the old path in the same slice.
- Temporary patches and workaround overlays are forbidden. Each supported behavior must have one canonical mainline path only; do not leave quick-fix branches, bypass adapters, hotfix copies, or "clean up later" alternate routes that can be forgotten and become forks.

FILE_PLACEMENT:
- Classify files before creation or movement: source, docs, tools, screenshots, evidence, backups, scratch, caches, runtime JSON, and logs each have an owner directory.
- Temporary/runtime outputs go under `repo_ignored/temp/` or a more specific ignored evidence/scratch directory.
- Fix the generator path when scattered temporary files are found; do not only hide them with `.gitignore`.

SOURCE_TRUTH:
- Do not use `D:\v3` as live source; it is frozen read-only reference only.
- New v5 maintainable entries must use v5 names, not new `z20_*` names. Existing hardware hostnames/SSH aliases may appear only as external facts.
- Windows local `D:\v5` is the source truth. Runtime source edits belong in `D:\v5\board`; docs and Windows/Vivado owners stay under `D:\v5`. The VM v5 directory is a generated copy only, must not be edited directly, and must be refreshed immediately after local Windows source changes before VM build/deploy work.
- The board SD card is not durable and is never a source repository. Treat every board filesystem path, including `/opt/8ax`, LinuxCNC runtime directories, service units, deployed scripts, binaries, HAL/INI files, and runtime state, as rebuildable output. A clean SD card must be reconstructable from the Windows source tree, documented external prerequisites, and the canonical VM build/deploy workflow.
- Source-bearing workspaces are limited to the Windows canonical source tree and the controlled VM sync/build workspace generated from it. Do not create source copies in board directories, desktop temp folders, backup folders, `bak/`, `repo_ignored/`, NAS mirrors, deploy staging folders, or SD-card paths. Archives/backups may exist only as non-source recovery/evidence artifacts and must not be edited, built from, or treated as owner truth.
- Direct board editing is forbidden as an implementation path. Do not use SSH, SCP, `sed`, `tee`, `vi`, `nano`, inline shell redirection, or ad hoc copy commands to modify `/opt/8ax`, LinuxCNC configs, services, scripts, UI binaries, HAL/INI files, or other product/runtime files on the board. Board-side access is for inspection, logs, runtime probes, safe recovery, and deployment of artifacts built from the Windows source truth only.
- If a temporary board-side diagnostic patch is ever required to understand a fault, it is not a source fix and cannot close the task. Reproduce the change in the owning Windows source under `D:\v5` / `D:\v5\board`, remove the temporary board mutation by redeploying from source, then verify the original operator path before claiming fixed.
- Use `D:\v5\board\tools\sync_win_source_to_vm.py` as the canonical one-way sync tool from Windows truth to the VM v5 copy. The tool performs manifest-based incremental overwrite: normal runs transfer only changed/deleted local truth, and the default remote drift check re-hashes VM copy files so any manual VM edit is overwritten by the Windows truth without a full resend. Do not create or edit VM source mirrors by hand.
- Do not install, enable, or use WSL on Windows for this project. When Linux userspace/tooling is required, sync `D:\v5\board` to the existing VM staging/build workspace and run the VM toolchain there.

BOARD_UNREACHABLE_RECOVERY:
- If board SSH/MCP/relay is unreachable, do not just wait or add a nicer error.
- Open COM monitoring when available, run the canonical project tool from `D:\v5\board\tools\v5_board_power_cycle.py` through the configured VM/Windows-capable path, then re-probe board readiness and record evidence.

PARAMETER_NATIVE_TRUTH:
- Use `REQ-PARAM-MEMORY-LIGHTWEIGHT-SAVE` in `功能/需求真源索引.md` and owner `功能/0-1开机参数入内存.md`.
- Microkernel/native parameters and data must be consumed from microkernel/native memory/API when they exist.
- Exception: SHM display whitelist only. Do not create a second V3 parameter table for native-owned truth.
- Drive parameter tables are an explicit drive-only custom-schema exception.

SELF_WRITTEN_CODE_SIZE:
- The 500-line guardrail applies only to v5 self-written business/control-flow code.
- Parameter files, parameter schemas, pure parameter tables, generated/schema/config/data files, tests, docs, constants, and other atomic files should not be mechanically split.
- Microkernel/native/upstream files should not be changed, split, or reformatted for line-count compliance; touch them only for the smallest owner-required native change.

PATH_AND_DOC_CONSTANTS:
- PROJECT_PATH_RULE: project-local paths stay under `PROJECT_ROOT` so the v5 folder can move intact.
- SOURCE_RELATIVE_ADDRESS_RULE: source code must use relative filesystem/resource addresses only. Do not hard-code absolute project, host, VM, board, Windows, or Linux paths such as `D:/...`, `C:\...`, `D:\v5\board\...`, `/home/...`, or `/tmp/...` in source, config, scripts, tests, manifests, or generated runtime constants; resolve them from `PROJECT_ROOT`, the executable/resource root, or a caller-provided relative path instead. A real OS/device/API absolute path is allowed only when it is an external non-project fact, fail-closed, and recorded by the indexed owner.
- MARKDOWN_LINK_RELATIVE_EXCEPTION: Markdown links are relative to the markdown file directory.
- REQUIREMENT_SOURCE_INDEX: `功能/需求真源索引.md`.
- HIGHEST_PARAMETER_REQUIREMENT: `功能/0-1开机参数入内存.md`.
- ARCH_BOUNDARY_DOC: `系统代码架构硬边界守则.md`.
- PROJECT_GUIDE_DOC: `项目软硬件架构和后期修改指导说明.md`.
- CMV_GUIDE_DOC: `CMV自动化体系.md`.
- LEGACY_FILE: `待做工作/遗留.md`.
- NO_TASK_BOARD: true. Do not recreate `AI_并行任务看板.md`, `MULTI_AI_MODULES.md`, `FILE_STRUCTURE.md`, or `AI自动作业指导书.md`.

## P0_ENTRY_RULES
1. V5_BOOT_MEMORY_NO_EMPTY_RUN_FIRST: For v5 project boot/startup, the UI, product self-written runtime code/scripts, microkernel/runtime services, provider/action paths, and parameter owner mappings must enter RAM/resident memory directly at boot or through the canonical controlled reload before runtime use. Because the self-written UI, self-written scripts, and microkernel footprint is small and available memory is sufficient, keeping them resident is the default performance path to speed up runtime execution; do not trade it for lazy disk reads, repeated import/path scans, startup probing, wrapper runners, or fallback loaders. During board runtime, except for necessary parameter save/writeback to the unique persistent owner, UI code, product self-written scripts/code, and microkernel/runtime services must not read from or depend on disk/flash for hot-path execution, state truth, action discovery, UI resources, scripts, command handlers, provider data, or proof of success; these must already be resident in RAM or refreshed only through the canonical controlled reload. Runtime code must solve the real execution direction and must not silently empty-run/no-op because data, owner, UI, action, microkernel state, or startup state is missing. Except for real owner-approved business branches, do not add fallback/compatibility/default/wrapper/shadow branches. First delete forked, legacy, fallback, duplicate, shadow, bypass, split-brain, or unsupported code/docs/tests/configs when no supported runtime path still depends on them; if still depended on, migrate that dependency to the canonical owner first, then delete the old path in the same slice.
1a. Only real owner-approved product/process branches may remain. Delete AI-made branches that do not correspond to an actual supported runtime, board, VM, VPS, safety, LinuxCNC/HAL, microkernel, or operator workflow; migrate any still-used dependency to the real flow before deletion.
2. Classify every slice before editing: P0 for native/SHM/State Publisher/Broker control/motion/safety/parameters/wcheckpoint/segment boundary/stillness/fallback survivors; P1 for ordinary product behavior, non-motion UI, feature docs, local refactors, and non-realtime config; P2 only for spelling, comments, formatting, links, test names, and non-semantic annotation/import compatibility.
3. If uncertain, choose the higher class. If a P1/P2 slice touches ownership, ABI/schema, resident daemon lifecycle, control IPC, native helper allowlists, board-visible behavior, or fallback policy, escalate immediately.
4. `功能/` files are mandatory settled product memory and the highest product requirement source. Work notes may describe active tasks and evidence, but behavior, runtime truth, owner boundaries, and acceptance conditions must obey `功能/需求真源索引.md` and the indexed `功能/` owner. For any user-proposed feature requirement, AI must first check whether the corresponding `功能/` document already records it; if recorded but inconsistent, update the owner first; if unrecorded, add or assign the `REQ-*` and owner record first. Behavior or requirement changes update that owner before source/config/tests/backlog.
5. Runtime truth has one owner. UI and Broker may display/request/consume typed state, but must not invent native, microkernel, SHM, parameter, motion, safety, wcheckpoint, segment boundary, or stillness truth.
6. Finish status must be honest: use `source_only` for source/doc-only work, `local_verified_only` for local gates without board proof, `board_verified` only after the original board/operator/motion path is proven, and `blocked` only for a real blocker with the next acceptance condition recorded.
7. In parallel AI work, do not take over another active owner/card or broad shared ABI/lifecycle file. If a shared file must change, make the smallest patch and state which owner semantics were not changed.

## P0_WORKDOC_TO_FEATURE_FLOW
- `待做工作/` is the user's active requirement input, investigation note, implementation card, and progress/evidence record. It may propose or correct requirements, but it is not the final execution truth after those requirements are settled.
- When the user asks to implement a specific `待做工作/*.md`, read that workdoc first, then locate the related `功能/` owner through `功能/需求真源索引.md`.
- If the specified workdoc changes, corrects, or clarifies product behavior, runtime truth, owner boundaries, acceptance conditions, or forbidden paths, update the indexed `功能/` owner first. Then implement code/tests/configs against the updated owner, not against stale copied text.
- If the workdoc and the current `功能/` owner conflict, treat the specified workdoc plus the latest user message as the requirement-change input; do not bypass `功能/`. Convert the stable requirement into the owner, then synchronize implementation.
- After implementation, do not write status, evidence, screenshots, command output, board results, or completion summaries back into the workdoc by default. Report verification in the final chat reply instead. Update a workdoc only when the user explicitly asks to update that workdoc, or when the user names that workdoc as the target artifact and asks for its content to change.

## P0_RULE_POINTERS

- PROJECT_PATH_RULE: all project-local paths stay under `PROJECT_ROOT`.
- SOURCE_RELATIVE_ADDRESS_RULE: all source-code filesystem/resource addresses must be relative; hard-coded absolute project/host paths are forbidden unless the indexed owner records a real external OS/device/API exception.
- SOURCE_FILE_MAX_LINES_RULE: see `SELF_WRITTEN_CODE_SIZE` and `TOUCH_OWNER_DOWNSHIFT`.
- MARKDOWN_LINK_RELATIVE_EXCEPTION: markdown link hrefs are relative to the markdown file directory.
- DOC_SINGLE_SOURCE_RULE: every fast-changing requirement has one `REQ-*` owner in `功能/需求真源索引.md`.
- HIGHEST_REQUIREMENT_DOC_FIRST_RULE: requirement changes update the indexed `功能/` owner before source/config/tests/backlog.
- PARAMETER_DOC_PRECEDENCE: microkernel-owned parameter truth uses `功能/0-1开机参数入内存.md`.
- HIGHEST_PARAMETER_TABLE_MEMORY_RULE: use `REQ-PARAM-MEMORY-LIGHTWEIGHT-SAVE`; microkernel/native truth wins except SHM display whitelist and drive-only parameter table exception.
- HIGHEST_NATIVE_TRUTH_RULE: use `REQ-NATIVE-OWNER-FIRST` and `REQ-LINUXCNC-COMMAND-GATE`; owner text lives in `系统代码架构硬边界守则.md`.
- NATIVE_GAP_EXCEPTION_RULE: native-gap exceptions need exact gap, minimal adapter, fail-closed boundary, readback evidence, and deletion/upstream condition.
- DEPRECATED_CODE_NO_REVIVAL_PRECEDENCE: do not revive retired paths unless latest user reverses retirement and owner docs are updated first.
- DELETE_OVER_ADD_PRECEDENCE: deleting retired/duplicate/shadow code is higher priority than adding another route.
- CANONICAL_PATH_OR_BLOCKED_RULE: fix the canonical path or record an honest blocker; do not add fallback paths.

## P0_RULE_NAVIGATION

RULE_READING_ORDER:
- Any task starts from `AGENTS.md` top `P0_ENTRY_RULES`, then uses `功能/需求真源索引.md` only to locate the relevant `REQ-*` owner, then reads that single owner document and the active work card or legacy entry if one exists.
- If the latest user message names a specific `待做工作/*.md`, read that workdoc as the active requirement-change input before implementation, then use `WORKDOC_TO_FEATURE_FLOW` to settle the stable requirement into the relevant `功能/` owner.
- Do not bulk-merge every rule, backlog, old note, or chat summary before acting. If a non-owner document repeats a volatile rule, treat the repeated text as a reference only and follow the indexed owner.
- Do not create a new rule document to resolve confusion until the existing index, owner document, `待做工作/拆分.md`, `待做工作/改进.md`, and `待做工作/遗留.md` have been checked for the same scope.
- When the user explicitly asks to update a human-facing backlog or work-instruction document, keep it short: what is wrong, how to close it, blocker if any, and next acceptance condition. Do not add command logs, AI reasoning logs, chat history, progress records, verification results, or duplicate policy text.
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
- Do not maintain a complete or authoritative `REQ-*` list in `AGENTS.md`; it drifts as `功能/` evolves. Always read `功能/需求真源索引.md` for the current owner list, route map, and conflict decision.
- Common high-volatility domains include doc single-source, file placement, boot-to-memory parameters, SHM display projection, UI refresh/cache, native/LinuxCNC command ownership, settings runtime drive-only, BUS/Pulse mode, G-code hot path, Start/Home/E-stop operator paths, RTCP native status, RTCP/G53/active model geometry, WCS/G92, rotary unwrap/rebase, remote display relay, board full-build closure, and automatic closed-loop proof. These names are reminders only; the index decides the exact `REQ-*` and owner document.
- Every volatile requirement must have one `REQ-*` entry and one owner document. Change that owner document when the requirement changes; do not chase every non-owner feature doc just to restate the same policy.
- User-proposed feature requirements must not be implemented from chat alone. First search the relevant `功能/` owner path through this index; when the requirement is missing, create or assign the `REQ-*` record and owner document before source/config/tests/backlog changes.
- Non-owner docs should cite the relevant `REQ-*` IDs and keep only local implementation detail, owner cards, field mappings, and verification requirements.
- If copied text in a non-owner doc conflicts with the indexed owner, the copied text is stale. Update the owner requirement first, then implementation; update references only when missing or actively misleading.
- New or edited feature docs that mention native owner, `settings_runtime.json`, SHM, UI refresh/cache, G-code Start/Open, Home, E-stop, board closure, G92, WCS, RTCP/G53, active model, rotary unwrap/checkpoint, remote display relay, or LinuxCNC command gate must cite the corresponding `REQ-*` entry from the index.

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
- Multiple meaningful attempts means at least three distinct relevant attempts, or three consecutive turns/tries ending at the same blocker after diagnosis. When this threshold is reached, report the blocker, attempts made, missing verification, next acceptance condition, and strongest honest status in the final chat reply.
- A LEGACY_FILE entry is not a substitute for attempting the solution. Do not write a LEGACY_FILE entry unless the user explicitly asks for a persistent unresolved-work record.

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
- For v5 UI layout-only shells, non-motion page navigation, and non-motion button hit testing, AI should drive the screen itself with simulated screen clicks or the canonical remote-input tool. Before every simulated click, drag, scroll, or remote-input action, first capture a screenshot/remote frame and confirm the current screen, color/state, and exact target position; never send a bare coordinate without this pre-action frame. After each action, record the target, after screenshot, and UI/remote-input event evidence. Do not wait for the human operator for these routine UI clicks.
- Simulated clicks are not valid evidence for touch calibration, raw touch hardware health, real-finger ergonomics, motion-capable closure, Start/MDI execution, or any owner that explicitly requires real operator touch. Those paths still require the owner-specified real input or motion gate.
- Do not start or continue the full automation flow while the single function is still unclosed. Full automation is final regression evidence, not the primary debugging tool for an unclosed single problem.
- For batch improvement work that explicitly requires local-first closure, complete each single item with its local focused gate first; after all local single items pass, build once from full current RUNTIME_SOURCE_ROOT, deploy once, then perform per-function board checks before final full automation.
- Under CONTINUOUS_PROGRESS_RULE, this gate defines the next safe action for UI/operator/motion work. Full automation is not a next safe action until the relevant single-function checks have passed.
- Running the full flow before per-function screenshot/click verification is a P0 workflow violation because it hides the real failing step and wastes VM/board/motion time. Report `blocked_by_incremental_ui_automation_gate` instead of bypassing this gate.

PROGRESS_DOC_SYNC_RULE:
- Do not auto-write progress, results, board/operator evidence, screenshot paths, command output, or completion summaries into human documents. The default result channel is the final chat reply.
- `功能/` owner documents record only settled requirements, owner boundaries, field mappings, and acceptance rules based on the user's requested behavior. Do not turn owner rule files into process logs or verification-result logs.
- `待做工作/`, backlog, legacy, and process documents are not updated with results by default. Update them only when the latest user message explicitly asks to modify that specific document or when the user names that document as the requested target artifact.
- If a true blocker prevents implementation or board testing, report the blocker in the final chat reply. Write a blocker into `待做工作/遗留.md` only when the user explicitly requests a persistent legacy/blocker record.
- If the user explicitly requests a human document update, keep it concise and human-readable. Do not write command流水, long logs, AI-only reasoning, progress records, or verification-result logs into human docs.

RISK_GRADED_EXECUTION_RULE:
- This is the detailed gate expansion for `P0_ENTRY_RULES` item 2. The top entry rules decide the first path; this section defines the full gate expectations after classification.
- Before choosing gates, classify the slice as P0, P1, or P2. If uncertain, choose the higher risk class. If a low-risk slice discovers native ownership, SHM ABI, Broker/control routing, motion safety, parameter truth, fallback survivor, or board-visible behavior, escalate immediately and use the higher class from that point.
- P0 high-risk slices include any change to native/LinuxCNC/HAL/EtherCAT ownership, microkernel parameter truth, CPU0 ownership/isolation/affinity/scheduling, typed SHM ABI or State Publisher ownership, Broker/product control paths, runtime motion, safety gates, wcheckpoint, segment boundary, stillness gate, settings-runtime truth, board-visible behavior, deploy/startup scripts for resident owners, or any supported fallback/compatibility survivor. P0 keeps the full doc-first, owner, no-fallback, ABI, focused test, VM/board/motion gates required elsewhere in this file.
- P1 medium-risk slices include ordinary product behavior, non-motion UI behavior, feature documentation, local refactors, non-realtime configuration, and tests that can affect a supported runtime path but do not change P0 ownership or safety semantics. P1 requires the relevant owner document when behavior changes, scoped no-fallback cleanup for touched paths, targeted compile/unit/contract tests, and honest `local_verified_only` if board gates are not run.
- P2 low-risk slices include spelling, comments, formatting, markdown link fixes, test name cleanup, and source annotation/import compatibility changes that do not alter runtime semantics, public contracts, owners, schemas, generated ABI, deployment, or board-visible behavior. P2 uses status/diff review, backup before edit, `git diff --check`, and the smallest syntax/import/focused gate that can catch breakage from the touched file; it must not expand into full VM/board/motion gates unless the change is reclassified.
- Low-risk changes must not update high-volatility owner requirements, SHM schemas, native helper allowlists, resident daemon lifecycle, product control IPC, or fallback policy text as a side effect. If those files must change, the slice is not P2.
- Board/operator/motion verification is mandatory for P0/P1 slices whose behavior is board-visible or motion-capable. Any board program change that enters the board artifact or affects board runtime behavior under `D:\v5\board` requires full current board-source build, board deploy, and original UI/operator/control path closure before pass/fixed/verified language. Do not require board gates for P2 cleanup or doc/rule-only edits unless the edit itself changes a board-visible requirement.

FINISH_LINE_MATRIX:
- doc_or_rule_only: update requested document/rule -> run text sanity such as `git diff --check` -> final may say doc/rule updated when the edited text is internally consistent.
- non_board_local_code: update doc first if behavior changed -> edit source/config -> run targeted compile/unit/contract/static gates -> final status `local_verified_only` unless board closure also ran.
- board_program_change: update doc first if behavior changed -> edit board source/config/script/service/deploy files in the owning truth location -> run focused local gates -> configure and build the full current board source from the Windows truth synchronized into the VM build directory -> deploy canonical artifact to the board -> restart/reload the affected board runtime closure -> trigger the original UI/operator/control path automatically -> collect deployment identity and behavior evidence -> final status `board_verified` only after the verification actually ran.
- board_visible_function_code: update doc first if behavior changed -> edit source/config in the owning truth location -> build full current board source when runtime is involved -> deploy canonical artifact -> trigger the original UI/operator path automatically -> collect the minimal verification outputs needed for the claim -> final status `board_verified` only after the verification actually ran.
- motion_capable_code: complete board_visible_function_code plus native active model 对应的 `board/gcode/golden/cc-ac.ngc` 或 `board/gcode/golden/cc-bc.ngc` golden motion run result；no golden loop means no pass/fixed/done claim.
- blocked_or_unsafe: keep source state honest -> record exact blocker and missing test in final reply -> final status `blocked` or `source_only`; never claim fixed/done/verified. Do not append/update LEGACY_FILE unless the user explicitly asks for a persistent blocker record.
DO_NOT_STOP_AT:
- source edits only
- docs/rules/legacy records only
- local compile/unit/mock tests only when board closure is required
- touched-target or partial VM build when a board program change requires full board-source build
- direct UDS diagnostics when the user-facing defect is a UI button/operator path
- replacing the reported failure with a new popup/error/guard/disabled-state instead of fixing the requested behavior
- keeping an existing popup/error/guard/disabled-state on the reported path after implementation work claims the behavior is supported
- keeping an AI-made branch, alternate route, compatibility mode, or shadow path that is not an actual supported product/process flow
- VM build without board deploy when board-visible behavior changed
- plan/checklist/legacy record without executing required tests
- reporting the first command/tool/test error without diagnosis and fix-forward attempts
- writing any result/blocker record into LEGACY_FILE or other human documents unless the user explicitly asks for that document update

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
CPU0_MICROKERNEL_EXCLUSIVE=CPU0 is reserved exclusively for the microkernel/native motion-precision control flow. UI, Broker, State Publisher, provider/action scripts, diagnostics, deploy/build helpers, logging, and other non-microkernel work must not pin, affinity-bind, isolate-escape, or intentionally schedule on CPU0. Motion precision and deterministic microkernel/native flow priority beat UI refresh, throughput, diagnostics, automation, and convenience; any CPU0 ownership, isolation, or scheduling change is P0 and must be owned by the indexed architecture/native owner before source changes.
ARCH_BOUNDARY_OWNER=系统代码架构硬边界守则.md
ARCH_BOUNDARY_INDEX=功能/需求真源索引.md REQ-NATIVE-OWNER-FIRST, REQ-LINUXCNC-COMMAND-GATE, REQ-PARAM-MEMORY-LIGHTWEIGHT-SAVE, REQ-SETTINGS-RUNTIME-DRIVE-ONLY
ARCH_FAST_FAIL=Any new direct LinuxCNC/HAL control path, UI disk/default parameter truth, file/JSON/FIFO product control IPC, product fallback to old scripts, env-gated compatibility survivor, or V3 private truth for native-owned state is blocked unless the architecture owner explicitly allows the narrow diagnostic exception.
ARCH_EXCEPTION_RULE=Temporary diagnostic exceptions must stay outside product source/runtime/deploy paths, must not become hidden fallbacks, and must be converted back into canonical source fixes before delivery; see 系统代码架构硬边界守则.md for the full exception contract. When the canonical source fix exists, the temporary exception path must be deleted in the same slice; it cannot remain disabled, env-gated, renamed, or test-only as insurance.

## P0_WORKFLOW

BEFORE_EDIT:
1. Read AGENTS.md
2. Run git status/diff for relevant paths
3. For user-proposed feature requirements, check `功能/需求真源索引.md` and the corresponding `功能/` owner before implementation.
4. If the requirement already exists but differs from the latest user message, update the indexed `功能/` owner first; if it does not exist, record it by adding or assigning the `REQ-*` and owner document first.
5. If the latest explicit user feedback conflicts with any project doc/rule/test/code comment/old AI note: treat the old project text as stale, edit the conflicting doc/rule first, then source/config
6. Backup edited files to repo_ignored/<short_task>/backup/
7. Do not create or update process files (`process.md` or `过程.md`)

PROCESS_FILE_DISABLED:
- Do not create or update `process.md`, `过程.md`, or equivalent per-task process markdown.
- Use git status/diff, final reply, and generated verification outputs for required traceability. Use LEGACY_FILE only when the user explicitly asks for a persistent legacy/blocker record.
- Low-level scratch logs may be generated only when required by a tool or verification output; do not create a separate process markdown just to narrate work.

LEGACY_RECORD:
- If any unfinished, blocked, deferred, fail-closed, not-board-verified, or follow-up item remains at the end of a task, report it in the final chat reply.
- If board closure was required but not executed or failed, report the exact missing test, blocker, and acceptance condition in the final chat reply.
- If multiple meaningful attempts cannot resolve a user-raised problem, report the exact blocker, attempts made, missing verification, next concrete action, and status `blocked`, `source_only`, or `local_verified_only` in the final chat reply.
- Do not use LEGACY_FILE as a way to stop early or as a default result sink. First diagnose, retry with allowed alternatives, and fix forward unless blocked by safety, missing user input, external unavailability, or repeated same-blocker attempts. Write LEGACY_FILE only when the user explicitly asks for a persistent record.
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

VM_BOARD_SINGLE_OPERATOR:
- VM sync, VM/ARM build, package, board deploy, board UI restart, operator probe, and motion closure are global single-operator resources.
- Do not acquire or wait on `repo_ignored/locks/vm_board_*` while locks are paused. Before starting any VM sync/build, package, board deploy, board UI restart, operator probe, or motion closure, confirm the VM and board are idle by live process/status probes.
- If a real VM, board, build, deploy, UI restart, operator, or motion process is busy, diagnose it. The current user authorizes ending other blocking processes when it is safe and non-destructive; otherwise stop the VM/board step and report the real process/resource blocker.
- Never sync while another real build is running, build while another real sync is running, deploy while another real build/deploy is running, or run board verification while another real operator/motion process is controlling the board.
- NAS/off-machine backup rsync jobs that only copy existing deliverables or backup folders are not VM build/deploy/operator resources. Do not wait for them, do not stop them, and do not treat them as blockers for VM build, ARM build, board deploy, or board verification unless they are actively modifying the current owner source/build/deploy directory.
- Every VM/ARM build must use a task-specific staging/build directory and must not reuse unknown old artifacts.
- Board deployment must use the full current `RUNTIME_SOURCE_ROOT` runtime source after it is synced into a clean VM build workspace, plus freshly built VM/ARM artifacts from that source; partial deploys or stale artifacts cannot close board delivery.

BOARD_UNREACHABLE_RECOVERY_RULE:
- If the development board is unreachable by ping/SSH/remote relay during a board deploy, restart, recovery, or verification slice, treat the failure as a recoverable board-access fault before declaring `blocked`: use the existing tools relay power-cycle path for the development board plus COM/serial boot monitoring when available, then re-probe ping, SSH, port 18080, `/remote/info`, and board relay/input readiness.
- Relay power-cycle and COM monitoring are recovery/diagnostic steps only. They must not patch board product files, bypass full-current `RUNTIME_SOURCE_ROOT` sync/build/deploy, replace original UI/operator-path verification, or become a hidden runtime fallback.
- Use bounded attempts and preserve evidence in generated verification output where needed; summarize only the relevant result in the final reply. If relay control or COM monitoring cannot restore access after meaningful attempts, report the exact remaining blocker, attempts made, missing verification, and next acceptance condition in the final reply; do not append/update `待做工作/遗留.md` unless the user explicitly asks.

SOURCE_TRUTH:
- Runtime source truth is split by owner: Linux/native/LVGL/runtime-service source and project tools belong in `RUNTIME_SOURCE_ROOT` (`D:\v5\board`); Markdown, screenshots, and Windows/Vivado-owned sources belong in `PROJECT_ROOT` (`D:\v5`); `D:\v3` is read-only reference only. Board `/opt/8ax/...`, VM staging/build workspaces, deploy outputs, `bak/`, `repo_ignored/`, `artifacts/`, `deploy_tmp/`, old builds, and temp clean copies are not source truth.
- Source fixes must be made in the owning truth location. If a board copy, VM staging copy, or temporary artifact is inspected or patched for diagnosis, reproduce the change in `RUNTIME_SOURCE_ROOT` or `PROJECT_ROOT` according to owner, rebuild from that owner, and redeploy before any source/fix claim.
- Every VM/board build/package/deploy must use the full latest `RUNTIME_SOURCE_ROOT` runtime source after current diffs and relevant untracked source files are understood, then sync that source into the VM build workspace before compiling. Partial copies, partial builds, side branches, stale VM/board copies, stale artifacts, or single-file deploys are diagnostic only and cannot close delivery.
- Destructive mirror delete/overwrite options are forbidden unless the source is the full current worktree and excluded files are recorded as generated/ignored/non-source.

DOC_FIRST:
- Any new or changed product requirement starts in `功能/`: update `功能/需求真源索引.md` only to locate or assign the single `REQ-*` owner, then update that owner document before changing code, tests, plans, backlog, legacy notes, or other docs.
- Latest explicit user feedback is the highest requirement-change input; when it conflicts with current docs/rules/tests/comments/old notes, settle the change into `功能/需求真源索引.md` and the relevant `功能/` owner first, then source/config.
- Parameter/native truth changes use `功能/需求真源索引.md` and `功能/0-1开机参数入内存.md`/architecture owners before source edits.
- Required order is owner doc/rule -> canonical source/config -> local gates -> VM/board original operator-path closure when required -> final chat report. Do not create a document result record unless the user explicitly asks for one.
- Board-facing behavior cannot close at doc/source/local-test level; without board/operator evidence, report only `source_only` or `local_verified_only`.

MICROKERNEL_PARAM_MIGRATION_WORKFLOW:
- Scope source of truth: 功能/0-1开机参数入内存.md is the highest feature requirement for boot-into-memory parameters, microkernel/native truth, and V3 second-truth retirement.
- New or retained V3-made parameters are allowed only after inventory proves the microkernel/LinuxCNC/HAL/EtherCAT/native owner has no existing field, no registrable native helper/gate, no extensible runtime-memory/API readback, and no equivalent persistent owner for that semantic. Record that proof in 功能/0-1开机参数入内存.md before adding the parameter.
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
- MCP_FIRST: When an MCP tool/resource is available for the target VM, board, browser, repository, filesystem, or service, use MCP first. Use SSH, shell commands, browser automation, or ad hoc probes only when the relevant MCP path is unavailable, insufficient, or explicitly not suited to the task; state the fallback reason in the final reply when it affects verification.
- V5_MCP_BOUNDARY: For this project, the only v5 remote MCP execution entries are VM `vm_exec` and board `board_exec`; do not use or add separate VM build/scan/sync or board diagnostic MCP entries. Use the canonical project sync/build/deploy scripts through these execution entries.
- REPEAT_WORK_TOOL_FIRST: If a task requires repeated checks, repeated edits, repeated migration, repeated verification, or broad pattern application, use an existing focused tool first. If no suitable tool exists, build the smallest task-appropriate tool or script, verify it on a narrow sample, then continue the repeated work through that tool instead of manual repetition.
- Prefer reusable project tools under `D:\v5\board\tools\` for project-wide checks; do not create or preserve `D:\v5\tools` as a second tool source.
- Use repo_ignored/<short_task>/scratch/ for task-only scratch tools
- Record useful tool command/path in final reply or generated verification output when it is needed for handoff
- Do not install, enable, configure, or depend on WSL. Linux build/test work must use the synced VM build workspace generated from `RUNTIME_SOURCE_ROOT`, or existing board/VM tools only; runtime source edits still belong in `D:\v5\board`.

TOOL_GAP_FIRST:
- If the required local, VM, board, SHM, Broker, ABI, artifact-identity, or motion evidence cannot be produced by existing tools, build or extend the smallest appropriate tool before changing product behavior further.
- Reusable evidence tools belong under `D:\v5\board\tools\` with focused tests or self-checks. Task-only probes belong under `repo_ignored/<short_task>/scratch/`; temporary board probes belong only under `/tmp/v5_test_tools` or `/run/v5_test_tools` and must never become product runtime paths or hidden fallbacks.
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
- The required board sequence is: sync full current `RUNTIME_SOURCE_ROOT` (`D:\v5\board`) into a clean VM build workspace -> build VM/ARM artifacts there -> deploy canonical artifact -> trigger the original operator path automatically -> collect the minimal Broker result, SHM/status, events/touch logs, UI screenshot/framebuffer, or service logs needed to prove the specific claim -> run motion golden verification when motion-capable.
- Writing documentation, adding a legacy item, showing a plan, or running only local tests cannot close a board-facing code change.
- If the board is unavailable, a live board/VM/operator/motion process is active, hardware is unsafe, or a required precondition is missing, retry or work around when practical; if still blocked, mark the task blocked/source-only in the final reply, and do not claim the feature is fixed or board-verified. Do not append/update `待做工作/遗留.md` unless the user explicitly asks.

LOCAL_GATES:
- compile/build where applicable
- python -m py_compile for touched Python
- focused pytest/contract tests
- UDS/SHM simulation for protocol/state changes
- rg/static scans for forbidden paths
- for microkernel-owned parameter work, run the current parameter audit tool from `D:\v5\board\tools\` when such a v5 tool exists, using the synced VM workspace if Linux execution is required, then scan/classify settings_runtime.json/current_tool_state.json/linuxcnc_modal_defaults.var/config_cache/Broker snapshot/SHM snapshot usages, direct linuxcnc.command/halcmd access, V3 private parameter files, and stale unnumbered boot-parameter doc references before claiming the rule/doc is aligned
- git diff --check for touched files

CODE_CHANGE_BATCH_CADENCE:
- Default code/config cadence is local-tool-first, then one final full-current VM/board/operator/motion slice when required. Detailed batch, field, integration manifest, probe, and rerun rules live in `LOCAL_FIRST_FAST_PROGRESS`; do not maintain a second copy here.

LOCAL_FIRST_FAST_PROGRESS:
- For split, Broker, UI, SHM, settings, drive, tool, or board-visible code work, start with the existing focused local gates for the touched path from `D:\v5\board`: touched-file `python -m py_compile`, focused pytest/contract tests, current v5 audit tools under `D:\v5\board\tools\` when microkernel/native truth is relevant, and `git diff --check`.
- Prefer the remaining focused local checks before any board use: touched-file `python -m py_compile`, focused pytest/contract tests, and current v5 microkernel-specific tools under `D:\v5\board\tools\` when relevant.
- Default cadence is batch-local-then-one-board: keep iterating source fixes against local/static/unit/contract gates until they are clean, then run one final full-current VM build, board deploy, and operator/motion closure for the accumulated slice. Do not repeatedly build/deploy/operate the board for every small local edit unless the current blocker is board-only or the user explicitly asks for incremental board probing.
- Field-level work must stop at `local_verified_only` until it joins an integration board slice. For each field or field group, first run current v5 audit tools under `D:\v5\board\tools\` when microkernel/native truth is relevant, touched-file `python -m py_compile`, focused pytest/contract tests, and `git diff --check`. Until the integration board slice runs, the only allowed positive conclusion is `local_verified_only`; do not write fixed/done/verified/board_verified/works on board/live/board usable equivalents.
- If existing tools do not cover the needed owner-local, VM, board, SHM, Broker, ABI, artifact-identity, or motion evidence, build the missing tool before changing product behavior: reusable project tools go under `D:\v5\board\tools\` with focused tests; one-off diagnostic probes stay under `repo_ignored/<short_task>/` and must never become product runtime paths or hidden fallbacks.
- For C/LVGL or board-shipped runtime changes, use the existing `RUNTIME_SOURCE_ROOT` v5 tooling instead of ad hoc copies: edit `D:\v5\board`, sync it to the VM build workspace, build there, deploy with the synced `tools/deploy/run_v5_board_acceptance.sh --apply` or the focused v5 deploy/verify script required by the owner, then run the focused operator or motion probe required by that owner.
- A failed final VM/board/operator/motion pipeline should be treated as evidence: fix the canonical source, rerun the relevant local gates first, and only rerun the required final pipeline stage after the local failure class is closed.
- After multiple fields reach `local_verified_only`, review the accumulated current source truth directly before the final integration board slice: sync full current `RUNTIME_SOURCE_ROOT`, build in the VM workspace, deploy the canonical artifact, trigger the original UI/operator path, collect SHM, Broker result, events/logs, screenshot or relay evidence, and run the native active model matching `board/gcode/golden/cc-ac.ngc` or `board/gcode/golden/cc-bc.ngc` for motion-related claims.
- If dry-run or touched-path classification requires `operator` or `motion`, do not run the final pipeline without an explicit probe. Provide `--operator-probe` and a task-local `--operator-probe-config` under `repo_ignored/<short_task>/` unless the selected pipeline mode already has a built-in probe.
- For Python shipped to the board, local Python may be newer than board Python; avoid runtime-evaluated modern type aliases or syntax in product modules unless target import/build proves compatibility.

BOARD_CLOSURE_ENTRY:
- Do not cite a missing orchestration tool as a required or optional closure path. A new orchestration entry may be named only after a real tool is added in the same slice with tests.
- Board-visible closure uses the existing direct v5 tools: sync full-current `RUNTIME_SOURCE_ROOT` into the VM workspace, run VM/ARM build there, deploy/verify through the synced `tools/deploy/run_v5_board_acceptance.sh --apply` or the focused v5 deploy script required by the owner, then the focused operator or motion probe required by that owner.
- stdout/stderr/result files must be redacted before persistence. Tokens, passwords, authorization headers, private key material, cookies, and secret-like environment values must not be written to `repo_ignored/` evidence.
- Before any board deploy, operator probe, or motion probe, perform a live board/VM busy check. Do not create lock files while `LOCKS_PAUSED_BY_USER=true`; stop as `blocked` only for real process/resource contention, not historical lock files.
- For driver profile, driver parameter, authorization, or VPS-downloaded map changes, evidence must include local SHA256, uploaded/board SHA256, and runtime-loaded identity where applicable. A mismatch is fail-closed and cannot support a pass claim.
- Board deploy artifacts must be ARM/Linux ELF from the VM/ARM toolchain. Verify `file`/`readelf -h` before upload; x86/x86-64/Windows/host-built artifacts fail with `ARTIFACT_ARCH_MISMATCH` and must not overwrite the board.
- Status words such as `relay_ready`, `board_deployed`, or `board_runtime_ready` do not override `PASS_CLAIM_GATE`; motion claims still require original operator path plus the native active model matching `cc-ac.ngc` or `cc-bc.ngc`.

BOARD_REQUIRED_WHEN:
- Use `BOARD_FUNCTION_CLOSURE_REQUIRED`, `INCREMENTAL_UI_AUTOMATION_GATE`, and `PASS_CLAIM_GATE` to decide board need; this block is only a fast reminder.
- Board closure is required for user-visible pass/verified/board_verified/release_ready claims, board-facing button/function behavior, and motion/state/control path behavior.

BOARD_CLOSURE:
- Board closure follows `BOARD_FUNCTION_CLOSURE_REQUIRED`: full current `RUNTIME_SOURCE_ROOT` sync, VM build/deploy, canonical artifact, original UI/operator path, and minimal SHM/Broker/log/screenshot or relay evidence.
- Direct UDS is diagnostic unless the original defect is UDS/API; UI issues require simulated screen tap/button path.
- Motion-capable closure requires the native active model matching `cc-ac.ngc` or `cc-bc.ngc` golden motion loop; simulated UI input is the trigger path, not a substitute.
- Do not use direct `/dev/fb0` dump/capture scripts for screenshots; use product remote relay/LVGL flush framebuffer or another verified color-correct path.

- Do not leave `/run/8ax_v3_product_ui/disable_remote_relay` or `/run/8ax_v3_product_ui/disable_remote_input` present at handoff unless the user explicitly asked for remote access to remain disabled.

PASS_CLAIM_GATE:
- Words or equivalents such as fixed, repaired, done, passed, verified, board_verified, release_ready, works on board, live, closed-loop complete, or operator_says_ok require matching executed verification.
- If board closure was required but not run, final status must explicitly say `not_board_verified` or `source_only`; do not bury this after summaries.
- If only local gates ran, final status is `local_verified_only`; it is not a substitute for board closure.
- Output paths are required only when files are actually generated or needed to substantiate the verification; do not create separate basis files just to satisfy process.

MOTION_OR_REAL_MACHINE:
- Home/Jog/Start/run/E-stop/program/axis/rotary validation requires native active model 对应的 `cc-ac.ngc` 或 `cc-bc.ngc` golden motion verification before any pass claim
- Motion-related "passed/verified/board_verified" requires UI original-path trigger plus model-matched golden loop verification
- `cc-ac.ngc` and `cc-bc.ngc` are model-specific validation inputs; do not rewrite or axis-substitute either program at runtime
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

ARCHIVE:
- Long-lived verification outputs/history -> repo_ignored/<short_task>/
- Do not create process files. Do not place verification outputs, backups, or task scratch outside PROJECT_ROOT; the project folder must remain self-contained for whole-folder moves

## P1_PATHS

MAIN_BRANCH=main
PRODUCT_UI=app
WIN_CLIENT=8ax-win
VM_SSH_TARGET=z20-vm
VM_PATH_USER=root
RUNTIME_SOURCE_ROOT=D:\v5\board
VM_SYNC_SOURCE_ROOT=/root/Desktop/v5
VM_BUILD_ROOT=/root/Desktop/v5_build/build
ARCHIVE_ROOT=repo_ignored

VM_ACCESS:
- Preferred target for Linux build/deploy execution is the existing VM MCP access; use the existing SSH alias only as fallback when MCP is unavailable or insufficient. Do not invent credentials or require a different user unless that is already configured.
- Before any VM build, first confirm `RUNTIME_SOURCE_ROOT` exists on Windows, then sync Windows `D:\v5` one-way into `VM_SYNC_SOURCE_ROOT` with `D:\v5\board\tools\sync_win_source_to_vm.py`; use the default remote drift check unless there is a measured reason to pass `--skip-remote-drift-check`. Probe the VM copy path with `test -d /root/Desktop/v5 && readlink -f /root/Desktop/v5` through VM MCP first, then the existing SSH alias if MCP cannot provide the probe.
- If MCP and SSH alias are unavailable, use the existing VM console/hypervisor network setup or documented `vm-bak/` recovery notes; do not install/enable WSL as a workaround
- Treat Windows `PROJECT_ROOT`/`RUNTIME_SOURCE_ROOT` as the canonical source. Treat VM `VM_SYNC_SOURCE_ROOT` as a generated copy refreshed from Windows before build/deploy. VM copies and build directories must not be edited or treated as source truth.

## P1_FEATURE_DOCS

FEATURE_DOCS_NOTE:
- This is a convenience route map only. `功能/需求真源索引.md` remains the authoritative `REQ-*` owner index when this map is incomplete or stale.
- Prefer searching the index for the exact `REQ-*` before editing requirements; use the aliases below only to open likely owner docs quickly.

requirements_index=功能/需求真源索引.md
boot_params_memory=功能/0-1开机参数入内存.md
param_classification=功能/0-2参数归类.md
shared_memory=功能/0-4共享内存.md
shm_display_projection=功能/0-4共享内存.md
remote_display_relay=功能/0-4共享内存.md
microkernel=功能/微内核.md
rtcp_native_status=功能/微内核.md
no_periodic_disk_writes=功能/微内核.md
settings_bus_pulse=功能/2-2设置页总线模式设置项作用说明.md
bus_pulse_difference=功能/2-1总线脉冲区别.md
settings_buttons=功能/2-3设置页每个按钮作用和目的.md
settings_set_drive=功能/2-4设置页设置驱动按钮作用和目的.md
axis_zero=功能/2-5轴设0软件偏置方案.md
home=功能/3-2回零按钮开机强制回零与双模式真实回零方案.md
wcs_work_offset=功能/1-1加工坐标系偏移值保存与读取实现.md
drive_command_map=功能/1驱动命令映射表命令需求.md
drive_profile_adapter=功能/1驱动命令映射表命令需求.md
vps_auth=功能/VPS登录与项目对接说明.md
toolpath=功能/3-4UI刀路绘图顺序与状态稳定方案.md
rtcp_g53_active_model=功能/3-4UI刀路绘图顺序与状态稳定方案.md
ui_refresh_rate=功能/需求真源索引.md
ui_first_frame_cache=功能/需求真源索引.md
main_buttons=功能/3-1主页面按钮功能.md
estop=功能/3-1主页面按钮功能.md
start=功能/3-1主页面按钮功能.md
gcode_hot_path=功能/0-1开机参数入内存.md
start_hot_path=功能/3-1主页面按钮功能.md
rotary_equivalent=功能/3-3旋转轴等效角目标与解决方案.md
native_helper_whitelist=功能/0-3native_helper白名单与验收说明.md
popup=功能/跳窗提示方案.md
touch_input=功能/-触摸校准与输入链路说明.md
auto_closed_loop=功能/自动闭环测试方式.md
board_full_build_closure=功能/自动闭环测试方式.md
red_point=功能/3-4UI刀路绘图顺序与状态稳定方案.md

## P1_COMMAND_STYLE

SEARCH=rg first
WINDOWS_SEARCH_QUOTING:
- For repository searches on Windows, call `rg` directly whenever possible; do not wrap `rg` searches in `cmd /d /c` unless a real cmd builtin is required.
- Do not pass multi-keyword searches as one quoted regex with `|` through PowerShell or cmd. Use separate ripgrep patterns: `rg -n -e pattern1 -e pattern2 -- path`.
- Treat `|`, `&`, `<`, `>`, `(`, `)`, `%`, `!`, and nested double quotes as shell metacharacters, not safe search text. Put each search term behind its own `-e`, and use `--` before paths when practical.
- If a required search term contains shell-sensitive characters or mixed quotes, put the search in a small scratch script under `repo_ignored/<short_task>/scratch/` and run the script; do not keep retrying fragile one-line shell quoting.
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
