# Windows 默认构建与 CI 改进方案

## 1. 目标

把当前分散的本地门禁收口为一套可重复的“Windows 主机快速门禁”：

1. 新电脑、空构建目录下，Windows 默认 CMake 配置、编译和 CTest 可以稳定通过。
2. Windows 不再尝试编译依赖 `dirent.h`、`unistd.h`、`sys/mman.h`、`linux/input.h` 或 Linux ABI 的目标。
3. 本地提交前检查与 GitHub CI 调用同一个脚本，不在 YAML、人工命令和其它工具中分别维护测试清单。
4. 保持当前一个 `main` 的 Git 策略；本地门禁阻止坏提交推入 `main`，GitHub CI 负责在干净环境再次复核。
5. 不把本工作扩展成 PetaLinux、Linux kernel、整张镜像或板端运动回归。

相关现有 owner：

- Windows 优先、最小受影响构建和产品运行闭包：[`../功能/全局通用配置需求.md`](../功能/全局通用配置需求.md)
- 板端程序构建、部署和 operator 闭环：[`../功能/自动闭环测试方式.md`](../功能/自动闭环测试方式.md)
- 当前 CMake 实现：[`../board/app/CMakeLists.txt`](../board/app/CMakeLists.txt)

## 2. 当前问题

### 2.1 Windows 默认构建失败

当前 CMake 已经做了部分平台判断：

- `v5_local_regression` 只注册当前平台允许执行的测试。
- 部署 manifest 聚合时存在 `V5_UNIX_ONLY_RUNTIME_TARGETS`。
- 部分链接库使用 `if(NOT WIN32)` 或 `if(UNIX)`。

但Linux专用目标仍在平台判断之前通过 `add_library()` 或 `add_executable()` 无条件创建。它们因此进入默认 `ALL` 构建，Windows会继续编译其源码并遇到：

- `dirent.h`
- `unistd.h`
- `sys/mman.h`
- `linux/input.h`
- `CLOCK_MONOTONIC`及其它POSIX/Linux接口

这不是产品业务逻辑编译错误，而是目标分类和平台边界不完整。只在 `add_test()` 阶段排除测试无法解决默认构建失败。

### 2.2 缺少持续集成

仓库当前没有 `.github/workflows`。文档路由、C smoke、Python测试、WinRemote安全测试以及Factory/Dealer构建都依赖人工或AI主动执行。

风险包括：

- 新提交遗漏某一类门禁。
- 不同执行者使用不同命令和测试集合。
- 旧构建缓存掩盖缺失依赖。
- `main`推送后才发现新电脑无法构建。
- 本地通过但干净环境失败。

## 3. CMake目标分类

所有自写C/C++目标分成以下三类：

| 分类 | 运行环境 | 典型内容 | Windows默认构建 |
|---|---|---|---|
| `portable_host` | Windows/Linux host | 纯算法、格式、协议、状态机、headless UI、无OS设备依赖的smoke | 构建并测试 |
| `linux_runtime` | Linux native | POSIX SHM、UDS、`/proc`、`linux/input.h`、LinuxCNC native访问 | 不定义、不构建 |
| `arm_product` | ARM交叉环境 | 部署manifest中的板端产品二进制 | 不在Windows构建 |

### 3.1 平台判断必须包住目标定义

Linux专用目标应在 `add_library()` / `add_executable()` 外层判断：

```cmake
if(UNIX)
    add_executable(v5_cpu_usage_snapshot_smoke
        ../services/state_publisher/v5_cpu_usage_snapshot.c
        ../services/state_publisher/v5_cpu_usage_snapshot_smoke.c
    )
endif()
```

不能只在测试注册处使用：

```cmake
if(UNIX)
    v5_register_local_smoke(v5_cpu_usage_snapshot_smoke)
endif()
```

后者只阻止CTest运行，不阻止默认构建编译该目标。

### 3.2 混合库按“纯逻辑核心＋平台传输”拆分

如果一个库同时被Windows smoke和Linux runtime使用，不增加假的Windows兼容实现，而是按职责拆分。

State Publisher建议边界：

```text
v5_status_projection_core
  纯结构、格式、generation、投影和校验逻辑

v5_status_shm_posix
  shm_open、mmap、clock_gettime、unistd及Linux生命周期
```

Command Gate建议边界：

```text
v5_command_gate_core
  请求校验、状态机、参数和结果逻辑

v5_command_gate_posix
  Unix Domain Socket、LinuxCNC传输、mmap及进程生命周期
```

拆分原则：

- Windows只编译真实可移植核心。
- Linux/ARM把核心与POSIX传输重新组合成当前唯一产品实现。
- 不添加Windows `mmap`、Linux input或Unix Socket假实现。
- 不保留第二套业务逻辑、fallback或仅测试使用的影子实现。

## 4. Windows统一构建入口

### 4.1 CMake Preset

增加：

```text
board/CMakePresets.json
```

至少提供一个 `windows-ci` preset，固定：

- `BUILD_TESTING=ON`
- Windows支持的生成器和编译器
- 独立、可删除的构建目录
- 不构建Linux runtime
- 不构建ARM product
- CTest失败时输出详细日志

建议调用：

```powershell
cmake --preset windows-ci
cmake --build --preset windows-ci --target v5_local_regression
ctest --preset windows-ci --output-on-failure
```

现有 `v5_local_regression` 应继续作为CMake内部测试聚合目标，不另建第二份C测试清单。

### 4.2 默认构建语义

Windows上的默认构建应表示：

> 构建当前平台真实支持的全部host目标和测试。

它不表示Windows能够生成ARM板端产品。板端完整产品目标仍由VM/ARM交叉环境中的 `v5_product_runtime` 和部署manifest裁决。

## 5. 唯一主机门禁脚本

增加：

```text
board/tools/ci/run_v5_host_gate.ps1
```

该脚本是本地和GitHub CI共同调用的唯一入口，建议顺序为：

1. 检查工作目录和工具版本。
2. 执行当前提交范围的 `git diff --check`。
3. 执行严格文档owner路由：

   ```powershell
   python board/tools/docs/verify_v5_document_routes.py --strict-details
   ```

4. 从空的Windows CI构建目录执行CMake configure。
5. 构建并运行 `v5_local_regression`。
6. 执行登记的Python focused tests。
7. 执行WinRemote TLS、证书固定、challenge-response、更新签名和anti-rollback测试。
8. 构建Factory Client。
9. 构建Dealer Client。
10. 汇总每个阶段的命令、耗时和PASS/FAIL；任一失败立即返回非零。

门禁脚本不得：

- 枚举或校验与当前host门禁无关的Linux/kernel/PetaLinux完整输入。
- 启动VM、板端部署或真实运动。
- 上传VPS或读取发布私钥。
- 在CI中生成或替换正式WinRemote发布文件。
- 把失败步骤静默降级为warning。

## 6. GitHub CI

增加：

```text
.github/workflows/v5-fast.yml
```

YAML只负责准备环境和调用：

```powershell
./board/tools/ci/run_v5_host_gate.ps1
```

测试命令、目标名单和通过标准不得复制到YAML。

### 6.1 第一阶段CI范围

| 门禁 | 是否进入GitHub CI |
|---|---|
| 文档owner路由 | 是 |
| 当前提交范围`git diff --check` | 是 |
| Windows CMake/CTest | 是 |
| Python focused tests | 是 |
| WinRemote TLS/签名测试 | 是 |
| Factory Client构建 | 是 |
| Dealer Client构建 | 是 |
| Linux kernel/PetaLinux/整镜像 | 否 |
| VM/ARM交叉编译 | 否，保留现有最小构建入口 |
| 板端部署和真实运动 | 否 |
| VPS真实上传 | 否 |

### 6.2 CI安全与资源边界

- 触发条件初始只保留 `push: main` 和 `workflow_dispatch`。
- GitHub token权限保持 `contents: read`。
- 不配置VPS私钥、设备secret、板端SSH凭据或更新签名私钥。
- 设置任务超时。
- 同一提交重复运行时取消旧任务。
- 只上传失败日志和测试报告，不自动发布EXE、OTA包或板端产物。
- Actions依赖固定受信版本；正式收口时可固定到完整commit SHA。
- 仓库体积较大时允许使用严格登记的sparse checkout，只拉取host门禁直接需要的owner；不能因此漏掉当前目标真实编译输入。

## 7. 单一main下的提交前门禁

只保留一个 `main` 时，GitHub CI只能在push之后发现问题，无法在问题进入main之前拦截。

因此正式链路应为：

```text
run_v5_host_gate.ps1
  -> PASS
prepare_v5_git_snapshot.ps1
  -> commit/push main
GitHub v5-fast
  -> 干净环境二次复核
```

现有：

```text
board/tools/git/prepare_v5_git_snapshot.ps1
```

应在普通staging、commit和push之前调用主机门禁。门禁失败必须停止推送。

这样不需要建立长期开发分支：

- 本地主机门禁负责阻止已知坏提交进入 `main`。
- GitHub CI负责证明同一提交在另一台干净机器仍能通过。
- CI失败时应立即修复 `main`，不能用跳过门禁的第二入口继续发布。

## 8. 实施顺序

### 切片A：修复CMake平台边界

1. 列出当前Windows默认构建失败目标。
2. 把Linux专用目标的条件移动到目标定义外层。
3. 仅对确实需要Windows测试的混合库拆出portable core。
4. 用空目录执行Windows configure/build/CTest。
5. 在VM复核ARM `v5_product_runtime`仍完整派生，部署manifest未丢目标。

### 切片B：建立唯一host gate

1. 增加`windows-ci` preset。
2. 增加`run_v5_host_gate.ps1`。
3. 将现有通过命令接入脚本。
4. 增加脚本自身的失败传播smoke。
5. 确认本地一条命令可以从空缓存完成快速门禁。

### 切片C：接入main推送和GitHub CI

1. 让Git快照/推送入口先调用host gate。
2. 增加`v5-fast.yml`，只调用同一脚本。
3. 在GitHub干净Windows runner完成首次PASS。
4. 人工制造一个隔离的编译或测试失败，确认本地和CI都能fail-closed；随后恢复测试输入。

## 9. 验收标准

以下全部满足才算两个问题关闭：

- Windows空构建目录执行默认configure/build成功。
- Windows构建日志不再尝试包含Linux专用头文件的目标。
- `v5_local_regression`全部通过。
- 文档路由、Python、WinRemote、Factory和Dealer门禁全部由同一host gate执行。
- host gate任一子步骤失败时整体返回非零。
- Git推送入口在host gate失败时停止。
- GitHub `main`上的同一提交在干净runner通过。
- GitHub workflow不包含VPS、设备或签名私钥。
- VM/ARM的 `v5_product_runtime` 仍从部署manifest完整派生。
- 若仅修改构建分类且ARM产物hash不变，不扩展板端运动回归；若产物发生变化，则按受影响owner继续最小构建、部署和readback。

## 10. 预计收益

- 新电脑不再依赖旧CMake缓存或人工挑选目标。
- Windows、Linux和ARM目标边界从“测试时判断”前移到“目标定义时判断”。
- 本地与CI不存在两份会漂移的门禁清单。
- 一个 `main` 的策略仍然保持，同时具备提交前拦截和远端干净环境复核。
- 日常CI保持在host快速门禁范围，不会因内核、PetaLinux、整镜像或板端硬件把普通开发拖成重型发布流程。
