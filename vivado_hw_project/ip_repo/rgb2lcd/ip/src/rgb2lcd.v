//****************************************Copyright (c)***********************************//
//原子哥在线教学平台：www.yuanzige.com
//技术支持：www.openedv.com
//淘宝店铺：http://openedv.taobao.com
//关注微信公众平台微信号："正点原子"，免费获取ZYNQ & FPGA & STM32 & LINUX资料。
//版权所有，盗版必究。
//Copyright(C) 正点原子 2018-2028
//All rights reserved
//----------------------------------------------------------------------------------------
// File name:           rgb2lcd
// Last modified Date:  2021/6/18 09:48:25
// Last Version:        V1.1
// Descriptions:        RGB格式转LCD格式
//----------------------------------------------------------------------------------------
// Created by:          正点原子
// Created date:        2019/10/8 17:25:36
// Version:             V1.0
// Descriptions:        The original version
//
//----------------------------------------------------------------------------------------
//****************************************************************************************//

module rgb2lcd #(
    //parameter define
    parameter    VID_DATA_WIDTH = 24,
    // Legacy outputs retained for compatibility with older top-level wrappers.
    parameter    LCD_BL_DEFAULT = 1'b1,
    parameter    LCD_RST_DEFAULT = 1'b1
)(
    //VID_OUT
    input  [VID_DATA_WIDTH-1:0]   rgb_data ,
    input                         rgb_vde  ,
    input                         rgb_hsync,
    input                         rgb_vsync,

    input                         pixel_clk,
   
    //RGB LCD
    output                        lcd_pclk ,
    output                        lcd_rst  ,
    output                        lcd_hs   ,
    output                        lcd_vs   ,
    output                        lcd_de   ,
    output                        lcd_bl   ,
   
    //AXI GPIO(LCD ID)
    input  [2:0]                  lcd_id_i, 
    input  [2:0]                  lcd_id_t,
    output [2:0]                  lcd_id_o,
    
    //LCD数据引脚为双向引脚,改成三态引脚的形式  
    input  [VID_DATA_WIDTH-1:0]   lcd_rgb_i,
    output [VID_DATA_WIDTH-1:0]   lcd_rgb_o,
    output [VID_DATA_WIDTH-1:0]   lcd_rgb_t
);

//*****************************************************
//**                  main code
//*****************************************************
wire [VID_DATA_WIDTH-1:0] lcd_id_lane_t;
// Force an internal sampled load for each LCD RGB input lane.
// This prevents IBUF "no internal load" BUFC-1 warnings when ID readback
// only uses sparse lanes (R7/G7/B7).
(* KEEP = "TRUE", DONT_TOUCH = "TRUE" *) reg [VID_DATA_WIDTH-1:0] lcd_rgb_i_monitor;

always @(posedge pixel_clk) begin
    lcd_rgb_i_monitor <= lcd_rgb_i;
end

initial begin
    if (VID_DATA_WIDTH != 24)
        $error("rgb2lcd expects VID_DATA_WIDTH=24 for LCD ID lane mapping.");
end

//LCD信号赋值
assign lcd_pclk = pixel_clk;
assign lcd_de = rgb_vde  ;
assign lcd_hs = rgb_hsync;
assign lcd_vs = rgb_vsync;
assign lcd_bl = LCD_BL_DEFAULT;
assign lcd_rst = LCD_RST_DEFAULT;

//读取LCD ID（只在对应 RGB lane 上进入高阻）
assign lcd_id_o[0] = lcd_id_t[0] ? lcd_rgb_i[23] : lcd_id_i[0];  //R7:M0
assign lcd_id_o[1] = lcd_id_t[1] ? lcd_rgb_i[15] : lcd_id_i[1];  //G7:M1
assign lcd_id_o[2] = lcd_id_t[2] ? lcd_rgb_i[7]  : lcd_id_i[2];  //B7:M2

assign lcd_id_lane_t = {
    lcd_id_t[0], 7'd0,
    lcd_id_t[1], 7'd0,
    lcd_id_t[2], 7'd0
};

assign lcd_rgb_t  = lcd_id_lane_t;
assign lcd_rgb_o  = rgb_data[VID_DATA_WIDTH-1:0] & ~lcd_id_lane_t;

endmodule
