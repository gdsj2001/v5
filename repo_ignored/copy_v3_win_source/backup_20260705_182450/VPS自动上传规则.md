# 8ax WinRemote VPS 自动上传规则

## 适用范围

本规则只适用于 `8ax-win` Windows 远程客户端的正式升级包生成、上传和交付。它不替代板端功能验证，也不证明 WinRemote 在板端可用；它只定义升级包生成后必须自动上传 VPS 的发布边界。

## 硬规则

1. 正式生成 WinRemote 升级包必须使用：

   ```powershell
   tools\publish_winremote_update.ps1 -Version <version>
   ```

2. 正式生成完成后必须在同一次脚本运行中自动上传到 VPS，目标目录为：

   ```text
   /var/www/html/updates/8ax-winremote/win-x64/
   ```

3. 正式发布不得停在本地 `zip`、`manifest.json` 或 `publish\win-x64\8ax.WinRemote.exe`。本地生成成功不等于发布成功。

4. 正式发布不得使用 `-SkipUpload`。`-SkipUpload` 只能用于本地诊断；使用后产物状态必须标记为 `local_only_diagnostic`，不能交付给升级按钮、不能说已经发布、不能让用户认为 VPS 已更新。

5. VPS 上传完成后必须校验：

   - VPS 上的升级包 SHA256 与本地包一致。
   - VPS 上的 `manifest.json` 与本次版本、文件名、大小、SHA256 一致。
   - 两个 HTTPS manifest 地址都能读到本次版本：

     ```text
     https://license.cjwsjzyy.xyz/8ax-winremote/win-x64/manifest.json
     https://license.3dtouch.top/8ax-winremote/win-x64/manifest.json
     ```

6. 只有脚本输出 `upload: uploaded and verified`，才可以说 VPS 上传完成。否则只能说本地生成或诊断生成。

7. 升级包必须仍然是单文件包：zip 内只能包含 `8ax.WinRemote.exe`，不得包含 `.dll`、`.deps.json`、`.runtimeconfig.json`、`.pdb` 或其他旁路文件。

8. 不得绕过发布脚本手工复制单个 exe、手工改 manifest、手工上传未校验文件来冒充正式发布。

## 失败处理

- 如果本地生成成功但 VPS 上传失败，状态是 `local_only_not_uploaded`。
- 如果 VPS 上传成功但 SHA256 或 manifest 校验失败，状态是 `upload_failed_unverified`。
- 如果 HTTPS manifest 校验失败，状态是 `vps_not_release_ready`。
- 任一失败状态下，都不得提示用户使用升级按钮获取该版本。

## 最小交付记录

正式上传完成后，最终回复或交接记录至少写明：

- 使用的版本号。
- 脚本命令。
- VPS 目标目录。
- 脚本输出的上传状态。
- manifest 两个 HTTPS 地址的校验结果。
