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
- 工厂授权私钥必须是本机私有文件，不得提交进 Git、不得打进发布 exe。Factory Client 顶部 `授权私钥` 路径默认指向 `%LOCALAPPDATA%\8ax\FactoryClient\secrets\factory-device-auth-private.pem`，也可通过 `选择私钥` 指向本机其它安全位置。
- 当前这台 Windows 工厂机的授权私钥保管位置是 `D:\授权私钥\factory-device-auth-private.pem`；对应公钥文件在同目录 `device_auth_public.pem` / `factory-device-auth-public.pem`，路径依据见 `功能/VPS登录与项目对接说明.md`。
- 点击 `人工确认授权` 成功上传授权文件后，VPS 才把该设备视为已授权；Factory Client 刷新列表后必须恢复普通行色。
- 点击 `删除登记` 删除设备 DNA 登记时，VPS 必须同时删除该 6 位 ID 对应 private 目录里的 `device_authorization.json`，防止以后同 ID/旧授权被误下载；删除响应要说明授权文件是否已删除。

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
