`timescale 1ns / 1ps

module asyn_rst_syn(
    input  wire clk,
    input  wire reset_n,
    output wire syn_reset
);

// Keep this as a 2-FF synchronizer in implementation.
(* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg reset_1;
(* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg reset_2;

assign syn_reset = reset_2;

always @(posedge clk or negedge reset_n) begin
    if (!reset_n) begin
        reset_1 <= 1'b1;
        reset_2 <= 1'b1;
    end else begin
        reset_1 <= 1'b0;
        reset_2 <= reset_1;
    end
end

endmodule
