`timescale 1ns / 1ps

module z20_v15_io_owner_axi_lite_tb;
    reg clk = 1'b0;
    reg rst_n = 1'b0;

    reg [7:0] awaddr = 8'd0;
    reg [2:0] awprot = 3'd0;
    reg awvalid = 1'b0;
    wire awready;
    reg [31:0] wdata = 32'd0;
    reg [3:0] wstrb = 4'h0;
    reg wvalid = 1'b0;
    wire wready;
    wire [1:0] bresp;
    wire bvalid;
    reg bready = 1'b0;
    reg [7:0] araddr = 8'd0;
    reg [2:0] arprot = 3'd0;
    reg arvalid = 1'b0;
    wire arready;
    wire [31:0] rdata;
    wire [1:0] rresp;
    wire rvalid;
    reg rready = 1'b0;

    reg [17:0] di_i = 18'd0;
    reg [15:0] fr_di_i = 16'd0;
    reg ts_di_i = 1'b0;
    reg [7:0] mpg_axis_sel_i = 8'd0;
    reg mpg_a_i = 1'b0;
    reg mpg_b_i = 1'b0;
    reg [2:0] scale_sel_i = 3'd0;
    reg [7:0] alarm_i = 8'd0;
    reg tp_int_i = 1'b0;

    wire [13:0] do_o;
    wire [1:0] pwm_o;
    wire [7:0] axis_ena_o;
    wire tp_rst_n_o;

    integer high0;
    integer low0;
    integer high1;
    integer i;
    reg [31:0] read_value;

    localparam [7:0] REG_DI = 8'h08;
    localparam [7:0] REG_FR_DI = 8'h0c;
    localparam [7:0] REG_MISC = 8'h10;
    localparam [7:0] REG_DO = 8'h14;
    localparam [7:0] REG_AXIS_ENA = 8'h18;
    localparam [7:0] REG_TOUCH_CTRL = 8'h1c;
    localparam [7:0] REG_PWM_CTRL = 8'h20;
    localparam [7:0] REG_PWM_PERIOD = 8'h24;
    localparam [7:0] REG_PWM0_DUTY = 8'h28;
    localparam [7:0] REG_PWM1_DUTY = 8'h2c;

    z20_v15_io_owner_axi_lite dut (
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
        .di_i(di_i),
        .fr_di_i(fr_di_i),
        .ts_di_i(ts_di_i),
        .mpg_axis_sel_i(mpg_axis_sel_i),
        .mpg_a_i(mpg_a_i),
        .mpg_b_i(mpg_b_i),
        .scale_sel_i(scale_sel_i),
        .alarm_i(alarm_i),
        .tp_int_i(tp_int_i),
        .do_o(do_o),
        .pwm_o(pwm_o),
        .axis_ena_o(axis_ena_o),
        .tp_rst_n_o(tp_rst_n_o)
    );

    always #5 clk = ~clk;

    task fail;
        input [255:0] message;
        begin
            $display("FAIL: %0s", message);
            $finish;
        end
    endtask

    task axi_write;
        input [7:0] addr;
        input [31:0] data;
        begin
            @(posedge clk);
            awaddr <= addr;
            wdata <= data;
            wstrb <= 4'hf;
            awvalid <= 1'b1;
            wvalid <= 1'b1;
            bready <= 1'b1;
            wait (awready == 1'b1 && wready == 1'b1);
            @(posedge clk);
            awvalid <= 1'b0;
            wvalid <= 1'b0;
            wait (bvalid == 1'b1);
            @(posedge clk);
            bready <= 1'b0;
            if (bresp != 2'b00) begin
                fail("AXI write response error");
            end
        end
    endtask

    task axi_read;
        input [7:0] addr;
        output [31:0] data;
        begin
            @(posedge clk);
            araddr <= addr;
            arvalid <= 1'b1;
            rready <= 1'b1;
            wait (arready == 1'b1);
            @(posedge clk);
            arvalid <= 1'b0;
            wait (rvalid == 1'b1);
            data = rdata;
            @(posedge clk);
            rready <= 1'b0;
            if (rresp != 2'b00) begin
                fail("AXI read response error");
            end
        end
    endtask

    initial begin
        repeat (4) @(posedge clk);
        rst_n <= 1'b1;
        repeat (4) @(posedge clk);

        if (do_o != 14'd0) begin
            fail("DO reset is not fail-closed");
        end
        if (axis_ena_o != 8'd0) begin
            fail("axis ENA reset is not fail-closed");
        end
        if (pwm_o != 2'd0) begin
            fail("PWM reset is not disabled low");
        end
        if (tp_rst_n_o != 1'b1) begin
            fail("touch reset default is not released high");
        end

        di_i <= 18'h2aaaa;
        fr_di_i <= 16'h55aa;
        ts_di_i <= 1'b1;
        mpg_axis_sel_i <= 8'ha5;
        mpg_a_i <= 1'b1;
        mpg_b_i <= 1'b0;
        scale_sel_i <= 3'b101;
        alarm_i <= 8'h3c;
        tp_int_i <= 1'b1;
        repeat (4) @(posedge clk);

        axi_read(REG_DI, read_value);
        if (read_value[17:0] != 18'h2aaaa) begin
            fail("DI snapshot mismatch");
        end
        axi_read(REG_FR_DI, read_value);
        if (read_value[15:0] != 16'h55aa) begin
            fail("FR_DI snapshot mismatch");
        end
        axi_read(REG_MISC, read_value);
        if (read_value[0] != 1'b1 ||
            read_value[8:1] != 8'ha5 ||
            read_value[9] != 1'b1 ||
            read_value[10] != 1'b0 ||
            read_value[13:11] != 3'b101 ||
            read_value[21:14] != 8'h3c ||
            read_value[22] != 1'b1) begin
            fail("misc input snapshot mismatch");
        end

        axi_write(REG_DO, 32'h00002a5a);
        axi_write(REG_AXIS_ENA, 32'h000000a5);
        axi_write(REG_TOUCH_CTRL, 32'h00000000);
        repeat (2) @(posedge clk);
        if (do_o != 14'h2a5a) begin
            fail("DO write did not drive output");
        end
        if (axis_ena_o != 8'ha5) begin
            fail("axis ENA write did not drive output");
        end
        if (tp_rst_n_o != 1'b0) begin
            fail("touch reset write did not drive low");
        end

        axi_write(REG_PWM_PERIOD, 32'd4);
        axi_write(REG_PWM0_DUTY, 32'd2);
        axi_write(REG_PWM1_DUTY, 32'd4);
        axi_write(REG_PWM_CTRL, 32'h00000003);
        high0 = 0;
        low0 = 0;
        high1 = 0;
        repeat (2) @(posedge clk);
        for (i = 0; i < 16; i = i + 1) begin
            @(posedge clk);
            if (pwm_o[0]) begin
                high0 = high0 + 1;
            end else begin
                low0 = low0 + 1;
            end
            if (pwm_o[1]) begin
                high1 = high1 + 1;
            end
        end
        if (high0 == 0 || low0 == 0) begin
            fail("PWM0 did not toggle for 50 percent duty");
        end
        if (high1 < 15) begin
            fail("PWM1 full-duty output was not high");
        end

        $display("PASS: z20_v15_io_owner_axi_lite_tb");
        $finish;
    end
endmodule
