`timescale 1ns / 1ps

module pl_estop_core #(
    parameter integer CLK_HZ = 100000000,
    parameter integer DEBOUNCE_MS = 10,
    parameter integer BRAKE_LEAD_US = 50,
    parameter integer AXIS_COUNT = 8,
    parameter integer Z_AXIS_INDEX = 2,
    parameter integer GENERAL_OUTPUT_COUNT = 16,
    parameter [GENERAL_OUTPUT_COUNT-1:0] GENERAL_OUTPUT_SAFE_LEVELS = {GENERAL_OUTPUT_COUNT{1'b0}},
    parameter integer BUS_TX_GATE_COUNT = 1,
    parameter [BUS_TX_GATE_COUNT-1:0] BUS_TX_IDLE_LEVELS = {BUS_TX_GATE_COUNT{1'b0}}
) (
    input  wire                  clk,
    input  wire                  rst_n,
    input  wire                  estop_nc_in,
    input  wire                  sw_reset_req,
    input  wire                  irq_clear,
    input  wire [AXIS_COUNT-1:0] step_in,
    input  wire [AXIS_COUNT-1:0] enable_in,
    input  wire [GENERAL_OUTPUT_COUNT-1:0] general_output_in,
    input  wire [BUS_TX_GATE_COUNT-1:0] bus_tx_enable_in,
    input  wire                  bus_tx_queue_flushed_in,
    output wire [AXIS_COUNT-1:0] step_out,
    output wire [AXIS_COUNT-1:0] enable_out,
    output wire [GENERAL_OUTPUT_COUNT-1:0] general_output_out,
    output wire [BUS_TX_GATE_COUNT-1:0] bus_tx_enable_out,
    output wire                  bus_tx_queue_flush_req,
    output wire                  brake_z_out,
    output reg                   estop_irq,
    output reg                   estop_latched,
    output wire                  estop_input_raw,
    output reg                   estop_input_filtered,
    output wire                  reset_allowed,
    output reg                   brake_delay_active,
    output wire                  general_output_forced_off,
    output wire                  bus_tx_gate_active,
    output wire                  bus_tx_queue_flushed
);

    function integer max_int;
        input integer a;
        input integer b;
        begin
            max_int = (a > b) ? a : b;
        end
    endfunction

    function integer clog2_int;
        input integer value;
        integer i;
        begin
            clog2_int = 1;
            for (i = value - 1; i > 1; i = i >> 1) begin
                clog2_int = clog2_int + 1;
            end
        end
    endfunction

    localparam integer DEBOUNCE_CYCLES_RAW = (CLK_HZ / 1000) * DEBOUNCE_MS;
    localparam integer DEBOUNCE_CYCLES = max_int(DEBOUNCE_CYCLES_RAW, 1);
    localparam integer DEBOUNCE_WIDTH = clog2_int(DEBOUNCE_CYCLES + 1);
    localparam [DEBOUNCE_WIDTH-1:0] DEBOUNCE_CYCLES_VAL = DEBOUNCE_CYCLES;
    localparam [DEBOUNCE_WIDTH-1:0] DEBOUNCE_CYCLES_MINUS1_VAL = DEBOUNCE_CYCLES - 1;

    localparam integer BRAKE_CYCLES_RAW = (CLK_HZ / 1000000) * BRAKE_LEAD_US;
    localparam integer BRAKE_CYCLES = max_int(BRAKE_CYCLES_RAW, 1);
    localparam integer BRAKE_WIDTH = clog2_int(BRAKE_CYCLES + 1);
    localparam [BRAKE_WIDTH-1:0] BRAKE_CYCLES_VAL = BRAKE_CYCLES;

    reg estop_sync_1;
    reg estop_sync_2;
    reg [DEBOUNCE_WIDTH-1:0] debounce_count;
    reg [BRAKE_WIDTH-1:0] brake_count;

    wire filtered_low = !estop_input_filtered;
    wire latch_event = !estop_latched && filtered_low;

    assign estop_input_raw = estop_sync_2;
    assign bus_tx_queue_flushed = bus_tx_queue_flushed_in;
    assign reset_allowed = estop_input_raw && estop_input_filtered && !brake_delay_active && bus_tx_queue_flushed;
    assign brake_z_out = estop_latched;

    pl_estop_general_output_gate #(
        .OUTPUT_COUNT(GENERAL_OUTPUT_COUNT),
        .SAFE_LEVELS(GENERAL_OUTPUT_SAFE_LEVELS)
    ) u_general_output_gate (
        .estop_latched(estop_latched),
        .output_in(general_output_in),
        .output_out(general_output_out),
        .forced_off(general_output_forced_off)
    );

    pl_estop_bus_tx_gate #(
        .GATE_COUNT(BUS_TX_GATE_COUNT),
        .IDLE_LEVELS(BUS_TX_IDLE_LEVELS)
    ) u_bus_tx_gate (
        .estop_latched(estop_latched),
        .tx_enable_in(bus_tx_enable_in),
        .tx_enable_out(bus_tx_enable_out),
        .queue_flush_req(bus_tx_queue_flush_req),
        .gate_active(bus_tx_gate_active)
    );

    genvar axis_idx;
    generate
        for (axis_idx = 0; axis_idx < AXIS_COUNT; axis_idx = axis_idx + 1) begin : g_axis_gate
            if (axis_idx == Z_AXIS_INDEX) begin : g_z_axis
                assign step_out[axis_idx] = (estop_latched && !brake_delay_active) ? 1'b0 : step_in[axis_idx];
                assign enable_out[axis_idx] = (estop_latched && !brake_delay_active) ? 1'b0 : enable_in[axis_idx];
            end else begin : g_non_z_axis
                assign step_out[axis_idx] = estop_latched ? 1'b0 : step_in[axis_idx];
                assign enable_out[axis_idx] = estop_latched ? 1'b0 : enable_in[axis_idx];
            end
        end
    endgenerate

    always @(posedge clk) begin
        if (!rst_n) begin
            estop_sync_1 <= 1'b0;
            estop_sync_2 <= 1'b0;
        end else begin
            estop_sync_1 <= estop_nc_in;
            estop_sync_2 <= estop_sync_1;
        end
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            debounce_count <= {DEBOUNCE_WIDTH{1'b0}};
            estop_input_filtered <= 1'b0;
        end else if (estop_input_raw) begin
            if (debounce_count < DEBOUNCE_CYCLES_VAL) begin
                debounce_count <= debounce_count + 1'b1;
            end
            if (debounce_count >= DEBOUNCE_CYCLES_MINUS1_VAL) begin
                estop_input_filtered <= 1'b1;
            end
        end else begin
            if (debounce_count > {DEBOUNCE_WIDTH{1'b0}}) begin
                debounce_count <= debounce_count - 1'b1;
            end
            if (debounce_count <= {{(DEBOUNCE_WIDTH-1){1'b0}}, 1'b1}) begin
                estop_input_filtered <= 1'b0;
            end
        end
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            estop_latched <= 1'b1;
            brake_delay_active <= 1'b0;
            brake_count <= {BRAKE_WIDTH{1'b0}};
        end else begin
            if (latch_event) begin
                estop_latched <= 1'b1;
                brake_delay_active <= 1'b1;
                brake_count <= BRAKE_CYCLES_VAL;
            end else if (sw_reset_req && reset_allowed) begin
                estop_latched <= 1'b0;
            end

            if (brake_delay_active) begin
                if (brake_count <= {{(BRAKE_WIDTH-1){1'b0}}, 1'b1}) begin
                    brake_delay_active <= 1'b0;
                    brake_count <= {BRAKE_WIDTH{1'b0}};
                end else begin
                    brake_count <= brake_count - 1'b1;
                end
            end
        end
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            estop_irq <= 1'b0;
        end else if (latch_event) begin
            estop_irq <= 1'b1;
        end else if (irq_clear) begin
            estop_irq <= 1'b0;
        end
    end

endmodule

module pl_estop_bus_tx_gate #(
    parameter integer GATE_COUNT = 1,
    parameter [GATE_COUNT-1:0] IDLE_LEVELS = {GATE_COUNT{1'b0}}
) (
    input  wire                  estop_latched,
    input  wire [GATE_COUNT-1:0] tx_enable_in,
    output wire [GATE_COUNT-1:0] tx_enable_out,
    output wire                  queue_flush_req,
    output wire                  gate_active
);

    genvar gate_idx;
    generate
        for (gate_idx = 0; gate_idx < GATE_COUNT; gate_idx = gate_idx + 1) begin : g_tx_gate
            assign tx_enable_out[gate_idx] = estop_latched ? IDLE_LEVELS[gate_idx] : tx_enable_in[gate_idx];
        end
    endgenerate

    assign queue_flush_req = estop_latched;
    assign gate_active = estop_latched;

endmodule

module pl_estop_general_output_gate #(
    parameter integer OUTPUT_COUNT = 16,
    parameter [OUTPUT_COUNT-1:0] SAFE_LEVELS = {OUTPUT_COUNT{1'b0}}
) (
    input  wire                    estop_latched,
    input  wire [OUTPUT_COUNT-1:0] output_in,
    output wire [OUTPUT_COUNT-1:0] output_out,
    output wire                    forced_off
);

    genvar output_idx;
    generate
        for (output_idx = 0; output_idx < OUTPUT_COUNT; output_idx = output_idx + 1) begin : g_output_gate
            assign output_out[output_idx] = estop_latched ? SAFE_LEVELS[output_idx] : output_in[output_idx];
        end
    endgenerate

    assign forced_off = estop_latched;

endmodule
