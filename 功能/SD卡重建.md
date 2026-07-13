# V5 产品 SD 卡重建操作

<!-- AI_FAST_READ_BEGIN -->
owner_reqs: [REQ-SD-REBUILD-RUNBOOK]
read_when: [SD卡重建, 空白SD, BOOT.BIN, image.ub, boot.scr, rootfs, 制卡, QSPI更新SD, 写后回读, 冷启动]
truth: [Windows输入门禁 -> VM增量生成一次current镜像 -> 直接写整卡或QSPI更新现有分区 -> 全量回读 -> SD冷启动验收]
forbidden: [猜测SD设备名, 写VM系统盘, 先stage-only再直接apply造成重复组装, clean kernel或清sstate/tmp/downloads, VM联网补源码, 只替换image.ub或DTB, QSPI冒充产品系统, 未冷启动声明board_verified]
readback: [source identity, Vivado与PetaLinux硬件输入hash, V5_LINUXCNC_BUILD_OK, V5_SD_CARD_READY或V5_QSPI_SD_UPDATE_OK, boot文件hash, rootfs全文件manifest, root=/dev/mmcblk0p2, 产品运行与真实触摸]
impact: [Windows canonical源码, VM_BUILD_ROOT, PetaLinux current产物, SD boot与rootfs分区, QSPI恢复通道, 板端冷启动]
acceptance: [所有命令零退出, 写后全量回读通过, SD冷启动来自mmcblk0p2, 产品服务通过, 物理屏与真实触摸通过]
detail_sections: [#sd-operation-choice, #sd-windows-check, #sd-build-current, #sd-direct-write, #sd-qspi-update, #sd-cold-boot, #sd-offline-cert]
<!-- AI_FAST_READ_END -->

引用需求真源：`REQ-SD-REBUILD-RUNBOOK`、`REQ-V5-OFFLINE-FULL-SD-REPRODUCTION`。

<a id="sd-operation-choice"></a>
## 1. 先选择操作方式

| 现场状态 | 使用方式 |
| --- | --- |
| 空白 SD、分区损坏、需要重新分区，或 SD 可以接到 VM | 按第 4 节直接写整卡 |
| SD 留在板内，且 `/dev/mmcblk0p1`、`/dev/mmcblk0p2` 已存在 | 按第 5 节从 QSPI 更新 boot 和 rootfs |
| 正式证明断网灾难恢复能力 | 按第 7 节在全新环境执行 |

普通重建使用现有 `/root/v5-build` 缓存，不删除 kernel、BitBake `tmp`、sstate、downloads 或唯一 source projection。QSPI updater 不创建分区；目标 SD 缺少两个分区时必须改用直接写整卡。

<a id="sd-windows-check"></a>
## 2. Windows 前置检查

以 PowerShell 在 `D:\v5` 执行：

```powershell
Set-Location D:\v5
python .\board\tools\petalinux\verify_v5_linux_source.py --project-root . --print-source-hashes
python .\board\tools\linuxcnc\verify_v5_linuxcnc_source.py --project-root . --source-root .\linuxcnc --allow-flattened-symlinks --print-source-hash
python .\board\tools\petalinux\verify_v5_petalinux_source.py --project-root . --source-root .\board\petalinux --print-source-hash
python .\board\tools\petalinux\verify_v5_source_packages.py --project-root .
python .\board\tools\deploy\verify_v5_product_source_closure.py --board-root .\board --validate-shell
git diff --check
```

再确认当前 Vivado 输出与 PetaLinux 硬件输入一致：

```powershell
$vivadoXsa = (Get-FileHash .\vivado_hw_project\board_inputs\system.xsa -Algorithm SHA256).Hash
$petaXsa = (Get-FileHash .\board\petalinux\project-spec\hw-description\system.xsa -Algorithm SHA256).Hash
$vivadoBit = (Get-FileHash .\vivado_hw_project\vivado_hw_project.runs\impl_1\system_wrapper.bit -Algorithm SHA256).Hash
$petaBit = (Get-FileHash .\board\petalinux\project-spec\hw-description\system.bit -Algorithm SHA256).Hash
if ($vivadoXsa -ne $petaXsa -or $vivadoBit -ne $petaBit) { throw 'Vivado/PetaLinux hardware handoff mismatch' }
```

任一命令失败立即停止。FPGA 源码有变化时，必须先在 `vivado_hw_project` 完成实现、XSA/bit 交接和 PetaLinux identity 更新；本操作不使用 `new-vivado`、`D:\v3` 或旧 SD 的硬件文件。

<a id="sd-build-current"></a>
## 3. 在 VM 生成 current 镜像

以下命令均在 VM 的 root shell 执行：

```sh
unset http_proxy https_proxy ftp_proxy all_proxy no_proxy
unset HTTP_PROXY HTTPS_PROXY FTP_PROXY ALL_PROXY NO_PROXY
. /opt/pkg/petalinux/2020.2/settings.sh
findmnt -n -o FSTYPE,OPTIONS -T /mnt/v5-source
id sj
```

输出必须证明 `/mnt/v5-source` 是 Windows owner 的只读共享挂载，且 `sj` 是存在的非 root 构建用户。

只有 LinuxCNC 源码、recipe 或最小运行闭包变化时，先运行 focused gate：

```sh
V5_PETALINUX_BUILD_USER=sj VM_BUILD_ROOT=/root/v5-build \
  sh /mnt/v5-source/board/tools/linuxcnc/build_v5_linuxcnc_petalinux.sh --focused
```

随后只运行一次最终 current image。该命令保留 BitBake 缓存，只重做受影响任务；不要附加 `--clean-kernel`：

```sh
V5_PETALINUX_BUILD_USER=sj VM_BUILD_ROOT=/root/v5-build \
  sh /mnt/v5-source/board/tools/linuxcnc/build_v5_linuxcnc_petalinux.sh --full
```

成功标志必须是 `V5_LINUXCNC_BUILD_OK mode=full`，并且此前出现 `V5_PETALINUX_NETWORK_DISABLED`。出现 `V5_WINDOWS_SOURCE_IMPORT_REQUIRED` 时停止，不得让 VM 下载；将报告复制到 Windows `repo_ignored/source_input_closure/v5-missing-source-inputs.json`，再在 Windows 执行：

```powershell
python .\board\tools\petalinux\import_v5_source_packages.py --project-root . --missing-report .\repo_ignored\source_input_closure\v5-missing-source-inputs.json
python .\board\tools\petalinux\verify_v5_source_packages.py --project-root .
```

导入通过后只重跑报告登记的失败层和下游步骤。

<a id="sd-direct-write"></a>
## 4. SD 接入 VM：直接重建整卡

先插拔一次 SD，并在 VM 核对整盘设备：

```sh
lsblk -dpno NAME,SIZE,MODEL,TRAN,RM,TYPE
```

只选择 `TYPE=disk`、`RM=1`、容量不小于 8 GiB，且明确由本次插入出现的整盘。不要选择分区，不要凭 `/dev/sdX` 字母猜测。确认后在同一 root shell 设置实际设备并写卡：

```sh
SD_DEVICE=/dev/sdX
VM_BUILD_ROOT=/root/v5-build \
  sh /mnt/v5-source/board/tools/petalinux/write_v5_sd_card.sh \
  --device "$SD_DEVICE" --apply
```

这里不要先运行 `--stage-only`。`--apply` 自己会完成 ARM runtime 构建、rootfs staging、`BOOT.BIN/image.ub/boot.scr/system.dtb` 组装、MBR/FAT32/ext4 重建、写入、boot SHA-256 回读、rootfs 全文件 manifest 回读、ARM ELF 检查、文件系统检查和安全卸载。

只有看到 `V5_SD_CARD_READY device=...` 且命令零退出后才能拔卡。脚本拒绝非 removable 整盘、VM 系统盘和小于 8 GiB 的设备。

<a id="sd-qspi-update"></a>
## 5. SD 留在板内：通过 QSPI 更新

先在 VM 生成一次 staging payload：

```sh
VM_BUILD_ROOT=/root/v5-build \
  sh /mnt/v5-source/board/tools/petalinux/write_v5_sd_card.sh --stage-only
tar -czf /root/v5-build/v5-sd-payload.tar.gz \
  -C /root/v5-build/sd-card boot rootfs
PAYLOAD_SHA256=$(sha256sum /root/v5-build/v5-sd-payload.tar.gz | awk '{print $1}')
printf '%s\n' "$PAYLOAD_SHA256"
```

在 Windows 切到 QSPI 并等待 SSH：

```powershell
python .\board\tools\v5_board_power_cycle.py --boot-mode qspi --target re-board --net-check-host 192.168.1.221 --net-check-ssh-target re-board --json-out v5-qspi-boot.json
```

回到刚才保存 `PAYLOAD_SHA256` 的 VM shell，传入临时恢复目录并执行 updater：

```sh
ssh re-board "grep -qw 'v5.recovery=qspi' /proc/cmdline && mkdir -p /run/v5_test_tools"
scp /root/v5-build/v5-sd-payload.tar.gz \
  /mnt/v5-source/board/tools/petalinux/update_v5_sd_from_qspi_recovery.sh \
  /mnt/v5-source/board/tools/deploy/v5_product_file_manifest.py \
  re-board:/run/v5_test_tools/
ssh re-board "sh /run/v5_test_tools/update_v5_sd_from_qspi_recovery.sh \
  --payload /run/v5_test_tools/v5-sd-payload.tar.gz \
  --payload-sha256 '$PAYLOAD_SHA256' \
  --manifest-tool /run/v5_test_tools/v5_product_file_manifest.py \
  --device /dev/mmcblk0 --apply"
```

只有看到 `V5_QSPI_SD_UPDATE_OK` 才能继续。该脚本确认当前根位于 eMMC `/dev/mmcblk1p*`、目标是非当前根 SD `/dev/mmcblk0`，并同时更新 boot、rootfs 和全量 manifest；禁止只复制 `image.ub` 或 DTB。

更新成功后在 Windows 切回 SD：

```powershell
python .\board\tools\v5_board_power_cycle.py --boot-mode sd --target re-board --net-check-host 192.168.1.221 --net-check-ssh-target re-board --json-out v5-sd-boot.json
```

<a id="sd-cold-boot"></a>
## 6. SD 冷启动验收

直接写卡后把 SD 插回开发板，并在 Windows 执行同一个 SD power-cycle 命令：

```powershell
python .\board\tools\v5_board_power_cycle.py --boot-mode sd --target re-board --net-check-host 192.168.1.221 --net-check-ssh-target re-board --json-out v5-sd-boot.json
```

确认实际根文件系统来自 SD：

```powershell
ssh re-board 'grep -qw "root=/dev/mmcblk0p2" /proc/cmdline && test "$(findmnt -n -o SOURCE /)" = "/dev/mmcblk0p2"'
```

在 VM 运行板端 runtime 检查：

```sh
V5_BOARD_SSH=re-board \
  sh /mnt/v5-source/board/tools/deploy/verify_v5_board_runtime.sh
```

最后在原始产品路径确认：物理 LCD 正常、真实手指可操作、LinuxCNC/PREEMPT_RT/EtherCAT/HAL/Command Gate/State Publisher/UI 服务在线。涉及运动时按对应功能 owner 执行 golden motion，结束后保持急停和 machine disabled。

写卡与 runtime 检查通过但未完成物理屏、真实触摸和 operator path 时，最高只能是 `local_verified_only`；全部完成后才是 `board_verified`。

<a id="sd-offline-cert"></a>
## 7. 断网灾难恢复认证

只在用户明确要求发布或离线恢复认证时执行：

1. 使用一台全新、物理断网的受支持电脑和 disposable VM；VM 中 `/root/v5-build` 必须在开始前不存在，downloads 与 sstate 为空。
2. 只读挂载完整 Windows `v5` 为 `/mnt/v5-source`，运行第 2 节全部 Windows 门禁。
3. 按第 3 节执行 `--full`，不得复用普通 current 产物或远程 sstate。
4. 将一张空白 SD 接入该 VM，按第 4 节直接重建整卡并完成全量回读。
5. 按第 6 节从 SD 冷启动并完成产品验收。

不要为了做离线认证而清理日常 VM 的有效缓存；使用独立 disposable 环境。缺少离线工具链、许可证、固定源码包或任何命令需要联网时，认证状态为 `blocked`。
