# VM Loss Rebuild README 2026-06-11

## Verdict

`D:\re\v3\vm-bak` is now sufficient for the specific risk: the current `z20-vm` virtual machine can be lost and the board system/LinuxCNC/v3 development state can still be restored.

This backup set includes:

- VM project sources and current LinuxCNC/v3 truth source.
- Current board runtime snapshot from `/opt/8ax` and SD boot files.
- Installed PetaLinux 2020.2 toolchain from `/opt/pkg/petalinux/2020.2`.
- PetaLinux build downloads, Yocto `git2` mirrors, `sstate-cache`, and `tmp/deploy`.
- VM environment files and host package manifest.

Remaining boundary: this is a VM-loss recovery set, not a guarantee that every external universe is gone. If a future rebuild also requires regenerating the FPGA bitstream from the original Vivado design, keep the Vivado project and Vivado installer/license backed up separately. The current PetaLinux board rebuild path is protected by the archived `system.xsa`, `system.bit`, boot artifacts, PetaLinux project, and PetaLinux toolchain/cache in this folder.

## Files

| File | Purpose | SHA256 |
| --- | --- | --- |
| `vm_rebuild_sources_20260611.tar.gz` | Main VM source tree: `deliverables/`, `downloads/`, `work/` | `21139bc12b5f8b55e8319eb91cdc45b81a41480298d9771e90b1d19154ea633d` |
| `board_runtime_snapshot_20260611.tar.gz` | Current board runtime `/opt/8ax` and SD boot files | `6708444015e3332c35e2b4037184659c31cd670d6f78d905b021a2abf44e0728` |
| `petalinux_2020.2_opt_pkg_20260611.tar.gz` | Installed PetaLinux 2020.2 toolchain | `89786780297337b5f79604005688f10e950a2f0d758bef589a6a0bfef0093750` |
| `z20_plnx_yocto_cache_20260611.tar.gz` | PetaLinux build downloads, git mirrors, sstate-cache, tmp/deploy | `3f367164ab202fc8c7d14cea76d87014d7214dd0e0a53eca7b08c1a13ab69ef7` |
| `z20_vm_env_files_20260611.tar.gz` | VM environment files: `use_petalinux_2020.2.sh`, `xsct`, shell profiles, os-release | `6e12fff522eba7c0ce75921bed61f4fc9d366d4a88f861b9fc2d0edf571f6b49` |
| `vm_environment_probe_20260611.txt` | Captured VM OS, tool paths, PetaLinux source result, recipe source evidence | `25f4f6a9399793ba83c014566dbf25b6c72cca2908c19ac7d381877a67798554` |
| `vm_dpkg_manifest_20260611.txt` | VM package list from `dpkg-query -W` | `fa68ae4de489940f0c6dbb38ba3a84ce79f08d73fc48d502330af01661de4d0d` |
| `VM_BAK_SHA256SUMS_20260611.txt` | Top-level checksum manifest for the folder | regenerate after adding files |
| `VM_BAK_FILE_INVENTORY_20260611.txt` | File size/time inventory for the folder | regenerate after adding files |

## Restore A Lost VM

Target a fresh Ubuntu 18.04 VM when possible. The captured VM was Ubuntu 18.04.6 with kernel `5.4.0-150-lowlatency`.

1. Copy all files from `D:\re\v3\vm-bak` into the new VM.

2. Verify archives:

```bash
cd /path/to/vm-bak
sha256sum -c VM_BAK_SHA256SUMS_20260611.txt
```

3. Create the expected user/work path:

```bash
sudo useradd -m -s /bin/bash sj 2>/dev/null || true
sudo mkdir -p /home/sj/Desktop /opt/pkg
sudo chown -R sj:sj /home/sj
```

4. Restore sources:

```bash
sudo -u sj tar -C /home/sj/Desktop -xzf vm_rebuild_sources_20260611.tar.gz
```

After this, the key paths should exist:

```text
/home/sj/Desktop/deliverables/z20_plnx
/home/sj/Desktop/deliverables/re_plnx_rebuild
/home/sj/Desktop/downloads/linuxcnc-2.9.7
/home/sj/Desktop/work/linuxcnc_src
```

5. Restore PetaLinux 2020.2:

```bash
sudo tar -C /opt/pkg -xzf petalinux_2020.2_opt_pkg_20260611.tar.gz
sudo chown -R sj:sj /opt/pkg/petalinux
```

6. Restore environment files:

```bash
sudo tar -C / -xzf z20_vm_env_files_20260611.tar.gz
sudo chmod +x /usr/local/bin/xsct 2>/dev/null || true
```

7. Restore Yocto/PetaLinux build cache and deploy outputs:

```bash
sudo -u sj tar -C /home/sj/Desktop/deliverables/z20_plnx -xzf z20_plnx_yocto_cache_20260611.tar.gz
```

This restores:

```text
build/downloads
build/downloads/git2
build/sstate-cache
build/tmp/deploy
downloads
components/yocto/downloads
```

The `git2` cache includes the fixed external sources used by the recipes, including:

```text
github.com.linuxcnc-ethercat.linuxcnc-ethercat.git
gitlab.com.etherlab.org.ethercat.git
github.com.Xilinx.linux-xlnx.git
github.com.Xilinx.u-boot-xlnx.git
github.com.xilinx.device-tree-xlnx.git
```

8. Install host packages as needed. The captured VM package list is in `vm_dpkg_manifest_20260611.txt`. At minimum, the current VM probe reported PetaLinux missing `gcc-multilib`; install it before expecting `settings.sh` to return cleanly.

```bash
sudo apt-get update
sudo apt-get install -y gcc-multilib
```

If rebuilding on a completely new Ubuntu VM, also install the standard PetaLinux 2020.2 host dependencies from Xilinx documentation or match the captured package manifest.

9. Source PetaLinux and rebuild:

```bash
source /opt/pkg/petalinux/2020.2/settings.sh
cd /home/sj/Desktop/deliverables/z20_plnx
petalinux-build
```

10. Restore current board runtime if the board SD/runtime also needs to be recreated:

```bash
mkdir -p /tmp/board_runtime_snapshot_20260611
tar -C /tmp -xzf board_runtime_snapshot_20260611.tar.gz
```

The extracted snapshot contains:

```text
board_runtime_snapshot_20260611/media/sd-mmcblk0p1/BOOT.BIN
board_runtime_snapshot_20260611/media/sd-mmcblk0p1/image.ub
board_runtime_snapshot_20260611/media/sd-mmcblk0p1/system.dtb
board_runtime_snapshot_20260611/opt/8ax/phase0_bus5
board_runtime_snapshot_20260611/opt/8ax/v3_product_ui
```

Copy those files to the target SD/card or board with the normal board recovery flow. For this environment, use `scp -O` when copying to the board.

## Validation Already Done

- `petalinux_2020.2_opt_pkg_20260611.tar.gz` lists `petalinux/2020.2/settings.sh`, `petalinux-build`, and `xsct`.
- `petalinux_2020.2_opt_pkg_20260611.tar.gz` has 52247 tar entries.
- `z20_plnx_yocto_cache_20260611.tar.gz` has 22147 tar entries.
- `z20_plnx_yocto_cache_20260611.tar.gz` contains the `linuxcnc-ethercat` and `ethercat-master` git mirrors and `build/tmp/deploy/images`.
- `z20_vm_env_files_20260611.tar.gz` lists all expected environment files.
- `vm_dpkg_manifest_20260611.txt` has 2055 package entries.

## Practical Meaning

If the current VM disappears, recreate an Ubuntu VM, restore the archives above, install missing host packages, and continue from `/home/sj/Desktop/deliverables/z20_plnx` and `/home/sj/Desktop/deliverables/re_plnx_rebuild`. The project no longer depends on the old VM disk as the only copy of the PetaLinux/LinuxCNC/v3 build state.
