# Z20 v1.5 Port Mapping

## Purpose

This file records the current Z20 v1.5 active port mapping for the independent new Vivado project. It is an execution aid for A3. It is not permission to enable a signal in active constraints unless the decision column says the signal is ready and the active XDC records the source.

## Source Files

- Current wrapper: `z20_v1_5_hw_project.srcs/sources_1/imports/hdl/system_wrapper.v`
- v1.5 constraint source: `../../z20-v1_5_20260623.xdc`
- Current active constraints: `constraints/z20_v1_5_active_mapped.xdc`

## Summary

- New v1.5 pin assignments: 194
- New v1.5 duplicate ports: 0
- New v1.5 duplicate pins: 0
- Current active package-pin assignments: 176

## Current Active XDC Subset

Only these ports are currently enabled in `constraints/z20_v1_5_active_mapped.xdc`. Every other v1.5 net below remains fail-closed until explicitly promoted.

| Active wrapper port | Source v1.5 net | Pin | Wrapper exists |
| --- | --- | --- | --- |
| lcd_bl[0] | LCD_BL | AA6 | yes |
| lcd_clk | LCD_CLK | V9 | yes |
| lcd_de | LCD_DE | V5 | yes |
| lcd_hs | LCD_HSYNC | V4 | yes |
| lcd_rst[0] | LCD_RST | AB9 | yes |
| lcd_vs | LCD_VSYNC | V10 | yes |
| lcd_rgb_tri_io[0] | LCD_B0 | R6 | yes |
| lcd_rgb_tri_io[1] | LCD_B1 | T6 | yes |
| lcd_rgb_tri_io[2] | LCD_B2 | U4 | yes |
| lcd_rgb_tri_io[3] | LCD_B3 | T4 | yes |
| lcd_rgb_tri_io[4] | LCD_B4 | V8 | yes |
| lcd_rgb_tri_io[5] | LCD_B5 | W8 | yes |
| lcd_rgb_tri_io[6] | LCD_B6 | W11 | yes |
| lcd_rgb_tri_io[7] | LCD_B7 | W10 | yes |
| lcd_rgb_tri_io[8] | LCD_G0 | AA9 | yes |
| lcd_rgb_tri_io[9] | LCD_G1 | AA8 | yes |
| lcd_rgb_tri_io[10] | LCD_G2 | AB2 | yes |
| lcd_rgb_tri_io[11] | LCD_G3 | AB1 | yes |
| lcd_rgb_tri_io[12] | LCD_G4 | W6 | yes |
| lcd_rgb_tri_io[13] | LCD_G5 | W5 | yes |
| lcd_rgb_tri_io[14] | LCD_G6 | U12 | yes |
| lcd_rgb_tri_io[15] | LCD_G7 | U11 | yes |
| lcd_rgb_tri_io[16] | LCD_R0 | AB7 | yes |
| lcd_rgb_tri_io[17] | LCD_R1 | AB6 | yes |
| lcd_rgb_tri_io[18] | LCD_R2 | AB5 | yes |
| lcd_rgb_tri_io[19] | LCD_R3 | AB4 | yes |
| lcd_rgb_tri_io[20] | LCD_R4 | Y11 | yes |
| lcd_rgb_tri_io[21] | LCD_R5 | Y10 | yes |
| lcd_rgb_tri_io[22] | LCD_R6 | Y9 | yes |
| lcd_rgb_tri_io[23] | LCD_R7 | Y8 | yes |
| mdio_mdc | GMAC2_MDC | E19 | yes |
| mdio_mdio_io | GMAC2_MDIO | E20 | yes |
| rgmii_rxc | GMAC2_RXCLK | D18 | yes |
| rgmii_rx_ctl | GMAC2_RXCTL | C19 | yes |
| rgmii_rd[0] | GMAC2_RXD0 | E15 | yes |
| rgmii_rd[1] | GMAC2_RXD1 | D15 | yes |
| rgmii_rd[2] | GMAC2_RXD2 | F16 | yes |
| rgmii_rd[3] | GMAC2_RXD3 | E16 | yes |
| rgmii_txc | GMAC2_TXCLK | B15 | yes |
| rgmii_tx_ctl | GMAC2_TXCTL | C15 | yes |
| rgmii_td[0] | GMAC2_TXD0 | G16 | yes |
| rgmii_td[1] | GMAC2_TXD1 | G15 | yes |
| rgmii_td[2] | GMAC2_TXD2 | D17 | yes |
| rgmii_td[3] | GMAC2_TXD3 | D16 | yes |
| estop_nc_in | EGS_DI | AA19 | top shell |
| BEEP_EN | BEEP_EN | H18 | top shell |
| do_out[0] | DO1 | A21 | yes |
| do_out[1] | DO2 | A22 | yes |
| do_out[2] | DO3 | B21 | yes |
| do_out[3] | DO4 | B22 | yes |
| do_out[4] | DO5 | C22 | yes |
| do_out[5] | DO6 | D22 | yes |
| do_out[6] | DO7 | F21 | yes |
| do_out[7] | DO8 | F22 | yes |
| do_out[8] | DO9 | F19 | yes |
| do_out[9] | DO10 | G19 | yes |
| do_out[10] | DO11 | F17 | yes |
| do_out[11] | DO12 | G17 | yes |
| do_out[12] | DO13 | G21 | yes |
| do_out[13] | DO14 | G20 | yes |
| pwm_out[0] | PWM1 | G22 | yes |
| pwm_out[1] | PWM2 | H22 | yes |
| DI1 | DI1 | V13 | top shell |
| DI2 | DI2 | W13 | top shell |
| DI3 | DI3 | W17 | top shell |
| DI4 | DI4 | W18 | top shell |
| DI5 | DI5 | V15 | top shell |
| DI6 | DI6 | V14 | top shell |
| DI7 | DI7 | U15 | top shell |
| DI8 | DI8 | U16 | top shell |
| DI9 | DI9 | U21 | top shell |
| DI10 | DI10 | T21 | top shell |
| DI11 | DI11 | V18 | top shell |
| DI12 | DI12 | V19 | top shell |
| DI13 | DI13 | Y18 | top shell |
| DI14 | DI14 | AA18 | top shell |
| DI15 | DI15 | Y13 | top shell |
| DI16 | DI16 | AA13 | top shell |
| DI17 | DI17 | Y21 | top shell |
| DI18 | DI18 | Y20 | top shell |
| FR_DI1 | FR_DI1 | W15 | top shell |
| FR_DI2 | FR_DI2 | Y15 | top shell |
| FR_DI3 | FR_DI3 | AA14 | top shell |
| FR_DI4 | FR_DI4 | Y14 | top shell |
| FR_DI5 | FR_DI5 | W16 | top shell |
| FR_DI6 | FR_DI6 | Y16 | top shell |
| FR_DI7 | FR_DI7 | V17 | top shell |
| FR_DI8 | FR_DI8 | U17 | top shell |
| FR_DI9 | FR_DI9 | AB14 | top shell |
| FR_DI10 | FR_DI10 | AB15 | top shell |
| FR_DI11 | FR_DI11 | AB16 | top shell |
| FR_DI12 | FR_DI12 | AA16 | top shell |
| FR_DI13 | FR_DI13 | AB17 | top shell |
| FR_DI14 | FR_DI14 | AA17 | top shell |
| FR_DI15 | FR_DI15 | AB19 | top shell |
| FR_DI16 | FR_DI16 | AB20 | top shell |
| TS_DI | TS_DI | Y19 | top shell |
| mpg_axis_sel[0] | AXIS_SEL0 | C17 | yes |
| mpg_axis_sel[1] | AXIS_SEL1 | C18 | yes |
| mpg_axis_sel[2] | AXIS_SEL2 | B19 | yes |
| mpg_axis_sel[3] | AXIS_SEL3 | B20 | yes |
| mpg_axis_sel[4] | AXIS_SEL4 | H19 | yes |
| mpg_axis_sel[5] | AXIS_SEL5 | H20 | yes |
| mpg_axis_sel[6] | AXIS_SEL6 | B16 | yes |
| mpg_axis_sel[7] | AXIS_SEL7 | B17 | yes |
| MPG_A | MPG_A | C20 | top shell |
| MPG_B | MPG_B | D20 | top shell |
| SCALE_SEL0 | SCALE_SEL0 | D21 | top shell |
| SCALE_SEL1 | SCALE_SEL1 | E21 | top shell |
| SCALE_SEL2 | SCALE_SEL2 | H17 | top shell |
| PULS1_IO | PULS1_IO | J17 | top shell |
| PULS2_IO | PULS2_IO | M16 | top shell |
| PULS3_IO | PULS3_IO | K15 | top shell |
| PULS4_IO | PULS4_IO | P18 | top shell |
| PULS5_IO | PULS5_IO | M20 | top shell |
| PULS6_IO | PULS6_IO | R18 | top shell |
| PULS7_IO | PULS7_IO | R16 | top shell |
| PULS8_IO | PULS8_IO | AA21 | top shell |
| DIR1_IO | DIR1_IO | J16 | top shell |
| DIR2_IO | DIR2_IO | N17 | top shell |
| DIR3_IO | DIR3_IO | J15 | top shell |
| DIR4_IO | DIR4_IO | M17 | top shell |
| DIR5_IO | DIR5_IO | M19 | top shell |
| DIR6_IO | DIR6_IO | T18 | top shell |
| DIR7_IO | DIR7_IO | P16 | top shell |
| DIR8_IO | DIR8_IO | AB22 | top shell |
| ENA1_IO | ENA1_IO | J18 | top shell |
| ENA2_IO | ENA2_IO | N18 | top shell |
| ENA3_IO | ENA3_IO | P15 | top shell |
| ENA4_IO | ENA4_IO | L17 | top shell |
| ENA5_IO | ENA5_IO | R19 | top shell |
| ENA6_IO | ENA6_IO | T16 | top shell |
| ENA7_IO | ENA7_IO | W20 | top shell |
| ENA8_IO | ENA8_IO | AA22 | top shell |
| ALM1_IO | ALM1_IO | K18 | top shell |
| ALM2_IO | ALM2_IO | K20 | top shell |
| ALM3_IO | ALM3_IO | N15 | top shell |
| ALM4_IO | ALM4_IO | N20 | top shell |
| ALM5_IO | ALM5_IO | T19 | top shell |
| ALM6_IO | ALM6_IO | T17 | top shell |
| ALM7_IO | ALM7_IO | W21 | top shell |
| ALM8_IO | ALM8_IO | W22 | top shell |
| A1_IO | A1_IO | K16 | top shell |
| B1_IO | B1_IO | L16 | top shell |
| Z1_IO | Z1_IO | M15 | top shell |
| A2_IO | A2_IO | K19 | top shell |
| B2_IO | B2_IO | J20 | top shell |
| Z2_IO | Z2_IO | K21 | top shell |
| A3_IO | A3_IO | J21 | top shell |
| B3_IO | B3_IO | J22 | top shell |
| Z3_IO | Z3_IO | P17 | top shell |
| A4_IO | A4_IO | N19 | top shell |
| B4_IO | B4_IO | P20 | top shell |
| Z4_IO | Z4_IO | P21 | top shell |
| A5_IO | A5_IO | L21 | top shell |
| B5_IO | B5_IO | L22 | top shell |
| Z5_IO | Z5_IO | P22 | top shell |
| A6_IO | A6_IO | N22 | top shell |
| B6_IO | B6_IO | R20 | top shell |
| Z6_IO | Z6_IO | R21 | top shell |
| A7_IO | A7_IO | V20 | top shell |
| B7_IO | B7_IO | U20 | top shell |
| Z7_IO | Z7_IO | AB21 | top shell |
| A8_IO | A8_IO | V22 | top shell |
| B8_IO | B8_IO | U22 | top shell |
| Z8_IO | Z8_IO | T22 | top shell |
| can0_rx | CAN_FPGA_RX | M22 | yes |
| can0_tx | CAN_FPGA_TX | M21 | yes |
| i2c0_scl_io | I2C3_SCL | A18 | yes |
| i2c0_sda_io | I2C3_SDA | A19 | yes |
| i2c1_scl_io | I2C_SCL_TP | Y4 | yes |
| i2c1_sda_io | I2C_SDA_TP | AA7 | yes |
| pl_uart_rxd | PL_UART_RX | F18 | yes |
| pl_uart_txd | PL_UART_TX | E18 | yes |
| rs232_rxd | RS232_FPGA_RX | A17 | yes |
| rs232_txd | RS232_FPGA_TX | A16 | yes |
| RS485_FPGA_RX | RS485_FPGA_RX | U14 | top shell -> PS UART1 EMIO |
| RS485_FPGA_TX | RS485_FPGA_TX | R7 | top shell -> PS UART1 EMIO |
| TP_INT | TP_INT | AA4 | top shell -> IO owner status input |
| TP_RST | TP_RST | W7 | top shell <- IO owner reset output |

## ADC One-Channel XADC Mapping

The current v1.5 constraint source defines one ADC channel on the dedicated XADC VP/VN analog pair. `ADC_IN1` uses `XADC_VP`/`XADC_VN`; `ADC_IN2` is not assigned in this revision.

Current BD/wrapper status:

- `XADC_VP`/`XADC_VN` are dedicated analog package pins and must not be constrained as normal PL `PACKAGE_PIN` rows in active XDC.
- `XADC_VP / ADC_P` is package `L11`; `XADC_VN / ADC_N` is package `M12`.
- ADC SPI through MCP3202 is retired for the ADC function. The BD, generated wrapper, and `system_top.v` no longer expose `adc_spi_*` as board-level ports.
- `U10`, `U9`, `AA12`, and `AB12` are no longer active ADC pins. The v1.5 source XDC restores them as `FPGA1_IO1_P`, `FPGA1_IO1_N`, `FPGA1_IO2_P`, and `FPGA1_IO2_N` spare nets; the current active XDC leaves those spare nets unassigned.

| ADC item | Source v1.5 net | Pin | Current status |
| --- | --- | --- | --- |
| `ADC_IN1+` | XADC_VP / ADC_P | L11 | dedicated analog pin, no PL `PACKAGE_PIN` |
| `ADC_IN1-` | XADC_VN / ADC_N | M12 | dedicated analog pin, no PL `PACKAGE_PIN` |
## Immediate A3 Decisions

- Active XDC is limited to current `system_top` and generated wrapper ports that exist, with source comments pointing back to `../../z20-v1_5_20260623.xdc`.
- Axis boundary is now the current 8-axis DB15 model. `PULS1-8` and `DIR1-8` are driven from wrapper `axis_puls_o[7:0]` and `axis_dir_o[7:0]` through the top E-stop gate.
- Encoder receiver-output pins `A1_IO/B1_IO/Z1_IO` through `A8_IO/B8_IO/Z8_IO` feed wrapper `axis_enc_a_i[7:0]`, `axis_enc_b_i[7:0]`, and `axis_enc_z_i[7:0]`, which connect to BD `step_ip`.
- `ENA1-8` are active top-level ports driven by wrapper `axis_ena_o[7:0]`, now sourced from `z20_v15_io_owner_axi_lite` at AXI base `0x41270000` and still forced low by the top E-stop gate when `estop_hw_active` is true.
- `ALM1-8`, `DI1-18`, `FR_DI1-16`, `TS_DI`, MPG, scale select, and `TP_INT` feed `z20_v15_io_owner_axi_lite` input synchronizers/status registers. They are no longer keep-only top inputs.
- `DO1` - `DO14` and `PWM1` - `PWM2` are current top-level outputs through `pl_estop_general_output_gate`; normal output owner is now `z20_v15_io_owner_axi_lite` (`do_o[13:0]` and two PWM channels) before that gate.
- ADC is one channel on `XADC_VP/XADC_VN` only. Do not add normal PL `PACKAGE_PIN` constraints for `XADC_VP`/`XADC_VN`, and do not keep `adc_spi_*` as board-level ports.
- `FPGA1_IO1_P/N` and `FPGA1_IO2_P/N` on `U10/U9/AA12/AB12` are source-XDC spare nets and remain inactive in active XDC until a non-ADC owner is defined.
- HDMI/DVI is abandoned. `AXIS_SEL0` - `AXIS_SEL7`, `MPG_A`, `MPG_B`, and `SCALE_SEL0-2` are DB25 MPG inputs.
- RS485 is exported by current top ports `RS485_FPGA_RX`/`RS485_FPGA_TX`, constrained to `U14/R7`, and connected to wrapper `rs485_rxd/rs485_txd` for PS UART1 EMIO. Board serial validation is not run.
- Touch I2C is active through `i2c1_scl_io=Y4` and `i2c1_sda_io=AA7`; `TP_INT=AA4` feeds the IO owner status path and `TP_RST=W7` is driven by the IO owner touch reset register, default released high.

## Current Functional Gaps

| Group | Current Vivado state | Gap that must not be hidden |
| --- | --- | --- |
| Axis PULS/DIR | 8-bit BD/wrapper/top path connected and E-stop gated | Motion owner, polarity, and board behavior are not board-verified. |
| Axis ENA | `z20_v15_io_owner_axi_lite` drives `axis_ena_o[7:0]`; top E-stop gate still forces low on E-stop | Enable polarity and board behavior are not board-verified. |
| Axis encoder ABZ | 8-bit input path connected into `step_ip` | Software/register readout, axis numbering, and direction are not reviewed. |
| ALM inputs | Feed IO owner input synchronizers/status registers | Alarm polarity and board behavior are not board-verified. |
| DI/FR_DI/TS_DI | Feed IO owner input synchronizers/status registers; `EGS_DI` feeds PL E-stop input | Input polarity, debounce/filtering policy, and board behavior are not board-verified. |
| MPG/SCALE | Feed IO owner input synchronizers/status registers | MPG decoding and board behavior are not board-verified. |
| DO/PWM | IO owner drives normal DO/PWM, then top PL E-stop gate forces safe low | Output off polarity/load behavior and board forced-off behavior are not board-verified. |
| RS485 | Exported through PS UART1 EMIO to `RS485_FPGA_RX/TX` | Board serial validation and duplicate `B13_IO_0` source-label review remain open. |
| TP_INT/TP_RST | `TP_INT` feeds IO owner status, `TP_RST` is IO owner reset output | Touch-controller board validation is not run. |

## Remaining Bitstream DRC Closure Matrix

`scripts/check_active_xdc.ps1` is the local source-side check for top-level port closure. Current expected output is `top_module=system_top`, `active_assignments=180 missing=[] duplicate_ports={} duplicate_pins={}`, and `unassigned_top_ports_count=0`.

`scripts/export_remaining_drc_ports.ps1` exports `docs/remaining_drc_ports.csv`. The current CSV is header-only with `csv_rows=0`; it no longer requires an old-project constraint reference file.

`scripts/export_active_pin_conflicts.ps1` derives `docs/active_pin_conflicts.md` from the CSV. After the current IO update it reports `active_pin_conflicts=0`.

Vivado flow current status:

1. Current active-top DRC is closed for this local build: `unassigned_top_ports_count=0`, `csv_rows=0`, and `active_pin_conflicts=0`.
2. Local Vivado/XSA artifacts must be regenerated after RTL/BD changes before any fresh handoff claim.
3. Board-facing behavior remains `local_verified_only` until real board/operator evidence exists.

Safety-chain next actions before any real output connection:

1. Confirm whether `EGS_DI` is the physical NC emergency-stop chain.
2. Confirm whether STO is wired to a dedicated output, one of `DO1` - `DO14`, `PWM1/PWM2`, or axis `ENA*_IO`.
3. Confirm the Z-axis or vertical-axis brake output, polarity, and required lead time.
4. Confirm every general output off polarity for `DO1` - `DO14` and `PWM1` - `PWM2`, then keep each confirmed channel upstream of the existing PL-side gate point.
5. Identify any additional bus path that must be inhibited by physical E-stop, then choose the exact TX send enable, driver-enable, TX idle, or TX_CTL gate point. The implementation must preserve link/clock/reset/RX and define queued-TX invalidation.
6. Only after the above are closed, promote confirmed E-stop/STO/brake/DO/PWM/bus-gate nets into active XDC and RTL.

## Verification Required Before Any Pass Claim

Run the local gates from the project root and do not claim `board_verified` from these local checks alone.
