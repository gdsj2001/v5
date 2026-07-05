## Active constraints for the new Z20 v1.5 project.
## Physical pin assignments are derived from ../z20-v1_5_20260623.xdc.
## Timing-only exceptions are allowed only when they target current BD cells and
## do not reintroduce old-project pin assignments.
## Only current system_wrapper ports with confirmed physical-pin mapping are enabled.
## All other Z20 v1.5 schematic nets remain fail-closed in docs/port_mapping.md.

## LCD wrapper-adapted ports
# Source: ../z20-v1_5_20260623.xdc, LCD_BL package AA6 -> current wrapper lcd_bl[0]
set_property PACKAGE_PIN AA6 [get_ports {lcd_bl[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_bl[0]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_CLK package V9 -> current wrapper lcd_clk
set_property PACKAGE_PIN V9 [get_ports {lcd_clk}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_clk}]

# Source: ../z20-v1_5_20260623.xdc, LCD_DE package V5 -> current wrapper lcd_de
set_property PACKAGE_PIN V5 [get_ports {lcd_de}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_de}]

# Source: ../z20-v1_5_20260623.xdc, LCD_HSYNC package V4 -> current wrapper lcd_hs
set_property PACKAGE_PIN V4 [get_ports {lcd_hs}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_hs}]

# Source: ../z20-v1_5_20260623.xdc, LCD_RST package AB9 -> current wrapper lcd_rst[0]
set_property PACKAGE_PIN AB9 [get_ports {lcd_rst[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rst[0]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_VSYNC package V10 -> current wrapper lcd_vs
set_property PACKAGE_PIN V10 [get_ports {lcd_vs}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_vs}]

## LCD RGB wrapper-adapted ports
# Source: ../z20-v1_5_20260623.xdc, LCD_B0 package R6 -> current wrapper lcd_rgb_tri_io[0]
set_property PACKAGE_PIN R6 [get_ports {lcd_rgb_tri_io[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[0]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_B1 package T6 -> current wrapper lcd_rgb_tri_io[1]
set_property PACKAGE_PIN T6 [get_ports {lcd_rgb_tri_io[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[1]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_B2 package U4 -> current wrapper lcd_rgb_tri_io[2]
set_property PACKAGE_PIN U4 [get_ports {lcd_rgb_tri_io[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[2]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_B3 package T4 -> current wrapper lcd_rgb_tri_io[3]
set_property PACKAGE_PIN T4 [get_ports {lcd_rgb_tri_io[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[3]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_B4 package V8 -> current wrapper lcd_rgb_tri_io[4]
set_property PACKAGE_PIN V8 [get_ports {lcd_rgb_tri_io[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[4]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_B5 package W8 -> current wrapper lcd_rgb_tri_io[5]
set_property PACKAGE_PIN W8 [get_ports {lcd_rgb_tri_io[5]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[5]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_B6 package W11 -> current wrapper lcd_rgb_tri_io[6]
set_property PACKAGE_PIN W11 [get_ports {lcd_rgb_tri_io[6]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[6]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_B7 package W10 -> current wrapper lcd_rgb_tri_io[7]
set_property PACKAGE_PIN W10 [get_ports {lcd_rgb_tri_io[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[7]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_G0 package AA9 -> current wrapper lcd_rgb_tri_io[8]
set_property PACKAGE_PIN AA9 [get_ports {lcd_rgb_tri_io[8]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[8]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_G1 package AA8 -> current wrapper lcd_rgb_tri_io[9]
set_property PACKAGE_PIN AA8 [get_ports {lcd_rgb_tri_io[9]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[9]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_G2 package AB2 -> current wrapper lcd_rgb_tri_io[10]
set_property PACKAGE_PIN AB2 [get_ports {lcd_rgb_tri_io[10]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[10]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_G3 package AB1 -> current wrapper lcd_rgb_tri_io[11]
set_property PACKAGE_PIN AB1 [get_ports {lcd_rgb_tri_io[11]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[11]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_G4 package W6 -> current wrapper lcd_rgb_tri_io[12]
set_property PACKAGE_PIN W6 [get_ports {lcd_rgb_tri_io[12]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[12]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_G5 package W5 -> current wrapper lcd_rgb_tri_io[13]
set_property PACKAGE_PIN W5 [get_ports {lcd_rgb_tri_io[13]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[13]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_G6 package U12 -> current wrapper lcd_rgb_tri_io[14]
set_property PACKAGE_PIN U12 [get_ports {lcd_rgb_tri_io[14]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[14]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_G7 package U11 -> current wrapper lcd_rgb_tri_io[15]
set_property PACKAGE_PIN U11 [get_ports {lcd_rgb_tri_io[15]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[15]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_R0 package AB7 -> current wrapper lcd_rgb_tri_io[16]
set_property PACKAGE_PIN AB7 [get_ports {lcd_rgb_tri_io[16]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[16]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_R1 package AB6 -> current wrapper lcd_rgb_tri_io[17]
set_property PACKAGE_PIN AB6 [get_ports {lcd_rgb_tri_io[17]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[17]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_R2 package AB5 -> current wrapper lcd_rgb_tri_io[18]
set_property PACKAGE_PIN AB5 [get_ports {lcd_rgb_tri_io[18]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[18]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_R3 package AB4 -> current wrapper lcd_rgb_tri_io[19]
set_property PACKAGE_PIN AB4 [get_ports {lcd_rgb_tri_io[19]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[19]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_R4 package Y11 -> current wrapper lcd_rgb_tri_io[20]
set_property PACKAGE_PIN Y11 [get_ports {lcd_rgb_tri_io[20]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[20]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_R5 package Y10 -> current wrapper lcd_rgb_tri_io[21]
set_property PACKAGE_PIN Y10 [get_ports {lcd_rgb_tri_io[21]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[21]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_R6 package Y9 -> current wrapper lcd_rgb_tri_io[22]
set_property PACKAGE_PIN Y9 [get_ports {lcd_rgb_tri_io[22]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[22]}]

# Source: ../z20-v1_5_20260623.xdc, LCD_R7 package Y8 -> current wrapper lcd_rgb_tri_io[23]
set_property PACKAGE_PIN Y8 [get_ports {lcd_rgb_tri_io[23]}]
set_property IOSTANDARD LVCMOS33 [get_ports {lcd_rgb_tri_io[23]}]

## RGMII/MDIO wrapper-adapted ports
# Source: ../z20-v1_5_20260623.xdc, GMAC2_MDC package E19 -> current wrapper mdio_mdc
set_property PACKAGE_PIN E19 [get_ports {mdio_mdc}]
set_property IOSTANDARD LVCMOS33 [get_ports {mdio_mdc}]

# Source: ../z20-v1_5_20260623.xdc, GMAC2_MDIO package E20 -> current wrapper mdio_mdio_io
set_property PACKAGE_PIN E20 [get_ports {mdio_mdio_io}]
set_property IOSTANDARD LVCMOS33 [get_ports {mdio_mdio_io}]

# Source: ../z20-v1_5_20260623.xdc, GMAC2_RXCLK package D18 -> current wrapper rgmii_rxc
set_property PACKAGE_PIN D18 [get_ports {rgmii_rxc}]
set_property IOSTANDARD LVCMOS33 [get_ports {rgmii_rxc}]

# Source: ../z20-v1_5_20260623.xdc, GMAC2_RXCTL package C19 -> current wrapper rgmii_rx_ctl
set_property PACKAGE_PIN C19 [get_ports {rgmii_rx_ctl}]
set_property IOSTANDARD LVCMOS33 [get_ports {rgmii_rx_ctl}]

# Source: ../z20-v1_5_20260623.xdc, GMAC2_RXD0 package E15 -> current wrapper rgmii_rd[0]
set_property PACKAGE_PIN E15 [get_ports {rgmii_rd[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {rgmii_rd[0]}]

# Source: ../z20-v1_5_20260623.xdc, GMAC2_RXD1 package D15 -> current wrapper rgmii_rd[1]
set_property PACKAGE_PIN D15 [get_ports {rgmii_rd[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {rgmii_rd[1]}]

# Source: ../z20-v1_5_20260623.xdc, GMAC2_RXD2 package F16 -> current wrapper rgmii_rd[2]
set_property PACKAGE_PIN F16 [get_ports {rgmii_rd[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {rgmii_rd[2]}]

# Source: ../z20-v1_5_20260623.xdc, GMAC2_RXD3 package E16 -> current wrapper rgmii_rd[3]
set_property PACKAGE_PIN E16 [get_ports {rgmii_rd[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {rgmii_rd[3]}]

# Source: ../z20-v1_5_20260623.xdc, GMAC2_TXCLK package B15 -> current wrapper rgmii_txc
set_property PACKAGE_PIN B15 [get_ports {rgmii_txc}]
set_property IOSTANDARD LVCMOS33 [get_ports {rgmii_txc}]

# Source: ../z20-v1_5_20260623.xdc, GMAC2_TXCTL package C15 -> current wrapper rgmii_tx_ctl
set_property PACKAGE_PIN C15 [get_ports {rgmii_tx_ctl}]
set_property IOSTANDARD LVCMOS33 [get_ports {rgmii_tx_ctl}]

# Source: ../z20-v1_5_20260623.xdc, GMAC2_TXD0 package G16 -> current wrapper rgmii_td[0]
set_property PACKAGE_PIN G16 [get_ports {rgmii_td[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {rgmii_td[0]}]

# Source: ../z20-v1_5_20260623.xdc, GMAC2_TXD1 package G15 -> current wrapper rgmii_td[1]
set_property PACKAGE_PIN G15 [get_ports {rgmii_td[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {rgmii_td[1]}]

# Source: ../z20-v1_5_20260623.xdc, GMAC2_TXD2 package D17 -> current wrapper rgmii_td[2]
set_property PACKAGE_PIN D17 [get_ports {rgmii_td[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {rgmii_td[2]}]

# Source: ../z20-v1_5_20260623.xdc, GMAC2_TXD3 package D16 -> current wrapper rgmii_td[3]
set_property PACKAGE_PIN D16 [get_ports {rgmii_td[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {rgmii_td[3]}]

## RGMII timing adapted to current wrapper names
# Source: ../z20-v1_5_20260623.xdc creates GMAC2_RXCLK_125m at 8 ns;
# Current wrapper timing is owned by this mapped active XDC.
create_clock -period 8.000 -name rgmii_rxc [get_ports {rgmii_rxc}]
create_generated_clock -name rgmii_txc_fwd \
  -source [get_ports {rgmii_rxc}] \
  -divide_by 1 \
  [get_ports {rgmii_txc}]

set_input_delay -clock rgmii_rxc -max 2.0 [get_ports {rgmii_rd[*] rgmii_rx_ctl}]
set_input_delay -clock rgmii_rxc -min 1.8 [get_ports {rgmii_rd[*] rgmii_rx_ctl}]
set_input_delay -clock rgmii_rxc -clock_fall -max 2.0 -add_delay [get_ports {rgmii_rd[*] rgmii_rx_ctl}]
set_input_delay -clock rgmii_rxc -clock_fall -min 1.8 -add_delay [get_ports {rgmii_rd[*] rgmii_rx_ctl}]

set_output_delay -clock rgmii_txc_fwd -max 2.0 [get_ports {rgmii_td[*] rgmii_tx_ctl}]
set_output_delay -clock rgmii_txc_fwd -min 0.5 [get_ports {rgmii_td[*] rgmii_tx_ctl}]
set_output_delay -clock rgmii_txc_fwd -clock_fall -max 2.0 -add_delay [get_ports {rgmii_td[*] rgmii_tx_ctl}]
set_output_delay -clock rgmii_txc_fwd -clock_fall -min 0.5 -add_delay [get_ports {rgmii_td[*] rgmii_tx_ctl}]

## PL E-stop hard input and hard-gated general outputs
# Source: ../z20-v1_5_20260623.xdc, EGS_DI package AA19 -> current wrapper estop_nc_in
set_property PACKAGE_PIN AA19 [get_ports {estop_nc_in}]
set_property IOSTANDARD LVCMOS33 [get_ports {estop_nc_in}]

# Source: ../z20-v1_5_20260623.xdc, BEEP_EN package H18 -> current wrapper BEEP_EN
set_property PACKAGE_PIN H18 [get_ports {BEEP_EN}]
set_property IOSTANDARD LVCMOS33 [get_ports {BEEP_EN}]

# Source: ../z20-v1_5_20260623.xdc, DO1 package A21 -> current wrapper do_out[0]
set_property PACKAGE_PIN A21 [get_ports {do_out[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {do_out[0]}]

# Source: ../z20-v1_5_20260623.xdc, DO2 package A22 -> current wrapper do_out[1]
set_property PACKAGE_PIN A22 [get_ports {do_out[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {do_out[1]}]

# Source: ../z20-v1_5_20260623.xdc, DO3 package B21 -> current wrapper do_out[2]
set_property PACKAGE_PIN B21 [get_ports {do_out[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {do_out[2]}]

# Source: ../z20-v1_5_20260623.xdc, DO4 package B22 -> current wrapper do_out[3]
set_property PACKAGE_PIN B22 [get_ports {do_out[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {do_out[3]}]

# Source: ../z20-v1_5_20260623.xdc, DO5 package C22 -> current wrapper do_out[4]
set_property PACKAGE_PIN C22 [get_ports {do_out[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {do_out[4]}]

# Source: ../z20-v1_5_20260623.xdc, DO6 package D22 -> current wrapper do_out[5]
set_property PACKAGE_PIN D22 [get_ports {do_out[5]}]
set_property IOSTANDARD LVCMOS33 [get_ports {do_out[5]}]

# Source: ../z20-v1_5_20260623.xdc, DO7 package F21 -> current wrapper do_out[6]
set_property PACKAGE_PIN F21 [get_ports {do_out[6]}]
set_property IOSTANDARD LVCMOS33 [get_ports {do_out[6]}]

# Source: ../z20-v1_5_20260623.xdc, DO8 package F22 -> current wrapper do_out[7]
set_property PACKAGE_PIN F22 [get_ports {do_out[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {do_out[7]}]

# Source: ../z20-v1_5_20260623.xdc, DO9 package F19 -> current wrapper do_out[8]
set_property PACKAGE_PIN F19 [get_ports {do_out[8]}]
set_property IOSTANDARD LVCMOS33 [get_ports {do_out[8]}]

# Source: ../z20-v1_5_20260623.xdc, DO10 package G19 -> current wrapper do_out[9]
set_property PACKAGE_PIN G19 [get_ports {do_out[9]}]
set_property IOSTANDARD LVCMOS33 [get_ports {do_out[9]}]

# Source: ../z20-v1_5_20260623.xdc, DO11 package F17 -> current wrapper do_out[10]
set_property PACKAGE_PIN F17 [get_ports {do_out[10]}]
set_property IOSTANDARD LVCMOS33 [get_ports {do_out[10]}]

# Source: ../z20-v1_5_20260623.xdc, DO12 package G17 -> current wrapper do_out[11]
set_property PACKAGE_PIN G17 [get_ports {do_out[11]}]
set_property IOSTANDARD LVCMOS33 [get_ports {do_out[11]}]

# Source: ../z20-v1_5_20260623.xdc, DO13 package G21 -> current wrapper do_out[12]
set_property PACKAGE_PIN G21 [get_ports {do_out[12]}]
set_property IOSTANDARD LVCMOS33 [get_ports {do_out[12]}]

# Source: ../z20-v1_5_20260623.xdc, DO14 package G20 -> current wrapper do_out[13]
set_property PACKAGE_PIN G20 [get_ports {do_out[13]}]
set_property IOSTANDARD LVCMOS33 [get_ports {do_out[13]}]

# Source: ../z20-v1_5_20260623.xdc, PWM1 package G22 -> current wrapper pwm_out[0]
set_property PACKAGE_PIN G22 [get_ports {pwm_out[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pwm_out[0]}]

# Source: ../z20-v1_5_20260623.xdc, PWM2 package H22 -> current wrapper pwm_out[1]
set_property PACKAGE_PIN H22 [get_ports {pwm_out[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pwm_out[1]}]

## Digital input terminal pins
# Source: ../z20-v1_5_20260623.xdc, DI1 package V13 -> current wrapper DI1
set_property PACKAGE_PIN V13 [get_ports {DI1}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI1}]

# Source: ../z20-v1_5_20260623.xdc, DI2 package W13 -> current wrapper DI2
set_property PACKAGE_PIN W13 [get_ports {DI2}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI2}]

# Source: ../z20-v1_5_20260623.xdc, DI3 package W17 -> current wrapper DI3
set_property PACKAGE_PIN W17 [get_ports {DI3}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI3}]

# Source: ../z20-v1_5_20260623.xdc, DI4 package W18 -> current wrapper DI4
set_property PACKAGE_PIN W18 [get_ports {DI4}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI4}]

# Source: ../z20-v1_5_20260623.xdc, DI5 package V15 -> current wrapper DI5
set_property PACKAGE_PIN V15 [get_ports {DI5}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI5}]

# Source: ../z20-v1_5_20260623.xdc, DI6 package V14 -> current wrapper DI6
set_property PACKAGE_PIN V14 [get_ports {DI6}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI6}]

# Source: ../z20-v1_5_20260623.xdc, DI7 package U15 -> current wrapper DI7
set_property PACKAGE_PIN U15 [get_ports {DI7}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI7}]

# Source: ../z20-v1_5_20260623.xdc, DI8 package U16 -> current wrapper DI8
set_property PACKAGE_PIN U16 [get_ports {DI8}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI8}]

# Source: ../z20-v1_5_20260623.xdc, DI9 package U21 -> current wrapper DI9
set_property PACKAGE_PIN U21 [get_ports {DI9}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI9}]

# Source: ../z20-v1_5_20260623.xdc, DI10 package T21 -> current wrapper DI10
set_property PACKAGE_PIN T21 [get_ports {DI10}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI10}]

# Source: ../z20-v1_5_20260623.xdc, DI11 package V18 -> current wrapper DI11
set_property PACKAGE_PIN V18 [get_ports {DI11}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI11}]

# Source: ../z20-v1_5_20260623.xdc, DI12 package V19 -> current wrapper DI12
set_property PACKAGE_PIN V19 [get_ports {DI12}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI12}]

# Source: ../z20-v1_5_20260623.xdc, DI13 package Y18 -> current wrapper DI13
set_property PACKAGE_PIN Y18 [get_ports {DI13}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI13}]

# Source: ../z20-v1_5_20260623.xdc, DI14 package AA18 -> current wrapper DI14
set_property PACKAGE_PIN AA18 [get_ports {DI14}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI14}]

# Source: ../z20-v1_5_20260623.xdc, DI15 package Y13 -> current wrapper DI15
set_property PACKAGE_PIN Y13 [get_ports {DI15}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI15}]

# Source: ../z20-v1_5_20260623.xdc, DI16 package AA13 -> current wrapper DI16
set_property PACKAGE_PIN AA13 [get_ports {DI16}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI16}]

# Source: ../z20-v1_5_20260623.xdc, DI17 package Y21 -> current wrapper DI17
set_property PACKAGE_PIN Y21 [get_ports {DI17}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI17}]

# Source: ../z20-v1_5_20260623.xdc, DI18 package Y20 -> current wrapper DI18
set_property PACKAGE_PIN Y20 [get_ports {DI18}]
set_property IOSTANDARD LVCMOS33 [get_ports {DI18}]

# Source: ../z20-v1_5_20260623.xdc, FR_DI1 package W15 -> current wrapper FR_DI1
set_property PACKAGE_PIN W15 [get_ports {FR_DI1}]
set_property IOSTANDARD LVCMOS33 [get_ports {FR_DI1}]

# Source: ../z20-v1_5_20260623.xdc, FR_DI2 package Y15 -> current wrapper FR_DI2
set_property PACKAGE_PIN Y15 [get_ports {FR_DI2}]
set_property IOSTANDARD LVCMOS33 [get_ports {FR_DI2}]

# Source: ../z20-v1_5_20260623.xdc, FR_DI3 package AA14 -> current wrapper FR_DI3
set_property PACKAGE_PIN AA14 [get_ports {FR_DI3}]
set_property IOSTANDARD LVCMOS33 [get_ports {FR_DI3}]

# Source: ../z20-v1_5_20260623.xdc, FR_DI4 package Y14 -> current wrapper FR_DI4
set_property PACKAGE_PIN Y14 [get_ports {FR_DI4}]
set_property IOSTANDARD LVCMOS33 [get_ports {FR_DI4}]

# Source: ../z20-v1_5_20260623.xdc, FR_DI5 package W16 -> current wrapper FR_DI5
set_property PACKAGE_PIN W16 [get_ports {FR_DI5}]
set_property IOSTANDARD LVCMOS33 [get_ports {FR_DI5}]

# Source: ../z20-v1_5_20260623.xdc, FR_DI6 package Y16 -> current wrapper FR_DI6
set_property PACKAGE_PIN Y16 [get_ports {FR_DI6}]
set_property IOSTANDARD LVCMOS33 [get_ports {FR_DI6}]

# Source: ../z20-v1_5_20260623.xdc, FR_DI7 package V17 -> current wrapper FR_DI7
set_property PACKAGE_PIN V17 [get_ports {FR_DI7}]
set_property IOSTANDARD LVCMOS33 [get_ports {FR_DI7}]

# Source: ../z20-v1_5_20260623.xdc, FR_DI8 package U17 -> current wrapper FR_DI8
set_property PACKAGE_PIN U17 [get_ports {FR_DI8}]
set_property IOSTANDARD LVCMOS33 [get_ports {FR_DI8}]

# Source: ../z20-v1_5_20260623.xdc, FR_DI9 package AB14 -> current wrapper FR_DI9
set_property PACKAGE_PIN AB14 [get_ports {FR_DI9}]
set_property IOSTANDARD LVCMOS33 [get_ports {FR_DI9}]

# Source: ../z20-v1_5_20260623.xdc, FR_DI10 package AB15 -> current wrapper FR_DI10
set_property PACKAGE_PIN AB15 [get_ports {FR_DI10}]
set_property IOSTANDARD LVCMOS33 [get_ports {FR_DI10}]

# Source: ../z20-v1_5_20260623.xdc, FR_DI11 package AB16 -> current wrapper FR_DI11
set_property PACKAGE_PIN AB16 [get_ports {FR_DI11}]
set_property IOSTANDARD LVCMOS33 [get_ports {FR_DI11}]

# Source: ../z20-v1_5_20260623.xdc, FR_DI12 package AA16 -> current wrapper FR_DI12
set_property PACKAGE_PIN AA16 [get_ports {FR_DI12}]
set_property IOSTANDARD LVCMOS33 [get_ports {FR_DI12}]

# Source: ../z20-v1_5_20260623.xdc, FR_DI13 package AB17 -> current wrapper FR_DI13
set_property PACKAGE_PIN AB17 [get_ports {FR_DI13}]
set_property IOSTANDARD LVCMOS33 [get_ports {FR_DI13}]

# Source: ../z20-v1_5_20260623.xdc, FR_DI14 package AA17 -> current wrapper FR_DI14
set_property PACKAGE_PIN AA17 [get_ports {FR_DI14}]
set_property IOSTANDARD LVCMOS33 [get_ports {FR_DI14}]

# Source: ../z20-v1_5_20260623.xdc, FR_DI15 package AB19 -> current wrapper FR_DI15
set_property PACKAGE_PIN AB19 [get_ports {FR_DI15}]
set_property IOSTANDARD LVCMOS33 [get_ports {FR_DI15}]

# Source: ../z20-v1_5_20260623.xdc, FR_DI16 package AB20 -> current wrapper FR_DI16
set_property PACKAGE_PIN AB20 [get_ports {FR_DI16}]
set_property IOSTANDARD LVCMOS33 [get_ports {FR_DI16}]

# Source: ../z20-v1_5_20260623.xdc, TS_DI package Y19 -> current wrapper TS_DI
set_property PACKAGE_PIN Y19 [get_ports {TS_DI}]
set_property IOSTANDARD LVCMOS33 [get_ports {TS_DI}]

## MPG pins replacing abandoned HDMI pin use
# Source: ../z20-v1_5_20260623.xdc, AXIS_SEL0 package C17 -> current wrapper mpg_axis_sel[0]
set_property PACKAGE_PIN C17 [get_ports {mpg_axis_sel[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {mpg_axis_sel[0]}]

# Source: ../z20-v1_5_20260623.xdc, AXIS_SEL1 package C18 -> current wrapper mpg_axis_sel[1]
set_property PACKAGE_PIN C18 [get_ports {mpg_axis_sel[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {mpg_axis_sel[1]}]

# Source: ../z20-v1_5_20260623.xdc, AXIS_SEL2 package B19 -> current wrapper mpg_axis_sel[2]
set_property PACKAGE_PIN B19 [get_ports {mpg_axis_sel[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {mpg_axis_sel[2]}]

# Source: ../z20-v1_5_20260623.xdc, AXIS_SEL3 package B20 -> current wrapper mpg_axis_sel[3]
set_property PACKAGE_PIN B20 [get_ports {mpg_axis_sel[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {mpg_axis_sel[3]}]

# Source: ../z20-v1_5_20260623.xdc, AXIS_SEL4 package H19 -> current wrapper mpg_axis_sel[4]
set_property PACKAGE_PIN H19 [get_ports {mpg_axis_sel[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {mpg_axis_sel[4]}]

# Source: ../z20-v1_5_20260623.xdc, AXIS_SEL5 package H20 -> current wrapper mpg_axis_sel[5]
set_property PACKAGE_PIN H20 [get_ports {mpg_axis_sel[5]}]
set_property IOSTANDARD LVCMOS33 [get_ports {mpg_axis_sel[5]}]

# Source: ../z20-v1_5_20260623.xdc, AXIS_SEL6 package B16 -> current wrapper mpg_axis_sel[6]
set_property PACKAGE_PIN B16 [get_ports {mpg_axis_sel[6]}]
set_property IOSTANDARD LVCMOS33 [get_ports {mpg_axis_sel[6]}]

# Source: ../z20-v1_5_20260623.xdc, AXIS_SEL7 package B17 -> current wrapper mpg_axis_sel[7]
set_property PACKAGE_PIN B17 [get_ports {mpg_axis_sel[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {mpg_axis_sel[7]}]

# Source: ../z20-v1_5_20260623.xdc, MPG_A package C20 -> current wrapper MPG_A
set_property PACKAGE_PIN C20 [get_ports {MPG_A}]
set_property IOSTANDARD LVCMOS33 [get_ports {MPG_A}]

# Source: ../z20-v1_5_20260623.xdc, MPG_B package D20 -> current wrapper MPG_B
set_property PACKAGE_PIN D20 [get_ports {MPG_B}]
set_property IOSTANDARD LVCMOS33 [get_ports {MPG_B}]

# Source: ../z20-v1_5_20260623.xdc, SCALE_SEL0 package D21 -> current wrapper SCALE_SEL0
set_property PACKAGE_PIN D21 [get_ports {SCALE_SEL0}]
set_property IOSTANDARD LVCMOS33 [get_ports {SCALE_SEL0}]

# Source: ../z20-v1_5_20260623.xdc, SCALE_SEL1 package E21 -> current wrapper SCALE_SEL1
set_property PACKAGE_PIN E21 [get_ports {SCALE_SEL1}]
set_property IOSTANDARD LVCMOS33 [get_ports {SCALE_SEL1}]

# Source: ../z20-v1_5_20260623.xdc, SCALE_SEL2 package H17 -> current wrapper SCALE_SEL2
set_property PACKAGE_PIN H17 [get_ports {SCALE_SEL2}]
set_property IOSTANDARD LVCMOS33 [get_ports {SCALE_SEL2}]

## 8-axis pulse/direction/enable outputs and alarm inputs
# Source: ../z20-v1_5_20260623.xdc, PULS1_IO package J17 -> current wrapper PULS1_IO
set_property PACKAGE_PIN J17 [get_ports {PULS1_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {PULS1_IO}]

# Source: ../z20-v1_5_20260623.xdc, PULS2_IO package M16 -> current wrapper PULS2_IO
set_property PACKAGE_PIN M16 [get_ports {PULS2_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {PULS2_IO}]

# Source: ../z20-v1_5_20260623.xdc, PULS3_IO package K15 -> current wrapper PULS3_IO
set_property PACKAGE_PIN K15 [get_ports {PULS3_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {PULS3_IO}]

# Source: ../z20-v1_5_20260623.xdc, PULS4_IO package P18 -> current wrapper PULS4_IO
set_property PACKAGE_PIN P18 [get_ports {PULS4_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {PULS4_IO}]

# Source: ../z20-v1_5_20260623.xdc, PULS5_IO package M20 -> current wrapper PULS5_IO
set_property PACKAGE_PIN M20 [get_ports {PULS5_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {PULS5_IO}]

# Source: ../z20-v1_5_20260623.xdc, PULS6_IO package R18 -> current wrapper PULS6_IO
set_property PACKAGE_PIN R18 [get_ports {PULS6_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {PULS6_IO}]

# Source: ../z20-v1_5_20260623.xdc, PULS7_IO package R16 -> current wrapper PULS7_IO
set_property PACKAGE_PIN R16 [get_ports {PULS7_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {PULS7_IO}]

# Source: ../z20-v1_5_20260623.xdc, PULS8_IO package AA21 -> current wrapper PULS8_IO
set_property PACKAGE_PIN AA21 [get_ports {PULS8_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {PULS8_IO}]

# Source: ../z20-v1_5_20260623.xdc, DIR1_IO package J16 -> current wrapper DIR1_IO
set_property PACKAGE_PIN J16 [get_ports {DIR1_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {DIR1_IO}]

# Source: ../z20-v1_5_20260623.xdc, DIR2_IO package N17 -> current wrapper DIR2_IO
set_property PACKAGE_PIN N17 [get_ports {DIR2_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {DIR2_IO}]

# Source: ../z20-v1_5_20260623.xdc, DIR3_IO package J15 -> current wrapper DIR3_IO
set_property PACKAGE_PIN J15 [get_ports {DIR3_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {DIR3_IO}]

# Source: ../z20-v1_5_20260623.xdc, DIR4_IO package M17 -> current wrapper DIR4_IO
set_property PACKAGE_PIN M17 [get_ports {DIR4_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {DIR4_IO}]

# Source: ../z20-v1_5_20260623.xdc, DIR5_IO package M19 -> current wrapper DIR5_IO
set_property PACKAGE_PIN M19 [get_ports {DIR5_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {DIR5_IO}]

# Source: ../z20-v1_5_20260623.xdc, DIR6_IO package T18 -> current wrapper DIR6_IO
set_property PACKAGE_PIN T18 [get_ports {DIR6_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {DIR6_IO}]

# Source: ../z20-v1_5_20260623.xdc, DIR7_IO package P16 -> current wrapper DIR7_IO
set_property PACKAGE_PIN P16 [get_ports {DIR7_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {DIR7_IO}]

# Source: ../z20-v1_5_20260623.xdc, DIR8_IO package AB22 -> current wrapper DIR8_IO
set_property PACKAGE_PIN AB22 [get_ports {DIR8_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {DIR8_IO}]

# Source: ../z20-v1_5_20260623.xdc, ENA1_IO package J18 -> current wrapper ENA1_IO
set_property PACKAGE_PIN J18 [get_ports {ENA1_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {ENA1_IO}]

# Source: ../z20-v1_5_20260623.xdc, ENA2_IO package N18 -> current wrapper ENA2_IO
set_property PACKAGE_PIN N18 [get_ports {ENA2_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {ENA2_IO}]

# Source: ../z20-v1_5_20260623.xdc, ENA3_IO package P15 -> current wrapper ENA3_IO
set_property PACKAGE_PIN P15 [get_ports {ENA3_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {ENA3_IO}]

# Source: ../z20-v1_5_20260623.xdc, ENA4_IO package L17 -> current wrapper ENA4_IO
set_property PACKAGE_PIN L17 [get_ports {ENA4_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {ENA4_IO}]

# Source: ../z20-v1_5_20260623.xdc, ENA5_IO package R19 -> current wrapper ENA5_IO
set_property PACKAGE_PIN R19 [get_ports {ENA5_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {ENA5_IO}]

# Source: ../z20-v1_5_20260623.xdc, ENA6_IO package T16 -> current wrapper ENA6_IO
set_property PACKAGE_PIN T16 [get_ports {ENA6_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {ENA6_IO}]

# Source: ../z20-v1_5_20260623.xdc, ENA7_IO package W20 -> current wrapper ENA7_IO
set_property PACKAGE_PIN W20 [get_ports {ENA7_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {ENA7_IO}]

# Source: ../z20-v1_5_20260623.xdc, ENA8_IO package AA22 -> current wrapper ENA8_IO
set_property PACKAGE_PIN AA22 [get_ports {ENA8_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {ENA8_IO}]

# Source: ../z20-v1_5_20260623.xdc, ALM1_IO package K18 -> current wrapper ALM1_IO
set_property PACKAGE_PIN K18 [get_ports {ALM1_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {ALM1_IO}]

# Source: ../z20-v1_5_20260623.xdc, ALM2_IO package K20 -> current wrapper ALM2_IO
set_property PACKAGE_PIN K20 [get_ports {ALM2_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {ALM2_IO}]

# Source: ../z20-v1_5_20260623.xdc, ALM3_IO package N15 -> current wrapper ALM3_IO
set_property PACKAGE_PIN N15 [get_ports {ALM3_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {ALM3_IO}]

# Source: ../z20-v1_5_20260623.xdc, ALM4_IO package N20 -> current wrapper ALM4_IO
set_property PACKAGE_PIN N20 [get_ports {ALM4_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {ALM4_IO}]

# Source: ../z20-v1_5_20260623.xdc, ALM5_IO package T19 -> current wrapper ALM5_IO
set_property PACKAGE_PIN T19 [get_ports {ALM5_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {ALM5_IO}]

# Source: ../z20-v1_5_20260623.xdc, ALM6_IO package T17 -> current wrapper ALM6_IO
set_property PACKAGE_PIN T17 [get_ports {ALM6_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {ALM6_IO}]

# Source: ../z20-v1_5_20260623.xdc, ALM7_IO package W21 -> current wrapper ALM7_IO
set_property PACKAGE_PIN W21 [get_ports {ALM7_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {ALM7_IO}]

# Source: ../z20-v1_5_20260623.xdc, ALM8_IO package W22 -> current wrapper ALM8_IO
set_property PACKAGE_PIN W22 [get_ports {ALM8_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {ALM8_IO}]

## Encoder ABZ receiver-output inputs
# Source: ../z20-v1_5_20260623.xdc, A1_IO package K16 -> current wrapper A1_IO
set_property PACKAGE_PIN K16 [get_ports {A1_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {A1_IO}]

# Source: ../z20-v1_5_20260623.xdc, B1_IO package L16 -> current wrapper B1_IO
set_property PACKAGE_PIN L16 [get_ports {B1_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {B1_IO}]

# Source: ../z20-v1_5_20260623.xdc, Z1_IO package M15 -> current wrapper Z1_IO
set_property PACKAGE_PIN M15 [get_ports {Z1_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {Z1_IO}]

# Source: ../z20-v1_5_20260623.xdc, A2_IO package K19 -> current wrapper A2_IO
set_property PACKAGE_PIN K19 [get_ports {A2_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {A2_IO}]

# Source: ../z20-v1_5_20260623.xdc, B2_IO package J20 -> current wrapper B2_IO
set_property PACKAGE_PIN J20 [get_ports {B2_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {B2_IO}]

# Source: ../z20-v1_5_20260623.xdc, Z2_IO package K21 -> current wrapper Z2_IO
set_property PACKAGE_PIN K21 [get_ports {Z2_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {Z2_IO}]

# Source: ../z20-v1_5_20260623.xdc, A3_IO package J21 -> current wrapper A3_IO
set_property PACKAGE_PIN J21 [get_ports {A3_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {A3_IO}]

# Source: ../z20-v1_5_20260623.xdc, B3_IO package J22 -> current wrapper B3_IO
set_property PACKAGE_PIN J22 [get_ports {B3_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {B3_IO}]

# Source: ../z20-v1_5_20260623.xdc, Z3_IO package P17 -> current wrapper Z3_IO
set_property PACKAGE_PIN P17 [get_ports {Z3_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {Z3_IO}]

# Source: ../z20-v1_5_20260623.xdc, A4_IO package N19 -> current wrapper A4_IO
set_property PACKAGE_PIN N19 [get_ports {A4_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {A4_IO}]

# Source: ../z20-v1_5_20260623.xdc, B4_IO package P20 -> current wrapper B4_IO
set_property PACKAGE_PIN P20 [get_ports {B4_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {B4_IO}]

# Source: ../z20-v1_5_20260623.xdc, Z4_IO package P21 -> current wrapper Z4_IO
set_property PACKAGE_PIN P21 [get_ports {Z4_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {Z4_IO}]

# Source: ../z20-v1_5_20260623.xdc, A5_IO package L21 -> current wrapper A5_IO
set_property PACKAGE_PIN L21 [get_ports {A5_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {A5_IO}]

# Source: ../z20-v1_5_20260623.xdc, B5_IO package L22 -> current wrapper B5_IO
set_property PACKAGE_PIN L22 [get_ports {B5_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {B5_IO}]

# Source: ../z20-v1_5_20260623.xdc, Z5_IO package P22 -> current wrapper Z5_IO
set_property PACKAGE_PIN P22 [get_ports {Z5_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {Z5_IO}]

# Source: ../z20-v1_5_20260623.xdc, A6_IO package N22 -> current wrapper A6_IO
set_property PACKAGE_PIN N22 [get_ports {A6_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {A6_IO}]

# Source: ../z20-v1_5_20260623.xdc, B6_IO package R20 -> current wrapper B6_IO
set_property PACKAGE_PIN R20 [get_ports {B6_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {B6_IO}]

# Source: ../z20-v1_5_20260623.xdc, Z6_IO package R21 -> current wrapper Z6_IO
set_property PACKAGE_PIN R21 [get_ports {Z6_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {Z6_IO}]

# Source: ../z20-v1_5_20260623.xdc, A7_IO package V20 -> current wrapper A7_IO
set_property PACKAGE_PIN V20 [get_ports {A7_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {A7_IO}]

# Source: ../z20-v1_5_20260623.xdc, B7_IO package U20 -> current wrapper B7_IO
set_property PACKAGE_PIN U20 [get_ports {B7_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {B7_IO}]

# Source: ../z20-v1_5_20260623.xdc, Z7_IO package AB21 -> current wrapper Z7_IO
set_property PACKAGE_PIN AB21 [get_ports {Z7_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {Z7_IO}]

# Source: ../z20-v1_5_20260623.xdc, A8_IO package V22 -> current wrapper A8_IO
set_property PACKAGE_PIN V22 [get_ports {A8_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {A8_IO}]

# Source: ../z20-v1_5_20260623.xdc, B8_IO package U22 -> current wrapper B8_IO
set_property PACKAGE_PIN U22 [get_ports {B8_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {B8_IO}]

# Source: ../z20-v1_5_20260623.xdc, Z8_IO package T22 -> current wrapper Z8_IO
set_property PACKAGE_PIN T22 [get_ports {Z8_IO}]
set_property IOSTANDARD LVCMOS33 [get_ports {Z8_IO}]

## ADC one-channel XADC dedicated analog input
## ADC_IN1 uses XADC_VP/XADC_VN dedicated analog pins L11/M12.
## Do not add normal PL PACKAGE_PIN constraints for XADC_VP/XADC_VN here.
## U10/U9/AA12/AB12 are no longer ADC SPI pins in this revision.

## Communication and touch I2C wrapper ports
# Source: ../z20-v1_5_20260623.xdc, CAN_FPGA_RX package M22 -> current wrapper can0_rx
set_property PACKAGE_PIN M22 [get_ports {can0_rx}]
set_property IOSTANDARD LVCMOS33 [get_ports {can0_rx}]

# Source: ../z20-v1_5_20260623.xdc, CAN_FPGA_TX package M21 -> current wrapper can0_tx
set_property PACKAGE_PIN M21 [get_ports {can0_tx}]
set_property IOSTANDARD LVCMOS33 [get_ports {can0_tx}]

# Source: ../z20-v1_5_20260623.xdc, I2C3_SCL package A18 -> current wrapper i2c0_scl_io
set_property PACKAGE_PIN A18 [get_ports {i2c0_scl_io}]
set_property IOSTANDARD LVCMOS33 [get_ports {i2c0_scl_io}]

# Source: ../z20-v1_5_20260623.xdc, I2C3_SDA package A19 -> current wrapper i2c0_sda_io
set_property PACKAGE_PIN A19 [get_ports {i2c0_sda_io}]
set_property IOSTANDARD LVCMOS33 [get_ports {i2c0_sda_io}]

# Source: ../z20-v1_5_20260623.xdc, I2C_SCL_TP package Y4 -> current wrapper i2c1_scl_io
set_property PACKAGE_PIN Y4 [get_ports {i2c1_scl_io}]
set_property IOSTANDARD LVCMOS33 [get_ports {i2c1_scl_io}]

# Source: ../z20-v1_5_20260623.xdc, I2C_SDA_TP package AA7 -> current wrapper i2c1_sda_io
set_property PACKAGE_PIN AA7 [get_ports {i2c1_sda_io}]
set_property IOSTANDARD LVCMOS33 [get_ports {i2c1_sda_io}]

# Source: ../z20-v1_5_20260623.xdc, TP_INT package AA4 -> current wrapper TP_INT
set_property PACKAGE_PIN AA4 [get_ports {TP_INT}]
set_property IOSTANDARD LVCMOS33 [get_ports {TP_INT}]

# Source: ../z20-v1_5_20260623.xdc, TP_RST package W7 -> current wrapper TP_RST
set_property PACKAGE_PIN W7 [get_ports {TP_RST}]
set_property IOSTANDARD LVCMOS33 [get_ports {TP_RST}]

# Source: ../z20-v1_5_20260623.xdc, PL_UART_RX package F18 -> current wrapper pl_uart_rxd
set_property PACKAGE_PIN F18 [get_ports {pl_uart_rxd}]
set_property IOSTANDARD LVCMOS33 [get_ports {pl_uart_rxd}]

# Source: ../z20-v1_5_20260623.xdc, PL_UART_TX package E18 -> current wrapper pl_uart_txd
set_property PACKAGE_PIN E18 [get_ports {pl_uart_txd}]
set_property IOSTANDARD LVCMOS33 [get_ports {pl_uart_txd}]

# Source: ../z20-v1_5_20260623.xdc, RS232_FPGA_RX package A17 -> current wrapper rs232_rxd
set_property PACKAGE_PIN A17 [get_ports {rs232_rxd}]
set_property IOSTANDARD LVCMOS33 [get_ports {rs232_rxd}]

# Source: ../z20-v1_5_20260623.xdc, RS232_FPGA_TX package A16 -> current wrapper rs232_txd
set_property PACKAGE_PIN A16 [get_ports {rs232_txd}]
set_property IOSTANDARD LVCMOS33 [get_ports {rs232_txd}]

# Source: ../z20-v1_5_20260623.xdc, RS485_FPGA_RX package U14 -> current wrapper RS485_FPGA_RX
set_property PACKAGE_PIN U14 [get_ports {RS485_FPGA_RX}]
set_property IOSTANDARD LVCMOS33 [get_ports {RS485_FPGA_RX}]

# Source: ../z20-v1_5_20260623.xdc, RS485_FPGA_TX package R7 -> current wrapper RS485_FPGA_TX
set_property PACKAGE_PIN R7 [get_ports {RS485_FPGA_TX}]
set_property IOSTANDARD LVCMOS33 [get_ports {RS485_FPGA_TX}]

## Timing-only exception: clk_wiz_0 dynamic reconfiguration configuration store
## The generated clk_wiz_0 DRP configuration store is controlled by PS GP0 AXI
## writes and has repeatedly dominated implementation with MAXIGP0ACLK ->
## CLK_CORE_DRP_I configuration-register setup paths. Limit the exception to
## the DRP configuration RAM/register endpoints; do not clock-group clk_fpga_0
## or cut other AXI peripherals.
set clk_wiz0_drp_cfg_dst_pins [get_pins -hier -quiet -regexp {.*system_i/clk_wiz_0/inst/CLK_CORE_DRP_I/(ram_clk_config_reg|clkfbout_reg_reg|clkout[0-9]+_reg_reg|divclk_reg_reg|lock_reg_reg|filter_reg_reg).*\/D$}]
set ps7_gp0_launch_pins [get_pins -hier -quiet -regexp {.*system_i/processing_system7_0/inst/PS7_i/MAXIGP0ACLK$}]
set_false_path -from $ps7_gp0_launch_pins -to $clk_wiz0_drp_cfg_dst_pins
