
`timescale 1 ns / 1 ps

module PWM_v2_0 #
	(
		// Users to add parameters here
        parameter integer NUM_PWM    = 1,
        parameter POLARITY = 1'b1,
		// User parameters ends
		// Do not modify the parameters beyond this line


		// Parameters of Axi Slave Bus Interface PWM_AXI
		parameter integer C_PWM_AXI_DATA_WIDTH	= 32,
		parameter integer C_PWM_AXI_ADDR_WIDTH	= 7
	)
	(
		// Users to add ports here
        output wire [NUM_PWM-1 : 0] pwm,
		// User ports ends
		// Do not modify the ports beyond this line


		// Ports of Axi Slave Bus Interface PWM_AXI
		input wire  pwm_axi_aclk,
		input wire  pwm_axi_aresetn,
		input wire [C_PWM_AXI_ADDR_WIDTH-1 : 0] pwm_axi_awaddr,
		input wire [2 : 0] pwm_axi_awprot,
		input wire  pwm_axi_awvalid,
		output wire  pwm_axi_awready,
		input wire [C_PWM_AXI_DATA_WIDTH-1 : 0] pwm_axi_wdata,
		input wire [(C_PWM_AXI_DATA_WIDTH/8)-1 : 0] pwm_axi_wstrb,
		input wire  pwm_axi_wvalid,
		output wire  pwm_axi_wready,
		output wire [1 : 0] pwm_axi_bresp,
		output wire  pwm_axi_bvalid,
		input wire  pwm_axi_bready,
		input wire [C_PWM_AXI_ADDR_WIDTH-1 : 0] pwm_axi_araddr,
		input wire [2 : 0] pwm_axi_arprot,
		input wire  pwm_axi_arvalid,
		output wire  pwm_axi_arready,
		output wire [C_PWM_AXI_DATA_WIDTH-1 : 0] pwm_axi_rdata,
		output wire [1 : 0] pwm_axi_rresp,
		output wire  pwm_axi_rvalid,
		input wire  pwm_axi_rready
	);

    initial begin
        if (NUM_PWM < 1 || NUM_PWM > 16)
            $error("PWM_v2_0 NUM_PWM valid range is 1..16.");
        if (C_PWM_AXI_DATA_WIDTH != 32)
            $error("PWM_v2_0 currently supports C_PWM_AXI_DATA_WIDTH = 32 only.");
        if (C_PWM_AXI_ADDR_WIDTH < 7)
            $error("PWM_v2_0 requires C_PWM_AXI_ADDR_WIDTH >= 7.");
        if (!((POLARITY == 1'b0) || (POLARITY == 1'b1)))
            $error("PWM_v2_0 POLARITY must be 0 or 1.");
    end
	
    wire [C_PWM_AXI_DATA_WIDTH-1:0]ctrl_reg;
    wire [C_PWM_AXI_DATA_WIDTH-1:0]status_reg;
	wire [C_PWM_AXI_DATA_WIDTH-1:0]duty_reg [0:NUM_PWM-1];
	wire [C_PWM_AXI_DATA_WIDTH-1:0]period_reg;
	
// Instantiation of Axi Bus Interface PWM_AXI
	PWM_AXI # ( 
		.C_S_AXI_DATA_WIDTH(C_PWM_AXI_DATA_WIDTH),
		.C_S_AXI_ADDR_WIDTH(C_PWM_AXI_ADDR_WIDTH),
		.NUM_PWM(NUM_PWM)
	) PWM_AXI_inst (
	    .ctrl_reg_out(ctrl_reg),
	    .status_reg_out(status_reg),
        .duty_reg_out(duty_reg),
        .period_reg_out(period_reg),
		.S_AXI_ACLK(pwm_axi_aclk),
		.S_AXI_ARESETN(pwm_axi_aresetn),
		.S_AXI_AWADDR(pwm_axi_awaddr),
		.S_AXI_AWPROT(pwm_axi_awprot),
		.S_AXI_AWVALID(pwm_axi_awvalid),
		.S_AXI_AWREADY(pwm_axi_awready),
		.S_AXI_WDATA(pwm_axi_wdata),
		.S_AXI_WSTRB(pwm_axi_wstrb),
		.S_AXI_WVALID(pwm_axi_wvalid),
		.S_AXI_WREADY(pwm_axi_wready),
		.S_AXI_BRESP(pwm_axi_bresp),
		.S_AXI_BVALID(pwm_axi_bvalid),
		.S_AXI_BREADY(pwm_axi_bready),
		.S_AXI_ARADDR(pwm_axi_araddr),
		.S_AXI_ARPROT(pwm_axi_arprot),
		.S_AXI_ARVALID(pwm_axi_arvalid),
		.S_AXI_ARREADY(pwm_axi_arready),
		.S_AXI_RDATA(pwm_axi_rdata),
		.S_AXI_RRESP(pwm_axi_rresp),
		.S_AXI_RVALID(pwm_axi_rvalid),
		.S_AXI_RREADY(pwm_axi_rready)
	);
    
    reg [C_PWM_AXI_DATA_WIDTH-1:0] duty_reg_latch [0:NUM_PWM-1];
    reg [C_PWM_AXI_DATA_WIDTH-1:0] count;
    reg                            enable;
    wire [C_PWM_AXI_DATA_WIDTH-1:0] period_safe;
    wire [C_PWM_AXI_DATA_WIDTH-1:0] duty_limit;
    wire                            period_valid;
    
    // Add user logic here
    assign period_safe = (period_reg == 0) ? {{(C_PWM_AXI_DATA_WIDTH-1){1'b0}},1'b1} : period_reg;
    assign duty_limit = (period_safe > {{(C_PWM_AXI_DATA_WIDTH-1){1'b0}},1'b1}) ? (period_safe - 1'b1) : period_safe;
    assign period_valid = (period_reg != 0);

    always @(posedge pwm_axi_aclk) begin
        if (!pwm_axi_aresetn)
            enable <= 1'b0;
        else
            enable <= ctrl_reg[0];
    end

   
    always @(posedge pwm_axi_aclk) begin
        if (!pwm_axi_aresetn) begin
            count <= {C_PWM_AXI_DATA_WIDTH{1'b0}};
        end else begin
            if (!enable) begin
                count <= {C_PWM_AXI_DATA_WIDTH{1'b0}};
            end else if (count >= (period_safe - 1'b1)) begin
                count <= {C_PWM_AXI_DATA_WIDTH{1'b0}};
            end else begin
                count <= count + 1'b1;
            end
        end
    end
    
    genvar i;
    generate
    for (i = 0; i < NUM_PWM ; i = i + 1) begin 
        always @(posedge pwm_axi_aclk) begin
            if (!pwm_axi_aresetn) begin
                duty_reg_latch[i] <= {C_PWM_AXI_DATA_WIDTH{1'b0}};
            end else if (!enable || (count == {C_PWM_AXI_DATA_WIDTH{1'b0}})) begin
                // Explicit saturation: preserve at least one inactive tick when possible.
                // With a 1-tick period there is no spare tick, so 100% duty remains possible.
                if (duty_reg[i] > duty_limit)
                    duty_reg_latch[i] <= duty_limit;
                else
                    duty_reg_latch[i] <= duty_reg[i];
            end
        end
        
        // Keep output in inactive polarity when period is invalid (period=0).
        assign pwm[i] = ((count < duty_reg_latch[i]) && enable && period_valid) ? POLARITY : !POLARITY;
    end
    endgenerate
    
	// User logic ends

	endmodule
