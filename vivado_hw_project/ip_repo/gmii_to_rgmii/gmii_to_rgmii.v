`timescale 1ns / 1ps

module gmii_to_rgmii #(
    // Input delay tap setting (0~31), each tap is about 78 ps at 200 MHz refclk.
    parameter integer IDELAY_VALUE = 0,
    parameter         IDELAY_TYPE  = "FIXED",
    // 1: use recovered RX clock for TX path (legacy-compatible)
    // 0: use raw RGMII_RXC for TX path timing (default in this project)
    parameter integer TX_CLK_FROM_RX = 0
)( 
    input              idelay_clk,

    // GMII RX side (to MAC)
    output             gmii_rx_clk,
    output             gmii_rx_dv,
    output      [7:0]  gmii_rxd,
    output             gmii_rx_er,

    // GMII TX side (from MAC)
    output             gmii_tx_clk,
    input              gmii_tx_en,
    // Optional TX_ER input. tri0 keeps backward compatibility with legacy
    // instantiations that only provide TX_EN/TXD.
    input      tri0    gmii_tx_er,
    input       [7:0]  gmii_txd,

    // RGMII PHY side
    input              rgmii_rxc,
    input              rgmii_rx_ctl,
    input       [3:0]  rgmii_rxd,
    output             rgmii_txc,
    output             rgmii_tx_ctl,
    output      [3:0]  rgmii_txd
);

initial begin
    if ((IDELAY_VALUE < 0) || (IDELAY_VALUE > 31))
        $error("gmii_to_rgmii IDELAY_VALUE must be in [0,31].");

    // This implementation does not expose CE/INC/LD/CNTVALUEIN control pins.
    // Keep mode fixed to avoid a misleading runtime-delay contract.
    if (!((IDELAY_TYPE == "FIXED") || (IDELAY_TYPE == "fixed")))
        $error("gmii_to_rgmii IDELAY_TYPE must be FIXED/fixed in this implementation.");

    if (!((TX_CLK_FROM_RX == 0) || (TX_CLK_FROM_RX == 1)))
        $error("gmii_to_rgmii TX_CLK_FROM_RX must be 0 or 1.");
end

wire gmii_tx_clk_int;

// TX path clock source selection.
// Legacy-compatible mode (TX clock follows recovered RX clock) is enabled when
// TX_CLK_FROM_RX=1; project default keeps TX clock tied to rgmii_rxc.
assign gmii_tx_clk_int = (TX_CLK_FROM_RX != 0) ? gmii_rx_clk : rgmii_rxc;
assign gmii_tx_clk     = gmii_tx_clk_int;

rgmii_rx #(
    .IDELAY_VALUE(IDELAY_VALUE),
    .IDELAY_TYPE (IDELAY_TYPE)
) u_rgmii_rx (
    .idelay_clk  (idelay_clk),
    .gmii_rx_clk (gmii_rx_clk),
    .rgmii_rxc   (rgmii_rxc),
    .rgmii_rx_ctl(rgmii_rx_ctl),
    .rgmii_rxd   (rgmii_rxd),
    .gmii_rx_dv  (gmii_rx_dv),
    .gmii_rx_er  (gmii_rx_er),
    .gmii_rxd    (gmii_rxd)
);

rgmii_tx u_rgmii_tx (
    .gmii_tx_clk (gmii_tx_clk_int),
    .gmii_tx_en  (gmii_tx_en),
    .gmii_tx_er  (gmii_tx_er),
    .gmii_txd    (gmii_txd),
    .rgmii_txc   (rgmii_txc),
    .rgmii_tx_ctl(rgmii_tx_ctl),
    .rgmii_txd   (rgmii_txd)
);

endmodule
