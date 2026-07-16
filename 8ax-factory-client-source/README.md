# 8AX Factory Client

## 设备 DNA 表

`设备DNA` 页显示 VPS `/api/v1/admin/devices` 返回的已登记设备。表格里的 `VPS分发ID` 是 VPS 给设备分发并保存的 6 位十进制显示 ID。

规则：

- API 字段名为 `vpsDistributionId`，值必须是 6 位数字字符串，例如 `359764`。
- 该字段由 VPS 在首次登记 PL DNA 时按固定算法生成，和已有 ID 对比确认不重复后保存；保存后不得因重新登记、刷新列表或客户端升级改变。
- Factory Client 只显示 VPS 返回值并用于人工核对，不得在本地生成最终 ID，也不得从 PL DNA、DNA 摘要或本地序号推导。
- 必须保留前导零；客户端不得把它转成整数再显示。
- VPS 未返回或返回非 6 位数字时，表格显示 `-`，不能用 `deviceId` 冒充。
- `设备ID` 仍是 VPS/数据库内部设备标识，删除、授权上传等接口继续使用 `deviceId`。
- 设备列表必须显示 VPS 返回的 `authorizationStatus`。未人工确认并上传授权文件时，该行状态必须用红色显示，状态文本可显示为“待人工授权”；只有 VPS 已存在该 ID 对应的 `device_authorization.json` 时才显示普通颜色/已授权。
- 工厂授权私钥必须是本机私有文件，不得提交进 Git、不得打进发布 exe。Factory Client 顶部 `授权私钥` 路径默认指向 `D:\授权私钥\factory-device-auth-private.pem`，也可通过 `选择私钥` 指向本机其它安全位置。
- 当前这台 Windows 工厂机的授权私钥保管位置是 `D:\授权私钥\factory-device-auth-private.pem`；对应公钥文件在同目录 `device_auth_public.pem` / `factory-device-auth-public.pem`，路径依据见 `功能/VPS登录与项目对接说明.md`。
- 点击 `人工确认授权` 成功上传授权文件后，VPS 才把该设备视为已授权；Factory Client 刷新列表后必须恢复普通行色。
- 点击 `删除登记` 删除设备 DNA 登记时，VPS 必须同时删除该 6 位 ID 对应 private 目录里的 `device_authorization.json`，防止以后同 ID/旧授权被误下载；删除响应要说明授权文件是否已删除。

## 远程访问板端 SSH

本节记录 `REQ-REMOTE-SSH-MAINTENANCE` 的源码实现、部署关系和排障入口，方便维护 `8ax-factory-client-source`。产品行为和安全边界仍由 [`../功能/VPS登录与项目对接说明.md`](../功能/VPS登录与项目对接说明.md) 裁决；本节不是第二份需求真源。

### 为什么不能连接“IP访问记录”里的 IP

`IP访问记录` 显示 VPS 观察到的板端出站地址。该地址可能是运营商 NAT、共享公网出口或防火墙后的地址，不能证明公网 `22` 端口能进入开发板。远程 SSH 因此使用板端主动建立的反向隧道：

```text
板端授权身份
  -> 经 Cloudflare HTTPS 入口登记设备和申请端口
  -> 板端主动 SSH 到 VPS 原始 SSH 地址
  -> VPS 只在 127.0.0.1:<设备端口> 建立反向监听

Factory Client 当前设备的 IP访问记录弹窗
  -> 按 6 位 VPS分发ID 查询隧道状态
  -> Windows ssh 经 vps3 跳到 VPS
  -> root@127.0.0.1:<设备端口>
  -> 反向隧道
  -> 板端本机 127.0.0.1:22
```

访问记录中的 IP 只用于出站审计。切换“明细”或“IP分布”的记录不会改变 SSH 目标；设备路由唯一键是已经授权的 6 位 `VPS分发ID`。

### 实现组件

| 位置 | Owner/文件 | 作用 |
| --- | --- | --- |
| Factory Client | `8ax.FactoryClient/DeviceIpAccessDialog.cs` | 在当前设备的 `IP访问记录` 弹窗显示“远程连接：设备 NNNNNN（VPS）”，查询状态并启动 Windows 系统 SSH |
| Factory Client API | `8ax.FactoryClient/ApiClient.cs`、`Models.cs` | 调用管理员状态 API，并解析设备 ID、在线状态、分配端口和 VPS 身份 |
| VPS HTTPS 网关 | `vps-auth-gateway/gateway.py` | 提供板端登记和 Factory Client 管理员状态查询入口；复用现有 DNA、授权和设备请求签名校验 |
| VPS 隧道 owner | `vps-auth-gateway/remote_ssh_gateway.py` | 创建独立表、固定设备端口、生成受限 authorized key、检查 loopback SSH banner |
| VPS SSH 限权 | `vps-auth-gateway/sshd_8ax_remote_ssh.conf`、`remote_ssh_authorized_keys.sh`、`30-remote-ssh.conf` | 只对 `8ax-tunnel` 用户允许 remote forwarding；不改变 root 和现有业务用户的 SSH 行为 |
| 板端 agent | `../board/services/remote_ssh/v5_remote_ssh_tunnel.py` | 校验授权、登记、启动并守护一条反向 SSH 隧道 |
| 板端启动脚本 | `../board/services/remote_ssh/init.d/v5-remote-ssh` | 使用隔离的 OpenSSH client、记录 PID/日志，并把进程放到非 CPU0 |
| 板端部署输入 | `../board/config/deploy/v5_runtime_deploy_manifest.tsv` | 部署 agent、init 脚本和 VPS pinned known_hosts |
| 地址配置 | `../board/petalinux/project-spec/meta-user/recipes-apps/v5-base-overlay/files/rootfs-overlay/etc/6x-cnc/vps_endpoints.json` | 分开声明 HTTPS API、VPS SSH 身份和实际连接地址 |

### 1. 板端登记与授权

板端服务启动后读取：

- live DNA；
- `/etc/6x-cnc/device_authorization.json`；
- `/etc/6x-cnc/device_private_key.pem`；
- `/etc/6x-cnc/vps_endpoints.json`；
- `/etc/6x-cnc/vps_remote_ssh_known_hosts`。

授权的 `permissions` 必须包含 `remote_ssh_tunnel`。agent 使用既有 `DeviceRequestSigner`，以 purpose `remote_ssh_tunnel` 对本次 HTTPS 请求签名，然后调用：

```http
POST /api/v1/device/remote-ssh/register
Content-Type: application/json

{"deviceId":"<6位VPS分发ID>"}
```

请求还携带现有设备 DNA、授权 envelope 和防重放签名头。VPS 依次核对 factory registration、授权签名、权限、请求签名、设备 ID 和登记公钥指纹；任何一项不一致均拒绝登记。日志和文档不得记录这些签名头、授权内容或私钥。

成功响应的核心字段为：

```json
{
  "success": true,
  "deviceId": "<6位VPS分发ID>",
  "assignedPort": 25000,
  "vpsHost": "it.cjwsjzyy.xyz",
  "vpsPort": 22,
  "tunnelUser": "8ax-tunnel"
}
```

`assignedPort` 是示意值；真实端口由 VPS 在 `25000..44999` 内为设备唯一分配。设备再次登记会复用原端口，不按访问记录 IP 重新分配。

### 2. VPS 端口与 SSH 限权

`remote_ssh_gateway.py` 使用独立表 `remote_ssh_tunnels` 保存：

- `device_id`；
- `assigned_port`；
- 转换后的设备 SSH 公钥；
- 登记公钥 SHA256；
- 最近登记来源 IP 和时间。

该表不修改既有设备、授权、OTA 或驱动 profile 表。状态查询仍与 `devices` 联表，只有授权未撤销且公钥指纹仍一致的设备才有效。

每个设备生成一行受限 authorized key，约束形态如下：

```text
restrict,port-forwarding,permitlisten="127.0.0.1:<assignedPort>",command="/bin/false" <设备SSH公钥> 8ax-device-<deviceId>
```

VPS 的 `Match User 8ax-tunnel` 进一步执行这些限制：

- 禁止密码和 keyboard-interactive；
- 禁止 TTY、shell、X11 和 agent forwarding；
- 只允许 remote forwarding；
- `GatewayPorts no`；
- 通过专用 `AuthorizedKeysCommand` 读取生成的 key inventory。

因此每个分配端口只能监听 VPS 的 `127.0.0.1`，公网不能直接访问 `25000..44999`。实现不占用或改写现有 `80/443`，也不需要让 Cloudflare 代理 SSH。

### 3. 板端反向隧道

板端把 HTTPS 注册地址与 SSH 连接地址分开：

- `api_base_urls` 使用 `https://license.cjwsjzyy.xyz` 等 HTTPS 地址，可继续经过 Cloudflare；
- `remote_ssh.host` 是需要固定校验的 VPS SSH 身份 `it.cjwsjzyy.xyz`；
- `remote_ssh.connect_host` 是实际原始 VPS SSH 地址，避免把 SSH 流量交给 Cloudflare 橙云 HTTP 代理；
- VPS host key 必须存在于板端 pinned known_hosts。

agent 最终启动的命令等价于：

```sh
/usr/bin/ssh.openssh -NT \
  -o BatchMode=yes \
  -o ExitOnForwardFailure=yes \
  -o ServerAliveInterval=30 \
  -o ServerAliveCountMax=3 \
  -o StrictHostKeyChecking=yes \
  -o UserKnownHostsFile=/etc/6x-cnc/vps_remote_ssh_known_hosts \
  -o HostKeyAlias=it.cjwsjzyy.xyz \
  -i /etc/6x-cnc/device_private_key.pem \
  -p 22 \
  -R 127.0.0.1:<assignedPort>:127.0.0.1:22 \
  8ax-tunnel@<remote_ssh.connect_host>
```

`ExitOnForwardFailure=yes` 防止端口绑定失败却被误报在线。进程退出后 agent 按 `5s` 起步、最大 `300s` 的指数退避重新登记和连接。运行状态原子写入 `/run/8ax_v5_remote_ssh/status.json`，启动日志写入 `/run/8ax/v5_remote_ssh.log`。

板端产品仍使用原有 SSH server；新增的 `/usr/bin/ssh.openssh` 只作为反向隧道 client，避免改变已有 server。init 脚本通过 `taskset` 尽量放到 CPU1，不能占用 CPU0 微内核实时资源。

### 4. Factory Client 点击流程

入口只在当前设备的 `IP访问记录` 弹窗底部右侧。`DeviceIpAccessDialog` 执行以下步骤：

1. 从 `vpsDistributionId` 取得 6 位数字 ID；没有时才读取同字段语义的 `deviceId`，结果仍必须是 6 位数字。
2. 要求列表中的 `authorizationStatus` 为 `authorized`。
3. 检查 `%WINDIR%\System32\OpenSSH\ssh.exe` 存在。
4. 执行 `ssh -G re-board`，解析第一个真实存在的 `IdentityFile`；Factory Client 不保存、复制或内嵌私钥。
5. 使用管理员认证查询：

   ```http
   GET /api/v1/admin/devices/remote-ssh?deviceId=<6位VPS分发ID>
   ```

6. fail-closed 核对响应中的 `deviceId`、`registered`、`online`、端口范围、`vpsHost=it.cjwsjzyy.xyz`、`vpsPort=22` 和 `tunnelUser=8ax-tunnel`。
7. 打开新的 PowerShell 窗口并启动 Windows 系统 SSH。

最终连接命令等价于：

```powershell
ssh.exe `
  -o ConnectTimeout=10 `
  -o HostKeyAlias=8ax-device-<deviceId> `
  -o StrictHostKeyChecking=ask `
  -o HostKeyAlgorithms=+ssh-rsa `
  -o PubkeyAcceptedAlgorithms=+ssh-rsa `
  -i <ssh -G re-board 解析出的 IdentityFile> `
  -J vps3 `
  -p <assignedPort> `
  root@127.0.0.1
```

`vps3` 负责登录 VPS，`-p` 指向仅在 VPS loopback 上存在的设备端口。`HostKeyAlias=8ax-device-<deviceId>` 让不同开发板分别保存 host key；首次连接由 Windows OpenSSH 询问，后续 key 变化必须失败，不能使用自动接受策略。旧板端 Dropbear 需要的 RSA 兼容项只传给这一次进程，不修改 Windows 全局 SSH 配置。

Windows 用户 SSH 配置至少需要具备：

```sshconfig
Host vps3
    HostName <VPS原始SSH地址或DNS-only名称>
    User <VPS运维账号>
    IdentityFile <VPS登录私钥>

Host re-board
    # 远程 SSH 板端 root 私钥，仅保存在本机
    IdentityFile D:\授权私钥\remote-ssh-board-root.pem
```

`D:\授权私钥\remote-ssh-board-root.pem` 是远程 SSH 板端 root 登录专用私钥，与 `factory-device-auth-private.pem` 的工厂设备授权用途不同。它只能保存在本机安全密钥目录，不得上传 VPS、复制到板端、打入发布 exe 或写入 Git。`re-board` 在这里仅用于解析板端 `IdentityFile`，实际目标仍由按钮固定为 VPS loopback 设备端口。

### 5. 在线判定与失败语义

VPS 状态 API 在 `127.0.0.1:<assignedPort>` 上读取 SSH banner。只有读取到 `SSH-` 才返回 `online=true`。这证明反向端口已通到一个 SSH server，但最终设备身份仍需由客户端 host key 和登录后的 6 位 ID 回读闭合。

常见失败及检查顺序：

| Factory Client 提示/现象 | 先检查 |
| --- | --- |
| 设备 ID 无效 | 设备列表的 `vpsDistributionId` 是否为保留前导零的 6 位字符串 |
| 设备尚未授权 | 最新授权的 `permissions` 是否包含 `remote_ssh_tunnel`，板端是否已取得该授权 |
| `re-board` 无可用 IdentityFile | `ssh -G re-board` 输出的路径是否存在，路径是否只在本机保存 |
| 尚未登记 | 板端 status/log 中是否为授权、DNA、endpoint 或 HTTPS 登记错误 |
| 已登记但不在线 | 板端 OpenSSH client、pinned VPS host key、VPS 原始 22 端口和反向端口绑定是否正常 |
| VPS 身份或端口无效 | 状态 API 是否返回同一设备 ID、`25000..44999`、正确 host/user；不得手工改成其它端口继续连接 |
| 首次出现 host key 询问 | 人工核对后确认；禁止代码自动写 `yes` |
| `REMOTE HOST IDENTIFICATION HAS CHANGED` | 停止连接并核对板端是否更换/重装及 host key 变更原因，禁止删除 known_hosts 后直接重试 |

只读诊断入口：

```sh
# 板端
/etc/init.d/v5-remote-ssh status
cat /run/8ax_v5_remote_ssh/status.json
tail -n 100 /run/8ax/v5_remote_ssh.log

# VPS：端口必须是 loopback，不得看到 0.0.0.0:<assignedPort>
ss -lntp
sshd -t
```

诊断可以读取状态、日志和监听；不得通过 SSH 直接修改板端产品文件，也不得把 SSH shell 当作正式部署、产品控制或 operator 验收路径。

### 6. 修改后的 focused 验证

修改对应组件后至少运行其直接验证：

```powershell
dotnet build 8ax-factory-client-source\8ax.FactoryClient\8ax.FactoryClient.csproj -c Release --no-restore
python 8ax-factory-client-source\vps-auth-gateway\tests\test_remote_ssh_gateway.py
python 8ax-factory-client-source\vps-auth-gateway\tests\test_remote_ssh_sshd_assets.py
python board\services\remote_ssh\test_v5_remote_ssh_tunnel.py
python board\tools\docs\verify_v5_document_routes.py --strict-details
```

VPS 部署前还必须对最终 sshd 配置运行 `sshd -t`。只允许 reload SSH 配置，不为本功能重启 nginx、xray、授权网关或 OTA 服务。板端可见行为发生变化并且当前任务要求上板时，必须从 Windows canonical source 构建、按 runtime manifest 部署，仅重启 `v5-remote-ssh`，最后从 Factory Client 原始按钮连接并回读同一设备 ID。

## 驱动发布

`驱动发布` 页用于把 `driver_profile_map.json` 发布到 VPS 端驱动 profile 真源。发布范围必须显式选择 `public` 或 `private`。

规则：

- public 发布写入 VPS public profile 真源和 public 静态镜像。
- private 发布必须填写 6 位 `VPS分发ID` 和 64 位 `PL DNA hash`，VPS 设备 private 根目录固定为 `/opt/8ax-auth/storage/private/<VPS分发ID>-<PL DNA hash>/`。
- private 发布不得写入裸 DNA hash 目录或旧 6 位 ID-only 目录；Factory Client 把 ID 和 DNA hash 传给 VPS，VPS 校验绑定后写入 id-dna 服务端内部目录，板端下载 URL 仍只携带 6 位 ID。
- 同一个 private 根目录保存设备授权文件、私有驱动映射表、private OTA 子目录和后续 WinRemote 用户上传文件。
- 发布页只上传 profile 文件和发布原因，不生成授权、不替板端下载、不合并 public/private。

## 窗口与缩放

主窗口默认客户区为 `1600x1200`。WinForms 启动配置和项目 `ApplicationHighDpiMode` 必须保持 PerMonitorV2/DPI aware，以适配 Windows 系统缩放。

## 发布规则

每次修改 Factory Client 的源码、项目配置、资源或发布行为后，必须生成新的 Release exe，并把发布结果记录到 `发布记录.md`。

发布要求：

- `Models.cs` 里的 `ClientInfo.Version` / `ClientInfo.Build` 必须和 `.csproj` 里的版本号同步更新。
- 发布命令必须使用 Release 配置生成 Windows x64 exe。
- 发布记录必须写明日期、版本、构建号、输出路径、exe SHA256、变更范围和验证结果。
- 没有生成新 exe 或没有写入发布记录时，不得把本次 Factory Client 修改标记为已交付。

当前标准发布命令：

```powershell
dotnet publish 8ax-factory-client-source\8ax.FactoryClient\8ax.FactoryClient.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:EnableCompressionInSingleFile=true -o 8ax-factory-client-source\artifacts\8ax.FactoryClient-<version>-win-x64 --nologo
```
