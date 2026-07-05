//////////////////////////////////////////////////////////////////////////////
//
//  Xilinx, Inc. 2008                 www.xilinx.com
//
//////////////////////////////////////////////////////////////////////////////
//
//  File name :       dvi_encoder.v
//
//  Description :     TMDS encoder  
//
//  Date - revision : Jan. 2008 - v 1.0
//
//  Author :          Bob Feng
//
//  Copyright 2006 Xilinx, Inc.
//  All rights reserved
//
//////////////////////////////////////////////////////////////////////////////  
`timescale 1 ps / 1ps

module dvi_encoder (
  input            clkin,    // pixel clock input
  input            rstin,    // async. reset input (active high)
  input      [7:0] din,      // data inputs: expect registered
  input            c0,       // c0 input
  input            c1,       // c1 input
  input            de,       // de input
  output reg [9:0] dout      // data outputs
);

  ////////////////////////////////////////////////////////////
  // Counting number of 1s and 0s for each incoming pixel
  // component. Pipe line the result.
  // Register Data Input so it matches the pipe lined adder
  // output
  ////////////////////////////////////////////////////////////
  reg [3:0] n1d; //number of 1s in din
  reg [7:0] din_q;

//计算像素数据中“1”的个数
  always @ (posedge clkin or posedge rstin) begin
    if (rstin) begin
      n1d <= 4'd0;
      din_q <= 8'd0;
    end else begin
      n1d <= din[0] + din[1] + din[2] + din[3] + din[4] + din[5] + din[6] + din[7];
      din_q <= din;
    end
  end

  ///////////////////////////////////////////////////////
  // Stage 1: 8 bit -> 9 bit
  // Refer to DVI 1.0 Specification, page 29, Figure 3-5
  ///////////////////////////////////////////////////////
  wire decision1;

  assign decision1 = (n1d > 4'h4) | ((n1d == 4'h4) & (din_q[0] == 1'b0));

  wire [8:0] q_m;
  assign q_m[0] = din_q[0];
  assign q_m[1] = (decision1) ? (q_m[0] ^~ din_q[1]) : (q_m[0] ^ din_q[1]);
  assign q_m[2] = (decision1) ? (q_m[1] ^~ din_q[2]) : (q_m[1] ^ din_q[2]);
  assign q_m[3] = (decision1) ? (q_m[2] ^~ din_q[3]) : (q_m[2] ^ din_q[3]);
  assign q_m[4] = (decision1) ? (q_m[3] ^~ din_q[4]) : (q_m[3] ^ din_q[4]);
  assign q_m[5] = (decision1) ? (q_m[4] ^~ din_q[5]) : (q_m[4] ^ din_q[5]);
  assign q_m[6] = (decision1) ? (q_m[5] ^~ din_q[6]) : (q_m[5] ^ din_q[6]);
  assign q_m[7] = (decision1) ? (q_m[6] ^~ din_q[7]) : (q_m[6] ^ din_q[7]);
  assign q_m[8] = (decision1) ? 1'b0 : 1'b1;

  /////////////////////////////////////////////////////////
  // Stage 2: 9 bit -> 10 bit
  // Refer to DVI 1.0 Specification, page 29, Figure 3-5
  /////////////////////////////////////////////////////////
  reg [3:0] n1q_m, n0q_m; // number of 1s and 0s for q_m
  always @ (posedge clkin or posedge rstin) begin
    if (rstin) begin
      n1q_m <= 4'd0;
      n0q_m <= 4'd0;
    end else begin
      n1q_m <= q_m[0] + q_m[1] + q_m[2] + q_m[3] + q_m[4] + q_m[5] + q_m[6] + q_m[7];
      n0q_m <= 4'h8 - (q_m[0] + q_m[1] + q_m[2] + q_m[3] + q_m[4] + q_m[5] + q_m[6] + q_m[7]);
    end
  end

  parameter CTRLTOKEN0 = 10'b1101010100;
  parameter CTRLTOKEN1 = 10'b0010101011;
  parameter CTRLTOKEN2 = 10'b0101010100;
  parameter CTRLTOKEN3 = 10'b1010101011;

  reg signed [4:0] cnt; // disparity counter
  wire decision2, decision3;
  wire signed [4:0] diff_1m0; // n1q_m - n0q_m
  wire signed [4:0] diff_0m1; // n0q_m - n1q_m
  wire signed [4:0] q_m_adj_pos;
  wire signed [4:0] q_m_adj_neg;

  assign decision2 = (cnt == 5'h0) | (n1q_m == n0q_m);
  assign diff_1m0 = $signed({1'b0, n1q_m}) - $signed({1'b0, n0q_m});
  assign diff_0m1 = -diff_1m0;
  assign q_m_adj_pos = q_m_reg[8] ? 5'sd2 : 5'sd0;
  assign q_m_adj_neg = q_m_reg[8] ? 5'sd0 : 5'sd2;
  /////////////////////////////////////////////////////////////////////////
  // [(cnt > 0) and (N1q_m > N0q_m)] or [(cnt < 0) and (N0q_m > N1q_m)]
  /////////////////////////////////////////////////////////////////////////
  assign decision3 = ((cnt > 0) && (n1q_m > n0q_m)) || ((cnt < 0) && (n0q_m > n1q_m));

  ////////////////////////////////////
  // pipe line alignment
  ////////////////////////////////////
  reg       de_q, de_reg;
  reg       c0_q, c1_q;
  reg       c0_reg, c1_reg;
  reg [8:0] q_m_reg;

  always @ (posedge clkin or posedge rstin) begin
    if (rstin) begin
      de_q <= 1'b0;
      de_reg <= 1'b0;
      c0_q <= 1'b0;
      c0_reg <= 1'b0;
      c1_q <= 1'b0;
      c1_reg <= 1'b0;
      q_m_reg <= 9'd0;
    end else begin
      de_q <= de;
      de_reg <= de_q;
      c0_q <= c0;
      c0_reg <= c0_q;
      c1_q <= c1;
      c1_reg <= c1_q;
      q_m_reg <= q_m;
    end
  end

  ///////////////////////////////
  // 10-bit out
  // disparity counter
  ///////////////////////////////
  always @ (posedge clkin or posedge rstin) begin
    if(rstin) begin
      dout <= 10'h0;
      cnt <= 5'h0;
    end else begin
      if (de_reg) begin
        if(decision2) begin
          dout[9]   <= ~q_m_reg[8]; 
          dout[8]   <= q_m_reg[8]; 
          dout[7:0] <= (q_m_reg[8]) ? q_m_reg[7:0] : ~q_m_reg[7:0];

          cnt <= (~q_m_reg[8]) ? (cnt + diff_0m1) : (cnt + diff_1m0);
        end else begin
          if(decision3) begin
            dout[9]   <= 1'b1;
            dout[8]   <= q_m_reg[8];
            dout[7:0] <= ~q_m_reg[7:0];

            cnt <= cnt + q_m_adj_pos + diff_0m1;
          end else begin
            dout[9]   <= 1'b0;
            dout[8]   <= q_m_reg[8];
            dout[7:0] <= q_m_reg[7:0];

            cnt <= cnt - q_m_adj_neg + diff_1m0;
          end
        end
      end else begin
        case ({c1_reg, c0_reg})
          2'b00:   dout <= CTRLTOKEN0;
          2'b01:   dout <= CTRLTOKEN1;
          2'b10:   dout <= CTRLTOKEN2;
          default: dout <= CTRLTOKEN3;
        endcase

        cnt <= 5'h0;
      end
    end
  end
  
endmodule 
