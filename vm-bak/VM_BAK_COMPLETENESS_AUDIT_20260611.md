# vm-bak Completeness Audit 2026-06-11

## 结论

补全后结论：针对“`z20-vm` 虚拟机丢失”这个风险，`D:\re\v3\vm-bak` 已经补齐到可恢复状态。现在备份集中除了项目源码和板端运行态，还包含 PetaLinux 2020.2 已安装工具链、Yocto downloads/git2/sstate-cache/tmp/deploy、VM 环境文件和 dpkg 包清单。恢复步骤见 `VM_BAK_ZERO_REBUILD_README_20260611.md`。

剩余边界：这不是“Windows 本机、Vivado 原工程、许可证服务器、Ubuntu 安装源也全部消失”的全世界离线灾备。当前 PetaLinux/LinuxCNC/v3 开发和当前板端系统恢复已经不再依赖旧 VM 唯一磁盘；如果未来还要求从 Verilog/Vivado 原工程重新生成 bitstream，仍应单独保管 Vivado 工程、Vivado 安装包和许可证条件。

## 原始审查结论

`D:\re\v3\vm-bak` 现在可以保护当前开发板项目源码、LinuxCNC/v3 源码、当前板端启动文件和当前板端运行态配置；如果 `z20-vm` 丢失，可以用这些文件恢复项目源码树、继续开发，并恢复当前板端 `/opt/8ax` 运行文件和 SD 启动文件。

但它还不是“空机器、无网络、无 Xilinx/PetaLinux 安装包”的完整离线重建包。要从完全空白 PC/VM 离线重建整套板端系统，还缺 PetaLinux/Vivado 工具链安装介质或工具链镜像、许可证/环境说明、完整 Yocto 下载缓存或源码镜像、sstate-cache。

## 已确认包含

主备份包：

```text
D:\re\v3\vm-bak\vm_rebuild_sources_20260611.tar.gz
SHA256: 21139bc12b5f8b55e8319eb91cdc45b81a41480298d9771e90b1d19154ea633d
Size: 3.778 GiB
File count: 123929
```

主备份包内包含：

- `deliverables/z20_plnx/`
  - PetaLinux 工程源码、`project-spec/`、`.petalinux/metadata`、`config.project`
  - `project-spec/meta-user/recipes-apps/*`
  - `project-spec/meta-user/recipes-kernel/ethercat-master/*`
  - `board_inputs/`
  - `images/linux/`
  - `artifacts/`、`baselines/`
  - `components/yocto/`
- `deliverables/re_plnx_rebuild/`
  - 当前 LinuxCNC/v3 真源
  - `work/8ax-native-linuxcnc_20260529_082424/src/`
  - `work/8ax-native-linuxcnc_20260529_082424/v3/lvgl_app/`
  - `work/8ax-native-linuxcnc_20260529_082424/v3/nc/cc.ngc`
- `downloads/linuxcnc-2.9.7/`
  - PetaLinux `linuxcnc-prebuilt.bb` 的 `EXTERNALSRC`
- `downloads/board_inputs_sync_20260408_084525/`
  - `.petalinux/metadata` 的 `HARDWARE_PATH`
- `work/linuxcnc_src/`

补充板端运行态快照：

```text
D:\re\v3\vm-bak\board_runtime_snapshot_20260611.tar.gz
SHA256: 6708444015e3332c35e2b4037184659c31cd670d6f78d905b021a2abf44e0728
File count: 319
```

补充快照内包含：

- `/opt/8ax/phase0_bus5/`
  - 当前 `settings_runtime.json`
  - 当前 `8ax_bus5_runtime_settings.ini`
  - 当前 `nc/cc.ngc`
  - 当前 HAL/INI/XML/settings 运行目录
- `/opt/8ax/v3_product_ui/`
  - 当前产品 UI 二进制
  - 当前后端脚本、UI init 脚本、scripts 目录
- `/media/sd-mmcblk0p1/`
  - 当前 `BOOT.BIN`
  - 当前 `image.ub`
  - 当前 `system.dtb`

## 当前板端启动文件对应关系

板端当前启动分区文件已在备份内找到来源：

- `BOOT.BIN`
  - SHA256 `70f06ffb93489001af215575450a95c775078275ef7e9050e191d4e931f72a09`
  - 对应 VM：`z20_plnx/images/linux/BOOT.BIN`
  - 也在 `board_runtime_snapshot_20260611.tar.gz`
- `image.ub`
  - SHA256 `3eedaebbba170fd6ec02c021a6137ae1943ce2b65b1622dc162a07be63063273`
  - 对应 VM：`z20_plnx/artifacts/stable_kernel_extract_20260515/image.ub`
  - 同哈希也在 `z20_plnx/baselines/v1/snapshot/boot_files/image.ub`
  - 也在 `board_runtime_snapshot_20260611.tar.gz`
- `system.dtb`
  - SHA256 `ffe4ace807cc2e793c0c55eb3c294ea7b1b4412e864fc9d044eb925d4891e61a`
  - 对应 VM：`z20_plnx/baselines/v1/snapshot/boot_files/system.dtb`
  - 也在 `board_runtime_snapshot_20260611.tar.gz`

## 当前板端运行文件对应关系

抽样核对结果：

- 板端 `/opt/8ax/v3_product_ui/scripts/v3_settings_apply.py`
  - SHA256 `06de72d3aabfa915f67306f2fe58b0e30bd2f6efa8bfbb51b4b6e352f55945ab`
  - 匹配主备份包内 `re_plnx_rebuild/.../v3/lvgl_app/scripts/v3_settings_apply.py`
- 板端 `/opt/8ax/v3_product_ui/scripts/re-v3-8ax-backend.sh`
  - SHA256 `b39195bb59b0396f53ab214b4d31c80782ee957f4529d810e5103f7ce607ceaf`
  - 匹配主备份包内 `re_plnx_rebuild/.../v3/lvgl_app/scripts/re-v3-8ax-backend.sh`
- 板端 `/opt/8ax/v3_product_ui/scripts/v3_linuxcnc_start_run.py`
  - SHA256 `bf18315e9f9a0b98480777a56ba13a4d16b34e1bad65de02c67d18fb8be13d55`
  - 匹配主备份包内 `re_plnx_rebuild/.../v3/lvgl_app/scripts/v3_linuxcnc_start_run.py`
- 板端 `/opt/8ax/v3_product_ui/scripts/v3_toolpath_status_snapshot.py`
  - SHA256 `a2af074ee5bd5999c4ac28c5573441ec88f0be8b3c8e702439bed51595490769`
  - 匹配主备份包内 `re_plnx_rebuild/.../v3/lvgl_app/scripts/v3_toolpath_status_snapshot.py`
- 板端 `/opt/8ax/phase0_bus5/nc/cc.ngc`
  - SHA256 `eb5e49bec13c7c5da5a61909004e7524f0555ca608e5cd36af40541684f895f4`
  - 匹配主备份包内 `re_plnx_rebuild/.../v3/nc/cc.ngc`
- 板端 `/opt/8ax/v3_product_ui/re_v3_lvgl_product_ui`
  - SHA256 `09c0550cee7dd0da216ad0960bf8dc0cfe91bf49c4eb1f9e27083d14992e07b0`
  - 在 `board_runtime_snapshot_20260611.tar.gz` 中保存

## 发现的缺口

### P1: 不是完整离线工具链灾备

VM 上存在 PetaLinux 安装目录：

```text
/opt/pkg/petalinux/2020.2
/home/sj/petalinux/2020.2
```

但 `vm-bak` 没有包含这些安装目录，也没有 PetaLinux 2020.2 installer `.run`。备份包内也没有 Vivado/Vitis/Xilinx 安装器或许可证文件。

影响：

- 如果只有 `vm-bak` 和一台空白电脑，不能直接执行 PetaLinux/Vivado 重建。
- 需要另行安装 PetaLinux 2020.2，并准备对应 Xilinx/Vivado 环境和许可证。

### P1: Yocto 下载缓存不完整

备份包内有：

```text
deliverables/z20_plnx/components/yocto/downloads/uninative/...
```

但未发现完整 `sstate-cache/`，也未发现完整 `downloads/git2/` 源码缓存。

影响：

- `petalinux-build` 可能需要联网重新拉取 Yocto/PetaLinux 依赖源码。
- 无网络或上游源消失时，不能保证从 0 完整重建。

### P1: EtherCAT 相关 recipe 仍依赖远程 Git

`linuxcnc-ethercat_git.bb`：

```text
SRC_URI = "git://github.com/linuxcnc-ethercat/linuxcnc-ethercat.git;protocol=https;branch=master ..."
SRCREV = "de7e377f76873fa99e8ea5dcafd7df916e118024"
```

`ethercat-master_git.bb`：

```text
SRC_URI = "git://gitlab.com/etherlab.org/ethercat.git;protocol=https;branch=stable-1.6 ..."
SRCREV = "b709e58147e65b5e3251b45f48c01ef33cc7366f"
```

备份包有 patch 和 recipe，但没有确认包含这两个 Git 仓库的离线镜像。

影响：

- 有网络时可以按固定 commit 拉取。
- 无网络时不能保证重建 EtherCAT master / linuxcnc-ethercat。

### P2: `z20_plnx/build/` 被排除

`z20_plnx/build/` 是约 26GB 的 PetaLinux 生成目录，已按前一次备份策略排除。

影响：

- 不影响“从源码重新 build”的目标。
- 影响“完全复用当前 VM build workdir 状态、不重新 fetch/compile”的能力。

### P2: PetaLinux 环境自身还需复核

在 VM 上 source `/opt/pkg/petalinux/2020.2/settings.sh` 时，命令路径出现：

```text
petalinux-build
petalinux-config
```

但 settings 输出仍提示缺少部分 host system tools。这个不是 `vm-bak` 的文件完整性问题，但表示新环境恢复后还要按 PetaLinux 2020.2 要求补齐 host packages。

## 风险判断

### 可以做到

- VM 丢失后，恢复 LinuxCNC/v3 源码树。
- 恢复 PetaLinux 工程源码和板端 overlay/recipe。
- 恢复当前板端启动文件和 `/opt/8ax` 运行态。
- 在已安装 PetaLinux 2020.2、具备网络/源码镜像、host packages 正确的环境中，继续重建和开发。
- 不重新编译时，直接用快照中的 `BOOT.BIN`、`image.ub`、`system.dtb`、`/opt/8ax` 文件恢复当前板端运行状态。

### 不能单靠当前 vm-bak 保证

- 空白电脑、无网络、无 PetaLinux/Vivado 安装介质时，从 0 离线重建完整系统。
- 上游 GitHub/GitLab 不可访问时，重建 `linuxcnc-ethercat` 和 `ethercat-master`。
- 无 Xilinx/PetaLinux 2020.2 工具链和许可证时，重建 boot/image/rootfs。

## 建议补齐项

要把 `vm-bak` 提升到“VM 丢失后真正自给自足”的级别，建议再补以下内容：

1. PetaLinux 2020.2 安装器或完整工具链目录备份。
2. Vivado/Vitis 2020.2 安装器、license 使用说明，或确认只需要现有 `system.xsa` 而不再重跑 Vivado。
3. `linuxcnc-ethercat` 的 Git 镜像，固定 commit `de7e377f76873fa99e8ea5dcafd7df916e118024`。
4. `ethercat-master` 的 Git 镜像，固定 commit `b709e58147e65b5e3251b45f48c01ef33cc7366f`。
5. Yocto/PetaLinux 完整 `downloads/` 和 `sstate-cache/`，或者一次成功构建后的离线 source mirror。
6. 一份恢复脚本：解压目录、恢复 `/home/sj/Desktop/...` 路径、重建 symlink、校验 SHA256、运行 `petalinux-build` 前置检查。

## 最终判定

当前 `D:\re\v3\vm-bak` 对“VM 丢失后不丢项目源码和当前板端运行态”是够的。

当前 `D:\re\v3\vm-bak` 对“只靠这个目录，在空白环境、无网络、无 Xilinx/PetaLinux 安装包时，从 0 完整重建开发板系统和 LinuxCNC 软件”还不够。
