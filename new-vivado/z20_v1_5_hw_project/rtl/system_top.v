`timescale 1 ps / 1 ps

module system_top
   (DDR_addr,
    DDR_ba,
    DDR_cas_n,
    DDR_ck_n,
    DDR_ck_p,
    DDR_cke,
    DDR_cs_n,
    DDR_dm,
    DDR_dq,
    DDR_dqs_n,
    DDR_dqs_p,
    DDR_odt,
    DDR_ras_n,
    DDR_reset_n,
    DDR_we_n,
    FIXED_IO_ddr_vrn,
    FIXED_IO_ddr_vrp,
    FIXED_IO_mio,
    FIXED_IO_ps_clk,
    FIXED_IO_ps_porb,
    FIXED_IO_ps_srstb,
    A1_IO,
    A2_IO,
    A3_IO,
    A4_IO,
    A5_IO,
    A6_IO,
    A7_IO,
    A8_IO,
    ALM1_IO,
    ALM2_IO,
    ALM3_IO,
    ALM4_IO,
    ALM5_IO,
    ALM6_IO,
    ALM7_IO,
    ALM8_IO,
    B1_IO,
    B2_IO,
    B3_IO,
    B4_IO,
    B5_IO,
    B6_IO,
    B7_IO,
    B8_IO,
    BEEP_EN,
    can0_rx,
    can0_tx,
    DI1,
    DI2,
    DI3,
    DI4,
    DI5,
    DI6,
    DI7,
    DI8,
    DI9,
    DI10,
    DI11,
    DI12,
    DI13,
    DI14,
    DI15,
    DI16,
    DI17,
    DI18,
    DIR1_IO,
    DIR2_IO,
    DIR3_IO,
    DIR4_IO,
    DIR5_IO,
    DIR6_IO,
    DIR7_IO,
    DIR8_IO,
    do_out,
    ENA1_IO,
    ENA2_IO,
    ENA3_IO,
    ENA4_IO,
    ENA5_IO,
    ENA6_IO,
    ENA7_IO,
    ENA8_IO,
    estop_nc_in,
    FR_DI1,
    FR_DI2,
    FR_DI3,
    FR_DI4,
    FR_DI5,
    FR_DI6,
    FR_DI7,
    FR_DI8,
    FR_DI9,
    FR_DI10,
    FR_DI11,
    FR_DI12,
    FR_DI13,
    FR_DI14,
    FR_DI15,
    FR_DI16,
    i2c0_scl_io,
    i2c0_sda_io,
    i2c1_scl_io,
    i2c1_sda_io,
    lcd_bl,
    lcd_clk,
    lcd_de,
    lcd_hs,
    lcd_rgb_tri_io,
    lcd_rst,
    lcd_vs,
    mdio_mdc,
    mdio_mdio_io,
    MPG_A,
    MPG_B,
    mpg_axis_sel,
    pl_uart_rxd,
    pl_uart_txd,
    PULS1_IO,
    PULS2_IO,
    PULS3_IO,
    PULS4_IO,
    PULS5_IO,
    PULS6_IO,
    PULS7_IO,
    PULS8_IO,
    pwm_out,
    rgmii_rd,
    rgmii_rx_ctl,
    rgmii_rxc,
    rgmii_td,
    rgmii_tx_ctl,
    rgmii_txc,
    rs232_rxd,
    rs232_txd,
    RS485_FPGA_RX,
    RS485_FPGA_TX,
    SCALE_SEL0,
    SCALE_SEL1,
    SCALE_SEL2,
    TP_INT,
    TP_RST,
    TS_DI,
    Z1_IO,
    Z2_IO,
    Z3_IO,
    Z4_IO,
    Z5_IO,
    Z6_IO,
    Z7_IO,
    Z8_IO);
  inout [14:0]DDR_addr;
  inout [2:0]DDR_ba;
  inout DDR_cas_n;
  inout DDR_ck_n;
  inout DDR_ck_p;
  inout DDR_cke;
  inout DDR_cs_n;
  inout [3:0]DDR_dm;
  inout [31:0]DDR_dq;
  inout [3:0]DDR_dqs_n;
  inout [3:0]DDR_dqs_p;
  inout DDR_odt;
  inout DDR_ras_n;
  inout DDR_reset_n;
  inout DDR_we_n;
  inout FIXED_IO_ddr_vrn;
  inout FIXED_IO_ddr_vrp;
  inout [53:0]FIXED_IO_mio;
  inout FIXED_IO_ps_clk;
  inout FIXED_IO_ps_porb;
  inout FIXED_IO_ps_srstb;
  input A1_IO;
  input A2_IO;
  input A3_IO;
  input A4_IO;
  input A5_IO;
  input A6_IO;
  input A7_IO;
  input A8_IO;
  input ALM1_IO;
  input ALM2_IO;
  input ALM3_IO;
  input ALM4_IO;
  input ALM5_IO;
  input ALM6_IO;
  input ALM7_IO;
  input ALM8_IO;
  input B1_IO;
  input B2_IO;
  input B3_IO;
  input B4_IO;
  input B5_IO;
  input B6_IO;
  input B7_IO;
  input B8_IO;
  output BEEP_EN;
  input can0_rx;
  output can0_tx;
  input DI1;
  input DI2;
  input DI3;
  input DI4;
  input DI5;
  input DI6;
  input DI7;
  input DI8;
  input DI9;
  input DI10;
  input DI11;
  input DI12;
  input DI13;
  input DI14;
  input DI15;
  input DI16;
  input DI17;
  input DI18;
  output DIR1_IO;
  output DIR2_IO;
  output DIR3_IO;
  output DIR4_IO;
  output DIR5_IO;
  output DIR6_IO;
  output DIR7_IO;
  output DIR8_IO;
  output [13:0]do_out;
  output ENA1_IO;
  output ENA2_IO;
  output ENA3_IO;
  output ENA4_IO;
  output ENA5_IO;
  output ENA6_IO;
  output ENA7_IO;
  output ENA8_IO;
  input estop_nc_in;
  input FR_DI1;
  input FR_DI2;
  input FR_DI3;
  input FR_DI4;
  input FR_DI5;
  input FR_DI6;
  input FR_DI7;
  input FR_DI8;
  input FR_DI9;
  input FR_DI10;
  input FR_DI11;
  input FR_DI12;
  input FR_DI13;
  input FR_DI14;
  input FR_DI15;
  input FR_DI16;
  inout i2c0_scl_io;
  inout i2c0_sda_io;
  inout i2c1_scl_io;
  inout i2c1_sda_io;
  output [0:0]lcd_bl;
  output lcd_clk;
  output lcd_de;
  output lcd_hs;
  inout [23:0]lcd_rgb_tri_io;
  output [0:0]lcd_rst;
  output lcd_vs;
  output mdio_mdc;
  inout mdio_mdio_io;
  input MPG_A;
  input MPG_B;
  input [7:0]mpg_axis_sel;
  input pl_uart_rxd;
  output pl_uart_txd;
  output PULS1_IO;
  output PULS2_IO;
  output PULS3_IO;
  output PULS4_IO;
  output PULS5_IO;
  output PULS6_IO;
  output PULS7_IO;
  output PULS8_IO;
  output [1:0]pwm_out;
  input [3:0]rgmii_rd;
  input rgmii_rx_ctl;
  input rgmii_rxc;
  output [3:0]rgmii_td;
  output rgmii_tx_ctl;
  output rgmii_txc;
  input rs232_rxd;
  output rs232_txd;
  input RS485_FPGA_RX;
  output RS485_FPGA_TX;
  input SCALE_SEL0;
  input SCALE_SEL1;
  input SCALE_SEL2;
  input TP_INT;
  output TP_RST;
  input TS_DI;
  input Z1_IO;
  input Z2_IO;
  input Z3_IO;
  input Z4_IO;
  input Z5_IO;
  input Z6_IO;
  input Z7_IO;
  input Z8_IO;

  wire [7:0]wrapper_axis_dir_o;
  wire [7:0]wrapper_axis_ena_o;
  wire [7:0]wrapper_axis_enc_a_i;
  wire [7:0]wrapper_axis_enc_b_i;
  wire [7:0]wrapper_axis_enc_z_i;
  wire [7:0]wrapper_axis_puls_o;
  wire [13:0]wrapper_io_owner_do_o;
  wire [1:0]wrapper_io_owner_pwm_o;
  wire [17:0]wrapper_io_owner_di_i;
  wire [15:0]wrapper_io_owner_fr_di_i;
  wire wrapper_io_owner_ts_di_i;
  wire [7:0]wrapper_io_owner_mpg_axis_sel_i;
  wire wrapper_io_owner_mpg_a_i;
  wire wrapper_io_owner_mpg_b_i;
  wire [2:0]wrapper_io_owner_scale_sel_i;
  wire [7:0]wrapper_io_owner_alarm_i;
  wire wrapper_io_owner_tp_int_i;
  wire wrapper_io_owner_tp_rst_n_o;
  wire [1:0]legacy_pl_led_unused;
  wire estop_hw_active;
  wire [15:0]do_pwm_normal;
  wire [15:0]do_pwm_gated;
  wire do_pwm_forced_off_unused;
  wire [7:0]axis_dir_normal;
  wire [7:0]axis_dir_gated;
  wire [7:0]axis_ena_normal;
  wire [7:0]axis_ena_gated;
  wire [7:0]axis_step_normal;
  wire [7:0]axis_step_gated;

  assign wrapper_axis_enc_a_i = {A8_IO, A7_IO, A6_IO, A5_IO, A4_IO, A3_IO, A2_IO, A1_IO};
  assign wrapper_axis_enc_b_i = {B8_IO, B7_IO, B6_IO, B5_IO, B4_IO, B3_IO, B2_IO, B1_IO};
  assign wrapper_axis_enc_z_i = {Z8_IO, Z7_IO, Z6_IO, Z5_IO, Z4_IO, Z3_IO, Z2_IO, Z1_IO};
  assign wrapper_io_owner_di_i = {DI18, DI17, DI16, DI15, DI14, DI13, DI12, DI11, DI10,
                                  DI9, DI8, DI7, DI6, DI5, DI4, DI3, DI2, DI1};
  assign wrapper_io_owner_fr_di_i = {FR_DI16, FR_DI15, FR_DI14, FR_DI13, FR_DI12, FR_DI11, FR_DI10, FR_DI9,
                                     FR_DI8, FR_DI7, FR_DI6, FR_DI5, FR_DI4, FR_DI3, FR_DI2, FR_DI1};
  assign wrapper_io_owner_ts_di_i = TS_DI;
  assign wrapper_io_owner_mpg_axis_sel_i = mpg_axis_sel;
  assign wrapper_io_owner_mpg_a_i = MPG_A;
  assign wrapper_io_owner_mpg_b_i = MPG_B;
  assign wrapper_io_owner_scale_sel_i = {SCALE_SEL2, SCALE_SEL1, SCALE_SEL0};
  assign wrapper_io_owner_alarm_i = {ALM8_IO, ALM7_IO, ALM6_IO, ALM5_IO, ALM4_IO, ALM3_IO, ALM2_IO, ALM1_IO};
  assign wrapper_io_owner_tp_int_i = TP_INT;
  assign TP_RST = wrapper_io_owner_tp_rst_n_o;
  assign estop_hw_active = ~estop_nc_in;
  assign do_pwm_normal = {wrapper_io_owner_pwm_o, wrapper_io_owner_do_o};
  assign do_out = do_pwm_gated[13:0];
  assign pwm_out = do_pwm_gated[15:14];
  assign axis_step_normal = wrapper_axis_puls_o;
  assign axis_dir_normal = wrapper_axis_dir_o;
  assign axis_ena_normal = wrapper_axis_ena_o;
  assign axis_step_gated = estop_hw_active ? 8'h00 : axis_step_normal;
  assign axis_dir_gated = estop_hw_active ? 8'h00 : axis_dir_normal;
  assign axis_ena_gated = estop_hw_active ? 8'h00 : axis_ena_normal;
  assign PULS1_IO = axis_step_gated[0];
  assign PULS2_IO = axis_step_gated[1];
  assign PULS3_IO = axis_step_gated[2];
  assign PULS4_IO = axis_step_gated[3];
  assign PULS5_IO = axis_step_gated[4];
  assign PULS6_IO = axis_step_gated[5];
  assign PULS7_IO = axis_step_gated[6];
  assign PULS8_IO = axis_step_gated[7];
  assign DIR1_IO = axis_dir_gated[0];
  assign DIR2_IO = axis_dir_gated[1];
  assign DIR3_IO = axis_dir_gated[2];
  assign DIR4_IO = axis_dir_gated[3];
  assign DIR5_IO = axis_dir_gated[4];
  assign DIR6_IO = axis_dir_gated[5];
  assign DIR7_IO = axis_dir_gated[6];
  assign DIR8_IO = axis_dir_gated[7];
  assign ENA1_IO = axis_ena_gated[0];
  assign ENA2_IO = axis_ena_gated[1];
  assign ENA3_IO = axis_ena_gated[2];
  assign ENA4_IO = axis_ena_gated[3];
  assign ENA5_IO = axis_ena_gated[4];
  assign ENA6_IO = axis_ena_gated[5];
  assign ENA7_IO = axis_ena_gated[6];
  assign ENA8_IO = axis_ena_gated[7];
  assign BEEP_EN = 1'b0;
  pl_estop_general_output_gate #(
        .OUTPUT_COUNT(16),
        .SAFE_LEVELS(16'h0000)
    ) top_do_pwm_estop_gate (
        .estop_latched(estop_hw_active),
        .output_in(do_pwm_normal),
        .output_out(do_pwm_gated),
        .forced_off(do_pwm_forced_off_unused));

  system_wrapper system_wrapper_i
       (.DDR_addr(DDR_addr),
        .DDR_ba(DDR_ba),
        .DDR_cas_n(DDR_cas_n),
        .DDR_ck_n(DDR_ck_n),
        .DDR_ck_p(DDR_ck_p),
        .DDR_cke(DDR_cke),
        .DDR_cs_n(DDR_cs_n),
        .DDR_dm(DDR_dm),
        .DDR_dq(DDR_dq),
        .DDR_dqs_n(DDR_dqs_n),
        .DDR_dqs_p(DDR_dqs_p),
        .DDR_odt(DDR_odt),
        .DDR_ras_n(DDR_ras_n),
        .DDR_reset_n(DDR_reset_n),
        .DDR_we_n(DDR_we_n),
        .FIXED_IO_ddr_vrn(FIXED_IO_ddr_vrn),
        .FIXED_IO_ddr_vrp(FIXED_IO_ddr_vrp),
        .FIXED_IO_mio(FIXED_IO_mio),
        .FIXED_IO_ps_clk(FIXED_IO_ps_clk),
        .FIXED_IO_ps_porb(FIXED_IO_ps_porb),
        .FIXED_IO_ps_srstb(FIXED_IO_ps_srstb),
        .axis_dir_o(wrapper_axis_dir_o),
        .axis_ena_o(wrapper_axis_ena_o),
        .axis_enc_a_i(wrapper_axis_enc_a_i),
        .axis_enc_b_i(wrapper_axis_enc_b_i),
        .axis_enc_z_i(wrapper_axis_enc_z_i),
        .axis_puls_o(wrapper_axis_puls_o),
        .can0_rx(can0_rx),
        .can0_tx(can0_tx),
        .estop_nc_in(estop_nc_in),
        .i2c0_scl_io(i2c0_scl_io),
        .i2c0_sda_io(i2c0_sda_io),
        .i2c1_scl_io(i2c1_scl_io),
        .i2c1_sda_io(i2c1_sda_io),
        .io_owner_alarm_i(wrapper_io_owner_alarm_i),
        .io_owner_di_i(wrapper_io_owner_di_i),
        .io_owner_do_o(wrapper_io_owner_do_o),
        .io_owner_fr_di_i(wrapper_io_owner_fr_di_i),
        .io_owner_mpg_a_i(wrapper_io_owner_mpg_a_i),
        .io_owner_mpg_axis_sel_i(wrapper_io_owner_mpg_axis_sel_i),
        .io_owner_mpg_b_i(wrapper_io_owner_mpg_b_i),
        .io_owner_pwm_o(wrapper_io_owner_pwm_o),
        .io_owner_scale_sel_i(wrapper_io_owner_scale_sel_i),
        .io_owner_tp_int_i(wrapper_io_owner_tp_int_i),
        .io_owner_tp_rst_n_o(wrapper_io_owner_tp_rst_n_o),
        .io_owner_ts_di_i(wrapper_io_owner_ts_di_i),
        .lcd_bl(lcd_bl),
        .lcd_clk(lcd_clk),
        .lcd_de(lcd_de),
        .lcd_hs(lcd_hs),
        .lcd_rgb_tri_io(lcd_rgb_tri_io),
        .lcd_rst(lcd_rst),
        .lcd_vs(lcd_vs),
        .mdio_mdc(mdio_mdc),
        .mdio_mdio_io(mdio_mdio_io),
        .pl_led_tri_o(legacy_pl_led_unused),
        .pl_uart_rxd(pl_uart_rxd),
        .pl_uart_txd(pl_uart_txd),
        .rgmii_rd(rgmii_rd),
        .rgmii_rx_ctl(rgmii_rx_ctl),
        .rgmii_rxc(rgmii_rxc),
        .rgmii_td(rgmii_td),
        .rgmii_tx_ctl(rgmii_tx_ctl),
        .rgmii_txc(rgmii_txc),
        .rs232_rxd(rs232_rxd),
        .rs232_txd(rs232_txd),
        .rs485_rxd(RS485_FPGA_RX),
        .rs485_txd(RS485_FPGA_TX));
endmodule
