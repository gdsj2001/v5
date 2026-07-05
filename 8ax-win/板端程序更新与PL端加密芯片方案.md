# Win 桌面端触发板端程序更新与 PL 端加密芯片方案

## 1. 目的

本文定义后期通过 Win 桌面端更新板端程序时的安全边界和落地路径。

当前开发板没有加密芯片；后期正式板的加密芯片连接在 PL 端。因此更新体系必须同时支持：

- 开发板：无加密芯片，只允许开发验证模式。
- 正式板：通过 PL 侧加密芯片建立设备身份、授权、更新验签和回滚证明。

本文只描述方案和边界，不表示该能力已经实现或已经上板验证。

## 2. 当前已有基础

现有 WinRemote 的升级功能是更新 Windows 桌面端自身，不是更新板端程序：

- `8ax-win/src/8ax.WinRemote/Update/WinRemoteUpdater.cs`
- `8ax-win/tools/publish_winremote_update.ps1`

现有 WinRemote 与板端的正式关系是远程显示和远程触摸输入：

- WinRemote 通过板端 `remote_ui_relay` 访问 `/remote/info`、`/remote/frame/full`、`/remote/stream`、`/remote/input`。
- WinRemote 不是第二套控制系统，不应直接执行板端 shell 命令、直接覆盖板端文件、直接读写 LinuxCNC/HAL。
- 板端 relay 入口和运行状态来自：
  - `app/src/v5_lvgl_remote_display.c`
  - `tools/deploy/install_v5_runtime.sh`
  - `tools/deploy/verify_v5_board_runtime.sh`

现有设备授权相关代码已经有 Device DNA、授权文件、公私钥文件等概念：

- `services/auth_download/v5_device_dna_register.py`
- `services/auth_download/v5_device_authorization_download.py`

后期接入加密芯片时，应把这些“设备身份能力”抽象成可替换后端，而不是继续把正式板私钥放在普通文件中。

## 3. 总体结论

可以实现 Win 桌面端更新板端程序，但推荐结构是：

```text
Win 桌面端
  -> 板端 update agent
  -> PL crypto gate
  -> PL 侧加密芯片
  -> VPS/厂家更新服务
  -> signed board update package
  -> 板端 staging 安装
  -> 原子切换
  -> 健康检查
  -> 失败回滚
```

Win 桌面端只负责“发起更新、传输更新包、显示进度、展示结果”。真正的信任判断、安装、回滚必须在板端 update agent 内完成。

## 4. 开发板与正式板模式

### 4.1 开发板模式

开发板没有加密芯片，只能使用开发验证模式：

- `hardware_profile=dev_no_crypto`
- 或 `hardware_profile=dev_software_key`

开发板模式允许验证：

- Win 桌面端更新 UI。
- update agent 协议。
- manifest/hash/版本判断。
- staging 安装、服务重启、健康检查、回滚流程。

开发板模式不能声明：

- 正式设备身份安全已闭环。
- 私钥不可导出。
- 加密芯片挑战签名已通过。
- 量产更新安全已达成。

开发板更新包和正式板更新包必须分开。开发板包不得被正式板接受，正式板包也不得被开发板软密钥冒充通过。

### 4.2 正式板模式

正式板加密芯片连接在 PL 端，PS/Linux 侧不得直接假设自己能访问芯片私钥。正式板模式应使用：

- `hardware_profile=prod_pl_crypto`
- PL 侧 crypto gate
- 板端 update agent
- 厂家签名 update manifest
- 板端健康检查与回滚证明

正式板私钥必须满足：

- 私钥在加密芯片内生成或安全注入。
- 私钥永不导出。
- 私钥不得落盘为 `/etc/6x-cnc/device_private_key.pem` 这类普通文件。
- Linux 侧只能请求芯片完成固定操作，例如签名 challenge、读取证书、读取单调计数器、解封装更新密钥。

## 5. PL 端加密芯片访问边界

因为加密芯片在 PL 端，PS/Linux 侧应通过一个固定、窄口径、可审计的接口访问。

允许的接口形态：

- AXI-Lite mailbox 寄存器。
- `/dev/8ax_crypto` 字符设备。
- UIO + 固定 C helper。
- PL 内部 SPI/I2C master + 命令状态机。

允许的命令范围：

- `get_device_cert`
- `get_device_public_key`
- `sign_challenge`
- `get_random`
- `unwrap_update_key`
- `read_monotonic_counter`
- `read_secure_element_status`

禁止：

- WinRemote 直接访问 PL 寄存器。
- UI C 代码直接读写加密芯片。
- Broker/product action Python 直接 mmap 或读写 PL crypto 寄存器。
- 暴露任意 SPI/I2C 转发接口。
- 暴露任意签名原文能力给非 update/auth 域。
- 用普通文件私钥冒充正式板加密芯片。

建议新增的正式边界是：

```text
board update/auth code
  -> registered native crypto helper/driver
  -> PL crypto gate
  -> secure element
```

## 6. 更新包与 manifest

板端更新包必须是签名包，不应只依赖 HTTPS 或 sha256。

manifest 至少包含：

```json
{
  "schema": "8ax-board-update-manifest-v1",
  "app_id": "8ax.board.v3.product_ui",
  "version": "YYYY.MMDD.HHMM",
  "hardware_profiles": ["prod_pl_crypto"],
  "min_current_version": "YYYY.MMDD.0000",
  "package_url": "board-v3-product-ui-YYYY.MMDD.HHMM.tar.zst",
  "package_sha256": "hex",
  "package_size": 0,
  "target_paths": [
    "/opt/8ax/v3_product_ui/releases/YYYY.MMDD.HHMM"
  ],
  "entry_binary": "re_v3_lvgl_product_ui",
  "requires_safe_machine_state": true,
  "rollback_supported": true,
  "anti_rollback_counter": 0,
  "published_at_utc": "YYYY-MM-DDTHH:MM:SSZ",
  "signature_alg": "Ed25519-or-ECDSA-P256-SHA256",
  "signature": "base64url"
}
```

签名规则：

- 厂家离线发布私钥签名 manifest。
- 板端内置厂家公钥或厂家证书链。
- 板端 update agent 先验 manifest 签名，再验包 sha256。
- 可选加密更新包；但签名是必须项，加密不是签名的替代。

包内容建议：

- `re_v3_lvgl_product_ui`
- `scripts/`
- `re-v3-lvgl-ui.init`
- 必要配置和 runtime capability manifest
- 包内文件 hash 清单
- 回滚信息
- 版本和构建来源信息

构建来源必须来自当前项目 canonical source，经 VM 全量构建产物打包；不得从板端、旧 staging、`repo_ignored/` 或临时目录拼包作为正式发布源。

## 7. Win 桌面端职责

Win 桌面端可以增加“板端升级”入口，但职责应保持受限：

- 读取板端 update agent 状态。
- 显示当前板端版本、目标版本、安全状态。
- 触发 challenge。
- 上传或转发 signed update package。
- 展示下载、验签、安装、重启、健康检查、回滚结果。
- 保存更新证据日志。

Win 桌面端不得：

- 直接 `scp` 覆盖 `/opt/8ax/v3_product_ui`。
- 直接 SSH 执行任意安装命令。
- 直接修改 init 脚本或运行目录 marker。
- 直接代替板端验签。
- 直接代替板端判断机器是否安全可更新。
- 把 `/remote/input` 或触摸 relay 当成更新控制通道。

WinRemote 现有自升级 app_id 与板端更新 app_id 必须分开：

- `8ax.WinRemote`：Windows 客户端自身更新。
- `8ax.board.v3.product_ui`：板端产品 UI/runtime 更新。

## 8. 板端 update agent 接口草案

建议新增独立 board update agent，不复用 operator remote relay。

接口可以是 HTTPS、本机服务经 relay 受控转发、或受限 SSH 子系统。无论选哪种，都必须有身份认证、权限边界、状态机和日志。

建议接口：

| 接口 | 作用 | 说明 |
| --- | --- | --- |
| `GET /board-update/info` | 查询当前版本和能力 | 返回 board version、hardware_profile、crypto 状态、update agent 版本 |
| `POST /board-update/challenge` | 设备身份挑战 | 板端用 PL 加密芯片签名 nonce |
| `POST /board-update/prepare` | 准备更新 | 校验 manifest、设备绑定、版本、机器安全状态 |
| `POST /board-update/upload` | 上传包 | 只接受 prepare 通过后的包 |
| `POST /board-update/apply` | 执行安装 | staging 解包、原子切换、重启服务 |
| `GET /board-update/status` | 查询进度 | Win 端轮询展示 |
| `POST /board-update/rollback` | 受控回滚 | 只回滚到已签名、已保存的上一版本 |

update agent 内部安装状态机：

```text
idle
  -> challenged
  -> prepared
  -> package_received
  -> verified
  -> machine_safe_checked
  -> staged
  -> switched
  -> restarted
  -> health_checked
  -> complete
```

失败路径：

```text
failed
  -> rollback_started
  -> rollback_restarted
  -> rollback_health_checked
  -> rollback_complete
```

## 9. 安全状态检查

板端执行更新前必须 fail-closed 检查：

- LinuxCNC/HAL/EtherCAT 不在加工中。
- 没有 G-code 正在运行。
- 轴不在运动中。
- 没有急停/伺服状态矛盾。
- Command Broker 和 SHM 状态可用，或 update agent 能明确判断安全状态。
- 当前 update request 未过期。
- 电源、磁盘空间、目标路径权限满足安装要求。

如果安全状态不可判断，必须拒绝更新，不得继续安装。

## 10. 安装与回滚策略

推荐目录结构：

```text
/opt/8ax/v3_product_ui/
  current -> releases/YYYY.MMDD.HHMM
  previous -> releases/YYYY.MMDD.PREV
  releases/
    YYYY.MMDD.HHMM/
      re_v3_lvgl_product_ui
      scripts/
      manifest.json
      file_hashes.json
```

安装步骤：

1. 下载或接收包到临时目录。
2. 验厂家 manifest 签名。
3. 验设备身份、设备绑定、硬件 profile、anti-rollback counter。
4. 验包 sha256 和包内文件 hash。
5. 解包到 `releases/<version>.staging`。
6. 校验 staging 文件权限和入口文件。
7. 更新 `current` 指向新版本。
8. 重启板端产品 UI 服务。
9. 健康检查通过后记录 `complete`。
10. 健康检查失败则恢复 `current` 到上一版本并重启。

不得直接覆盖当前运行目录里的单个文件作为正式更新方式。

## 11. 更新后验证

更新完成后，board update agent 至少要验证：

- `re_v3_lvgl_product_ui` 进程存在。
- `remote_ui_relay_status.json` 状态 ready/listening。
- `remote_input_status.json` 状态 ready。
- `/remote/info` 可访问。
- Command Broker 状态正常。
- `/dev/shm/v3_status_shm` 状态帧可读。
- 版本文件或 `/board-update/info` 显示新版本。

如果更新影响运动、回零、启动、急停、程序运行等运动能力，仍需按项目规则跑原始 UI/operator path 和 `nc/cc.ngc` golden motion loop，不能只靠更新服务健康检查声明运动功能已通过。

## 12. 开发阶段实施顺序

建议按以下顺序实施，避免一开始就把正式硬件依赖写死：

1. 定义 board update manifest 和 signed bundle 格式。
2. 实现开发板 `dev_no_crypto` update agent，先跑通状态机、staging、回滚、日志。
3. WinRemote 增加独立“板端升级”入口，只对接 update agent，不直接改板端文件。
4. 加入厂家 manifest 签名验签。
5. 加入 hardware_profile 区分，开发板包和正式板包隔离。
6. 正式板硬件出来后，实现 PL crypto gate。
7. 将设备身份后端从 soft/dev 切换到 `prod_pl_crypto`。
8. 加入 anti-rollback counter 和芯片 challenge 签名。
9. 做正式板上板闭环：更新、重启、relay、remote input、SHM、Broker、必要运动 golden loop。

## 13. 必须保留的架构边界

- WinRemote 是远程显示/输入终端，不是板端控制系统。
- 板端更新要走独立 update agent，不走触摸输入链路。
- UI/Broker/product action 不直接访问加密芯片或 PL 寄存器。
- 正式板私钥不落盘、不导出、不由 Python 生成。
- update agent 不接受任意 shell 命令。
- 更新包不能绕过厂家签名。
- 开发板模式必须在状态和日志中显式标记为非正式安全链路。
- 板端运行内容以完整签名包和原子版本目录为单位，不做零散文件覆盖。
- 所有更新闭环结论必须区分 `local_verified_only`、`source_only`、`board_verified`。

## 14. Secure Boot 补充

加密芯片只能证明设备身份和更新授权。如果攻击者已经能替换 update agent 或启动链，仅靠应用层加密芯片检查不够。

正式量产若需要较强安全，应配合：

- secure boot
- FSBL/U-Boot/kernel/rootfs 完整性保护
- update agent 自身完整性校验
- 只读根文件系统或受控 A/B 分区
- rollback counter 或 secure monotonic counter

否则加密芯片仍有价值，但安全边界主要是“防非授权更新”和“设备授权识别”，不是完整的防 root 篡改体系。
