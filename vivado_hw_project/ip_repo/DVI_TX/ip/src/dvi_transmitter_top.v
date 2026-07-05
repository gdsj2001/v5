`timescale 1ns / 1ps

module dvi_transmitter_top(
    input        pclk,
    input        pclk_x5,
    input        reset_n,
    input [23:0] video_din,
    input        video_hsync,
    input        video_vsync,
    input        video_de,
    output       tmds_clk_p,
    output       tmds_clk_n,
    output [2:0] tmds_data_p,
    output [2:0] tmds_data_n,
    output       tmds_oen
    );

wire        reset_pclk;
wire        reset_serdes_5x;
wire        reset_serdes;
wire        reset_req_pclk;
wire        reset_req_serdes_5x;
wire [9:0]  red_10bit;
wire [9:0]  green_10bit;
wire [9:0]  blue_10bit;
wire [9:0]  clk_10bit;
wire [2:0]  tmds_data_serial;
wire        tmds_clk_serial;
wire        serial_clk_5x_io;

assign clk_10bit = 10'b1111100000;
// Serializer reset uses async request aggregation:
// - reset_req_pclk      : encoder / CLKDIV domain request
// - reset_req_serdes_5x : serializer 5x domain request
// The OR result drives serializer async reset pin.
assign reset_req_pclk      = reset_pclk;
assign reset_req_serdes_5x = reset_serdes_5x;
assign reset_serdes        = reset_req_pclk | reset_req_serdes_5x;
assign tmds_oen  = ~reset_serdes;

BUFG u_bufg_tmds_clk_5x(
    .I (pclk_x5),
    .O (serial_clk_5x_io)
);

// Sync-reset in pixel domain (encoder/pipeline domain).
asyn_rst_syn reset_syn_pclk(
    .reset_n   (reset_n),
    .clk       (pclk),
    .syn_reset (reset_pclk)
);

// Sync-reset in serializer (5x) domain.
asyn_rst_syn reset_syn_serdes(
    .reset_n   (reset_n),
    .clk       (serial_clk_5x_io),
    .syn_reset (reset_serdes_5x)
);

dvi_encoder encoder_b (
    .clkin (pclk),
    .rstin (reset_pclk),
    .din   (video_din[7:0]),
    .c0    (video_hsync),
    .c1    (video_vsync),
    .de    (video_de),
    .dout  (blue_10bit)
);

dvi_encoder encoder_g (
    .clkin (pclk),
    .rstin (reset_pclk),
    .din   (video_din[15:8]),
    .c0    (1'b0),
    .c1    (1'b0),
    .de    (video_de),
    .dout  (green_10bit)
);

dvi_encoder encoder_r (
    .clkin (pclk),
    .rstin (reset_pclk),
    .din   (video_din[23:16]),
    .c0    (1'b0),
    .c1    (1'b0),
    .de    (video_de),
    .dout  (red_10bit)
);

serializer_10_to_1 serializer_b(
    .reset           (reset_serdes),
    .paralell_clk    (pclk),
    .serial_clk_5x   (serial_clk_5x_io),
    .paralell_data   (blue_10bit),
    .serial_data_out (tmds_data_serial[0])
);

serializer_10_to_1 serializer_g(
    .reset           (reset_serdes),
    .paralell_clk    (pclk),
    .serial_clk_5x   (serial_clk_5x_io),
    .paralell_data   (green_10bit),
    .serial_data_out (tmds_data_serial[1])
);

serializer_10_to_1 serializer_r(
    .reset           (reset_serdes),
    .paralell_clk    (pclk),
    .serial_clk_5x   (serial_clk_5x_io),
    .paralell_data   (red_10bit),
    .serial_data_out (tmds_data_serial[2])
);

serializer_10_to_1 serializer_clk(
    .reset           (reset_serdes),
    .paralell_clk    (pclk),
    .serial_clk_5x   (serial_clk_5x_io),
    .paralell_data   (clk_10bit),
    .serial_data_out (tmds_clk_serial)
);

OBUFDS #(
    .IOSTANDARD("TMDS_33")
) TMDS0 (
    .I  (tmds_data_serial[0]),
    .O  (tmds_data_p[0]),
    .OB (tmds_data_n[0])
);

OBUFDS #(
    .IOSTANDARD("TMDS_33")
) TMDS1 (
    .I  (tmds_data_serial[1]),
    .O  (tmds_data_p[1]),
    .OB (tmds_data_n[1])
);

OBUFDS #(
    .IOSTANDARD("TMDS_33")
) TMDS2 (
    .I  (tmds_data_serial[2]),
    .O  (tmds_data_p[2]),
    .OB (tmds_data_n[2])
);

OBUFDS #(
    .IOSTANDARD("TMDS_33")
) TMDS3 (
    .I  (tmds_clk_serial),
    .O  (tmds_clk_p),
    .OB (tmds_clk_n)
);

endmodule
