# Native Helper 白名单与最终边界

> [!IMPORTANT]
> **最高目标：UI 简单直控微内核。**
> 产品控制链只允许 UI C 进程通过 Unix Domain Socket (UDS) 长连接向微内核原生网关（`linuxcncrsh`）发送极简控制报文。外置辅助程序、命令行 `halcmd` 旁路、Python 脚本多级中转、C 端手写 JSON/Broker 胶水均不得作为产品控制路径。机床 UI 显示状态只消费允许的 30ms SHM 显示投影；控制结果和安全事实以微内核/native owner 为准。

---

## AI 阅读入口

- 启动内存/热路径通用规则：见 `REQ-PARAM-MEMORY-LIGHTWEIGHT-SAVE` / `功能/0开机参数入内存.md`，本文只保留 native helper 特有边界。
- native helper 特有边界：linuxcncrsh/native gate、UI 控制入口、allowed helper 表、动作登记表和状态 provider 必须随产品自写运行闭包开机进入 RAM；运行期控制热路径不得临时扫描 helper、懒导入脚本、按文件名执行未登记程序或用旧 wrapper 兜底。
- 修改 helper 白名单时只允许收缩到微内核/native owner 明确需要的长驻 gate 或诊断入口；已退役 helper 不得以 renamed wrapper、环境变量、测试入口或 VM 打包校验形式保留。

---

## 1. 架构去中介化规则

1.  **禁止业务层 Python 动作中介**：
    Home、Work Zero、Jog、Start、RTCP/WCS 等控制动作不得经 Python product action 中转；UI C 端必须直达微内核/native gate。
2.  **禁止外置临时 Helper 进程**：
    不得通过编译或执行 `re_hal_safety_helper`、`re_hal_select_helper` 等外部小程序间接读写 HAL。RTCP 状态切换等操作必须通过登记 native/linuxcncrsh gate 或微内核内部能力完成。
3.  **发送与状态显示分域**：
    UI 控制函数只发送请求，不把 SHM、JSON、Broker result 或本地缓存作为动作成功真源。物理动作结果、安全事实和控制拒绝由微内核/native owner 返回或回读；30ms SHM 只承担允许的 UI 显示投影。

---

## 2. 状态与通道白名单

本表定义产品允许通道和禁止通道。外置 Helper、脚本中介和 C JSON/Broker 胶水不得恢复或改名保留。

| 通道 / Helper 例外 | 源码路径 | 状态 | 最终要求 |
| --- | --- | --- | --- |
| **linuxcncrsh native command gate** | VM 真源 `/root/Desktop/v5/services/command_gate/v5_command_gate.c`、`v5_linuxcncrsh_client.c`、`v5_native_gate_registry.c` 及 UI 侧 `app/src/v5_command_*.c` | **[CANONICAL] 唯一标准控制路径** | **保留并作为唯一合规网关**。UI C 进程通过当前 v5 native command gate 发送 `SET Open`、`SET Run`、`SET Mode` 等极简控制报文，不进行任何前置 logical precheck，机床动作由微内核硬安全直接拦截或放行。旧 v3 native run 源码只能作只读历史参考，不再作为产品 owner。 |
| **HAL safety helper** | `lvgl_app/native/re_hal_safety_helper.c` | **[FORBIDDEN] 禁止** | **不得恢复**。UI 不得通过外部程序拉取 `cia402.N.stat-op-enabled` 等状态。使能与驱动运行证据只能来自登记的微内核/native 状态直连读取；SHM 只作允许显示投影。 |
| **HAL select helper** | `lvgl_app/native/re_hal_select_helper.c` | **[FORBIDDEN] 禁止** | **不得恢复**。RTCP/switchkins 的 ON/OFF 状态直接作为运行期 actual，由 UI 发送原生 MDI/命令驱动微内核内部切换，禁止外部引脚写盘与 helper 中转。 |
| **backend realtime cleanup** | `lvgl_app/scripts/re-v3-8ax-backend-lifecycle.inc.sh` | **[INTERNAL] 后端自用** | 仅作为后台系统拉起前的 LinuxCNC 进程及实时层脏状态自愈清理使用，禁止向 UI 进程或普通控制热路径暴露任何接口。 |

---

## 3. 合规边界

*   **源码要求**：
    product Python 或普通 C UI 中不得出现未登记的 `halcmd`、`subprocess.run` 或直接 `import linuxcnc` 控制路径。
*   **禁止保留要求**：
    Helper 源码、编译目标、VM 打包校验和部署入口不得存在。RTCP、Home、WCS 等功能只能使用 UDS/native/LinuxCNC/微内核 canonical 实现；发现 helper、脚本中介、C JSON/Broker direct 胶水或等价 fallback survivor 时，必须在同一切片物理删除。
