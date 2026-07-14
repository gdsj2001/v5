# VPS 登录与项目对接说明

引用需求真源：`REQ-DOC-SINGLE-SOURCE`、`REQ-DRIVE-PROFILE-AUTH-CHAIN`、`REQ-REMOTE-SSH-MAINTENANCE`、`REQ-NATIVE-OWNER-FIRST`、`REQ-LINUXCNC-COMMAND-GATE`、`REQ-SETTINGS-RUNTIME-DRIVE-ONLY`、`REQ-PARAM-MEMORY-LIGHTWEIGHT-SAVE`、`REQ-SHM-DISPLAY-PROJECTION`、`REQ-WCS-NATIVE-OWNER`。

## AI 阅读入口

<!-- AI_FAST_READ_BEGIN -->
owner_reqs: [REQ-DRIVE-PROFILE-AUTH-CHAIN, REQ-REMOTE-SSH-MAINTENANCE]
read_when: [VPS 登录, 授权, private/public 下载, drive profile, OTA package, remote relay, 厂家远程 SSH]
truth: [授权身份 -> package/profile 或 IP访问记录选择 -> 板端校验/加载或 system SSH host-key readback -> consumer]
forbidden: [public fallback 冒充授权, 客户分叉产品代码, 未验 hash 的下载, 未选择/无效 IP 启动 SSH, 自动接受 host key, SSH/FIFO 产品控制旁路]
readback: [license/entitlement, package/profile hash/version, 板端加载 identity, relay health, selected access IP, SSH host-key/device readback]
impact: [dealer/factory client, IP访问记录弹窗, drive mapping, OTA, settings actiond]
acceptance: [身份与产物 hash 闭合；远程 SSH 必须证明当前设备弹窗 -> 选中或最近 IP -> 板端 SSH 主机身份与命令回读]
detail_sections: [2. Remote Relay 健康, 3. 板端诊断、G-code 上传与 OTA 升级入口, 4. 厂家远程 SSH 维护通道, 6. VPS 登录, 7. VPS 数据, 9. 核对项, 10. 禁止事项]
<!-- AI_FAST_READ_END -->

- 启动内存/热路径通用规则：见 `REQ-PARAM-MEMORY-LIGHTWEIGHT-SAVE` / `功能/0-1开机参数入内存.md`，本文只保留本功能特有边界。
- 先读 `REQ-DRIVE-PROFILE-AUTH-CHAIN`；涉及控制链、WCS 或参数真源时回到对应 REQ owner。
- 不得为客户、驱动、收费档位或现场临时需求 fork 板端产品代码；下载状态、诊断快照和 public fallback 不能冒充 private 授权成功。




正式链路：

```text
Windows remote client -> relay input -> board remote_input.sock -> UI C -> native command gate -> LinuxCNC/微内核/HAL/EtherCAT
```

正式输入入口：

```text
board remote_input.sock framed JSON
```

禁止：

- 接到 `/run/.../remote_input` FIFO、`tap`、`tap_cmd`、SSH tap、全局临时 JSON。

`tap` / `tap_cmd` 是退役测试入口，产品 UI 不得创建或消费 `/run/8ax_v3_product_ui/tap*`。

## 2. Remote Relay 健康

产品 UI 启动时远程显示和输入按 canonical runtime manifest 常驻开启；产品源码、部署项和板端不得保留 disable/enable marker、环境变量或备用启动入口作为 fallback。


- HTTP/WS 已监听。
- `/remote/info` 可返回。
- frame/input 路径可用。
- relay 进程、本地 mmap/FIFO/socket、`/remote/info` 和实际 frame/input 事件共同证明 ready；不得用周期性 status JSON 作为 steady-state actual。

一次性诊断文件只能用于明确的启动/故障诊断，不能参与控制链路、heartbeat 或 steady-state ready；正常运行状态使用进程/socket health 和 `/remote/info`。

`/remote/info` 还提供顶部诊断栏：

- `system_metrics.cpu0_percent`
- `system_metrics.cpu1_percent`
- `system_metrics.memory_percent`
- `system_metrics.disk_percent`
- 内存/磁盘字节数

这些指标只显示，不参与运动、按钮、授权、设置或任何控制判断。字段缺失显示 `--%`。
CPU 百分比必须用相邻两次 `/proc/stat` 采样的 delta 计算当前占用率，不得用开机累计 idle/total 比例冒充实时占用；第一次采样或采样异常返回缺失值，由 WinRemote 显示 `--%`，后续 `/remote/info` 周期刷新显示最新值。内存和磁盘百分比每次从 OS 现读，只作诊断显示。

## 3. 板端诊断、G-code 上传与 OTA 升级入口


| 入口 | 用途 | 禁止 |
| --- | --- | --- |
| `/remote/diagnostics` | 只读诊断：relay/UI 状态、关键 `/run` 状态摘要、SHM 文件证据、资源指标 | 不作为运动、按钮、权限、设置或控制真值 |
| `/remote/program/upload` | 上传 Windows 本地 G-code 到板端程序目录 `/opt/8ax/v5/gcode` | 不打开程序、不运行程序、不替代 UI/operator 路径 |

上传要求：

- 只允许 basename 文件名。
- 限制扩展名和大小。
- 原子写入。
- 返回 SHA256 和字节数读回证据。

OTA 升级按钮规则：

- Windows 客户端只发起 `/remote/ota/upgrade` 请求并显示 job 状态；实际下载、验签、写入、切换和回滚由板端 OTA client 与受控状态机完成。
- scope 解析必须覆盖当前 DNA private 区和 public 区；manifest/metadata 可以都解析，package 下载互斥。
- 当前 DNA private 区存在 OTA 包记录时，只能下载 private OTA 包，不得继续下载 public OTA 包。
- 当前 DNA private 区明确返回 no_package/absent/skipped 并记录 reason 后，才允许选择 public OTA 包。
- private OTA 包存在但授权、版本、hash、签名或 profile 校验失败时必须 fail-closed，不得改用 public 包冒充升级成功。
- 后台下载、验签和 staging job 必须可取消；进入写 inactive 分区、boot target 切换或重启前的不可取消阶段前，必须有操作员显式确认。
- 进度至少区分：连接 VPS、解析 private/public scope、选择包、下载中、验签中、等待确认、安装中、重启待确认、成功、失败/已取消。
- 错误和需要操作员处理的结果按 `REQ-UI-POPUP-RESULT-POLICY` 弹窗显示 code、中文原因和处理建议。

Windows 客户端不得为这些按钮新增 SSH、shell、SFTP、直接读写 `/run`、直接调用 LinuxCNC/HAL 或远程点击替代路径。

WinRemote 远程画面只允许使用板端 `remote_ui_relay` 的 HTTP 首帧初始化和 `WS /remote/stream` dirty-rect 流。`WS /remote/stream` 无法连接、升级失败或运行中断开时，客户端必须记录 `relay_stream_unavailable` 并进入重连/失败状态，不得降级为低频 HTTP 全帧轮询、旧 `/dev/fb0` 抓屏、SSH/SFTP 文件读取、FIFO 或其它显示 fallback；远程输入仍只允许 `WS /remote/input`。

## 4. 厂家远程 SSH 维护通道

`REQ-REMOTE-SSH-MAINTENANCE` 只提供厂家维护 shell，不是产品运动控制、设置保存、部署真源或 operator path。厂家控制台标题栏不得放置 `远程连接`；按钮必须位于当前设备的 `IP访问记录` 弹窗底部右侧，并显示本次将连接的 IP。弹窗初始目标取该设备最近一条有效 IP；用户在 `明细` 或 `IP分布` 中选择另一条有效 IP 后，按钮必须同步切换到该 IP。没有有效 IP、选中值不是合法 IP 或 Windows 系统 SSH 缺失时必须明确失败，不得静默选择其它设备或其它地址。

正式链路：

```text
Factory Client 设备行 -> IP访问记录弹窗 -> selected/latest valid IP
  -> Windows system ssh root@<selected-ip>:22
  -> board Dropbear host key -> interactive maintenance shell
```

身份与地址规则：

- `IP访问记录` 仍是服务器观察到的请求来源；按钮按用户明确选择直连该地址，但 IP 本身不构成设备身份或在线证明。若地址受 NAT、防火墙或运营商网络限制，系统 SSH 的连接失败必须原样可见，不得回退到另一 IP、VPS 隧道或共享代理。
- Factory Client 只启动 Windows 系统 `ssh.exe`，固定目标账号 `root`、端口 `22`、`StrictHostKeyChecking=ask`，并按 6 位设备 ID 使用独立 `HostKeyAlias`。客户端不得保存设备密码、私钥或自动接受板端 host key。
- 当前设备 ID、弹窗显示的目标 IP 和最终 SSH host key 必须同时可见；首次连接由人工核对 host key，已记录 host key 变化时必须由系统 SSH fail-closed。
- SSH 只允许厂家维护 shell；通过 SSH 直接修改板端产品文件、补发产品控制命令或替代正式 source/build/deploy/operator 证据仍然禁止。

验收必须从设备表的 `IP访问记录` 原始入口开始，证明：弹窗属于当前设备、按钮初始显示最近有效 IP、选择 `明细`/`IP分布` 中另一 IP 后按钮同步更新，点击后系统 SSH 只连接该 IP，并读取同一板端的 `uname -m` 和 6 位登记 ID。只看到终端启动、TCP 连接或 IP 相同不能代替板端身份回读。



规则：

- 客户端优先用可访问网络的 HTTPS `Date` 响应头。
- 网络时间不可用时立即回退 Windows 本机 UTC。
- 板端 relay 只把该字段用于系统时钟维护，不接入运动、授权、设置或业务控制。
- 每个 relay 进程首次成功覆盖后不重复刷新。
- 结果可一次性写入 `/run/8ax_v5_product_ui/remote_time_sync_status.json` 作为显式诊断；不得周期刷新或作为业务 actual。


Windows 客户端程序名固定：

```text
```

发布包只允许包含同名单文件 exe，不发布 `.dll`、`.deps.json`、`.runtimeconfig.json`、`.pdb` 等多文件产物。

VPS manifest：

```text
```

VPS 静态目录：

```text
```

升级规则：

- manifest 版本和 exe 文件版本必须一致。
- 服务器版本高于本地版本才下载。
- 本地版本等于或高于服务器版本时不下载、不安装、不重启。
- 下载包只含单 exe zip。
- 安装时删除旧多文件发布残留。
- 升级窗口必须有进度、超时、错误恢复和按钮恢复。
- 升级器使用 Windows 系统网络/代理配置，不得强制 `UseProxy=false`。

常规发布命令：

```powershell
```

正式发布必须上传到 VPS 并校验：本地 zip SHA256、VPS 文件 SHA256、manifest `sha256`、HTTPS 下载后 SHA256。四者不一致即发布失败。任何证据进入 `repo_ignored/` 前必须脱敏。

## 6. VPS 登录

优先使用本机 SSH 别名：

```powershell
ssh vps3
```

已知别名：

```text
vps3-dmit
vps3
it.cjwsjzyy.xyz
```

只记录 `IdentityFile` 路径，不复制私钥内容。改服务器配置前必须另行登记任务和备份；日常只读核对优先。

## 7. VPS 数据

工厂设备授权签名密钥：

- 本项目访问 VPS 涉及设备登记、授权下载、private/public profile、OTA private scope 和 Factory Client 发布签名时，公钥/私钥材料统一以 Windows 人工保管目录 `D:\授权私钥` 为来源；只记录路径和摘要，不复制私钥内容。
- Factory Client 顶部 `授权私钥` 字段应选择 `D:\授权私钥\factory-device-auth-private.pem`。
- 同目录公钥文件为 `D:\授权私钥\device_auth_public.pem` 和 `D:\授权私钥\factory-device-auth-public.pem`；当前两个公钥文件内容一致，SHA256 为 `5608ee95805979e3a9936a5803716fd65afeb8b64fc72e56acb0dcfc8835ac1f`。
- 源码部署输入只允许保存同一把公钥副本：`D:\v5\board\config\auth\device_auth_public.pem`，部署到板端 `/etc/6x-cnc/device_auth_public.pem` 供授权验签使用；不得把 `factory-device-auth-private.pem` 或任何设备私钥复制进 Windows 项目源码、VM build/output 目录、部署 manifest、截图、日志或仓库，VM 也不得创建源码副本。
- VPS live 签发密钥使用同一组材料：私钥运行路径是 `/opt/8ax-auth/secrets/device-auth-signing-private.pem`，公钥运行路径是 `/opt/8ax-auth/public/device-auth-signing-public.pem`；即使服务器底层有历史 symlink，文档、服务配置和人工核对只使用 `/opt/8ax-auth` 口径，不再新增 `/opt/z20-auth` 产品路径。
- 按当前 v5/VPS 代码口径，VPS 存储和发布路径使用 `/opt/8ax-auth/...`；板端验签公钥位置是 `/etc/6x-cnc/device_auth_public.pem`。VPS 端签名私钥/公钥若由运维部署在 secrets/public 目录，必须落在当前 8ax-auth 部署树下，不再把旧 z20-auth 部署树作为产品路径。
- 私钥不得提交进 Git、不得打进 Factory Client exe、不得写入截图或过程日志；更换密钥后必须重新生成并上传设备授权文件，再让板端执行 `下载授权` 验签闭环。

关键目录：

```text
/opt/8ax-auth/storage/drive-profiles/public/driver_profile_map.json
/opt/8ax-auth/storage/private/<vpsDistributionId>-<pl_dna_hash>/device_authorization.json
/opt/8ax-auth/storage/private/<vpsDistributionId>-<pl_dna_hash>/driver_profile_map.json
/var/www/html/drive-profiles/public/driver_profile_map.json
/var/www/html/updates/drive-profiles/public/driver_profile_map.json
/opt/8ax-auth/storage/ota/public/<product>/<channel>/manifest.json
/opt/8ax-auth/storage/private/<vpsDistributionId>-<pl_dna_hash>/ota/<product>/<channel>/manifest.json
/var/www/html/updates/ota/public/<product>/<channel>/manifest.json
/var/www/html/updates/ota/private/<vpsDistributionId>-<pl_dna_hash>/ota/<product>/<channel>/manifest.json
```

板端正式 API 入口来自 endpoint 配置的主域名和备用域名，不是 IP 直连或单一域名。

设备 `VPS分发ID` 规则：

- `VPS分发ID` 是 VPS 分发并保存的 6 位十进制显示 ID，Factory Client 只从 VPS 读取，不在本地生成最终 ID。
- VPS 首次登记设备 PL DNA 时生成该 ID；生成后必须保存到 VPS 数据库，后续重新登记、刷新列表、客户端升级或算法实现调整都不得改变已保存 ID。
- 生成输入是规范化后的 PL DNA；当前候选算法沿用 VPS 端 `_pl_dna_short_device_id` 口径：对规范化 PL DNA 的大写字符串计算 SHA256，取 digest 前 8 字节 big-endian 后对 `1000000` 取模，并格式化为 6 位数字。
- VPS 得到候选 ID 后必须和已保存设备 ID 对比：同一 PL DNA 已有保存 ID 时返回原保存 ID；候选 ID 被其它 DNA 占用时，不得覆盖、不允许重复，必须按同一算法加稳定 salt/counter 继续寻找未占用 6 位 ID 并保存，或 fail-closed 返回明确 collision 错误。
- `/api/v1/admin/devices` 和板端登记接口必须返回 canonical 字段 `vpsDistributionId`；该值就是 VPS 原样分发并冻结的 6 位显示 ID，由 VPS 端负责生成、冲突处理、保存和准确返回。板端、Factory Client、UI 和下载链路不得用 `device_id`、`vps_distribution_id`、DNA hash、本地算法或旧状态字段推导、替换或格式化显示 ID；若 VPS 没有返回合法 `vpsDistributionId`，客户端必须按登记失败处理。
- VPS 端设备 private 根目录统一使用 `/opt/8ax-auth/storage/private/<vpsDistributionId>-<pl_dna_hash>/`。`vpsDistributionId` 必须是 VPS 已保存的 6 位分发 ID；`pl_dna_hash` 是该 ID 绑定的 PL DNA hash。这个 id-dna folder 只允许作为 VPS 服务端内部存储路径和数据管理字段，不得暴露给板端 URL、UI、日志或下载接口。Factory Client 发布 private profile 或 private OTA 时必须同时提交 `vpsDistributionId` 和 `pl_dna_hash` 供 VPS 校验，VPS 写入路径只能使用校验通过后的 id-dna folder，不得写入 hash-only 或旧 ID-only 目录。
- Factory Client 管理界面可以显示 `vpsDistributionId`、`dna_binding=server_verified`、scope、SHA 和服务端返回摘要，但不得在预览、发布结果、日志或弹窗中原样显示 private `targetRel`、`private_folder`、`<vpsDistributionId>-<pl_dna_hash>`、hash-only 目录或旧 ID-only 目录；private 目标只能显示为当前设备 private profile/OTA 已由服务端校验。
- VPS `/api/v1/admin/devices` 必须返回 `authorizationStatus`。对应 id-dna private 目录里还没有 `device_authorization.json` 时返回 `pending_factory_authorization`；存在授权文件时返回 `authorized`。Factory Client 设备 DNA 表必须把 `pending_factory_authorization` 行显示为红色，提醒还没人工确认授权。

板端设置页 `登记本机码` 链路：

- 点击 `登记本机码` 后，板端必须读取本机 PL DNA，同时提交设备公钥（至少包含 `device_public_key_pem` 和 `device_public_key_sha256`）到 VPS 登记接口；VPS 是 `vpsDistributionId` 的唯一生成和保存 owner。
- PL DNA 的板端真源是当前位流中 DNA reader 的 live UIO 寄存器，只允许动作 owner 在内存中读取并做多次一致性校验。U-Boot 只校验 DNA reader 的 magic/version/status/bit-count ABI 已就绪，不得在 `boot.scr`、设备树、源码、参数文件或状态文件中保存、比较或注入某一块板的 raw DNA 常量；Linux 侧不得从 `/chosen`、旧状态、授权文件或磁盘缓存回退推断 live DNA。默认设备节点为外部 OS 设备事实 `/dev/v5-dna-uio`，节点缺失、ABI 不符、读回为零或多次读回不一致时必须 fail-closed。
- VPS 必须先验证本次提交的设备公钥：公钥必须可解析、hash 必须与 `device_public_key_sha256` 一致，且满足 VPS 设备登记策略；公钥验证不通过时不得登记 DNA、不得生成或返回新 ID、不得创建 private folder。
- 公钥验证通过后，VPS 才允许规范化 PL DNA、计算/冻结 6 位 `vpsDistributionId`、记录数据库并创建 private folder。若同一 PL DNA 已经登记，VPS 不得重新生成 ID，不得覆盖既有绑定，必须直接返回已保存的 6 位 `vpsDistributionId` 给板端；必要时只按 VPS 公钥策略更新或确认该设备公钥记录。
- VPS 必须在数据库中记录 6 位分发 ID、PL DNA hash、公钥 hash/公钥记录之间的绑定关系，并创建 `/opt/8ax-auth/storage/private/<vpsDistributionId>-<pl_dna_hash>/`。VPS 返回给板端的登记结果只需要暴露 canonical `vpsDistributionId` 和校验状态；不得要求板端保存或使用 id-dna folder 作为下载 URL。
- 板端必须从 VPS 响应的 `vpsDistributionId` 字段原样回读该 ID，不得在本地按 DNA 自行生成最终 ID，不得从 `device_id`、`vps_distribution_id`、旧状态文件或授权文件里回退推断显示 ID。回读成功后本地登记状态只允许保存该 VPS 原样返回的 `vpsDistributionId`、登记结果码和公钥摘要，不允许保存 raw PL DNA、DNA hash、`pl_dna_hash` 或 id-dna folder。写入成功后设置页顶部只显示该 6 位 ID，不显示 `已登记` 等状态后缀、本机码片段或 DNA hash；没有合法 `vpsDistributionId` 时同一 ID 位置显示 `未登记`，便于人工核对且不暴露 DNA。`DNA_REGISTER_UPLOADED_PENDING_AUTH` 表示 VPS 已验证公钥、保存设备绑定并创建 private folder、板端已保存本地 ID 状态，只是授权文件等待工厂端生成/上传；不得当作登记失败。
- 如果 VPS 未返回 canonical `vpsDistributionId`、ID 与 DNA hash 关系无法确认、或本地状态文件写入失败，`登记本机码` 必须 fail-closed；不得显示本地推导 ID，也不得允许后续授权/服务器下载把旧 ID 当作当前 ID。

板端设置页 `下载授权` 与 `服务器下载` 链路：

- `下载授权` 和 `服务器下载` 必须先读取本地登记状态中的 `vpsDistributionId`；板端本地状态不得保存或暴露 raw DNA、DNA hash、`pl_dna_hash` 或带 DNA/hash 的 private 目录。
- 板端任何 VPS 上传/下载动作继续前，本地前置校验顺序固定为：先确认本地登记状态存在合法 6 位 `vpsDistributionId`，再临时读取本机 live DNA 并在内存中计算 DNA hash；DNA 和 hash 只能用于本次请求校验，不允许落盘保存，不允许写 UI/日志/SHM。是否匹配由 VPS 用本次提交的临时 DNA/DNA hash 与数据库绑定关系一次校验返回；任一条件缺失都必须 fail-closed 并提示先登记本机码或检查本机 DNA，不得继续发起 VPS 文件查找。
- 下载授权和服务器下载的客户端 URL/path 只能携带保存的 6 位 `vpsDistributionId`，同时把本次临时读取的 DNA/DNA hash 作为 header 或 body 校验字段发给 VPS，不得放入 query。VPS 必须对自带 ID 的访问校验临时 DNA/DNA hash，校验通过后在服务端解析到 `/opt/8ax-auth/storage/private/<vpsDistributionId>-<pl_dna_hash>/`，再读取授权文件或私有驱动映射表；客户端不得把 id-dna、hash-only 或旧 ID-only private 目录当作直接下载路径。
- 下载授权只允许在 VPS 校验设备绑定后读取当前设备 id-dna private 根目录中的 `device_authorization.json`；不得再只按 DNA hash 查找，也不得从其它设备 private 目录兜底。
- 服务器下载必须在 VPS 校验设备绑定后读取同一个 id-dna private 根目录中的 `driver_profile_map.json`；private 缺失时必须记录 absent/skipped reason 并按规则处理 public fallback，private 存在但 hash/签名/授权/设备绑定不匹配时必须 fail-closed，不得改用 public 冒充 private 成功。
- 如果本地没有已登记 `vpsDistributionId`，这两个按钮必须提示先登记本机码，不得只拿 DNA hash 访问服务器。

驱动 profile 规则：

- VPS profile 真源分为 public 和当前 DNA private 两类，二者都是 `driver_profile_map.json`。
- VPS 不发布第三张合成产品表。
- 板端不得把 public/private 合并成新的产品真源。
- public/private scope 都必须经过 DNA、device authorization、request signature、challenge 门禁。
- VPS 端驱动 profile 发布入口由厂家客户端 `8ax-factory-client-source` 的 `驱动发布` 页调用，服务端源码在 `8ax-factory-client-source/vps-admin-api/vps_ota_admin_api.py`：厂家账号通过 `POST /api/v1/admin/drive-profiles` 上传 public 或 private `driver_profile_map.json`；private 发布必须写入设备 private 根目录下的 `<vpsDistributionId>-<pl_dna_hash>/driver_profile_map.json`，不得写入 hash-only 或旧 ID-only 目录。发布前 VPS 必须用提交的 `vpsDistributionId` 和 `pl_dna_hash` 查询 `devices` 表确认同一设备绑定；设备缺失、ID/hash 不匹配或数据库校验不可用时 fail-closed，不能写入 private 目录。
- private 缺失必须记录为 absent/skipped/failed reason，并删除本地 stale private 缓存。
- 只有 public fallback 成功时，不能声称 DNA private 下载成功。

OTA package 规则：

- VPS OTA 真源分为 public 和当前 DNA private 两类，二者都必须有 manifest、package、SHA256、签名、版本、目标产品/profile 和发布 reason。
- 板端 OTA client 必须通过 endpoint 配置的主/备域名解析 private/public scope，不得 IP 直连或绕过 DNA、device authorization、request signature、challenge 门禁。
- 包选择顺序固定：先解析当前 DNA private；private 区存在 OTA 包记录时下载 private 包并停止选择，不得下载 public 包。
- private 区明确 no_package/absent/skipped 并记录 reason 后，才允许解析并下载 public 包。
- private 包存在但 hash、签名、版本、profile、授权或防降级校验失败时必须 fail-closed，不得改用 public 包替代。
- public/private 包不得合并；一次升级只能有一个 `selected_scope`、一个 manifest 和一个 package hash。
- 板端可见的 selected manifest、下载 URL、目录名和 UI 状态必须记录 `source_scope`、`vps_distribution_id`、`dna_binding=server_verified`、package SHA256、version、anti-rollback 信息和选择 reason，但不得暴露 PL DNA 或 PL DNA hash。VPS admin 发布端和内部存储 metadata 可以使用 `<vpsDistributionId>-<pl_dna_hash>` 定位服务端 private folder，但该 `targetRel/private_folder` 不得作为板端下载 URL 或 UI 显示真源。`pl_dna_hash` 只允许作为受控请求体/header 中的校验字段或 VPS 内部校验输入，不得放入 query；UI/VPS 状态不能把 public 成功显示为 private 成功。
- VPS 端 OTA 发布入口由厂家客户端 `8ax-factory-client-source` 的 `OTA发布` 页调用，服务端源码在 `8ax-factory-client-source/vps-admin-api/vps_ota_admin_api.py`：厂家账号通过 `POST /api/v1/admin/ota/packages` 上传已签名 OTA package、detached signature、hash 和发布 metadata 到 public 或当前设备 id-dna private 目录；private 发布路径只能使用 `/opt/8ax-auth/storage/private/<vpsDistributionId>-<pl_dna_hash>/ota/<product>/<channel>/`。发布前 VPS 必须用提交的 `vpsDistributionId` 和 `pl_dna_hash` 查询 `devices` 表确认同一设备绑定；设备缺失、ID/hash 不匹配或数据库校验不可用时 fail-closed，不能写入 private 目录或静态镜像。VPS 服务端负责校验 package/signature SHA256 和 size、生成/保存 manifest 与 SHA256 sidecar，并镜像到 `/var/www/html/updates/ota/...`。该入口不生成签名、不启动板端升级、不写板端状态。

## 8. 代码归属

| 范围 | owner |
| --- | --- |
| remote relay | UI 本地投影 owner 为 `board/app/src/v5_lvgl_remote_display.*`、`v5_lvgl_remote_input.*`；网络 owner 为 `board/services/ui/v5_remote_ui_relay.py` 及同目录 `v5_remote_ui_contract.py`、`v5_remote_ui_protocol.py`、`v5_remote_ui_state.py`、`v5_remote_ui_support.py`；历史 v3 relay 只作参考，不是产品 owner |
| remote input | `board/services/ui/v5_remote_ui_relay.py` 把换行 JSON 写入 `/run/8ax_v5_product_ui/remote_input` FIFO；`board/app/src/v5_remote_input_ipc.*` 负责 FIFO 解析，再调用 `v5_lvgl_remote_input.*` 的 LVGL 指针设备 |
| 驱动 profile 下载/上传 | 当前板端下载真源为 `board/services/auth_download/v5_drive_profile_download.py`、`board/services/drive_profile/v5_settings_actiond.py`、`v5_drive_bus_action.py` 及同目录拆分模块；发布输入以当前 Factory Client/VPS admin API 和 `board/config/drive-profiles/*` 的 intended remote path 为准 |
| Windows Remote 客户端 | `8ax-win/src/8ax.WinRemote/MainWindow.xaml` 是布局；relay 会话、frame、指针、程序和 OTA 分别在 `MainWindow.RelaySession.cs`、`MainWindow.RelayFrame.cs`、`MainWindow.Pointer.cs`、`MainWindow.Programs.cs`、`MainWindow.OtaUpgrade.cs`，`MainWindow.xaml.cs` 只保留窗口总入口 |
| Factory Client 管理界面 | `8ax-factory-client-source/8ax.FactoryClient/MainForm.cs` 是总入口；布局、设备、经销商和升级已拆到 `MainForm.Layout.cs`、`MainForm.Devices.cs`、`MainForm.Dealers.cs`、`MainForm.Upgrades.cs`；OTA 发布面板在 `OtaPublishPanel.cs`，VPS API 在 `8ax-factory-client-source/vps-admin-api/` |
| 厂家远程 SSH | Windows 按钮与启动器在 `8ax-factory-client-source/8ax.FactoryClient/MainForm.RemoteSsh.cs`；设备登记/状态 API 与端口/key owner 在 `8ax-factory-client-source/vps-auth-gateway/remote_ssh_gateway.py`；板端 agent 在 `board/services/remote_ssh/`，由 canonical runtime manifest 部署 |
| Dealer Client 管理界面 | `8ax-dealer-client-source/8ax.DealerClient/MainForm.cs` 是总入口，操作实现已拆到 `MainForm.Actions.cs` |
| 板端 OTA relay/action/client | Windows 操作入口当前在 `8ax-win/src/8ax.WinRemote/MainWindow.OtaUpgrade.cs`，VPS 发布端在 `8ax-factory-client-source/vps-admin-api/vps_ota_admin_api.py`；板端实现仍按当前 v5 代码和 `待做工作/板端升级.md` 推进，未实现前必须 fail-closed，不得把 Windows/VPS 文件写成板端 owner |


## 9. 核对项

只读核对至少包括：

- `ssh vps3` 可登录。
- public profile 存在且 SHA 可核对。
- private profile 存在或有明确 absent/skipped/failed reason。
- endpoint 配置里的主/备域名可访问。
- OTA private/public manifest 解析、`selected_scope`、选择 reason、package SHA256/签名和板端 staging hash 一致。
- 板端 relay `/remote/info`、WS frame、input granted、frame applied 证据齐全。
- 板端一次性诊断 `/run/8ax_v5_product_ui/remote_time_sync_status.json` 能说明时间同步来源和结果，但不作为 ready、heartbeat 或控制真源。
- 远程 SSH 需核对按钮只出现在当前设备的 `IP访问记录` 弹窗、目标 IP 随最近记录或用户选择更新、系统 SSH 严格校验板端 host key，并且登记 ID 回读一致。

## 10. 禁止事项

- 在仓库写 token、私钥、设备授权密钥、DNA 原文或 Authorization/Cookie。
- 用 SSH、shell、SFTP、临时 JSON 或远程点击绕过正式 UI/operator 路径。
- 在标题栏保留第二个远程连接入口、无有效 IP 时猜测地址、选择失败后回退其它 IP，或自动接受 SSH host key。
- 在 Factory Client、VPS、仓库、日志或通用镜像中保存设备私钥、SSH 密码，或多个设备共用同一设备私钥。
- 把 profile public fallback 写成 private 授权成功。
- 有当前 DNA private OTA 包时继续下载 public OTA 包，或用 public 包替代 private 包失败。
- 让资源指标、时间同步、诊断接口参与业务控制。
- 发布只停留在本地生成，不上传和回读 VPS hash。

## 11. 关闭条件


- 使用的正式入口和 owner 文件。
- 是否触碰控制链；若触碰，必须回到 UI -> Broker -> native/LinuxCNC/HAL/EtherCAT 路径证明。
