`timescale 1ns / 1ps

module pl_estop_axi_lite_tb;
    localparam integer AXIS_COUNT = 8;
    localparam integer GENERAL_OUTPUT_COUNT = 16;
    localparam integer BUS_TX_GATE_COUNT = 1;
    localparam [5:0] ADDR_MAGIC   = 6'h00;
    localparam [5:0] ADDR_STATUS  = 6'h08;
    localparam [5:0] ADDR_CONTROL = 6'h0c;
    localparam [5:0] ADDR_GENERAL_CONFIG = 6'h1c;
    localparam [5:0] ADDR_BUS_TX_CONFIG = 6'h20;

    reg clk;
    reg rst_n;

    reg [5:0] awaddr;
    reg [2:0] awprot;
    reg awvalid;
    wire awready;
    reg [31:0] wdata;
    reg [3:0] wstrb;
    reg wvalid;
    wire wready;
    wire [1:0] bresp;
    wire bvalid;
    reg bready;

    reg [5:0] araddr;
    reg [2:0] arprot;
    reg arvalid;
    wire arready;
    wire [31:0] rdata;
    wire [1:0] rresp;
    wire rvalid;
    reg rready;

    reg estop_nc_in;
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
    wire bus_tx_gate_active;
    wire bus_tx_queue_flushed;
    wire brake_z_out;
    wire estop_irq;

    pl_estop_axi_lite #(
        .C_S_AXI_DATA_WIDTH(32),
        .C_S_AXI_ADDR_WIDTH(6),
        .CLK_HZ(1000000),
        .DEBOUNCE_MS(1),
        .BRAKE_LEAD_US(10),
        .AXIS_COUNT(AXIS_COUNT),
        .Z_AXIS_INDEX(2),
        .GENERAL_OUTPUT_COUNT(GENERAL_OUTPUT_COUNT),
        .GENERAL_OUTPUT_SAFE_LEVELS(16'h0000),
        .BUS_TX_GATE_COUNT(BUS_TX_GATE_COUNT),
        .BUS_TX_IDLE_LEVELS(1'b0)
    ) dut (
        .S_AXI_ACLK(clk),
        .S_AXI_ARESETN(rst_n),
        .S_AXI_AWADDR(awaddr),
        .S_AXI_AWPROT(awprot),
        .S_AXI_AWVALID(awvalid),
        .S_AXI_AWREADY(awready),
        .S_AXI_WDATA(wdata),
        .S_AXI_WSTRB(wstrb),
        .S_AXI_WVALID(wvalid),
        .S_AXI_WREADY(wready),
        .S_AXI_BRESP(bresp),
        .S_AXI_BVALID(bvalid),
        .S_AXI_BREADY(bready),
        .S_AXI_ARADDR(araddr),
        .S_AXI_ARPROT(arprot),
        .S_AXI_ARVALID(arvalid),
        .S_AXI_ARREADY(arready),
        .S_AXI_RDATA(rdata),
        .S_AXI_RRESP(rresp),
        .S_AXI_RVALID(rvalid),
        .S_AXI_RREADY(rready),
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

    task axi_write;
        input [5:0] addr;
        input [31:0] data;
        begin
            @(posedge clk);
            awaddr <= addr;
            wdata <= data;
            wstrb <= 4'hf;
            awvalid <= 1'b1;
            wvalid <= 1'b1;
            bready <= 1'b1;
            wait (bvalid == 1'b1);
            @(posedge clk);
            awvalid <= 1'b0;
            wvalid <= 1'b0;
            bready <= 1'b0;
        end
    endtask

    task axi_read;
        input [5:0] addr;
        output [31:0] data;
        begin
            @(posedge clk);
            araddr <= addr;
            arvalid <= 1'b1;
            rready <= 1'b1;
            wait (rvalid == 1'b1);
            data = rdata;
            @(posedge clk);
            arvalid <= 1'b0;
            rready <= 1'b0;
        end
    endtask

    reg [31:0] value;

    initial begin
        rst_n = 1'b0;
        awaddr = 6'd0;
        awprot = 3'd0;
        awvalid = 1'b0;
        wdata = 32'd0;
        wstrb = 4'd0;
        wvalid = 1'b0;
        bready = 1'b0;
        araddr = 6'd0;
        arprot = 3'd0;
        arvalid = 1'b0;
        rready = 1'b0;
        estop_nc_in = 1'b0;
        step_in = {AXIS_COUNT{1'b1}};
        enable_in = {AXIS_COUNT{1'b1}};
        general_output_in = 16'hA55A;
        bus_tx_enable_in = 1'b1;
        bus_tx_queue_flushed_in = 1'b1;

        wait_cycles(4);
        rst_n = 1'b1;
        wait_cycles(2);

        axi_read(ADDR_MAGIC, value);
        check(value == 32'h45535450, "magic register must read ESTP");

        axi_read(ADDR_GENERAL_CONFIG, value);
        check(value[31:16] == 16'd16, "general config must report 16 gated outputs");
        check(value[15:0] == 16'h0000, "general config must report zero safe level");

        axi_read(ADDR_BUS_TX_CONFIG, value);
        check(value[31:16] == 16'd1, "bus TX config must report one placeholder gate");
        check(value[15:0] == 16'h0000, "bus TX config must report zero idle level");

        axi_read(ADDR_STATUS, value);
        check(value[0] == 1'b1, "status latched bit must be set after reset");
        check(value[6] == 1'b1, "status general forced-off bit must be set after reset");
        check(value[7] == 1'b1, "status bus TX gate active bit must be set after reset");
        check(value[8] == 1'b1, "status bus TX queue flushed bit must follow input after reset");
        check(general_output_out == 16'h0000, "general outputs must be forced off after reset");
        check(bus_tx_enable_out == 1'b0, "bus TX enable must be forced idle after reset");
        check(bus_tx_queue_flush_req == 1'b1, "bus TX queue flush must be requested after reset");

        estop_nc_in = 1'b1;
        wait_cycles(1105);
        axi_read(ADDR_STATUS, value);
        check(value[3] == 1'b1, "status reset_allowed bit must set after healthy debounce");

        axi_write(ADDR_CONTROL, 32'h00000001);
        wait_cycles(2);
        axi_read(ADDR_STATUS, value);
        check(value[0] == 1'b0, "control reset_request must clear latch when allowed");
        check(value[6] == 1'b0, "general forced-off bit must clear after latch reset");
        check(value[7] == 1'b0, "bus TX gate active bit must clear after latch reset");
        check(value[8] == 1'b1, "bus TX queue flushed bit must remain set when input is flushed");
        check(general_output_out == general_output_in, "general outputs must pass when not latched");
        check(bus_tx_enable_out == bus_tx_enable_in, "bus TX enable must pass when not latched");

        estop_nc_in = 1'b0;
        wait (estop_irq == 1'b1);
        wait_cycles(2);
        axi_read(ADDR_STATUS, value);
        check(value[0] == 1'b1, "sustained low input must latch through AXI wrapper");
        check(value[5] == 1'b1, "status IRQ bit must set after latch");
        check(value[6] == 1'b1, "status general forced-off bit must set after latch");
        check(value[7] == 1'b1, "status bus TX gate active bit must set after latch");
        check(value[8] == 1'b1, "status bus TX queue flushed bit must reflect flushed input after latch");
        check(general_output_out == 16'h0000, "general outputs must force off after latch");
        check(bus_tx_enable_out == 1'b0, "bus TX enable must force idle after latch");
        check(bus_tx_queue_flush_req == 1'b1, "bus TX queue flush must be requested after latch");

        axi_write(ADDR_CONTROL, 32'h00000002);
        wait_cycles(2);
        axi_read(ADDR_STATUS, value);
        check(value[5] == 1'b0, "control irq_clear must clear IRQ status");
        check(value[0] == 1'b1, "irq_clear must not clear latch");

        axi_write(ADDR_CONTROL, 32'h00000001);
        wait_cycles(2);
        axi_read(ADDR_STATUS, value);
        check(value[0] == 1'b1, "reset_request must be rejected while input remains low");

        bus_tx_queue_flushed_in = 1'b0;
        estop_nc_in = 1'b1;
        wait_cycles(1105);
        axi_read(ADDR_STATUS, value);
        check(value[3] == 1'b0, "status reset_allowed must wait for bus TX queue flush");
        check(value[8] == 1'b0, "status bus TX queue flushed bit must clear while queue is not flushed");
        axi_write(ADDR_CONTROL, 32'h00000001);
        wait_cycles(2);
        axi_read(ADDR_STATUS, value);
        check(value[0] == 1'b1, "reset_request must be rejected while bus TX queue is not flushed");

        bus_tx_queue_flushed_in = 1'b1;
        wait_cycles(2);
        axi_read(ADDR_STATUS, value);
        check(value[3] == 1'b1, "status reset_allowed must set after bus TX queue flush");
        check(value[8] == 1'b1, "status bus TX queue flushed bit must set after queue flush");
        axi_write(ADDR_CONTROL, 32'h00000001);
        wait_cycles(2);
        axi_read(ADDR_STATUS, value);
        check(value[0] == 1'b0, "reset_request must clear latch after input and bus TX queue are safe");
        check(value[7] == 1'b0, "bus TX gate active bit must clear after safe reset");
        check(bus_tx_enable_out == bus_tx_enable_in, "bus TX enable must pass after safe reset");

        $display("PASS: pl_estop_axi_lite_tb");
        $finish;
    end
endmodule
