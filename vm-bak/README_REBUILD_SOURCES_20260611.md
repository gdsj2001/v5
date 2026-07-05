# VM Rebuild Sources Backup 2026-06-11

本目录保存从 VM `z20-vm` 拉回的当前开发板可重建源码备份。

## 备份文件

- Archive: `vm_rebuild_sources_20260611.tar.gz`
- SHA256: `21139bc12b5f8b55e8319eb91cdc45b81a41480298d9771e90b1d19154ea633d`
- Size: `3.778 GiB`
- File list: `vm_rebuild_sources_20260611_filelist.txt`
- Manifest: `vm_rebuild_sources_20260611_manifest.txt`
- File count in archive: `123929`

## VM 真源定位

### 板端系统 / PetaLinux 工程

VM path:

```text
/home/sj/Desktop/deliverables/z20_plnx
```

用途：

- PetaLinux 2020.2 工程：`project-spec/`、`.petalinux/metadata`、`config.project`
- 板端 overlay/recipe：`project-spec/meta-user/recipes-apps/*`
- 当前硬件输入和镜像输出：`board_inputs/`、`images/linux/`
- Vivado/XSA/bit 输入：`project-spec/hw-description/system.xsa`、`system.bit`
- 历史启动镜像来源：`artifacts/`、`baselines/`

注意：

- `z20_plnx/build/` 是 26GB 的 PetaLinux 生成目录，已从本备份排除。
- `images/linux/` 未排除，保留当前 VM 可启动镜像输出。

### 当前 LinuxCNC / v3 UI 真源

VM current symlink:

```text
/home/sj/Desktop/deliverables/re_plnx_rebuild/work/8ax-native-linuxcnc-current
```

Real path:

```text
/home/sj/Desktop/deliverables/re_plnx_rebuild/work/8ax-native-linuxcnc_20260529_082424
```

用途：

- LinuxCNC 2.9.7 源码和本地补丁状态
- `v3/` 产品 UI 真源
- `configs/by_machine/8ax/`、HAL/INI/脚本相关源码

整个 `/home/sj/Desktop/deliverables/re_plnx_rebuild` 已纳入备份。

### PetaLinux recipe 外部依赖

`z20_plnx/project-spec/meta-user/recipes-apps/linuxcnc-prebuilt/linuxcnc-prebuilt.bb` 当前引用：

```text
EXTERNALSRC = "/home/sj/Desktop/downloads/linuxcnc-2.9.7"
```

该目录已纳入备份：

```text
/home/sj/Desktop/downloads/linuxcnc-2.9.7
```

`.petalinux/metadata` 当前引用：

```text
HARDWARE_PATH=/home/sj/Desktop/downloads/board_inputs_sync_20260408_084525/system.xsa
```

该目录已纳入备份：

```text
/home/sj/Desktop/downloads/board_inputs_sync_20260408_084525
```

同时也纳入了小型辅助目录：

```text
/home/sj/Desktop/work/linuxcnc_src
```

## 当前板端启动文件对应关系

板端：

```text
/media/sd-mmcblk0p1/BOOT.BIN
/media/sd-mmcblk0p1/image.ub
/media/sd-mmcblk0p1/system.dtb
```

核对结果：

- `BOOT.BIN` SHA256 `70f06ffb93489001af215575450a95c775078275ef7e9050e191d4e931f72a09`
  - VM 对应：`/home/sj/Desktop/deliverables/z20_plnx/images/linux/BOOT.BIN`
- `image.ub` SHA256 `3eedaebbba170fd6ec02c021a6137ae1943ce2b65b1622dc162a07be63063273`
  - VM 对应：`/home/sj/Desktop/deliverables/z20_plnx/artifacts/stable_kernel_extract_20260515/image.ub`
  - 同哈希也在：`/home/sj/Desktop/deliverables/z20_plnx/baselines/v1/snapshot/boot_files/image.ub`
- `system.dtb` SHA256 `ffe4ace807cc2e793c0c55eb3c294ea7b1b4412e864fc9d044eb925d4891e61a`
  - VM 对应：`/home/sj/Desktop/deliverables/z20_plnx/baselines/v1/snapshot/boot_files/system.dtb`

这些对应文件均包含在 `vm_rebuild_sources_20260611.tar.gz` 内。

## 恢复方式

示例：

```bash
mkdir -p /home/sj/Desktop/restore-vm-sources
tar -xzf vm_rebuild_sources_20260611.tar.gz -C /home/sj/Desktop/restore-vm-sources
```

解压后目录结构从 `home/sj/Desktop` 下的相对路径开始，例如：

```text
deliverables/z20_plnx
deliverables/re_plnx_rebuild
downloads/linuxcnc-2.9.7
downloads/board_inputs_sync_20260408_084525
work/linuxcnc_src
```

如需完全恢复原 VM 路径，可将这些目录放回 `/home/sj/Desktop/` 下。
