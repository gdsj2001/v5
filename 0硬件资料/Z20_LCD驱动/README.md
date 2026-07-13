# Z20 LCD 驱动参考资料

## 来源

当前替换来源：

`\\sjnas\备份\正点原子开发板资料\【正点原子】Z20开发板资料盘(A盘)\4_Source_Code\3_Embeded_Linux\Linux驱动例程.tar`

压缩包内部来源为 `Linux驱动例程/24_lcd/`。该资料直接对应 Z20 开发板，替代此前来自领航者开发板 `25_lcd_disp` 的通用参考文件。

本目录保存与 RGB LCD 显示链直接相关的驱动源码和设备树参考输入：

- `drivers/`：从 Z20 LCD 例程及其配套 Linux 5.4 内核源码中提取的显示主驱动、时钟和背光驱动。
- `drivers/pwm-dglnt.c`：LCD 背光 PWM 相关驱动。
- `drivers/clk-xlnx-clock-wizard.c`：LCD 像素时钟相关驱动。
- `device-tree/`：原资料配套的 ZYNQ-7010、ZYNQ-7020 设备树参考文件。

文件以 NAS 中 Z20 专用例程为唯一导入来源；不再混用此前领航者开发板版本。

## 最新屏幕模块参考驱动

屏幕模块侧最新下载资料来自：

`D:\BaiduNetdiskDownload\【正点原子】7寸RGBLCD电容触摸屏模块1024600\【正点原子】7寸RGB LCD电容触摸屏模块资料800480、1024600\【正点原子】7寸RGB LCD电容触摸屏模块资料（新资料）`

其中可提取的源码是 STM32H743 的 `ATK_RGBLCD` BSP，保存到 `panel-reference/stm32-atk-rgblcd/`，只用于核对 1024x600 RGB 时序、背光和 FTxx/GTxx 触摸协议。它不是 Z20/Linux DRM 驱动，不能覆盖 `drivers/` 中的 Z20 Linux 驱动，也不能直接进入 V5 产品内核。

该下载资料明确对应 **7 寸** ATK-MD0700R（800x480/1024x600），而 V5 当前登记目标为 **10.1 寸 1024x600**。二者分辨率相同不代表 panel timing、背光或触摸硬件合同相同；正式产品参数仍必须由当前 10.1 寸屏规格和实际板端 readback/测量确认。

## V5 当前目标屏

V5 当前实际屏幕为 **10.1 寸、1024x600**。

原始 `xlnx_atk_lcd.c` 没有“10.1 寸 1024x600”这一完全匹配的硬件型号：其中 `ATK7016` 虽然同为 1024x600，但登记的是 7 寸屏；`ATK1018` 登记的是 10 寸 1280x800。因此不得仅按分辨率复用 `ATK7016`，也不得把 `ATK1018` 当作当前屏。正式移植必须从当前 10.1 寸屏规格书或实际硬件合同确认 pixel clock、水平/垂直 front porch、back porch、sync pulse、HSYNC/VSYNC/DE 极性、数据位宽、背光 PWM 极性和上电时序，再为当前屏建立唯一设备树 timing/readback 证据。

## 边界

这些文件当前仅为硬件资料和后续移植参考，不代表已经进入 V5 当前 Linux/PetaLinux 产品驱动 owner，也不能作为已完成编译、镜像或板端验证的证明。正式接入前必须核对当前 `linux/kernel/`、`board/petalinux/`、实际 DTS/XSA、LCD 接口时序、背光极性和许可证边界，再按当前产品 owner 修改与验证。

未复制原资料中的预编译 `BOOT.BIN`、`zImage`、`system.dtb`、`rootfs`、Vivado 压缩工程、测试产物以及完整 Linux 5.4 内核压缩包，避免把过程产物或第二套内核源码误放入硬件资料目录。
