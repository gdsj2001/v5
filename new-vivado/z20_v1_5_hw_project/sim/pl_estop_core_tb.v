`timescale 1ns / 1ps

module pl_estop_core_tb;
    localparam integer AXIS_COUNT = 8;
    localparam integer Z_AXIS_INDEX = 2;
    localparam integer GENERAL_OUTPUT_COUNT = 16;
    localparam integer BUS_TX_GATE_COUNT = 1;

    reg clk;
    reg rst_n;
    reg estop_nc_in;
    reg sw_reset_req;
    reg irq_clear;
    reg [AXIS_COUNT-1:0] step_in;
    reg [AXIS_COUNT-1:0] enable_in;
    reg [GENERAL_OUTPUT_COUNT-1:0] general_output_in;
    reg [BUS_TX_GATE_COUNT-1:0] bus_tx_enable_in;
    reg bus_tx_queue_flushed_in;

    wire [AXIS_COUNT-1:0] step_out;
    wire [AXIS_COUNT-1:0] enable_out;
    wire [GENERAL_OUTPUT_COUNT-1:0] general_output_out;
    wire [BUS_TX_GATE_COUNT-1:0] bus_tx_enable_out;
    wire bus_tx_queue_flush_req;
    wire brake_z_out;
    wire estop_irq;
    wire estop_latched;
    wire estop_input_raw;
    wire estop_input_filtered;
    wire reset_allowed;
    wire brake_delay_active;
    wire general_output_forced_off;
    wire bus_tx_gate_active;
    wire bus_tx_queue_flushed;

    pl_estop_core #(
        .CLK_HZ(1000000),
        .DEBOUNCE_MS(1),
        .BRAKE_LEAD_US(10),
        .AXIS_COUNT(AXIS_COUNT),
        .Z_AXIS_INDEX(Z_AXIS_INDEX),
        .GENERAL_OUTPUT_COUNT(GENERAL_OUTPUT_COUNT),
        .GENERAL_OUTPUT_SAFE_LEVELS(16'h0000),
        .BUS_TX_GATE_COUNT(BUS_TX_GATE_COUNT),
        .BUS_TX_IDLE_LEVELS(1'b0)
    ) dut (
        .clk(clk),
        .rst_n(rst_n),
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

    initial begin
        clk = 1'b0;
        forever #500 clk = ~clk;
    end

    task wait_cycles;
        input integer cycles;
        integer i;
        begin
            for (i = 0; i < cycles; i = i + 1) begin
                @(posedge clk);
            end
        end
    endtask

    task pulse_reset_request;
        begin
            @(posedge clk);
            sw_reset_req <= 1'b1;
            @(posedge clk);
            sw_reset_req <= 1'b0;
        end
    endtask

    task pulse_irq_clear;
        begin
            @(posedge clk);
            irq_clear <= 1'b1;
            @(posedge clk);
            irq_clear <= 1'b0;
        end
    endtask

    task check;
        input condition;
        input [255:0] message;
        begin
            if (!condition) begin
                $display("FAIL: %0s", message);
                $finish;
            end
        end
    endtask

    initial begin
        rst_n = 1'b0;
        estop_nc_in = 1'b0;
        sw_reset_req = 1'b0;
        irq_clear = 1'b0;
        step_in = {AXIS_COUNT{1'b1}};
        enable_in = {AXIS_COUNT{1'b1}};
        general_output_in = 16'hA55A;
        bus_tx_enable_in = 1'b1;
        bus_tx_queue_flushed_in = 1'b1;

        wait_cycles(4);
        rst_n = 1'b1;
        wait_cycles(2);

        check(estop_latched == 1'b1, "reset must leave outputs latched safe");
        check(step_out == {AXIS_COUNT{1'b0}}, "all step outputs blocked after reset");
        check(enable_out == {AXIS_COUNT{1'b0}}, "all enable outputs blocked after reset");
        check(general_output_out == 16'h0000, "general outputs forced off after reset");
        check(general_output_forced_off == 1'b1, "general output forced-off status set after reset");
        check(bus_tx_enable_out == 1'b0, "bus TX enable forced idle after reset");
        check(bus_tx_gate_active == 1'b1, "bus TX gate active after reset");
        check(bus_tx_queue_flush_req == 1'b1, "bus TX queue flush requested after reset");
        check(bus_tx_queue_flushed == 1'b1, "bus TX queue flushed status follows input after reset");

        estop_nc_in = 1'b1;
        wait_cycles(1105);
        check(reset_allowed == 1'b1, "healthy NC input must allow software reset after debounce");
        pulse_reset_request();
        wait_cycles(2);
        check(estop_latched == 1'b0, "software reset clears latch only when physical input is healthy");
        check(step_out == step_in, "step outputs pass when not latched");
        check(enable_out == enable_in, "enable outputs pass when not latched");
        check(general_output_out == general_output_in, "general outputs pass when not latched");
        check(general_output_forced_off == 1'b0, "general output forced-off status clears when not latched");
        check(bus_tx_enable_out == bus_tx_enable_in, "bus TX enable passes when not latched");
        check(bus_tx_gate_active == 1'b0, "bus TX gate clears when not latched");
        check(bus_tx_queue_flush_req == 1'b0, "bus TX queue flush request clears when not latched");

        estop_nc_in = 1'b0;
        wait_cycles(100);
        estop_nc_in = 1'b1;
        wait_cycles(1105);
        check(estop_latched == 1'b0, "short low bounce must not latch");

        estop_nc_in = 1'b0;
        wait (estop_latched == 1'b1);
        #1;
        check(estop_latched == 1'b1, "sustained low NC input must latch");
        check(estop_irq == 1'b1, "latch event must raise IRQ");
        check(brake_z_out == 1'b1, "latched estop must request brake");
        check(general_output_out == 16'h0000, "latched estop must force general outputs off");
        check(general_output_forced_off == 1'b1, "latched estop must set general forced-off status");
        check(bus_tx_enable_out == 1'b0, "latched estop must force bus TX enable idle");
        check(bus_tx_gate_active == 1'b1, "latched estop must set bus TX gate active");
        check(bus_tx_queue_flush_req == 1'b1, "latched estop must request bus TX queue flush");
        check(step_out[0] == 1'b0, "non-Z axis step must block immediately");
        check(enable_out[0] == 1'b0, "non-Z axis enable must block immediately");
        check(brake_delay_active == 1'b1, "brake delay must start on latch event");
        check(step_out[Z_AXIS_INDEX] == step_in[Z_AXIS_INDEX], "Z step passes during brake lead time");
        wait_cycles(15);
        check(brake_delay_active == 1'b0, "brake delay must expire");
        check(step_out[Z_AXIS_INDEX] == 1'b0, "Z step must block after brake lead time");
        check(enable_out[Z_AXIS_INDEX] == 1'b0, "Z enable must block after brake lead time");

        pulse_reset_request();
        wait_cycles(2);
        check(estop_latched == 1'b1, "reset request must be rejected while NC input remains low");

        pulse_irq_clear();
        wait_cycles(2);
        check(estop_irq == 1'b0, "IRQ clear must clear IRQ flag");
        check(estop_latched == 1'b1, "IRQ clear must not clear estop latch");

        bus_tx_queue_flushed_in = 1'b0;
        estop_nc_in = 1'b1;
        wait_cycles(1105);
        check(reset_allowed == 1'b0, "reset must wait for bus TX queue flush");
        check(bus_tx_queue_flushed == 1'b0, "bus TX queue flushed status must clear while queue is not flushed");
        pulse_reset_request();
        wait_cycles(2);
        check(estop_latched == 1'b1, "reset request must be rejected while bus TX queue is not flushed");

        bus_tx_queue_flushed_in = 1'b1;
        wait_cycles(2);
        check(reset_allowed == 1'b1, "healthy input plus bus queue flushed must allow reset");
        pulse_reset_request();
        wait_cycles(2);
        check(estop_latched == 1'b0, "healthy input plus reset request clears latch");
        check(general_output_out == general_output_in, "general outputs pass again after release");
        check(bus_tx_enable_out == bus_tx_enable_in, "bus TX enable passes again after release");

        $display("PASS: pl_estop_core_tb");
        $finish;
    end
endmodule
