`timescale 1ns / 1ps

module pl_estop_axi_lite #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 6,
    parameter integer CLK_HZ = 100000000,
    parameter integer DEBOUNCE_MS = 10,
    parameter integer BRAKE_LEAD_US = 50,
    parameter integer AXIS_COUNT = 8,
    parameter integer Z_AXIS_INDEX = 2,
    parameter integer GENERAL_OUTPUT_COUNT = 16,
    parameter [GENERAL_OUTPUT_COUNT-1:0] GENERAL_OUTPUT_SAFE_LEVELS = {GENERAL_OUTPUT_COUNT{1'b0}},
    parameter integer BUS_TX_GATE_COUNT = 1,
    parameter [BUS_TX_GATE_COUNT-1:0] BUS_TX_IDLE_LEVELS = {BUS_TX_GATE_COUNT{1'b0}},
    parameter [31:0] BUILD_ID = 32'h20260629
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
    input  wire                              S_AXI_RREADY,

    input  wire                              estop_nc_in,
    input  wire [AXIS_COUNT-1:0]             step_in,
    input  wire [AXIS_COUNT-1:0]             enable_in,
    input  wire [GENERAL_OUTPUT_COUNT-1:0]   general_output_in,
    input  wire [BUS_TX_GATE_COUNT-1:0]      bus_tx_enable_in,
    input  wire                              bus_tx_queue_flushed_in,
    output wire [AXIS_COUNT-1:0]             step_out,
    output wire [AXIS_COUNT-1:0]             enable_out,
    output wire [GENERAL_OUTPUT_COUNT-1:0]   general_output_out,
    output wire [BUS_TX_GATE_COUNT-1:0]      bus_tx_enable_out,
    output wire                              bus_tx_queue_flush_req,
    output wire                              bus_tx_gate_active,
    output wire                              bus_tx_queue_flushed,
    output wire                              brake_z_out,
    output wire                              estop_irq
);

    localparam [31:0] REG_MAGIC   = 32'h45535450; // "ESTP"
    localparam [31:0] REG_VERSION = 32'h00010001;

    localparam [3:0] ADDR_MAGIC       = 4'h0;
    localparam [3:0] ADDR_VERSION     = 4'h1;
    localparam [3:0] ADDR_STATUS      = 4'h2;
    localparam [3:0] ADDR_CONTROL     = 4'h3;
    localparam [3:0] ADDR_TIMING      = 4'h4;
    localparam [3:0] ADDR_AXIS_CONFIG = 4'h5;
    localparam [3:0] ADDR_BUILD_ID    = 4'h6;
    localparam [3:0] ADDR_GENERAL_CONFIG = 4'h7;
    localparam [3:0] ADDR_BUS_TX_CONFIG  = 4'h8;

    localparam [15:0] DEBOUNCE_MS_VAL = DEBOUNCE_MS;
    localparam [15:0] BRAKE_LEAD_US_VAL = BRAKE_LEAD_US;
    localparam [15:0] AXIS_COUNT_VAL = AXIS_COUNT;
    localparam [15:0] Z_AXIS_INDEX_VAL = Z_AXIS_INDEX;
    localparam [15:0] GENERAL_OUTPUT_COUNT_VAL = GENERAL_OUTPUT_COUNT;
    localparam [15:0] BUS_TX_GATE_COUNT_VAL = BUS_TX_GATE_COUNT;

    reg sw_reset_req;
    reg irq_clear;

    wire estop_latched;
    wire estop_input_raw;
    wire estop_input_filtered;
    wire reset_allowed;
    wire brake_delay_active;
    wire general_output_forced_off;
    wire [15:0] bus_tx_idle_levels_status;

    generate
        if (BUS_TX_GATE_COUNT >= 16) begin : g_bus_tx_status_truncate
            assign bus_tx_idle_levels_status = BUS_TX_IDLE_LEVELS[15:0];
        end else begin : g_bus_tx_status_pad
            assign bus_tx_idle_levels_status = {{(16-BUS_TX_GATE_COUNT){1'b0}}, BUS_TX_IDLE_LEVELS};
        end
    endgenerate

    pl_estop_core #(
        .CLK_HZ(CLK_HZ),
        .DEBOUNCE_MS(DEBOUNCE_MS),
        .BRAKE_LEAD_US(BRAKE_LEAD_US),
        .AXIS_COUNT(AXIS_COUNT),
        .Z_AXIS_INDEX(Z_AXIS_INDEX),
        .GENERAL_OUTPUT_COUNT(GENERAL_OUTPUT_COUNT),
        .GENERAL_OUTPUT_SAFE_LEVELS(GENERAL_OUTPUT_SAFE_LEVELS),
        .BUS_TX_GATE_COUNT(BUS_TX_GATE_COUNT),
        .BUS_TX_IDLE_LEVELS(BUS_TX_IDLE_LEVELS)
    ) u_estop_core (
        .clk(S_AXI_ACLK),
        .rst_n(S_AXI_ARESETN),
        .estop_nc_in(estop_nc_in),
        .sw_reset_req(sw_reset_req),
        .irq_clear(irq_clear),
        .step_in(step_in),
        .enable_in(enable_in),
        .general_output_in(general_output_in),
        .bus_tx_enable_in(bus_tx_enable_in),
        .bus_tx_queue_flushed_in(bus_tx_queue_flushed_in),
        .step_out(step_out),
        .enable_out(enable_out),
        .general_output_out(general_output_out),
        .bus_tx_enable_out(bus_tx_enable_out),
        .bus_tx_queue_flush_req(bus_tx_queue_flush_req),
        .brake_z_out(brake_z_out),
        .estop_irq(estop_irq),
        .estop_latched(estop_latched),
        .estop_input_raw(estop_input_raw),
        .estop_input_filtered(estop_input_filtered),
        .reset_allowed(reset_allowed),
        .brake_delay_active(brake_delay_active),
        .general_output_forced_off(general_output_forced_off),
        .bus_tx_gate_active(bus_tx_gate_active),
        .bus_tx_queue_flushed(bus_tx_queue_flushed)
    );

    function [31:0] read_reg;
        input [C_S_AXI_ADDR_WIDTH-1:0] addr;
        begin
            case (addr[5:2])
                ADDR_MAGIC: read_reg = REG_MAGIC;
                ADDR_VERSION: read_reg = REG_VERSION;
                ADDR_STATUS: begin
                    read_reg = {
                        23'd0,
                        bus_tx_queue_flushed,
                        bus_tx_gate_active,
                        general_output_forced_off,
                        estop_irq,
                        brake_delay_active,
                        reset_allowed,
                        estop_input_filtered,
                        estop_input_raw,
                        estop_latched
                    };
                end
                ADDR_CONTROL: read_reg = 32'd0;
                ADDR_TIMING: read_reg = {DEBOUNCE_MS_VAL, BRAKE_LEAD_US_VAL};
                ADDR_AXIS_CONFIG: read_reg = {AXIS_COUNT_VAL, Z_AXIS_INDEX_VAL};
                ADDR_BUILD_ID: read_reg = BUILD_ID;
                ADDR_GENERAL_CONFIG: read_reg = {GENERAL_OUTPUT_COUNT_VAL, GENERAL_OUTPUT_SAFE_LEVELS[15:0]};
                ADDR_BUS_TX_CONFIG: read_reg = {BUS_TX_GATE_COUNT_VAL, bus_tx_idle_levels_status};
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
            sw_reset_req  <= 1'b0;
            irq_clear     <= 1'b0;
        end else begin
            S_AXI_AWREADY <= 1'b0;
            S_AXI_WREADY  <= 1'b0;
            sw_reset_req  <= 1'b0;
            irq_clear     <= 1'b0;

            if (!S_AXI_BVALID && S_AXI_AWVALID && S_AXI_WVALID) begin
                S_AXI_AWREADY <= 1'b1;
                S_AXI_WREADY  <= 1'b1;
                S_AXI_BRESP   <= 2'b00;
                S_AXI_BVALID  <= 1'b1;

                if (S_AXI_AWADDR[5:2] == ADDR_CONTROL && S_AXI_WSTRB[0]) begin
                    sw_reset_req <= S_AXI_WDATA[0];
                    irq_clear    <= S_AXI_WDATA[1];
                end
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

    wire _unused_axi_inputs = &{1'b0, S_AXI_AWPROT, S_AXI_ARPROT, 1'b0};

endmodule

module pl_estop_axi_lite_v2 #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 6,
    parameter integer CLK_HZ = 100000000,
    parameter integer DEBOUNCE_MS = 10,
    parameter integer BRAKE_LEAD_US = 50,
    parameter integer AXIS_COUNT = 8,
    parameter integer Z_AXIS_INDEX = 2,
    parameter [15:0] GENERAL_OUTPUT_SAFE_LEVELS = 16'h0000,
    parameter [31:0] BUILD_ID = 32'h20260629
) (
    input  wire                              S_AXI_ACLK,
    input  wire                              S_AXI_ARESETN,

    input  wire [C_S_AXI_ADDR_WIDTH-1:0]     S_AXI_AWADDR,
    input  wire [2:0]                        S_AXI_AWPROT,
    input  wire                              S_AXI_AWVALID,
    output wire                              S_AXI_AWREADY,
    input  wire [C_S_AXI_DATA_WIDTH-1:0]     S_AXI_WDATA,
    input  wire [(C_S_AXI_DATA_WIDTH/8)-1:0] S_AXI_WSTRB,
    input  wire                              S_AXI_WVALID,
    output wire                              S_AXI_WREADY,
    output wire [1:0]                        S_AXI_BRESP,
    output wire                              S_AXI_BVALID,
    input  wire                              S_AXI_BREADY,

    input  wire [C_S_AXI_ADDR_WIDTH-1:0]     S_AXI_ARADDR,
    input  wire [2:0]                        S_AXI_ARPROT,
    input  wire                              S_AXI_ARVALID,
    output wire                              S_AXI_ARREADY,
    output wire [C_S_AXI_DATA_WIDTH-1:0]     S_AXI_RDATA,
    output wire [1:0]                        S_AXI_RRESP,
    output wire                              S_AXI_RVALID,
    input  wire                              S_AXI_RREADY,

    input  wire                              estop_nc_in,
    input  wire [AXIS_COUNT-1:0]             step_in,
    input  wire [AXIS_COUNT-1:0]             enable_in,
    input  wire [15:0]                       general_output_in,
    output wire [AXIS_COUNT-1:0]             step_out,
    output wire [AXIS_COUNT-1:0]             enable_out,
    output wire [15:0]                       general_output_out,
    output wire                              brake_z_out,
    output wire                              estop_irq
);

    wire [0:0] v2_bus_tx_enable_out;
    wire v2_bus_tx_queue_flush_req;
    wire v2_bus_tx_gate_active;
    wire v2_bus_tx_queue_flushed;

    pl_estop_axi_lite #(
        .C_S_AXI_DATA_WIDTH(C_S_AXI_DATA_WIDTH),
        .C_S_AXI_ADDR_WIDTH(C_S_AXI_ADDR_WIDTH),
        .CLK_HZ(CLK_HZ),
        .DEBOUNCE_MS(DEBOUNCE_MS),
        .BRAKE_LEAD_US(BRAKE_LEAD_US),
        .AXIS_COUNT(AXIS_COUNT),
        .Z_AXIS_INDEX(Z_AXIS_INDEX),
        .GENERAL_OUTPUT_COUNT(16),
        .GENERAL_OUTPUT_SAFE_LEVELS(GENERAL_OUTPUT_SAFE_LEVELS),
        .BUS_TX_GATE_COUNT(1),
        .BUS_TX_IDLE_LEVELS(1'b0),
        .BUILD_ID(BUILD_ID)
    ) u_impl (
        .S_AXI_ACLK(S_AXI_ACLK),
        .S_AXI_ARESETN(S_AXI_ARESETN),
        .S_AXI_AWADDR(S_AXI_AWADDR),
        .S_AXI_AWPROT(S_AXI_AWPROT),
        .S_AXI_AWVALID(S_AXI_AWVALID),
        .S_AXI_AWREADY(S_AXI_AWREADY),
        .S_AXI_WDATA(S_AXI_WDATA),
        .S_AXI_WSTRB(S_AXI_WSTRB),
        .S_AXI_WVALID(S_AXI_WVALID),
        .S_AXI_WREADY(S_AXI_WREADY),
        .S_AXI_BRESP(S_AXI_BRESP),
        .S_AXI_BVALID(S_AXI_BVALID),
        .S_AXI_BREADY(S_AXI_BREADY),
        .S_AXI_ARADDR(S_AXI_ARADDR),
        .S_AXI_ARPROT(S_AXI_ARPROT),
        .S_AXI_ARVALID(S_AXI_ARVALID),
        .S_AXI_ARREADY(S_AXI_ARREADY),
        .S_AXI_RDATA(S_AXI_RDATA),
        .S_AXI_RRESP(S_AXI_RRESP),
        .S_AXI_RVALID(S_AXI_RVALID),
        .S_AXI_RREADY(S_AXI_RREADY),
        .estop_nc_in(estop_nc_in),
        .step_in(step_in),
        .enable_in(enable_in),
        .general_output_in(general_output_in),
        .bus_tx_enable_in(1'b0),
        .bus_tx_queue_flushed_in(1'b1),
        .step_out(step_out),
        .enable_out(enable_out),
        .general_output_out(general_output_out),
        .bus_tx_enable_out(v2_bus_tx_enable_out),
        .bus_tx_queue_flush_req(v2_bus_tx_queue_flush_req),
        .bus_tx_gate_active(v2_bus_tx_gate_active),
        .bus_tx_queue_flushed(v2_bus_tx_queue_flushed),
        .brake_z_out(brake_z_out),
        .estop_irq(estop_irq)
    );

endmodule

module pl_estop_axi_lite_v3 #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 6,
    parameter integer CLK_HZ = 100000000,
    parameter integer DEBOUNCE_MS = 10,
    parameter integer BRAKE_LEAD_US = 50,
    parameter integer AXIS_COUNT = 8,
    parameter integer Z_AXIS_INDEX = 2,
    parameter [15:0] GENERAL_OUTPUT_SAFE_LEVELS = 16'h0000,
    parameter [0:0] BUS_TX_IDLE_LEVELS = 1'b0,
    parameter [31:0] BUILD_ID = 32'h20260629
) (
    input  wire                              S_AXI_ACLK,
    input  wire                              S_AXI_ARESETN,

    input  wire [C_S_AXI_ADDR_WIDTH-1:0]     S_AXI_AWADDR,
    input  wire [2:0]                        S_AXI_AWPROT,
    input  wire                              S_AXI_AWVALID,
    output wire                              S_AXI_AWREADY,
    input  wire [C_S_AXI_DATA_WIDTH-1:0]     S_AXI_WDATA,
    input  wire [(C_S_AXI_DATA_WIDTH/8)-1:0] S_AXI_WSTRB,
    input  wire                              S_AXI_WVALID,
    output wire                              S_AXI_WREADY,
    output wire [1:0]                        S_AXI_BRESP,
    output wire                              S_AXI_BVALID,
    input  wire                              S_AXI_BREADY,

    input  wire [C_S_AXI_ADDR_WIDTH-1:0]     S_AXI_ARADDR,
    input  wire [2:0]                        S_AXI_ARPROT,
    input  wire                              S_AXI_ARVALID,
    output wire                              S_AXI_ARREADY,
    output wire [C_S_AXI_DATA_WIDTH-1:0]     S_AXI_RDATA,
    output wire [1:0]                        S_AXI_RRESP,
    output wire                              S_AXI_RVALID,
    input  wire                              S_AXI_RREADY,

    input  wire                              estop_nc_in,
    input  wire [AXIS_COUNT-1:0]             step_in,
    input  wire [AXIS_COUNT-1:0]             enable_in,
    input  wire [15:0]                       general_output_in,
    input  wire [0:0]                        bus_tx_enable_in,
    input  wire                              bus_tx_queue_flushed_in,
    output wire [AXIS_COUNT-1:0]             step_out,
    output wire [AXIS_COUNT-1:0]             enable_out,
    output wire [15:0]                       general_output_out,
    output wire [0:0]                        bus_tx_enable_out,
    output wire                              bus_tx_queue_flush_req,
    output wire                              bus_tx_gate_active,
    output wire                              bus_tx_queue_flushed,
    output wire                              brake_z_out,
    output wire                              estop_irq
);

    pl_estop_axi_lite #(
        .C_S_AXI_DATA_WIDTH(C_S_AXI_DATA_WIDTH),
        .C_S_AXI_ADDR_WIDTH(C_S_AXI_ADDR_WIDTH),
        .CLK_HZ(CLK_HZ),
        .DEBOUNCE_MS(DEBOUNCE_MS),
        .BRAKE_LEAD_US(BRAKE_LEAD_US),
        .AXIS_COUNT(AXIS_COUNT),
        .Z_AXIS_INDEX(Z_AXIS_INDEX),
        .GENERAL_OUTPUT_COUNT(16),
        .GENERAL_OUTPUT_SAFE_LEVELS(GENERAL_OUTPUT_SAFE_LEVELS),
        .BUS_TX_GATE_COUNT(1),
        .BUS_TX_IDLE_LEVELS(BUS_TX_IDLE_LEVELS),
        .BUILD_ID(BUILD_ID)
    ) u_impl (
        .S_AXI_ACLK(S_AXI_ACLK),
        .S_AXI_ARESETN(S_AXI_ARESETN),
        .S_AXI_AWADDR(S_AXI_AWADDR),
        .S_AXI_AWPROT(S_AXI_AWPROT),
        .S_AXI_AWVALID(S_AXI_AWVALID),
        .S_AXI_AWREADY(S_AXI_AWREADY),
        .S_AXI_WDATA(S_AXI_WDATA),
        .S_AXI_WSTRB(S_AXI_WSTRB),
        .S_AXI_WVALID(S_AXI_WVALID),
        .S_AXI_WREADY(S_AXI_WREADY),
        .S_AXI_BRESP(S_AXI_BRESP),
        .S_AXI_BVALID(S_AXI_BVALID),
        .S_AXI_BREADY(S_AXI_BREADY),
        .S_AXI_ARADDR(S_AXI_ARADDR),
        .S_AXI_ARPROT(S_AXI_ARPROT),
        .S_AXI_ARVALID(S_AXI_ARVALID),
        .S_AXI_ARREADY(S_AXI_ARREADY),
        .S_AXI_RDATA(S_AXI_RDATA),
        .S_AXI_RRESP(S_AXI_RRESP),
        .S_AXI_RVALID(S_AXI_RVALID),
        .S_AXI_RREADY(S_AXI_RREADY),
        .estop_nc_in(estop_nc_in),
        .step_in(step_in),
        .enable_in(enable_in),
        .general_output_in(general_output_in),
        .bus_tx_enable_in(bus_tx_enable_in),
        .bus_tx_queue_flushed_in(bus_tx_queue_flushed_in),
        .step_out(step_out),
        .enable_out(enable_out),
        .general_output_out(general_output_out),
        .bus_tx_enable_out(bus_tx_enable_out),
        .bus_tx_queue_flush_req(bus_tx_queue_flush_req),
        .bus_tx_gate_active(bus_tx_gate_active),
        .bus_tx_queue_flushed(bus_tx_queue_flushed),
        .brake_z_out(brake_z_out),
        .estop_irq(estop_irq)
    );

endmodule
