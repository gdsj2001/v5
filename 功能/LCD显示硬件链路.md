# V5 LCD 显示硬件链路

<!-- AI_FAST_READ_BEGIN -->
owner_reqs: [REQ-LCD-PIXEL-CLOCK-STABILITY]
read_when: [LCD闪烁, LCD黑屏, 1024x600, 7寸屏, 10寸屏, 像素时钟, clock-wizard, 背光, system-top.dts]
truth: [Windows canonical Linux时钟驱动 -> 实际出镜的system-top.dts单屏参数 -> 当前Vivado/XSA硬件输入 -> PetaLinux current镜像 -> 板端时钟锁定与物理屏观察]
forbidden: [把7寸和10寸物理尺寸差异误判为显示时序差异, 修改system-user.dtsi后声称已出镜, 驱动里写死另一套时序或PWM参数, 32位中间值计算GHz级时钟分频, 直接修改板端产品文件, 未经物理屏观察声明无闪烁]
readback: [system-top.dts出镜身份, 1024x600 native-mode, 200Hz背光周期与占空比, clock-wizard锁定, 实际分频寄存器, framebuffer与UI就绪, 操作员长时间观察]
impact: [linux/kernel时钟驱动, board/petalinux实际system-top.dts与LCD驱动, 当前Vivado/XSA身份, 最小内核与current镜像构建, 板端冷启动]
acceptance: [system-top.dts是唯一显示参数源且只保留一个native timing, LCD驱动不再根据ID选屏或硬编PWM, 32位ARM分频计算使用64位中间值, 受影响内核与设备树构建通过, current镜像部署并冷启动, 时钟锁定且物理屏长时间无闪烁]
detail_sections: [#lcd-contract, #lcd-dt-owner, #lcd-clock-owner, #lcd-build-acceptance]
<!-- AI_FAST_READ_END -->

引用需求真源：`REQ-LCD-PIXEL-CLOCK-STABILITY`、`REQ-BUILD-INPUT-WINDOWS-CLOSURE`、`REQ-BOARD-PROGRAM-FULL-BUILD-CLOSURE`。

<a id="lcd-contract"></a>
## 1. 固定单屏合同

- 当前实物组合固定为：**7 寸屏来源的 LCD 驱动板 + 10.1 寸 1024×600 物理屏 + PS I2C1/0x38 的 FT5x26 触摸**。“7 寸”只描述驱动板来源，不代表当前物理屏尺寸，也不能作为运行时选屏依据。
- 当前产品只支持一个 LCD 显示合同：`1024×600`、像素时钟 `51.2 MHz`、水平总周期 `1024 + 140 + 160 + 20 = 1344`、垂直总周期 `600 + 20 + 12 + 3 = 635`，目标刷新率约 `60 Hz`。
- 7 寸和 10 寸只有显示物理尺寸、触摸物理尺寸不同；其他显示电气与像素时序合同相同。软件不得再根据 LCD ID 切换 `800×480` 或 `1280×800`。
- 设备树背光合同固定为 `200 Hz`（周期 `5,000,000 ns`）、`100%` 占空比。若未来硬件变更需要调整，必须先修改本 owner 和实际出镜的设备树，不得在驱动中另加硬编值。

<a id="lcd-dt-owner"></a>
## 2. 设备树唯一参数源

- 当前 PetaLinux recipe 实际打包的唯一显示设备树 owner 是 `board/petalinux/project-spec/meta-user/recipes-bsp/device-tree/files/system-top.dts`。`system-user.dtsi` 不是当前镜像的显示参数输入，单独修改它不会生效，也不能声称已修复板端。
- `atk_lcd_drm` 节点只保留一个 `display-timings` 子节并将它指定为 `native-mode`。像素时钟、有效区、porch、sync、极性、PWM 周期和占空比均只在该节点定义。
- `xlnx_atk_lcd.c` 只读取 `OF_USE_NATIVE_MODE`、PWM args 和设备树占空比并执行。参数缺失、超范围或 PWM 应用失败必须 fail closed，不得 fallback 到驱动默认值。

<a id="lcd-clock-owner"></a>
## 3. 像素时钟 owner 与计算边界

- 可维护源码唯一 owner 是 `linux/kernel/drivers/clk/clk-xlnx-clock-wizard.c`；`0硬件资料/Z20_LCD驱动/drivers/clk-xlnx-clock-wizard.c` 是硬件参考输入，不是第二份可编辑 owner。
- 32 位 ARM 中 `unsigned long` 为 32 位。计算 fractional divider 时，`parent_rate * 1000` 必须在乘法前提升为 64 位，并使用 64 位除法/取整；禁止先以 32 位相乘再传入 64 位 helper。
- 以当前 `1.05 GHz` parent 和 `51.2 MHz` target 为例，分频应约为 `20.508`。若 32 位溢出，计算会退化为约 `0.040`，写入无效分频并导致黑屏、漂移或闪烁。
- 板端 `devmem` 写寄存器只能用于有恢复路径的临时诊断。它不能替代 Windows owner 修复、内核构建、镜像部署和冷启动。

<a id="lcd-build-acceptance"></a>
## 4. 最小构建与板端验收

1. 先在 Windows 核对 owner diff、64 位算术、实际 `system-top.dts` 路由和 source identity，只修改受影响的时钟驱动、LCD 驱动、设备树及必要 identity。
2. VM 只执行 Windows 无法完成的最小 ARM/Linux 内核、device-tree 与 PetaLinux 增量步骤；不得清理 kernel、tmp、sstate 或 downloads，也不得执行无关全量重建。
3. 单包通过后，只生成部署所需的 current `image.ub`/SD 启动产物；不得使用旧 SD、VM 手改源码或板端直接替换产品文件关闭任务。
4. 部署后执行冷启动，回读出镜 DT 的单一 `1024×600` native mode、`200 Hz`/`100%` 背光合同、clock-wizard lock、分频寄存器和 LCD/UI 就绪。
5. 完成上述技术回读后必须明确告知操作员“开始观察”，然后停止改变屏幕状态，等待操作员长时间观察并回复。未获得物理屏确认时最高状态为 `local_verified_only`；确认无闪烁后才允许 `board_verified`。
