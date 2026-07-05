# Z20 v1.5 IO Owner Register Map

文件编码：UTF-8 without BOM。Linux 侧可复制的寄存器宏、offset 和示例文本不得带 BOM。

This file is the board-software-facing register map for `z20_v15_io_owner_axi_lite`. It is a hardware handoff aid only; it is not board IO validation proof.

## Block Identity

- RTL source: `rtl/z20_v15_io_owner_axi_lite.v`
- AXI block: `z20_v15_io_owner/S_AXI`
- Base address: `0x41270000`
- Range: `64K`
- AXI data width: `32`
- AXI address width: `C_S_AXI_ADDR_WIDTH=8`
- `MAGIC`: `0x494F4F57` / ASCII `IOOW`
- `VERSION`: `0x00010000`
- `BUILD_ID`: `0x20260701`
- Current closure state: `local_verified_only`

All register offsets below are byte offsets from `0x41270000`. The RTL decodes `S_AXI_*ADDR[7:2]`, so all registers are 32-bit word aligned.

## Register Table

| Offset | Name | Access | Reset/read value | Description |
| ---: | --- | --- | --- | --- |
| `0x00` | `MAGIC` | RO | `0x494F4F57` | ASCII `IOOW`. |
| `0x04` | `VERSION` | RO | `0x00010000` | IO owner register ABI version. |
| `0x08` | `DI` | RO | `0x00000000` after reset | Synchronized `DI1` - `DI18` inputs. |
| `0x0C` | `FR_DI` | RO | `0x00000000` after reset | Synchronized `FR_DI1` - `FR_DI16` inputs. |
| `0x10` | `MISC` | RO | `0x00000000` after reset | `TS_DI`, MPG, scale select, alarm, and touch interrupt status. |
| `0x14` | `DO` | RW | `0x00000000` | Normal owner for `DO1` - `DO14` before the top PL E-stop gate. |
| `0x18` | `AXIS_ENA` | RW | `0x00000000` | Normal owner for `ENA1` - `ENA8` before the top PL E-stop gate. |
| `0x1C` | `TOUCH_CTRL` | RW | `0x00000001` | Touch reset control; bit 0 drives `TP_RST_N`. |
| `0x20` | `PWM_CTRL` | RW | `0x00000000` | PWM channel enable bits before the top PL E-stop gate. |
| `0x24` | `PWM_PERIOD` | RW | `0x000186A0` | PWM period in `S_AXI_ACLK` ticks; reset value is `100000`. |
| `0x28` | `PWM0_DUTY` | RW | `0x00000000` | Duty for `PWM1` / `pwm_o[0]`. |
| `0x2C` | `PWM1_DUTY` | RW | `0x00000000` | Duty for `PWM2` / `pwm_o[1]`. |
| `0x30` | `OUT_STATUS` | RO | `0x01000000` after reset | Readback of normal output-owner registers and raw PWM state before the PL E-stop gate. |
| `0x34` | `BUILD_ID` | RO | `0x20260701` | Build identifier parameter from RTL. |

Writes to undefined offsets are ignored. Reads from undefined offsets return zero in the current RTL. RW registers honor AXI `WSTRB`; reserved bits should be written as zero.

## `DI` Bits

| Bits | Name | Meaning |
| ---: | --- | --- |
| `17:0` | `DI[17:0]` | `DI1` - `DI18`, with bit 0 = `DI1` and bit 17 = `DI18`. |
| `31:18` | `reserved` | Reads as zero. |

The inputs are two-stage synchronized into the AXI clock domain. No board-level polarity or debounce policy is proven by this register.

## `FR_DI` Bits

| Bits | Name | Meaning |
| ---: | --- | --- |
| `15:0` | `FR_DI[15:0]` | `FR_DI1` - `FR_DI16`, with bit 0 = `FR_DI1` and bit 15 = `FR_DI16`. |
| `31:16` | `reserved` | Reads as zero. |

The inputs are two-stage synchronized into the AXI clock domain. No board-level polarity or debounce policy is proven by this register.

## `MISC` Bits

| Bits | Name | Meaning |
| ---: | --- | --- |
| `0` | `TS_DI` | Synchronized tool-setter / touch-sense discrete input. |
| `8:1` | `MPG_AXIS_SEL[7:0]` | Handwheel axis select inputs, bit 1 = `AXIS_SEL0`, bit 8 = `AXIS_SEL7`. |
| `9` | `MPG_A` | Handwheel A phase input. |
| `10` | `MPG_B` | Handwheel B phase input. |
| `13:11` | `SCALE_SEL[2:0]` | Handwheel scale select, bit 11 = `SCALE_SEL0`, bit 13 = `SCALE_SEL2`. |
| `21:14` | `ALM[7:0]` | Axis alarm inputs, bit 14 = `ALM1`, bit 21 = `ALM8`. |
| `22` | `TP_INT` | Touch-controller interrupt input. |
| `31:23` | `reserved` | Reads as zero. |

These inputs are status inputs only. MPG decoding, input debounce, input polarity, and board behavior remain software/board-integration responsibilities.

## `DO` Bits

| Bits | Name | Meaning |
| ---: | --- | --- |
| `13:0` | `DO[13:0]` | Normal output register for `DO1` - `DO14`, with bit 0 = `DO1` and bit 13 = `DO14`. |
| `31:14` | `reserved` | Reads as zero; writes ignored by truncation. |

Reset value is zero. The top-level PL E-stop gate still forces these outputs low when E-stop is active, regardless of this register.

## `AXIS_ENA` Bits

| Bits | Name | Meaning |
| ---: | --- | --- |
| `7:0` | `ENA[7:0]` | Normal output register for `ENA1` - `ENA8`, with bit 0 = `ENA1` and bit 7 = `ENA8`. |
| `31:8` | `reserved` | Reads as zero; writes ignored by truncation. |

Reset value is zero. The top-level PL E-stop gate still forces these outputs low when E-stop is active. Board enable polarity is not board-validated by this handoff.

## `TOUCH_CTRL` Bits

| Bits | Name | Meaning |
| ---: | --- | --- |
| `0` | `TP_RST_N` | Touch reset output. Reset value `1` means released high; write `0` to assert reset low. |
| `31:1` | `reserved` | Reads as zero; writes ignored by truncation. |

## `PWM_CTRL` Bits

| Bits | Name | Meaning |
| ---: | --- | --- |
| `0` | `PWM1_ENABLE` | Enables raw `PWM1` generation from `PWM_PERIOD` and `PWM0_DUTY`. |
| `1` | `PWM2_ENABLE` | Enables raw `PWM2` generation from `PWM_PERIOD` and `PWM1_DUTY`. |
| `31:2` | `reserved` | Reads as zero; writes ignored by truncation. |

Reset value is zero, so both PWM outputs are off before the PL E-stop gate.

## PWM Timing Registers

- `PWM_PERIOD` is the shared period in `S_AXI_ACLK` ticks. If software writes zero, the RTL treats the effective period as `1`.
- Writing `PWM_PERIOD` resets the PWM counter to zero.
- `PWM0_DUTY` controls `PWM1`; `PWM1_DUTY` controls `PWM2`.
- With a channel enabled, duty `0` produces raw low; duty greater than or equal to the effective period produces raw high.
- The top-level PL E-stop gate still forces `PWM1` and `PWM2` low when E-stop is active, regardless of raw PWM state.

## `OUT_STATUS` Bits

| Bits | Name | Meaning |
| ---: | --- | --- |
| `13:0` | `DO[13:0]` | Readback of the normal `DO` register, before the PL E-stop gate. |
| `14` | `PWM1_RAW` | Raw `PWM1` state before the PL E-stop gate. |
| `15` | `PWM2_RAW` | Raw `PWM2` state before the PL E-stop gate. |
| `23:16` | `ENA[7:0]` | Readback of the normal axis enable register, before the PL E-stop gate. |
| `24` | `TP_RST_N` | Readback of touch reset output register. |
| `31:25` | `reserved` | Reads as zero. |

`OUT_STATUS` reports the normal owner state, not the final pin state after the top PL E-stop gate.

## Software Rules

- Poll `MAGIC`, `VERSION`, and `BUILD_ID` before binding the Linux driver or HAL component to this block.
- Treat all DO, PWM, and ENA reset values as safe low/off defaults.
- Do not bypass the top PL E-stop gate. Final output safety state must consider both this block and `pl_estop/S_AXI`.
- Do not claim DI/FR_DI/MPG/ALM polarity, filtering, or physical board behavior from this register map alone.
- Do not claim DO/PWM/ENA physical off polarity or load behavior from this register map alone.
