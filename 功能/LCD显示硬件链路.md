# V5 LCD 显示硬件链路

<!-- AI_FAST_READ_BEGIN -->
owner_reqs: [REQ-LCD-PIXEL-CLOCK-STABILITY]
read_when: [LCD闪烁, LCD黑屏, LCD颜色异常, LCD驱动, 1024x600, 像素时钟, clock-wizard, 背光, system-top.dts]
truth: [system-top.dts单屏参数 -> xlnx_atk_lcd.c显示驱动 -> clk-xlnx-clock-wizard.c像素时钟驱动 -> PetaLinux current镜像 -> 板端寄存器回读与物理屏确认]
forbidden: [修改system-user.dtsi后声称已出镜, 重建已退休的Z20或STM32参考源码副本, 恢复LCD-ID选时序或fallback, 32位中间值计算GHz级时钟, DRM mode_set热重配像素时钟, RGB最高位复用GPIO保持输入或高阻, 未经物理屏确认声明board_verified]
readback: [DPI-1连接且1024x600, clock-wizard锁定, VCO与输出分频寄存器, RGB-MSB GPIO data=0且tri=0, 20kHz与80%背光, framebuffer与UI就绪, 操作员观察]
impact: [实际system-top.dts, xlnx_atk_lcd.c, clk-xlnx-clock-wizard.c, 最小内核与设备树构建, current镜像冷启动]
acceptance: [唯一1024x600 native timing, 正确驱动编入内核, 像素时钟约51.2MHz且不热重配, RGB最高三位被确定驱动, 背光20kHz与80%, 颜色正常且物理屏无闪烁]
detail_sections: [#lcd-contract, #lcd-drivers, #lcd-fix, #lcd-board-closure]
<!-- AI_FAST_READ_END -->

引用需求真源：`REQ-LCD-PIXEL-CLOCK-STABILITY`。

<a id="lcd-contract"></a>
## 1. 当前硬件与显示合同

- 实物是 7 寸屏来源的 LCD 驱动板、10.1 寸 `1024×600` 物理屏，以及 PS I2C1/`0x38` 上的 FT5x26 触摸。“7 寸”只表示驱动板来源，不是运行时选屏条件。
- 显示只保留一套模式：`1024×600`、像素时钟 `51.2 MHz`、水平总周期 `1344`、垂直总周期 `635`，刷新率约 `60 Hz`。
- 背光固定为 `20 kHz`（周期 `50,000 ns`）、`80%` 占空比。
- FPGA `rgb2lcd` 将原 LCD-ID 的三路 AXI GPIO 与 `R7/G7/B7` 最高位复用；运行期三路 GPIO 必须为输出低，即 data=`0`、tri=`0`，不得作为 LCD-ID 输入。

<a id="lcd-drivers"></a>
## 2. 实际使用的驱动

- **LCD/DPI 显示驱动**：`board/petalinux/project-spec/meta-user/recipes-kernel/linux/linux-xlnx/xlnx_atk_lcd.c`。编译后位于内核 `drivers/gpu/drm/xlnx/xlnx_atk_lcd.c`，Kconfig 是 `CONFIG_DRM_XLNX_ATK_LCD=y`，platform driver 名为 `atk-dpi`，设备树匹配串为 `atk,atk_dpi`。这是当前 LCD 接口、native mode、背光和 RGB-MSB GPIO 的实际驱动。
- **像素时钟驱动**：`linux/kernel/drivers/clk/clk-xlnx-clock-wizard.c`，Kconfig 是 `CONFIG_COMMON_CLK_XLNX_CLKWZRD`，设备树匹配 `xlnx,clocking-wizard`。LCD 使用其中 `0x43c30000` Clock Wizard 的 `lcd_pclk` 输出。
- **帧扫描链路**：设备树 `drm_pl_disp_lcd` 使用 Xilinx `xlnx,pl-disp` 和 framebuffer-read DMA，格式为 `BG24`，其输出连接到上述 `atk-dpi` 驱动。
- **唯一出镜设备树**：`board/petalinux/project-spec/meta-user/recipes-bsp/device-tree/files/system-top.dts`。其中 `atk_lcd_drm` 只保留一个 `1024×600` native timing，并保存 `51.2 MHz`、PWM 和 `rgb-msb-gpios` 参数。`system-user.dtsi` 不是当前产品输入；退役的 `0硬件资料/Z20_LCD驱动` 设备树、Linux 驱动和 STM32 参考源码副本已删除，不得重建第二 owner 或 fallback。

<a id="lcd-fix"></a>
## 3. 闪烁、黑屏和颜色异常的修复实现

- `clk-xlnx-clock-wizard.c` 原先在 32 位 ARM 上用 32 位中间值计算 GHz 级 VCO 和输出分频，发生溢出后会得到错误分频。现已在乘法前提升为 `u64`，使用 `DIV_ROUND_*_ULL`，并在存在小数部分时同时设置 `WZRD_CLKOUT0_FRAC_EN` 与 `WZRD_CLKFBOUT_FRAC_EN`。当前 `1.05 GHz / 51.2 MHz` 输出分频约为 `20.508`。
- `xlnx_atk_lcd.c` 只在 `atk_dpi_probe()` 中、DRM component 注册前执行一次 `clk_set_rate(51.2 MHz)`。`lcd_mode_valid()` 只接受设备树 native clock，`atk_dpi_mode_set()` 仅拒绝并记录不一致请求，不再热重配 Clock Wizard，避免 VTC、DMA 和视频输出在运行中失去同步。
- `atk_dpi_drive_rgb_msb_lanes()` 从 `rgb-msb-gpios` 取得三路 GPIO，并用 `GPIOF_OUT_INIT_LOW` 请求为输出低；任一路缺失或请求失败都停止 probe。该修复消除了 `R7/G7/B7` 最高位高阻造成的颜色错误和发白。
- `xlnx_atk_lcd.c` 只读取设备树 native mode、PWM 周期和 `backlight-duty-percent`。实际 `system-top.dts` 固定为 `51.2 MHz`、`20 kHz`、`80%`，驱动内没有第二套屏参数或 PWM fallback。

<a id="lcd-board-closure"></a>
## 4. 2026-07-13 最小部署与板端闭环

- 只增量构建受影响的 Linux 内核和 device-tree，生成一套 current 启动产物；没有清理 kernel、BitBake `tmp`、sstate 或 downloads。部署到 SD 后执行冷启动，未在板端直接修改产品文件。
- 冷启动根文件系统为 `/dev/mmcblk0p2`；DRM `DPI-1` 为 connected，模式为 `1024×600`，framebuffer/DMA 为宽 `1024`、高 `600`、stride `3072`、格式 `0x1d`，UI 进程运行。
- Clock Wizard status=`0x1`；VCO 配置寄存器=`0x05F40A01`，对应 `1.05 GHz`；输出配置寄存器=`0x0005FC14`，对应约 `20.508` 分频并启用小数输出，得到约 `51.2 MHz`。
- RGB-MSB AXI GPIO data=`0`、tri=`0`；背光 PWM period=`0x1388`、duty=`0x0FA0`，对应 `20 kHz`、`80%`。
- 操作员已确认物理 LCD 显示正常。该次源码、最小构建、部署、冷启动、寄存器回读和实际屏观察闭环状态为 `board_verified`。
