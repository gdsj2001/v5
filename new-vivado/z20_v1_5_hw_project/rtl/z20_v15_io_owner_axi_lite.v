`timescale 1ns / 1ps

module z20_v15_io_owner_axi_lite #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 8,
    parameter [31:0] BUILD_ID = 32'h20260701
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

    input  wire [17:0]                       di_i,
    input  wire [15:0]                       fr_di_i,
    input  wire                              ts_di_i,
    input  wire [7:0]                        mpg_axis_sel_i,
    input  wire                              mpg_a_i,
    input  wire                              mpg_b_i,
    input  wire [2:0]                        scale_sel_i,
    input  wire [7:0]                        alarm_i,
    input  wire                              tp_int_i,

    output wire [13:0]                       do_o,
    output wire [1:0]                        pwm_o,
    output wire [7:0]                        axis_ena_o,
    output wire                              tp_rst_n_o
);

    localparam [31:0] REG_MAGIC   = 32'h494f4f57; // "IOOW"
    localparam [31:0] REG_VERSION = 32'h00010000;

    localparam [5:0] ADDR_MAGIC       = 6'h00;
    localparam [5:0] ADDR_VERSION     = 6'h01;
    localparam [5:0] ADDR_DI          = 6'h02;
    localparam [5:0] ADDR_FR_DI       = 6'h03;
    localparam [5:0] ADDR_MISC        = 6'h04;
    localparam [5:0] ADDR_DO          = 6'h05;
    localparam [5:0] ADDR_AXIS_ENA    = 6'h06;
    localparam [5:0] ADDR_TOUCH_CTRL  = 6'h07;
    localparam [5:0] ADDR_PWM_CTRL    = 6'h08;
    localparam [5:0] ADDR_PWM_PERIOD  = 6'h09;
    localparam [5:0] ADDR_PWM0_DUTY   = 6'h0a;
    localparam [5:0] ADDR_PWM1_DUTY   = 6'h0b;
    localparam [5:0] ADDR_OUT_STATUS  = 6'h0c;
    localparam [5:0] ADDR_BUILD_ID    = 6'h0d;

    reg [17:0] di_meta;
    reg [17:0] di_sync;
    reg [15:0] fr_di_meta;
    reg [15:0] fr_di_sync;
    reg ts_di_meta;
    reg ts_di_sync;
    reg [7:0] mpg_axis_sel_meta;
    reg [7:0] mpg_axis_sel_sync;
    reg mpg_a_meta;
    reg mpg_a_sync;
    reg mpg_b_meta;
    reg mpg_b_sync;
    reg [2:0] scale_sel_meta;
    reg [2:0] scale_sel_sync;
    reg [7:0] alarm_meta;
    reg [7:0] alarm_sync;
    reg tp_int_meta;
    reg tp_int_sync;

    reg [13:0] do_reg;
    reg [7:0] axis_ena_reg;
    reg tp_rst_n_reg;
    reg [1:0] pwm_enable_reg;
    reg [31:0] pwm_period_reg;
    reg [31:0] pwm0_duty_reg;
    reg [31:0] pwm1_duty_reg;
    reg [31:0] pwm_counter;

    wire [31:0] pwm_period_eff = (pwm_period_reg == 32'd0) ? 32'd1 : pwm_period_reg;
    wire [31:0] pwm_period_last = pwm_period_eff - 32'd1;
    wire [31:0] do_write_value = apply_wstrb({18'd0, do_reg}, S_AXI_WDATA, S_AXI_WSTRB);
    wire [31:0] axis_ena_write_value = apply_wstrb({24'd0, axis_ena_reg}, S_AXI_WDATA, S_AXI_WSTRB);
    wire [31:0] touch_ctrl_write_value = apply_wstrb({31'd0, tp_rst_n_reg}, S_AXI_WDATA, S_AXI_WSTRB);
    wire [31:0] pwm_ctrl_write_value = apply_wstrb({30'd0, pwm_enable_reg}, S_AXI_WDATA, S_AXI_WSTRB);
    wire [31:0] pwm_period_write_value = apply_wstrb(pwm_period_reg, S_AXI_WDATA, S_AXI_WSTRB);
    wire [31:0] pwm0_duty_write_value = apply_wstrb(pwm0_duty_reg, S_AXI_WDATA, S_AXI_WSTRB);
    wire [31:0] pwm1_duty_write_value = apply_wstrb(pwm1_duty_reg, S_AXI_WDATA, S_AXI_WSTRB);
    wire pwm0_raw = pwm_enable_reg[0] &&
                    ((pwm0_duty_reg >= pwm_period_eff) ||
                     ((pwm0_duty_reg != 32'd0) && (pwm_counter < pwm0_duty_reg)));
    wire pwm1_raw = pwm_enable_reg[1] &&
                    ((pwm1_duty_reg >= pwm_period_eff) ||
                     ((pwm1_duty_reg != 32'd0) && (pwm_counter < pwm1_duty_reg)));

    assign do_o = do_reg;
    assign pwm_o = {pwm1_raw, pwm0_raw};
    assign axis_ena_o = axis_ena_reg;
    assign tp_rst_n_o = tp_rst_n_reg;

    function [31:0] apply_wstrb;
        input [31:0] current;
        input [31:0] data;
        input [3:0] wstrb;
        begin
            apply_wstrb = current;
            if (wstrb[0]) begin
                apply_wstrb[7:0] = data[7:0];
            end
            if (wstrb[1]) begin
                apply_wstrb[15:8] = data[15:8];
            end
            if (wstrb[2]) begin
                apply_wstrb[23:16] = data[23:16];
            end
            if (wstrb[3]) begin
                apply_wstrb[31:24] = data[31:24];
            end
        end
    endfunction

    function [31:0] read_reg;
        input [C_S_AXI_ADDR_WIDTH-1:0] addr;
        begin
            case (addr[7:2])
                ADDR_MAGIC: read_reg = REG_MAGIC;
                ADDR_VERSION: read_reg = REG_VERSION;
                ADDR_DI: read_reg = {14'd0, di_sync};
                ADDR_FR_DI: read_reg = {16'd0, fr_di_sync};
                ADDR_MISC: read_reg = {
                    9'd0,
                    tp_int_sync,
                    alarm_sync,
                    scale_sel_sync,
                    mpg_b_sync,
                    mpg_a_sync,
                    mpg_axis_sel_sync,
                    ts_di_sync
                };
                ADDR_DO: read_reg = {18'd0, do_reg};
                ADDR_AXIS_ENA: read_reg = {24'd0, axis_ena_reg};
                ADDR_TOUCH_CTRL: read_reg = {31'd0, tp_rst_n_reg};
                ADDR_PWM_CTRL: read_reg = {30'd0, pwm_enable_reg};
                ADDR_PWM_PERIOD: read_reg = pwm_period_reg;
                ADDR_PWM0_DUTY: read_reg = pwm0_duty_reg;
                ADDR_PWM1_DUTY: read_reg = pwm1_duty_reg;
                ADDR_OUT_STATUS: read_reg = {
                    7'd0,
                    tp_rst_n_reg,
                    axis_ena_reg,
                    pwm1_raw,
                    pwm0_raw,
                    do_reg
                };
                ADDR_BUILD_ID: read_reg = BUILD_ID;
                default: read_reg = 32'd0;
            endcase
        end
    endfunction

    always @(posedge S_AXI_ACLK) begin
        if (!S_AXI_ARESETN) begin
            di_meta <= 18'd0;
            di_sync <= 18'd0;
            fr_di_meta <= 16'd0;
            fr_di_sync <= 16'd0;
            ts_di_meta <= 1'b0;
            ts_di_sync <= 1'b0;
            mpg_axis_sel_meta <= 8'd0;
            mpg_axis_sel_sync <= 8'd0;
            mpg_a_meta <= 1'b0;
            mpg_a_sync <= 1'b0;
            mpg_b_meta <= 1'b0;
            mpg_b_sync <= 1'b0;
            scale_sel_meta <= 3'd0;
            scale_sel_sync <= 3'd0;
            alarm_meta <= 8'd0;
            alarm_sync <= 8'd0;
            tp_int_meta <= 1'b0;
            tp_int_sync <= 1'b0;
        end else begin
            di_meta <= di_i;
            di_sync <= di_meta;
            fr_di_meta <= fr_di_i;
            fr_di_sync <= fr_di_meta;
            ts_di_meta <= ts_di_i;
            ts_di_sync <= ts_di_meta;
            mpg_axis_sel_meta <= mpg_axis_sel_i;
            mpg_axis_sel_sync <= mpg_axis_sel_meta;
            mpg_a_meta <= mpg_a_i;
            mpg_a_sync <= mpg_a_meta;
            mpg_b_meta <= mpg_b_i;
            mpg_b_sync <= mpg_b_meta;
            scale_sel_meta <= scale_sel_i;
            scale_sel_sync <= scale_sel_meta;
            alarm_meta <= alarm_i;
            alarm_sync <= alarm_meta;
            tp_int_meta <= tp_int_i;
            tp_int_sync <= tp_int_meta;
        end
    end

    always @(posedge S_AXI_ACLK) begin
        if (!S_AXI_ARESETN) begin
            do_reg <= 14'd0;
            axis_ena_reg <= 8'd0;
            tp_rst_n_reg <= 1'b1;
            pwm_enable_reg <= 2'd0;
            pwm_period_reg <= 32'd100000;
            pwm0_duty_reg <= 32'd0;
            pwm1_duty_reg <= 32'd0;
            pwm_counter <= 32'd0;
            S_AXI_AWREADY <= 1'b0;
            S_AXI_WREADY <= 1'b0;
            S_AXI_BRESP <= 2'b00;
            S_AXI_BVALID <= 1'b0;
        end else begin
            S_AXI_AWREADY <= 1'b0;
            S_AXI_WREADY <= 1'b0;

            if (pwm_counter >= pwm_period_last) begin
                pwm_counter <= 32'd0;
            end else begin
                pwm_counter <= pwm_counter + 32'd1;
            end

            if (!S_AXI_BVALID && S_AXI_AWVALID && S_AXI_WVALID) begin
                S_AXI_AWREADY <= 1'b1;
                S_AXI_WREADY <= 1'b1;
                S_AXI_BRESP <= 2'b00;
                S_AXI_BVALID <= 1'b1;

                case (S_AXI_AWADDR[7:2])
                    ADDR_DO: begin
                        do_reg <= do_write_value[13:0];
                    end
                    ADDR_AXIS_ENA: begin
                        axis_ena_reg <= axis_ena_write_value[7:0];
                    end
                    ADDR_TOUCH_CTRL: begin
                        tp_rst_n_reg <= touch_ctrl_write_value[0];
                    end
                    ADDR_PWM_CTRL: begin
                        pwm_enable_reg <= pwm_ctrl_write_value[1:0];
                    end
                    ADDR_PWM_PERIOD: begin
                        pwm_period_reg <= pwm_period_write_value;
                        pwm_counter <= 32'd0;
                    end
                    ADDR_PWM0_DUTY: begin
                        pwm0_duty_reg <= pwm0_duty_write_value;
                    end
                    ADDR_PWM1_DUTY: begin
                        pwm1_duty_reg <= pwm1_duty_write_value;
                    end
                    default: begin
                    end
                endcase
            end else if (S_AXI_BVALID && S_AXI_BREADY) begin
                S_AXI_BVALID <= 1'b0;
            end
        end
    end

    always @(posedge S_AXI_ACLK) begin
        if (!S_AXI_ARESETN) begin
            S_AXI_ARREADY <= 1'b0;
            S_AXI_RDATA <= 32'd0;
            S_AXI_RRESP <= 2'b00;
            S_AXI_RVALID <= 1'b0;
        end else begin
            S_AXI_ARREADY <= 1'b0;

            if (!S_AXI_RVALID && S_AXI_ARVALID) begin
                S_AXI_ARREADY <= 1'b1;
                S_AXI_RDATA <= read_reg(S_AXI_ARADDR);
                S_AXI_RRESP <= 2'b00;
                S_AXI_RVALID <= 1'b1;
            end else if (S_AXI_RVALID && S_AXI_RREADY) begin
                S_AXI_RVALID <= 1'b0;
            end
        end
    end

    wire _unused_axi_inputs = &{1'b0, S_AXI_AWPROT, S_AXI_ARPROT, 1'b0};

endmodule
