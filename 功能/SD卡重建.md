# V5 产品 SD 卡重建

<!-- AI_FAST_READ_BEGIN -->
owner_reqs: [REQ-SD-REBUILD-RUNBOOK]
read_when: [SD卡重建, 启动盘文件, BOOT.BIN, image.ub, boot.scr, rootfs, LinuxCNC重建, PREEMPT_RT, Vivado工程, UI重建, 制卡, 写后回读, 冷启动]
truth: [Windows v5 唯一源码与离线输入 -> Windows 可执行门禁/Vivado -> VM 唯一 current 投影与 canonical build -> 唯一制卡入口 -> SD 全量回读 -> 开发板冷启动 operator 验收]
forbidden: [错误Vivado工程, D:/v3作为输入, VM或板端下载源码, VM第二源码树, 旧SD回填, initrd fallback, 手工复制启动文件, 未过单包直接全镜像, 无证据清缓存, 未回读宣称成功]
readback: [四类source identity, 当前XSA/bit身份, 受影响单包结果, rootfs package/file manifest, BOOT.BIN/image.ub/boot.scr/system.dtb hash, SD逐文件回读, 冷启动COM与真实触摸/服务证据]
impact: [功能/全局通用配置需求.md, 功能/微内核.md, 功能/自动闭环测试方式.md, AGENTS.md, vivado_hw_project, linux, linuxcnc, board/petalinux, board/config/deploy, board/tools/petalinux]
acceptance: [日常incremental-current只重建受影响闭包, 离线认证从空缓存断网全量生成, SD写后完整清单一致, 开发板从SD冷启动并通过产品验收]
detail_sections: [唯一主线, 输入闭包, 启动文件构成, 日常快速重建, 断网全量认证, 制卡与回读, 板端验收, 弯路与禁止项]
<!-- AI_FAST_READ_END -->

引用需求真源：`REQ-V5-OFFLINE-FULL-SD-REPRODUCTION`、`REQ-BUILD-INPUT-WINDOWS-CLOSURE`、`REQ-PRODUCT-RUNTIME-CLOSURE`、`REQ-LINUXCNC-MINIMAL-RUNTIME`、`REQ-MICROKERNEL-SOURCE-REBUILD`、`REQ-BOARD-PROGRAM-FULL-BUILD-CLOSURE`。

## 1. 目的和唯一主线

本文给后续 AI 一条可直接执行的 SD 重建主线。它不替代上面的 requirement owner，只规定怎样把已经登记的 Windows 真源准确、快速地变成一张可启动并可验收的产品 SD。

```text
Windows v5 唯一源码/离线输入
  -> Windows 身份、语法、Vivado 门禁
  -> VM 只读挂载和唯一 current 加速投影
  -> 受影响 target/recipe
  -> 必要 rootfs/manifest
  -> 一次 current 镜像
  -> stage-only 完整产品闭包
  -> 唯一制卡脚本写入并全量回读
  -> SD 冷启动、物理屏、真实触摸、服务和 operator path 验收
```

日常修复默认使用 `incremental-current`。只有明确做发布、灾难恢复里程碑或离线能力重新认证时，才从空 build/downloads/sstate 在断网环境全量重建。两种模式不能混为一谈。

## 2. Windows 唯一输入闭包

| 输入 | 唯一 Windows owner | 必须证明 |
| --- | --- | --- |
| Linux kernel | `../linux/kernel/` + `v5_linux_source_identity.json` | 完整源文件集合、符号链接语义、内容 hash |
| PREEMPT_RT/Yocto realtime metadata | `../linux/realtime/` + `v5_realtime_source_identity.json` | RT 配置/补丁闭包、内容 hash |
| LinuxCNC 2.9.7 与 V5 native 修改 | `../linuxcnc/` + `v5_linuxcnc_source_identity.json` | 完整源树、headless/minimal 构建修改、内容 hash |
| PetaLinux 工程、DT、recipe、XSA/bit | `../board/petalinux/` + `v5_petalinux_source_identity.json` | 当前 project-spec、硬件身份链、内容 hash |
| BitBake 外部源码包 | `../board/third_party/petalinux-source-packages/` + `v5_bitbake_source_inventory.json` | 文件数、总字节、每包 SHA-256、许可证/来源 |
| 当前产品 FPGA | `../vivado_hw_project/` | 当前 `.xpr`、RTL/IP/约束、实现结果、XSA/bit |
| UI、服务、HAL/INI、配置和部署工具 | `../board/` | 产品 target、部署 manifest、脚本/配置门禁 |
| 制卡工具 | `../board/tools/petalinux/write_v5_sd_card.sh` | 设备防误写、分区、组装、manifest、写后回读 |

项目根只允许一个 `.git`。上游源码树内的 `.gitignore`、`.gitattributes` 是普通源文件，不是嵌套 Git；真正的子目录 `.git`、第二工作树、VM checkout、源码备份树必须不存在。

缺源码时只能在 Windows 执行一次显式导入：

```powershell
python .\board\tools\petalinux\import_v5_source_packages.py --project-root .
python .\board\tools\petalinux\verify_v5_source_packages.py --project-root .
```

正式构建开始后禁止 Git/HTTP/HTTPS 临时补源码。VM 的 downloads、work、sstate 和已生成镜像都不是源码真值。

## 3. 当前 Vivado 工程和硬件交接

当前产品只能使用 `../vivado_hw_project/vivado_hw_project.xpr`。`../new-vivado/z20_v1_5_hw_project/` 是不同 FPGA 功能，`D:/v3` 只可读参考，二者都不得给当前 SD 提供 XSA 或 bitstream。

仅在 FPGA/约束/IP/硬件交接变化或当前 XSA/bit 缺失时，在 Windows Vivado 2020.2 中执行：

```powershell
Set-Location .\vivado_hw_project
$env:VIVADO_JOBS='8'
vivado.bat -mode batch -source .\scripts\vivado_gate_current.tcl
vivado.bat -mode batch -source .\scripts\vivado_export_xsa_current.tcl
```

门禁必须得到 fresh `system_wrapper.bit`、实现完成、DRC/timing 报告和 `board_inputs/system.xsa`。随后把当前硬件交接同步到 `../board/petalinux/project-spec/hw-description/system.xsa` 与 `system.bit`，重新生成并验证 `v5_petalinux_source_identity.json`。不得拿历史 bit、VM 生成副本或“能启动的旧 SD”替换当前工程输出。

## 4. 启动盘文件的准确构成

| 产物 | 唯一构成 |
| --- | --- |
| `BOOT.BIN` | 当前 FSBL + 当前 `system.bit` + 当前 U-Boot |
| `image.ub` | 当前 `zImage` + 当前 `system.dtb`；不包含 initrd |
| `boot.scr` | 由 `boot.cmd.default.ext4` 生成，必须回读到 `root=/dev/mmcblk0p2 rw rootwait` |
| `system.dtb` | 当前 PetaLinux device tree，必须包含当前显示、触摸和板级硬件配置 |
| rootfs | PetaLinux minimal rootfs + 最小 LinuxCNC 运行闭包 + EtherCAT/HAL + `v5_product_runtime` + deploy manifest 全部脚本/服务/配置 |

SD 固定为 MBR：2 GiB FAT32 `V5_BOOT` 分区，加占用剩余空间的 ext4 `rootfs` 分区。旧 initrd 启动、`root=/dev/ram0`、旧卡文件回填都不是支持路径。

## 5. Windows 前置门禁

从项目根执行能在 Windows 正确完成的检查：

```powershell
python .\board\tools\petalinux\verify_v5_linux_source.py --project-root . --print-source-hashes
python .\board\tools\linuxcnc\verify_v5_linuxcnc_source.py --project-root . --source-root .\linuxcnc --allow-flattened-symlinks --print-source-hash
python .\board\tools\petalinux\verify_v5_petalinux_source.py --project-root . --source-root .\board\petalinux --print-source-hash
python .\board\tools\petalinux\verify_v5_source_packages.py --project-root .
python .\board\tools\deploy\verify_v5_product_source_closure.py --board-root .\board --validate-shell
git diff --check
```

任一 identity、源码包、硬件输入、产品 target 或 manifest 失败，都必须在 Windows owner 修复后再进 VM。

## 6. VM 只承担 Windows 无法完成的最小步骤

VM 必须证明 `/mnt/v5-source` 是 Windows `v5` 的实时只读共享挂载。构建输出只在 `/root/v5-build`。允许的唯一加速投影是 `/root/v5-build/temp_source/current`：identity 相同直接复用，不同只刷新这一处；禁止 run-id、日期、任务名或备用投影，禁止人工编辑和反向覆盖 Windows。

进入 VM 后先执行：

```sh
findmnt -n -o FSTYPE,OPTIONS -T /mnt/v5-source
. /opt/pkg/petalinux/2020.2/settings.sh
```

正式 BitBake 必须打印 Windows source package 校验和网络关闭门禁；远程 sstate 不得成为必要输入。没有具体缓存损坏证据，不运行 kernel clean、`cleanall`、`cleansstate`，不删除 `tmp/work`、sstate 或全局 fetch stamp。

## 7. 日常快速重建：incremental-current

1. 根据 Windows diff 和 owner identity 只确定受影响闭包。
2. LinuxCNC 变化先跑 canonical focused 入口，证明 package 和必要 rootfs；不得先跑最终镜像：

```sh
V5_PETALINUX_BUILD_USER=sj VM_BUILD_ROOT=/root/v5-build \
  sh /mnt/v5-source/board/tools/linuxcnc/build_v5_linuxcnc_petalinux.sh --focused
```

3. kernel/DT 变化只跑对应 recipe/deploy；UI/runtime 变化先在 canonical ARM build 目录构建 `v5_product_runtime`。失败只重跑本级和下游，不重跑 identity 未变的 kernel、LinuxCNC 或其它包。
4. 单包和必要 rootfs/manifest 通过、且确实需要整卡或启动文件时，才生成一次 current 镜像：

```sh
V5_PETALINUX_BUILD_USER=sj VM_BUILD_ROOT=/root/v5-build \
  sh /mnt/v5-source/board/tools/linuxcnc/build_v5_linuxcnc_petalinux.sh --full
```

5. `--full` 只能在最终一次使用；不带明确证据不得附加 `--clean-kernel`。

成功必须同时看到 Linux/PREEMPT_RT、LinuxCNC、PetaLinux、源码包身份通过，`V5_PETALINUX_NETWORK_DISABLED`、LinuxCNC 最小运行闭包通过，以及 `V5_LINUXCNC_BUILD_OK`。仅“进程退出”或“文件存在”不算。

## 8. 断网空缓存全量认证

该模式只用于证明完整 `v5` 在本地硬盘、VM 缓存和 SD 全丢失后仍可恢复：

1. 使用受支持且已离线落盘的 Windows/Vivado/PetaLinux 工具链或安装介质。
2. 断开网络；使用空的 disposable VM build/downloads/sstate，不复用 current 产物。
3. 只读挂载同一个 Windows `v5`，从四类 identity、Vivado 硬件输入、222 类 BitBake 固定源码包和 product manifest 开始。
4. 从零完成 Vivado（需要时）、Linux/PREEMPT_RT、LinuxCNC、EtherCAT、PetaLinux rootfs/image 和 UI/runtime ARM 构建。
5. 写入空白 SD，全量回读，再在开发板冷启动。
6. 认证完成后删除 disposable 认证构建目录，只保留 canonical current 构建和一套 current 镜像。

只用已有 VM cache 成功构建不能证明离线灾难恢复能力。

## 9. 制卡、全量回读和安全卸载

唯一制卡入口是：

```sh
sh /mnt/v5-source/board/tools/petalinux/write_v5_sd_card.sh --stage-only
sh /mnt/v5-source/board/tools/petalinux/write_v5_sd_card.sh \
  --device /dev/<确认后的可移动整盘> --apply
```

必须先 `--stage-only`。它会重新交叉编译 `v5_product_runtime`、按 `v5_runtime_deploy_manifest.tsv` 组装 rootfs、生成 `BOOT.BIN/image.ub/boot.scr/system.dtb`，并生成 rootfs 全文件 manifest。`--apply` 前用 `lsblk`、`udevadm`、容量、removable 标志和系统盘父设备共同确认目标整盘；不得凭 `/dev/sdX` 字母猜测。

`--apply` 必须完成：卸载旧分区、重建 MBR/FAT32/ext4、写入 boot/rootfs、`sync`、boot 文件 SHA-256 回读、rootfs 全文件类型/权限/大小/hash/链接目标回读、ARM ELF 校验、无 `.git` 检查和安全卸载。缺一项不得拔卡或声明写卡成功。

SD 已直接插在 VM 时就由该入口写卡；QSPI 只在板端无法从 SD 启动且需要远程更新 SD 文件时作为独立 SSH 恢复通道，不是产品 rootfs，也不参与产品功能验收。

## 10. 开发板冷启动验收

1. 将已安全卸载的 SD 插回开发板，继电器完整断电再上电，COM 从 FSBL/U-Boot 开始记录。
2. 证明 `BOOT.BIN -> boot.scr -> image.ub -> /dev/mmcblk0p2 rootfs` 链路正确，无 initrd 或旧 rootfs fallback。
3. 物理 LCD 必须在产品首帧交接后无 login/console 覆盖；远程帧与物理帧一致。
4. 必须用真实手指验证 FT5426、`/dev/input/by-path/z20-touchscreen` 和正式校准 owner；模拟输入不能替代。
5. 验证 LinuxCNC、PREEMPT_RT、EtherCAT 5 从站、HAL、Command Gate、State Publisher、UI relay 和参数 owner 全部在线。
6. 记录上电到“完整主页面可见且真实触摸可操作”的阶段时间；异常超时值不能冒充正常启动耗时。
7. 走真实 UI/operator path；运动验收按 active model 使用原始 `cc-ac.ngc` 或 `cc-bc.ngc`，结束后回到急停/安全状态。

只有完成物理屏、真实触摸、服务、operator/motion 和冷启动证据才允许 `board_verified`。制卡成功但未上板只能报 `local_verified_only`。

## 11. 两天排障中确认的弯路与禁止项

- 不要从 VM、BitBake cache、GitLab、旧 SD 或板端补缺失源码；先在 Windows 下载、解压到唯一 owner、登记 hash，再正式构建。
- 不要把 `D:/v3` 或 `new-vivado` 的 bit/XSA 当当前产品输入；当前产品只认 `vivado_hw_project` 身份链。
- 不要每改一处 recipe 就跑完整镜像；先解析/单包，再 rootfs/manifest，最后一次 current image。
- 不要为了“干净”反复清 kernel、sstate、tmp/work、downloads 或 fetch stamp；这会把分钟级修复变成数小时重建。
- 不要在 VMware shared-folder 上反复扫描/编译数万小文件；使用 identity 一致的唯一 `temp_source/current` 投影，生成物仍写在投影外。
- 不要保留 run-id、日期、任务、备份或第二 VM 源码投影；临时 index/overlay 必须位于 build root 并在结束后删除。
- 不要依赖 LinuxCNC 上游 GUI、Qt、GTK、Tk、文档/manpage 构建；产品只打包解释器、task、HAL、motion、kinematics、状态 API 和必要用户态闭包。fresh source 构建必须证明不会调用 `a2x`。
- 不要用旧 work/stamp 的“曾经成功”掩盖完整源码缺文件；每次 LinuxCNC 源 identity 变化先跑 fresh 单包门禁。
- 不要看见构建进程睡眠就直接杀死；先看日志时间、CPU、wchan、父子进程和锁。只对已确认的僵死节点做最小处置，禁止按名称批量 kill。
- 不要手工拼 `BOOT.BIN`、复制 `image.ub` 或在挂载后的 SD 上补几个文件冒充完成；只运行 canonical 制卡脚本并接受全量回读结果。
- 不要把 QSPI 修成第二产品系统；它只负责独立 SSH、挂载 SD、按 manifest 更新和 hash 回读。

## 12. 最短验收清单

```text
[ ] Windows 只有一棵 v5 源码和一个根 Git
[ ] kernel/realtime/LinuxCNC/PetaLinux/source-packages identity 全通过
[ ] 当前 Vivado 工程、XSA、bit 身份链一致
[ ] 正式构建未联网补源码
[ ] 受影响单包先通过，必要 rootfs/manifest 后通过
[ ] 最终 current 镜像只生成一次
[ ] stage-only 生成 ARM runtime、BOOT.BIN、image.ub、boot.scr 和全文件 manifest
[ ] 目标是已确认的可移动整盘，不是 VM 系统盘
[ ] boot 文件和 rootfs 全量写后回读一致并安全卸载
[ ] 开发板 SD 冷启动、物理屏、真实触摸、服务和 operator path 通过
[ ] 最后处于急停/安全状态
```
