//****************************************Copyright (c)***********************************//
//鍘熷瓙鍝ュ湪绾挎暀瀛﹀钩鍙帮細www.yuanzige.com
//鎶€鏈敮鎸侊細www.openedv.com
//娣樺疂搴楅摵锛歨ttp://openedv.taobao.com 
//鍏虫敞寰俊鍏紬骞冲彴寰俊鍙凤細"姝ｇ偣鍘熷瓙"锛屽厤璐硅幏鍙朲YNQ & FPGA & STM32 & LINUX璧勬枡銆?
//鐗堟潈鎵€鏈夛紝鐩楃増蹇呯┒銆?
//Copyright(C) 姝ｇ偣鍘熷瓙 2018-2028
//All rights reserved                                  
//----------------------------------------------------------------------------------------
// File name:           rgmii_rx
// Last modified Date:  2020/2/13 9:20:14
// Last Version:        V1.0
// Descriptions:        RGMII鎺ユ敹妯″潡
//----------------------------------------------------------------------------------------
// Created by:          姝ｇ偣鍘熷瓙
// Created date:        2020/2/13 9:20:14
// Version:             V1.0
// Descriptions:        The original version
//
//----------------------------------------------------------------------------------------
//****************************************************************************************//

module rgmii_rx #(
    // Default to 0 tap for PHY rgmii-id baseline (PHY-side internal delay).
    parameter integer IDELAY_VALUE = 0,
    // FIXED, VARIABLE, VAR_LOAD, VAR_LOAD_PIPE.
    parameter         IDELAY_TYPE  = "FIXED"
)(
    input              idelay_clk  , //200Mhz鏃堕挓锛孖DELAY鏃堕挓
    
    //浠ュお缃慠GMII鎺ュ彛
    input              rgmii_rxc   , //RGMII鎺ユ敹鏃堕挓
    input              rgmii_rx_ctl, //RGMII鎺ユ敹鏁版嵁鎺у埗淇″彿
    input       [3:0]  rgmii_rxd   , //RGMII鎺ユ敹鏁版嵁    

    //浠ュお缃慓MII鎺ュ彛
    output             gmii_rx_clk , //GMII鎺ユ敹鏃堕挓
    output             gmii_rx_dv  , //GMII鎺ユ敹鏁版嵁鏈夋晥淇″彿
    output             gmii_rx_er  , //GMII鎺ユ敹閿欒淇″彿
    output      [7:0]  gmii_rxd      //GMII鎺ユ敹鏁版嵁   
    );

initial begin
    if ((IDELAY_VALUE < 0) || (IDELAY_VALUE > 31))
        $error("rgmii_rx IDELAY_VALUE must be in [0,31].");
    // Runtime delay-control ports are not exported; constrain mode to FIXED.
    if (!((IDELAY_TYPE == "FIXED") || (IDELAY_TYPE == "fixed")))
        $error("rgmii_rx IDELAY_TYPE must be FIXED/fixed in this implementation.");
end

//wire define
wire         rgmii_rxc_bufg;     //鍏ㄥ眬鏃堕挓缂撳瓨
wire  [3:0]  rgmii_rxd_delay;    //rgmii_rxd杈撳叆寤舵椂
wire         rgmii_rx_ctl_delay; //rgmii_rx_ctl杈撳叆寤舵椂
wire  [1:0]  gmii_rxdv_t;        //涓や綅GMII鎺ユ敹鏈夋晥淇″彿 
wire  [7:0]  gmii_rxd_raw;
wire         idelay_rdy;
localparam integer IDELAY_RST_HOLD_CYCLES = 64;
localparam integer IDELAY_RDY_WAIT_CYCLES = 1024;
reg   [9:0]  idelay_rst_cnt = 10'd0;
reg   [9:0]  idelay_wait_cnt = 10'd0;
reg          idelay_rst = 1'b1;

//*****************************************************
//**                    main code
//*****************************************************

assign gmii_rx_clk = rgmii_rxc_bufg;
assign gmii_rx_dv = idelay_rdy ? (gmii_rxdv_t[0] & gmii_rxdv_t[1]) : 1'b0;
assign gmii_rx_er = idelay_rdy ? (gmii_rxdv_t[0] ^ gmii_rxdv_t[1]) : 1'b0;
assign gmii_rxd   = idelay_rdy ? gmii_rxd_raw : 8'd0;

// Hold IDELAYCTRL reset high for a startup window, then wait for RDY.
// If RDY does not assert within a bounded time (or drops later), retry.
always @(posedge idelay_clk) begin
    if (idelay_rst) begin
        if (idelay_rst_cnt < (IDELAY_RST_HOLD_CYCLES - 1)) begin
            idelay_rst_cnt <= idelay_rst_cnt + 1'b1;
        end else begin
            idelay_rst_cnt <= 10'd0;
            idelay_wait_cnt <= 10'd0;
            idelay_rst <= 1'b0;
        end
    end else if (idelay_rdy) begin
        idelay_wait_cnt <= 10'd0;
    end else if (idelay_wait_cnt < (IDELAY_RDY_WAIT_CYCLES - 1)) begin
        idelay_wait_cnt <= idelay_wait_cnt + 1'b1;
    end else begin
        idelay_rst <= 1'b1;
        idelay_rst_cnt <= 10'd0;
        idelay_wait_cnt <= 10'd0;
    end
end

//鍏ㄥ眬鏃堕挓缂撳瓨
BUFG BUFG_inst (
  .I            (rgmii_rxc),     // 1-bit input: Clock input
  .O            (rgmii_rxc_bufg) // 1-bit output: Clock output
);

//杈撳叆寤舵椂鎺у埗
// Specifies group name for associated IDELAYs/ODELAYs and IDELAYCTRL
(* IODELAY_GROUP = "rgmii_rx_delay" *) 
IDELAYCTRL  IDELAYCTRL_inst (
    .RDY(idelay_rdy),            // 1-bit output: Ready output
    .REFCLK(idelay_clk),         // 1-bit input: Reference clock input
    .RST(idelay_rst)             // 1-bit input: Active high reset input
);

//rgmii_rx_ctl杈撳叆寤舵椂涓庡弻娌块噰鏍?
(* IODELAY_GROUP = "rgmii_rx_delay" *) 
IDELAYE2 #(
  .IDELAY_TYPE     (IDELAY_TYPE),       // FIXED, VARIABLE, VAR_LOAD, VAR_LOAD_PIPE
  .IDELAY_VALUE    (IDELAY_VALUE),      // Input delay tap setting (0-31)
  .REFCLK_FREQUENCY(200.0)              // IDELAYCTRL clock input frequency in MHz 
)
u_delay_rx_ctrl (
  .CNTVALUEOUT     (),                  // 5-bit output: Counter value output
  .DATAOUT         (rgmii_rx_ctl_delay),// 1-bit output: Delayed data output
  .C               (idelay_clk),        // 1-bit input: Clock input
  .CE              (1'b0),              // 1-bit input: enable increment/decrement
  .CINVCTRL        (1'b0),              // 1-bit input: Dynamic clock inversion input
  .CNTVALUEIN      (5'b0),              // 5-bit input: Counter value input
  .DATAIN          (1'b0),              // 1-bit input: Internal delay data input
  .IDATAIN         (rgmii_rx_ctl),      // 1-bit input: Data input from the I/O
  .INC             (1'b0),              // 1-bit input: Increment / Decrement tap delay
  .LD              (1'b0),              // 1-bit input: Load IDELAY_VALUE input
  .LDPIPEEN        (1'b0),              // 1-bit input: Enable PIPELINE register
  .REGRST          (1'b0)               // 1-bit input: Active-high reset tap-delay input
);

//杈撳叆鍙屾部閲囨牱瀵勫瓨鍣?
IDDR #(
    .DDR_CLK_EDGE("SAME_EDGE_PIPELINED"),// "OPPOSITE_EDGE", "SAME_EDGE" 
                                        //    or "SAME_EDGE_PIPELINED" 
    .INIT_Q1  (1'b0),                   // Initial value of Q1: 1'b0 or 1'b1
    .INIT_Q2  (1'b0),                   // Initial value of Q2: 1'b0 or 1'b1
    .SRTYPE   ("SYNC")                  // Set/Reset type: "SYNC" or "ASYNC" 
) u_iddr_rx_ctl (
    .Q1       (gmii_rxdv_t[0]),         // 1-bit output for positive edge of clock
    .Q2       (gmii_rxdv_t[1]),         // 1-bit output for negative edge of clock
    .C        (rgmii_rxc_bufg),         // 1-bit clock input
    .CE       (1'b1),                   // 1-bit clock enable input
    .D        (rgmii_rx_ctl_delay),     // 1-bit DDR data input
    .R        (1'b0),                   // 1-bit reset
    .S        (1'b0)                    // 1-bit set
);

//rgmii_rxd杈撳叆寤舵椂涓庡弻娌块噰鏍?
genvar i;
generate for (i=0; i<4; i=i+1)
    (* IODELAY_GROUP = "rgmii_rx_delay" *) 
    begin : rxdata_bus
        //杈撳叆寤舵椂           
        (* IODELAY_GROUP = "rgmii_rx_delay" *) 
        IDELAYE2 #(
          .IDELAY_TYPE     (IDELAY_TYPE),       // FIXED,VARIABLE,VAR_LOAD,VAR_LOAD_PIPE
          .IDELAY_VALUE    (IDELAY_VALUE),      // Input delay tap setting (0-31)    
          .REFCLK_FREQUENCY(200.0)              // IDELAYCTRL clock input frequency in MHz
        )
        u_delay_rxd (
          .CNTVALUEOUT     (),                  // 5-bit output: Counter value output
          .DATAOUT         (rgmii_rxd_delay[i]),// 1-bit output: Delayed data output
          .C               (idelay_clk),        // 1-bit input: Clock input
          .CE              (1'b0),              // 1-bit input: enable increment/decrement
          .CINVCTRL        (1'b0),              // 1-bit input: Dynamic clock inversion
          .CNTVALUEIN      (5'b0),              // 5-bit input: Counter value input
          .DATAIN          (1'b0),              // 1-bit input: Internal delay data input
          .IDATAIN         (rgmii_rxd[i]),      // 1-bit input: Data input from the I/O
          .INC             (1'b0),              // 1-bit input: Inc/Decrement tap delay
          .LD              (1'b0),              // 1-bit input: Load IDELAY_VALUE input
          .LDPIPEEN        (1'b0),              // 1-bit input: Enable PIPELINE register 
          .REGRST          (1'b0)               // 1-bit input: Active-high reset tap-delay
        );
        
        //杈撳叆鍙屾部閲囨牱瀵勫瓨鍣?
        IDDR #(
            .DDR_CLK_EDGE("SAME_EDGE_PIPELINED"),// "OPPOSITE_EDGE", "SAME_EDGE" 
                                                //    or "SAME_EDGE_PIPELINED" 
            .INIT_Q1  (1'b0),                   // Initial value of Q1: 1'b0 or 1'b1
            .INIT_Q2  (1'b0),                   // Initial value of Q2: 1'b0 or 1'b1
            .SRTYPE   ("SYNC")                  // Set/Reset type: "SYNC" or "ASYNC" 
        ) u_iddr_rxd (
            .Q1       (gmii_rxd_raw[i]),        // 1-bit output for positive edge of clock
            .Q2       (gmii_rxd_raw[4+i]),      // 1-bit output for negative edge of clock
            .C        (rgmii_rxc_bufg),         // 1-bit clock input
            .CE       (1'b1),                   // 1-bit clock enable input
            .D        (rgmii_rxd_delay[i]),     // 1-bit DDR data input
            .R        (1'b0),                   // 1-bit reset
            .S        (1'b0)                    // 1-bit set
        );
    end
endgenerate

endmodule
