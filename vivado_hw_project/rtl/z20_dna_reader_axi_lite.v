`timescale 1ns / 1ps

module z20_dna_reader_axi_lite #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 6,
    parameter [31:0] BUILD_ID = 32'h20260428
) (
    input  wire                              S_AXI_ACLK,
    input  wire                              S_AXI_ARESETN,

    input  wire [C_S_AXI_ADDR_WIDTH-1:0]     S_AXI_AWADDR,
    input  wire [2:0]                        S_AXI_AWPROT,
    input  wire                              S_AXI_AWVALID,
    output reg                               S_AXI_AWREADY,
    input  wire [C_S_AXI_DATA_WIDTH-1:0]     S_AXI_WDATA,
    input  wire [(C_S_AXI_DATA_WIDTH/8)-1:0] S_AXI_WSTRB,
    input  wire                              S_AXI_WVALID,
    output reg                               S_AXI_WREADY,
    output reg  [1:0]                        S_AXI_BRESP,
    output reg                               S_AXI_BVALID,
    input  wire                              S_AXI_BREADY,

    input  wire [C_S_AXI_ADDR_WIDTH-1:0]     S_AXI_ARADDR,
    input  wire [2:0]                        S_AXI_ARPROT,
    input  wire                              S_AXI_ARVALID,
    output reg                               S_AXI_ARREADY,
    output reg  [C_S_AXI_DATA_WIDTH-1:0]     S_AXI_RDATA,
    output reg  [1:0]                        S_AXI_RRESP,
    output reg                               S_AXI_RVALID,
    input  wire                              S_AXI_RREADY
);

    localparam [31:0] REG_MAGIC    = 32'h444E4130; // "DNA0"
    localparam [31:0] REG_VERSION  = 32'h00010000;
    localparam [31:0] REG_DNA_BITS = 32'd57;

    localparam [2:0] S_RESET      = 3'd0;
    localparam [2:0] S_LOAD       = 3'd1;
    localparam [2:0] S_LOAD_WAIT  = 3'd2;
    localparam [2:0] S_SAMPLE     = 3'd3;
    localparam [2:0] S_SHIFT      = 3'd4;
    localparam [2:0] S_SHIFT_WAIT = 3'd5;
    localparam [2:0] S_DONE       = 3'd6;

    wire dna_dout;
    reg dna_read;
    reg dna_shift;
    reg [2:0] state;
    reg [6:0] bit_idx;
    reg [56:0] dna_value;
    reg dna_done;

    DNA_PORT #(
        .SIM_DNA_VALUE(57'h0123456789ABCDE)
    ) u_dna_port (
        .DOUT  (dna_dout),
        .CLK   (S_AXI_ACLK),
        .DIN   (1'b0),
        .READ  (dna_read),
        .SHIFT (dna_shift)
    );

    always @(posedge S_AXI_ACLK) begin
        if (!S_AXI_ARESETN) begin
            state     <= S_RESET;
            bit_idx   <= 7'd56;
            dna_value <= 57'd0;
            dna_done  <= 1'b0;
            dna_read  <= 1'b0;
            dna_shift <= 1'b0;
        end else begin
            case (state)
                S_RESET: begin
                    bit_idx   <= 7'd56;
                    dna_done  <= 1'b0;
                    dna_read  <= 1'b0;
                    dna_shift <= 1'b0;
                    state     <= S_LOAD;
                end

                S_LOAD: begin
                    dna_read  <= 1'b1;
                    dna_shift <= 1'b0;
                    state     <= S_LOAD_WAIT;
                end

                S_LOAD_WAIT: begin
                    dna_read  <= 1'b0;
                    dna_shift <= 1'b0;
                    state     <= S_SAMPLE;
                end

                S_SAMPLE: begin
                    dna_value[bit_idx] <= dna_dout;
                    if (bit_idx == 0) begin
                        dna_done <= 1'b1;
                        state    <= S_DONE;
                    end else begin
                        state <= S_SHIFT;
                    end
                end

                S_SHIFT: begin
                    dna_shift <= 1'b1;
                    state     <= S_SHIFT_WAIT;
                end

                S_SHIFT_WAIT: begin
                    dna_shift <= 1'b0;
                    bit_idx   <= bit_idx - 1'b1;
                    state     <= S_SAMPLE;
                end

                S_DONE: begin
                    dna_read  <= 1'b0;
                    dna_shift <= 1'b0;
                    dna_done  <= 1'b1;
                end

                default: begin
                    state <= S_RESET;
                end
            endcase
        end
    end

    function [31:0] read_reg;
        input [C_S_AXI_ADDR_WIDTH-1:0] addr;
        begin
            case (addr[5:2])
                4'h0: read_reg = REG_MAGIC;
                4'h1: read_reg = REG_VERSION;
                4'h2: read_reg = {29'd0, 1'b1, dna_done, dna_done};
                4'h3: read_reg = REG_DNA_BITS;
                4'h4: read_reg = dna_value[31:0];
                4'h5: read_reg = {7'd0, dna_value[56:32]};
                4'h6: read_reg = BUILD_ID;
                default: read_reg = 32'd0;
            endcase
        end
    endfunction

    always @(posedge S_AXI_ACLK) begin
        if (!S_AXI_ARESETN) begin
            S_AXI_AWREADY <= 1'b0;
            S_AXI_WREADY  <= 1'b0;
            S_AXI_BRESP   <= 2'b00;
            S_AXI_BVALID  <= 1'b0;
        end else begin
            S_AXI_AWREADY <= 1'b0;
            S_AXI_WREADY  <= 1'b0;

            if (!S_AXI_BVALID && S_AXI_AWVALID && S_AXI_WVALID) begin
                S_AXI_AWREADY <= 1'b1;
                S_AXI_WREADY  <= 1'b1;
                S_AXI_BRESP   <= 2'b00;
                S_AXI_BVALID  <= 1'b1;
            end else if (S_AXI_BVALID && S_AXI_BREADY) begin
                S_AXI_BVALID <= 1'b0;
            end
        end
    end

    always @(posedge S_AXI_ACLK) begin
        if (!S_AXI_ARESETN) begin
            S_AXI_ARREADY <= 1'b0;
            S_AXI_RDATA   <= 32'd0;
            S_AXI_RRESP   <= 2'b00;
            S_AXI_RVALID  <= 1'b0;
        end else begin
            S_AXI_ARREADY <= 1'b0;

            if (!S_AXI_RVALID && S_AXI_ARVALID) begin
                S_AXI_ARREADY <= 1'b1;
                S_AXI_RDATA   <= read_reg(S_AXI_ARADDR);
                S_AXI_RRESP   <= 2'b00;
                S_AXI_RVALID  <= 1'b1;
            end else if (S_AXI_RVALID && S_AXI_RREADY) begin
                S_AXI_RVALID <= 1'b0;
            end
        end
    end

    wire _unused_axi_inputs = &{1'b0, S_AXI_AWPROT, S_AXI_WDATA, S_AXI_WSTRB, S_AXI_ARPROT, 1'b0};

endmodule
