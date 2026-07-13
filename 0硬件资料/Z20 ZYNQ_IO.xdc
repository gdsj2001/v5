#开发板IO约束文件

#----------------------系统时钟---------------------------
#时钟周期约束
create_clock -period 20.000 -waveform {0.000 10.000} [get_ports sys_clk]
#时钟管脚
set_property -dict {PACKAGE_PIN L18 IOSTANDARD LVCMOS33} [get_ports sys_clk]

#----------------------系统复位---------------------------
set_property -dict {PACKAGE_PIN R15 IOSTANDARD LVCMOS33} [get_ports sys_rst_n]

#----------------------KEY---------------------------
set_property -dict {PACKAGE_PIN H15 IOSTANDARD LVCMOS33} [get_ports {key[0]}]
set_property -dict {PACKAGE_PIN L19 IOSTANDARD LVCMOS33} [get_ports {key[1]}]

#----------------------LED---------------------------
set_property -dict {PACKAGE_PIN H17 IOSTANDARD LVCMOS33} [get_ports {led[0]}]
set_property -dict {PACKAGE_PIN H18 IOSTANDARD LVCMOS33} [get_ports {led[1]}]

#----------------------UART---------------------------
set_property PACKAGE_PIN F18 [get_ports uart_rxd]
set_property PACKAGE_PIN E18 [get_ports uart_txd]

set_property IOSTANDARD LVCMOS33 [get_ports uart_txd]
set_property IOSTANDARD LVCMOS33 [get_ports uart_rxd]

#----------------------RS232---------------------------
set_property -dict {PACKAGE_PIN A17 IOSTANDARD LVCMOS33} [get_ports rs232_uart_rxd]
set_property -dict {PACKAGE_PIN A16 IOSTANDARD LVCMOS33} [get_ports rs232_uart_txd]

#----------------------RS485---------------------------
set_property -dict {PACKAGE_PIN U14 IOSTANDARD LVCMOS33} [get_ports rs485_uart_rxd]
set_property -dict {PACKAGE_PIN U19 IOSTANDARD LVCMOS33} [get_ports rs485_uart_txd]

#----------------------CAN---------------------------
set_property -dict {PACKAGE_PIN M22 IOSTANDARD LVCMOS33} [get_ports can_rx]
set_property -dict {PACKAGE_PIN M21 IOSTANDARD LVCMOS33} [get_ports can_tx]

#----------------------IIC（EEPROM/RTC）----------------------
set_property -dict {PACKAGE_PIN A18 IOSTANDARD LVCMOS33} [get_ports iic_scl]
set_property -dict {PACKAGE_PIN A19 IOSTANDARD LVCMOS33} [get_ports iic_sda]

#----------------------LCD显示接口----------------------------
set_property -dict {PACKAGE_PIN R6 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[0]}]
set_property -dict {PACKAGE_PIN T6 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[1]}]
set_property -dict {PACKAGE_PIN U4 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[2]}]
set_property -dict {PACKAGE_PIN T4 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[3]}]
set_property -dict {PACKAGE_PIN V8 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[4]}]
set_property -dict {PACKAGE_PIN W8 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[5]}]
set_property -dict {PACKAGE_PIN W11 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[6]}]
set_property -dict {PACKAGE_PIN W10 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[7]}]
set_property -dict {PACKAGE_PIN AA9 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[8]}]
set_property -dict {PACKAGE_PIN AA8 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[9]}]
set_property -dict {PACKAGE_PIN AB2 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[10]}]
set_property -dict {PACKAGE_PIN AB1 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[11]}]
set_property -dict {PACKAGE_PIN W6 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[12]}]
set_property -dict {PACKAGE_PIN W5 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[13]}]
set_property -dict {PACKAGE_PIN U12 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[14]}]
set_property -dict {PACKAGE_PIN U11 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[15]}]
set_property -dict {PACKAGE_PIN AB7 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[16]}]
set_property -dict {PACKAGE_PIN AB6 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[17]}]
set_property -dict {PACKAGE_PIN AB5 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[18]}]
set_property -dict {PACKAGE_PIN AB4 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[19]}]
set_property -dict {PACKAGE_PIN Y11 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[20]}]
set_property -dict {PACKAGE_PIN Y10 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[21]}]
set_property -dict {PACKAGE_PIN Y9 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[22]}]
set_property -dict {PACKAGE_PIN Y8 IOSTANDARD LVCMOS33} [get_ports {lcd_rgb[23]}]

set_property -dict {PACKAGE_PIN V4 IOSTANDARD LVCMOS33} [get_ports lcd_hs]
set_property -dict {PACKAGE_PIN V10 IOSTANDARD LVCMOS33} [get_ports lcd_vs]
set_property -dict {PACKAGE_PIN V5 IOSTANDARD LVCMOS33} [get_ports lcd_de]
set_property -dict {PACKAGE_PIN AA6 IOSTANDARD LVCMOS33} [get_ports lcd_bl]
set_property -dict {PACKAGE_PIN V9 IOSTANDARD LVCMOS33} [get_ports lcd_clk]
set_property -dict {PACKAGE_PIN AB9 IOSTANDARD LVCMOS33} [get_ports lcd_rst]

#----------------------LCD触摸接口----------------------------
set_property -dict {PACKAGE_PIN Y4 IOSTANDARD LVCMOS33} [get_ports touch_scl]
set_property -dict {PACKAGE_PIN AA7 IOSTANDARD LVCMOS33} [get_ports touch_sda]
set_property -dict {PACKAGE_PIN AA4 IOSTANDARD LVCMOS33} [get_ports touch_int]
set_property -dict {PACKAGE_PIN W7 IOSTANDARD LVCMOS33} [get_ports touch_rst_n]
set_property PULLUP true [get_ports touch_int]
set_property PULLUP true [get_ports touch_scl]
set_property PULLUP true [get_ports touch_sda]
set_property PULLUP true [get_ports touch_rst_n]

#----------------------LVDS显示接口----------------------------
set_property -dict {PACKAGE_PIN Y20 IOSTANDARD LVDS_25} [get_ports {tx_out_p[0]}]
set_property -dict {PACKAGE_PIN Y21 IOSTANDARD LVDS_25} [get_ports {tx_out_n[0]}]
set_property -dict {PACKAGE_PIN W15 IOSTANDARD LVDS_25} [get_ports {tx_out_p[1]}]
set_property -dict {PACKAGE_PIN Y15 IOSTANDARD LVDS_25} [get_ports {tx_out_n[1]}]
set_property -dict {PACKAGE_PIN W20 IOSTANDARD LVDS_25} [get_ports {tx_out_p[2]}]
set_property -dict {PACKAGE_PIN W21 IOSTANDARD LVDS_25} [get_ports {tx_out_n[2]}]
set_property -dict {PACKAGE_PIN U20 IOSTANDARD LVDS_25} [get_ports {tx_out_p[3]}]
set_property -dict {PACKAGE_PIN V20 IOSTANDARD LVDS_25} [get_ports {tx_out_n[3]}]
set_property -dict {PACKAGE_PIN AA21 IOSTANDARD LVDS_25} [get_ports tx_clk_p]
set_property -dict {PACKAGE_PIN AB21 IOSTANDARD LVDS_25} [get_ports tx_clk_n]

#----------------------摄像头接口----------------------------
create_clock -period 40.000 -name cmos_pclk [get_ports cam_pclk]
set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets cam_pclk_IBUF]
set_property -dict {PACKAGE_PIN D20 IOSTANDARD LVCMOS33} [get_ports cam_pclk]
set_property -dict {PACKAGE_PIN E21 IOSTANDARD LVCMOS33} [get_ports cam_rst_n]
set_property -dict {PACKAGE_PIN C20 IOSTANDARD LVCMOS33} [get_ports cam_pwdn]
set_property -dict {PACKAGE_PIN B21 IOSTANDARD LVCMOS33 IOB TRUE} [get_ports {cam_data[0]}]
set_property -dict {PACKAGE_PIN D21 IOSTANDARD LVCMOS33 IOB TRUE} [get_ports {cam_data[1]}]
set_property -dict {PACKAGE_PIN B22 IOSTANDARD LVCMOS33 IOB TRUE} [get_ports {cam_data[2]}]
set_property -dict {PACKAGE_PIN G19 IOSTANDARD LVCMOS33 IOB TRUE} [get_ports {cam_data[3]}]
set_property -dict {PACKAGE_PIN D22 IOSTANDARD LVCMOS33 IOB TRUE} [get_ports {cam_data[4]}]
set_property -dict {PACKAGE_PIN F19 IOSTANDARD LVCMOS33 IOB TRUE} [get_ports {cam_data[5]}]
set_property -dict {PACKAGE_PIN C22 IOSTANDARD LVCMOS33 IOB TRUE} [get_ports {cam_data[6]}]
set_property -dict {PACKAGE_PIN G17 IOSTANDARD LVCMOS33 IOB TRUE} [get_ports {cam_data[7]}]
set_property -dict {PACKAGE_PIN F21 IOSTANDARD LVCMOS33} [get_ports cam_vsync]
set_property -dict {PACKAGE_PIN F22 IOSTANDARD LVCMOS33} [get_ports cam_href]
set_property -dict {PACKAGE_PIN A21 IOSTANDARD LVCMOS33} [get_ports cam_scl]
set_property -dict {PACKAGE_PIN A22 IOSTANDARD LVCMOS33} [get_ports cam_sda]

#----------------------HDMI输入接口----------------------------
set_property -dict {PACKAGE_PIN W12 IOSTANDARD LVCMOS33} [get_ports iic_scl]
set_property -dict {PACKAGE_PIN V12 IOSTANDARD LVCMOS33} [get_ports iic_sda]
set_property -dict {PACKAGE_PIN Y5 IOSTANDARD LVCMOS33} [get_ports rstn_out]

set_property -dict {PACKAGE_PIN Y6 IOSTANDARD LVCMOS33} [get_ports video_clk_in]
set_property -dict {PACKAGE_PIN U7 IOSTANDARD LVCMOS33} [get_ports video_vs_in]
set_property -dict {PACKAGE_PIN R7 IOSTANDARD LVCMOS33} [get_ports video_hs_in]
set_property -dict {PACKAGE_PIN AB10 IOSTANDARD LVCMOS33} [get_ports video_de_in]
set_property -dict {PACKAGE_PIN AB7 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[0]}]
set_property -dict {PACKAGE_PIN AB6 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[1]}]
set_property -dict {PACKAGE_PIN AB5 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[2]}]
set_property -dict {PACKAGE_PIN AB4 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[3]}]
set_property -dict {PACKAGE_PIN Y11 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[4]}]
set_property -dict {PACKAGE_PIN Y10 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[5]}]
set_property -dict {PACKAGE_PIN Y9 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[6]}]
set_property -dict {PACKAGE_PIN Y8 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[7]}]
set_property -dict {PACKAGE_PIN AA9 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[8]}]
set_property -dict {PACKAGE_PIN AA8 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[9]}]
set_property -dict {PACKAGE_PIN AB2 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[10]}]
set_property -dict {PACKAGE_PIN AB1 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[11]}]
set_property -dict {PACKAGE_PIN W6 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[12]}]
set_property -dict {PACKAGE_PIN W5 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[13]}]
set_property -dict {PACKAGE_PIN U12 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[14]}]
set_property -dict {PACKAGE_PIN U11 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[15]}]
set_property -dict {PACKAGE_PIN R6 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[16]}]
set_property -dict {PACKAGE_PIN T6 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[17]}]
set_property -dict {PACKAGE_PIN U4 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[18]}]
set_property -dict {PACKAGE_PIN T4 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[19]}]
set_property -dict {PACKAGE_PIN V8 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[20]}]
set_property -dict {PACKAGE_PIN W8 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[21]}]
set_property -dict {PACKAGE_PIN W11 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[22]}]
set_property -dict {PACKAGE_PIN W10 IOSTANDARD LVCMOS33} [get_ports {video_rgb_in[23]}]

#----------------------HDMI输出接口--------------------------
set_property -dict {PACKAGE_PIN B19 IOSTANDARD TMDS_33} [get_ports clk_hdmi_out_p]
set_property -dict {PACKAGE_PIN H19 IOSTANDARD TMDS_33} [get_ports {data_hdmi_out_p[0]}]
set_property -dict {PACKAGE_PIN B16 IOSTANDARD TMDS_33} [get_ports {data_hdmi_out_p[1]}]
set_property -dict {PACKAGE_PIN C18 IOSTANDARD TMDS_33} [get_ports {data_hdmi_out_p[2]}]

#----------------------PL端以太网（GE_PL）---------------------------
create_clock -period 8.000 -waveform {0.000 4.000} [get_ports eth_rxc]
set_property -dict {PACKAGE_PIN E19 IOSTANDARD LVCMOS33} [get_ports eth_mdc]
set_property -dict {PACKAGE_PIN E20 IOSTANDARD LVCMOS33} [get_ports eth_mdio]
set_property -dict {PACKAGE_PIN D18 IOSTANDARD LVCMOS33} [get_ports eth_rxc]
set_property -dict {PACKAGE_PIN C19 IOSTANDARD LVCMOS33} [get_ports eth_rx_ctl]
set_property -dict {PACKAGE_PIN E15 IOSTANDARD LVCMOS33} [get_ports {eth_rxd[0]}]
set_property -dict {PACKAGE_PIN D15 IOSTANDARD LVCMOS33} [get_ports {eth_rxd[1]}]
set_property -dict {PACKAGE_PIN F16 IOSTANDARD LVCMOS33} [get_ports {eth_rxd[2]}]
set_property -dict {PACKAGE_PIN E16 IOSTANDARD LVCMOS33} [get_ports {eth_rxd[3]}]
set_property -dict {PACKAGE_PIN B15 IOSTANDARD LVCMOS33} [get_ports eth_txc]
set_property -dict {PACKAGE_PIN C15 IOSTANDARD LVCMOS33} [get_ports eth_tx_ctl]
set_property -dict {PACKAGE_PIN G16 IOSTANDARD LVCMOS33} [get_ports {eth_txd[0]}]
set_property -dict {PACKAGE_PIN G15 IOSTANDARD LVCMOS33} [get_ports {eth_txd[1]}]
set_property -dict {PACKAGE_PIN D17 IOSTANDARD LVCMOS33} [get_ports {eth_txd[2]}]
set_property -dict {PACKAGE_PIN D16 IOSTANDARD LVCMOS33} [get_ports {eth_txd[3]}]

#---------------------------音频（ES8388）--------------------------------
set_property -dict {PACKAGE_PIN A18 IOSTANDARD LVCMOS33} [get_ports aud_scl]
set_property -dict {PACKAGE_PIN A19 IOSTANDARD LVCMOS33} [get_ports aud_sda]
set_property -dict {PACKAGE_PIN U5 IOSTANDARD LVCMOS33} [get_ports aud_adcdat]
set_property -dict {PACKAGE_PIN AB11 IOSTANDARD LVCMOS33} [get_ports aud_dacdat]
set_property -dict {PACKAGE_PIN U6 IOSTANDARD LVCMOS33} [get_ports aud_lrc]
set_property -dict {PACKAGE_PIN U9 IOSTANDARD LVCMOS33} [get_ports aud_mclk]
set_property -dict {PACKAGE_PIN AA11 IOSTANDARD LVCMOS33} [get_ports aud_bclk]
