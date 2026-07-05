# Active project constraints file.
# Legacy reference file `Z20 ZYNQ_IO.xdc` is intentionally not loaded.
# Keep default strict DRC policy for unconstrained I/O.
set_property SEVERITY {Error} [get_drc_checks UCIO-1]
# pl led
set_property -dict {PACKAGE_PIN H17 IOSTANDARD LVCMOS33} [get_ports {pl_led_tri_o[0]}]
set_property -dict {PACKAGE_PIN H18 IOSTANDARD LVCMOS33} [get_ports {pl_led_tri_o[1]}]

# GPIO0
#pl key 54-55
set_property -dict {PACKAGE_PIN H15 IOSTANDARD LVCMOS33} [get_ports {gpio0_tri_io[0]}]
set_property -dict {PACKAGE_PIN L19 IOSTANDARD LVCMOS33} [get_ports {gpio0_tri_io[1]}]
#pl rst 56
set_property -dict {PACKAGE_PIN R15 IOSTANDARD LVCMOS33} [get_ports {gpio0_tri_io[2]}]

#lcd touch int restored to original dev-board pinout
set_property -dict {PACKAGE_PIN AA4 IOSTANDARD LVCMOS33} [get_ports {gpio0_tri_io[3]}]
#lcd touch rst_n restored to original dev-board pinout
set_property -dict {PACKAGE_PIN W7 IOSTANDARD LVCMOS33} [get_ports {gpio0_tri_io[4]}]
# released from camera interface
set_property -dict {PACKAGE_PIN C20 IOSTANDARD LVCMOS33} [get_ports {gpio0_tri_io[5]}]
# released from camera interface
set_property -dict {PACKAGE_PIN E21 IOSTANDARD LVCMOS33} [get_ports {gpio0_tri_io[6]}]

# CAN 0
set_property -dict {PACKAGE_PIN M22 IOSTANDARD LVCMOS33} [get_ports can0_rx]
set_property -dict {PACKAGE_PIN M21 IOSTANDARD LVCMOS33} [get_ports can0_tx]
#rs485
set_property -dict {PACKAGE_PIN U14 IOSTANDARD LVCMOS33} [get_ports rs485_rxd]
set_property -dict {PACKAGE_PIN U19 IOSTANDARD LVCMOS33} [get_ports rs485_txd]
#pl_uart
set_property -dict {PACKAGE_PIN E18 IOSTANDARD LVCMOS33} [get_ports pl_uart_txd]
set_property -dict {PACKAGE_PIN F18 IOSTANDARD LVCMOS33} [get_ports pl_uart_rxd]
#rs232
set_property -dict {PACKAGE_PIN A16 IOSTANDARD LVCMOS33} [get_ports rs232_txd]
set_property -dict {PACKAGE_PIN A17 IOSTANDARD LVCMOS33} [get_ports rs232_rxd]

# Audio interface
# LCD
set_property -dict {PACKAGE_PIN V9 IOSTANDARD LVCMOS33} [get_ports lcd_clk]
set_property -dict {PACKAGE_PIN V5 IOSTANDARD LVCMOS33} [get_ports lcd_de]
set_property -dict {PACKAGE_PIN V4 IOSTANDARD LVCMOS33} [get_ports lcd_hs]
set_property -dict {PACKAGE_PIN V10 IOSTANDARD LVCMOS33} [get_ports lcd_vs]
#PWM_0
set_property -dict {PACKAGE_PIN AA6 IOSTANDARD LVCMOS33} [get_ports {lcd_bl[0]}]
#lcd rst
set_property -dict {PACKAGE_PIN AB9 IOSTANDARD LVCMOS33} [get_ports lcd_rst]

#LCD Blue
set_property -dict {PACKAGE_PIN R6 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[0]}]
set_property -dict {PACKAGE_PIN T6 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[1]}]
set_property -dict {PACKAGE_PIN U4 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[2]}]
set_property -dict {PACKAGE_PIN T4 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[3]}]
set_property -dict {PACKAGE_PIN V8 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[4]}]
set_property -dict {PACKAGE_PIN W8 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[5]}]
set_property -dict {PACKAGE_PIN W11 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[6]}]
set_property -dict {PACKAGE_PIN W10 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[7]}]
#LCD Green
set_property -dict {PACKAGE_PIN AA9 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[8]}]
set_property -dict {PACKAGE_PIN AA8 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[9]}]
set_property -dict {PACKAGE_PIN AB2 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[10]}]
set_property -dict {PACKAGE_PIN AB1 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[11]}]
set_property -dict {PACKAGE_PIN W6 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[12]}]
set_property -dict {PACKAGE_PIN W5 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[13]}]
set_property -dict {PACKAGE_PIN U12 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[14]}]
set_property -dict {PACKAGE_PIN U11 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[15]}]
#LCD Red
set_property -dict {PACKAGE_PIN AB7 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[16]}]
set_property -dict {PACKAGE_PIN AB6 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[17]}]
set_property -dict {PACKAGE_PIN AB5 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[18]}]
set_property -dict {PACKAGE_PIN AB4 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[19]}]
set_property -dict {PACKAGE_PIN Y11 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[20]}]
set_property -dict {PACKAGE_PIN Y10 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[21]}]
set_property -dict {PACKAGE_PIN Y9 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[22]}]
set_property -dict {PACKAGE_PIN Y8 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb_tri_io[23]}]

# PL Ethernet (RGMII + MDIO)
set_property -dict {PACKAGE_PIN E19 IOSTANDARD LVCMOS33} [get_ports mdio_mdc]
set_property -dict {PACKAGE_PIN E20 IOSTANDARD LVCMOS33} [get_ports mdio_mdio_io]

set_property -dict {PACKAGE_PIN D18 IOSTANDARD LVCMOS33} [get_ports rgmii_rxc]
set_property -dict {PACKAGE_PIN C19 IOSTANDARD LVCMOS33} [get_ports rgmii_rx_ctl]
set_property -dict {PACKAGE_PIN E15 IOSTANDARD LVCMOS33} [get_ports {rgmii_rd[0]}]
set_property -dict {PACKAGE_PIN D15 IOSTANDARD LVCMOS33} [get_ports {rgmii_rd[1]}]
set_property -dict {PACKAGE_PIN F16 IOSTANDARD LVCMOS33} [get_ports {rgmii_rd[2]}]
set_property -dict {PACKAGE_PIN E16 IOSTANDARD LVCMOS33} [get_ports {rgmii_rd[3]}]

# Active project baseline uses B15/C15 for TXC/TX_CTL.
# If board wiring is switched back to legacy K2/J3, update this XDC and
# "核心板-底板-外部端口定义引脚对应表.md" in the same change.
set_property -dict {PACKAGE_PIN B15 IOSTANDARD LVCMOS33} [get_ports rgmii_txc]
set_property -dict {PACKAGE_PIN C15 IOSTANDARD LVCMOS33} [get_ports rgmii_tx_ctl]
set_property -dict {PACKAGE_PIN G16 IOSTANDARD LVCMOS33} [get_ports {rgmii_td[0]}]
set_property -dict {PACKAGE_PIN G15 IOSTANDARD LVCMOS33} [get_ports {rgmii_td[1]}]
set_property -dict {PACKAGE_PIN D17 IOSTANDARD LVCMOS33} [get_ports {rgmii_td[2]}]
set_property -dict {PACKAGE_PIN D16 IOSTANDARD LVCMOS33} [get_ports {rgmii_td[3]}]

create_clock -period 8.000 -name rgmii_rxc [get_ports rgmii_rxc]
# RGMII TXC is forwarded from the same recovered RGMII clock source used by
# gmii2rgmii in this candidate. Model TXC as a generated output clock so the
# TX data/control paths are timed instead of hidden behind an async exception.
create_generated_clock -name rgmii_txc_fwd \
  -source [get_ports rgmii_rxc] \
  -divide_by 1 \
  [get_ports rgmii_txc]

# I2C
#  eeprom & rtc & lvds iic & audio iic
set_property -dict {PACKAGE_PIN A18 IOSTANDARD LVCMOS33} [get_ports i2c0_scl_io]
set_property -dict {PACKAGE_PIN A19 IOSTANDARD LVCMOS33} [get_ports i2c0_sda_io]
#lcd touch
set_property -dict {PACKAGE_PIN Y4 IOSTANDARD LVCMOS33} [get_ports i2c1_scl_io]
set_property -dict {PACKAGE_PIN AA7 IOSTANDARD LVCMOS33} [get_ports i2c1_sda_io]
# camera i2c is removed in CNC profile

#ms7200 iic
set_property -dict {PACKAGE_PIN W12 IOSTANDARD LVCMOS33} [get_ports i2c4_scl_io]
set_property -dict {PACKAGE_PIN V12 IOSTANDARD LVCMOS33} [get_ports i2c4_sda_io]

# HDMI
set_property -dict {PACKAGE_PIN C17 IOSTANDARD TMDS_33} [get_ports {tmds_tmds_data_p[2]}]
set_property -dict {PACKAGE_PIN B16 IOSTANDARD TMDS_33} [get_ports {tmds_tmds_data_p[1]}]
set_property -dict {PACKAGE_PIN H19 IOSTANDARD TMDS_33} [get_ports {tmds_tmds_data_p[0]}]
set_property -dict {PACKAGE_PIN B19 IOSTANDARD TMDS_33} [get_ports tmds_tmds_clk_p]
set_property -dict {PACKAGE_PIN C18 IOSTANDARD TMDS_33} [get_ports {tmds_tmds_data_n[2]}]
set_property -dict {PACKAGE_PIN B17 IOSTANDARD TMDS_33} [get_ports {tmds_tmds_data_n[1]}]
set_property -dict {PACKAGE_PIN H20 IOSTANDARD TMDS_33} [get_ports {tmds_tmds_data_n[0]}]
set_property -dict {PACKAGE_PIN B20 IOSTANDARD TMDS_33} [get_ports tmds_tmds_clk_n]

# DVI reset synchronizers assert asynchronously and release through ASYNC_REG
# flip-flop chains inside each pixel/serializer clock domain. Do not time the
# proc_sys_reset output into those async PRE pins as a normal recovery/removal
# data path; the synchronizer chain owns the release timing.
set_false_path -quiet -to [get_pins -hier -quiet -filter {REF_PIN_NAME == PRE && NAME =~ *DVI_Transmitter_0*/reset_syn_*/*_reg/PRE}]

# CMOS camera is removed in CNC profile

# StepGen IP Module Constraints (30 I/O) - Reallocated to 40-pin interface
# === Step/Dir Output Ports (12 pins) ===
# dir_o[5:0] - Direction output
# Reallocated to 40-pin interface pins
# Original allocation: P20, P21, P22, M20, T16, T17
# New allocation: L16, K16, W22, V22, T16, T17 (dir_o[4-5] unchanged)
# X axis direction - 40 pin interface
set_property -dict {PACKAGE_PIN L16 IOSTANDARD LVCMOS33} [get_ports {dir_o[0]}]
# Y axis direction - 40 pin interface
set_property -dict {PACKAGE_PIN K16 IOSTANDARD LVCMOS33} [get_ports {dir_o[1]}]
# Z axis direction - 40 pin interface
set_property -dict {PACKAGE_PIN W22 IOSTANDARD LVCMOS33} [get_ports {dir_o[2]}]
# A axis direction - 40 pin interface
set_property -dict {PACKAGE_PIN V22 IOSTANDARD LVCMOS33} [get_ports {dir_o[3]}]
# B axis direction - 40 pin interface (unchanged)
set_property -dict {PACKAGE_PIN T16 IOSTANDARD LVCMOS33} [get_ports {dir_o[4]}]
# C axis direction - 40 pin interface (unchanged)
set_property -dict {PACKAGE_PIN T17 IOSTANDARD LVCMOS33} [get_ports {dir_o[5]}]
# Reallocated to 40-pin interface pins
# Original allocation: U16, U17, V17, V18, W17, V19
# Active allocation: U10, R21, R20, U22, T22, G19
# X axis step - 40 pin interface
set_property -dict {PACKAGE_PIN U10 IOSTANDARD LVCMOS33} [get_ports {step_o[0]}]
# Y axis step - 40 pin interface
set_property -dict {PACKAGE_PIN R21 IOSTANDARD LVCMOS33} [get_ports {step_o[1]}]
# Z axis step - 40 pin interface
set_property -dict {PACKAGE_PIN R20 IOSTANDARD LVCMOS33} [get_ports {step_o[2]}]
# A axis step - 40 pin interface
set_property -dict {PACKAGE_PIN U22 IOSTANDARD LVCMOS33} [get_ports {step_o[3]}]
# B axis step - 40 pin interface
set_property -dict {PACKAGE_PIN T22 IOSTANDARD LVCMOS33} [get_ports {step_o[4]}]
# C axis step - 40 pin interface
set_property -dict {PACKAGE_PIN G19 IOSTANDARD LVCMOS33} [get_ports {step_o[5]}]

# === Encoder Input Ports (18 pins) ===
# enc_a_i[5:0] - Encoder A phase input
# Updated based on successfully placed pin information in system_wrapper.vdi
# Note: Bank 13 is full (44/50), enc_a_i[0] needs to be placed in Bank 33 (together with enc_a_i[1] and enc_a_i[5])
# Use AA18 (unused pin in Bank 33, same series as AA13, AA14, AA16, AA17)
set_property -dict {PACKAGE_PIN AA18 IOSTANDARD LVCMOS33} [get_ports {enc_a_i[0]}]
set_property -dict {PACKAGE_PIN Y18 IOSTANDARD LVCMOS33} [get_ports {enc_a_i[1]}]
set_property -dict {PACKAGE_PIN B21 IOSTANDARD LVCMOS33} [get_ports {enc_a_i[2]}]
set_property -dict {PACKAGE_PIN AA12 IOSTANDARD LVCMOS33} [get_ports {enc_a_i[3]}]
set_property -dict {PACKAGE_PIN AB12 IOSTANDARD LVCMOS33} [get_ports {enc_a_i[4]}]
set_property -dict {PACKAGE_PIN AA13 IOSTANDARD LVCMOS33} [get_ports {enc_a_i[5]}]

# enc_b_i[5:0] - Encoder B phase input
# enc_b_i[0], enc_b_i[2], enc_b_i[4] moved off 40P display interface onto FPGA2 expansion
set_property -dict {PACKAGE_PIN D22 IOSTANDARD LVCMOS33} [get_ports {enc_b_i[0]}]
set_property -dict {PACKAGE_PIN AA14 IOSTANDARD LVCMOS33} [get_ports {enc_b_i[1]}]
set_property -dict {PACKAGE_PIN C22 IOSTANDARD LVCMOS33} [get_ports {enc_b_i[2]}]
set_property -dict {PACKAGE_PIN AA17 IOSTANDARD LVCMOS33} [get_ports {enc_b_i[3]}]
set_property -dict {PACKAGE_PIN D20 IOSTANDARD LVCMOS33} [get_ports {enc_b_i[4]}]
set_property -dict {PACKAGE_PIN AA16 IOSTANDARD LVCMOS33} [get_ports {enc_b_i[5]}]

# enc_z_i[5:0] - Encoder Z phase input
# enc_z_i[0] moved off 40P display interface onto FPGA2 expansion
set_property -dict {PACKAGE_PIN F19 IOSTANDARD LVCMOS33} [get_ports {enc_z_i[0]}]
set_property -dict {PACKAGE_PIN Y13 IOSTANDARD LVCMOS33} [get_ports {enc_z_i[1]}]
set_property -dict {PACKAGE_PIN Y14 IOSTANDARD LVCMOS33} [get_ports {enc_z_i[2]}]
set_property -dict {PACKAGE_PIN D21 IOSTANDARD LVCMOS33} [get_ports {enc_z_i[3]}]
set_property -dict {PACKAGE_PIN Y19 IOSTANDARD LVCMOS33} [get_ports {enc_z_i[4]}]
set_property -dict {PACKAGE_PIN B22 IOSTANDARD LVCMOS33} [get_ports {enc_z_i[5]}]

# High robustness constraints for Encoder inputs
set_property PULLUP TRUE [get_ports {enc_a_i[*] enc_b_i[*] enc_z_i[*]}]
set_property IOB TRUE [get_ports {enc_a_i[*] enc_b_i[*] enc_z_i[*]}]

# NOTE:
# The previous broad set_clock_groups constraints masked timing between
# synchronous clock domains (clk_fpga_0 <-> hdmi/lcd pclk), causing TIMING-47
# and overriding IP-provided set_max_delay -datapath_only constraints (TIMING-24).
# Keep these paths visible to STA; add point-to-point exceptions only after CDC review.

# ----------------------------------------------------------------------
# I/O timing closure baseline
# ----------------------------------------------------------------------
# RGMII RX/TX in this candidate use rgmii_rxc as the board-side reference.
# BD explicitly sets gmii2rgmii TX_CLK_FROM_RX = 1 so TX ODDR uses the
# recovered/BUFGed RX clock rather than the raw pad clock.
# Apply DDR-aware I/O delays (rise/fall) on both edges to avoid
# incomplete timing models reported as TIMING-18.
set_input_delay  -clock rgmii_rxc -max 2.0 [get_ports {rgmii_rd[*] rgmii_rx_ctl}]
set_input_delay  -clock rgmii_rxc -min 1.8 [get_ports {rgmii_rd[*] rgmii_rx_ctl}]
set_input_delay  -clock rgmii_rxc -clock_fall -max 2.0 -add_delay [get_ports {rgmii_rd[*] rgmii_rx_ctl}]
set_input_delay  -clock rgmii_rxc -clock_fall -min 1.8 -add_delay [get_ports {rgmii_rd[*] rgmii_rx_ctl}]

set_output_delay -clock rgmii_txc_fwd -max 2.0 [get_ports {rgmii_td[*] rgmii_tx_ctl}]
set_output_delay -clock rgmii_txc_fwd -min 0.5 [get_ports {rgmii_td[*] rgmii_tx_ctl}]
set_output_delay -clock rgmii_txc_fwd -clock_fall -max 2.0 -add_delay [get_ports {rgmii_td[*] rgmii_tx_ctl}]
set_output_delay -clock rgmii_txc_fwd -clock_fall -min 0.5 -add_delay [get_ports {rgmii_td[*] rgmii_tx_ctl}]

# TMDS output-enable uses dedicated PL pin M20 in current buildable baseline.
# Legacy board sheet may still list Y5 (HDMI IN rstn_out); do not dual-define.
set_property -dict {PACKAGE_PIN M20 IOSTANDARD LVCMOS33} [get_ports tmds_tmds_oen]

# DVI reset chain intentionally crosses hdmi_pclk <-> hdmi_pclk_5x domains via
# async control pins (PRE/RST). Keep data paths timed; exempt reset control arcs only.
set_false_path \
  -from [get_cells -hier -filter {NAME =~ *system_i/hdmi_out/rst_hdmi_dynclk/U0/ACTIVE_LOW_PR_OUT_DFF*FDRE_PER_N}] \
  -to [get_cells -hier -filter {NAME =~ *system_i/hdmi_out/DVI_Transmitter_0/inst/reset_syn_serdes/reset_1_reg}]
set_false_path \
  -from [get_cells -hier -filter {NAME =~ *system_i/hdmi_out/DVI_Transmitter_0/inst/reset_syn_serdes/reset_2_reg}] \
  -to [get_cells -hier -filter {NAME =~ *system_i/hdmi_out/DVI_Transmitter_0/inst/serializer_*/OSERDESE2_*}]

# ----------------------------------------------------------------------
# Asynchronous external interfaces (explicitly excluded from I/O delay checks)
# ----------------------------------------------------------------------
# Keep exceptions scoped to truly asynchronous control-style I/O.
# Do not mask timing on synchronous pixel/audio/TMDS interfaces.
# If any listed port is repurposed to synchronous/high-speed interface, remove
# corresponding false_path and add explicit interface timing constraints first.
# GPIO0 bit mapping in active baseline:
#   [0]=PL_KEY0(input), [1]=PL_KEY1(input), [2]=PL_RST_N(input), [3]=TOUCH_INT(input)
#   [4]=TOUCH_RST_N(output), [5]=CAM_SGM_CTRL/CAM_PWDN(output), [6]=CAM_RST_N(output)
# Keep bit-level explicit listing to avoid whole-bus masking drift.
set_false_path -from [get_ports { \
  enc_a_i[*] enc_b_i[*] enc_z_i[*] \
  gpio0_tri_io[0] gpio0_tri_io[1] gpio0_tri_io[2] gpio0_tri_io[3] \
}]


