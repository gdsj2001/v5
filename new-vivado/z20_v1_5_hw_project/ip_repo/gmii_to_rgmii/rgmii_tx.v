`timescale 1ns / 1ps

module rgmii_tx(
    // GMII transmit interface
    input              gmii_tx_clk,
    input              gmii_tx_en,
    input              gmii_tx_er,
    input       [7:0]  gmii_txd,

    // RGMII transmit interface
    output             rgmii_txc,
    output             rgmii_tx_ctl,
    output      [3:0]  rgmii_txd
    );

// Forward TX clock through an ODDR so TXC uses a dedicated clock-output path.
ODDR #(
    .DDR_CLK_EDGE  ("SAME_EDGE"),
    .INIT          (1'b0),
    .SRTYPE        ("SYNC")
) ODDR_txc (
    .Q             (rgmii_txc),
    .C             (gmii_tx_clk),
    .CE            (1'b1),
    .D1            (1'b1),
    .D2            (1'b0),
    .R             (1'b0),
    .S             (1'b0)
);

// TX_CTL coding (RGMII v2.0):
// rising edge  = TX_EN
// falling edge = TX_EN XOR TX_ER
ODDR #(
    .DDR_CLK_EDGE  ("SAME_EDGE"),
    .INIT          (1'b0),
    .SRTYPE        ("SYNC")
) ODDR_ctl (
    .Q             (rgmii_tx_ctl),
    .C             (gmii_tx_clk),
    .CE            (1'b1),
    .D1            (gmii_tx_en),
    .D2            (gmii_tx_en ^ gmii_tx_er),
    .R             (1'b0),
    .S             (1'b0)
);

genvar i;
generate
for (i = 0; i < 4; i = i + 1) begin : txdata_bus
    ODDR #(
        .DDR_CLK_EDGE  ("SAME_EDGE"),
        .INIT          (1'b0),
        .SRTYPE        ("SYNC")
    ) ODDR_data (
        .Q             (rgmii_txd[i]),
        .C             (gmii_tx_clk),
        .CE            (1'b1),
        .D1            (gmii_txd[i]),
        .D2            (gmii_txd[4+i]),
        .R             (1'b0),
        .S             (1'b0)
    );
end
endgenerate

endmodule
