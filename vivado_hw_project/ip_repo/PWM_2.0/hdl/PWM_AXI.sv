
`timescale 1 ns / 1 ps

	module PWM_AXI #
	(
		// Users to add parameters here
        parameter integer NUM_PWM    = 1,
		// User parameters ends
		// Do not modify the parameters beyond this line

		// Width of S_AXI data bus
		parameter integer C_S_AXI_DATA_WIDTH	= 32,
		// Width of S_AXI address bus
		parameter integer C_S_AXI_ADDR_WIDTH	= 7
	)
	(
		// Users to add ports here
        output wire [C_S_AXI_DATA_WIDTH-1:0]	duty_reg_out [0:NUM_PWM-1],
        output wire [C_S_AXI_DATA_WIDTH-1:0]    period_reg_out,
        output wire [C_S_AXI_DATA_WIDTH-1:0]    ctrl_reg_out,
        output wire [C_S_AXI_DATA_WIDTH-1:0]    status_reg_out,
		// User ports ends
		// Do not modify the ports beyond this line

		// Global Clock Signal
		input wire  S_AXI_ACLK,
		// Global Reset Signal. This Signal is Active LOW
		input wire  S_AXI_ARESETN,
		// Write address (issued by master, acceped by Slave)
		input wire [C_S_AXI_ADDR_WIDTH-1 : 0] S_AXI_AWADDR,
		// Write channel Protection type. This signal indicates the
    		// privilege and security level of the transaction, and whether
    		// the transaction is a data access or an instruction access.
		input wire [2 : 0] S_AXI_AWPROT,
		// Write address valid. This signal indicates that the master signaling
    		// valid write address and control information.
		input wire  S_AXI_AWVALID,
		// Write address ready. This signal indicates that the slave is ready
    		// to accept an address and associated control signals.
		output wire  S_AXI_AWREADY,
		// Write data (issued by master, acceped by Slave) 
		input wire [C_S_AXI_DATA_WIDTH-1 : 0] S_AXI_WDATA,
		// Write strobes. This signal indicates which byte lanes hold
    		// valid data. There is one write strobe bit for each eight
    		// bits of the write data bus.    
		input wire [(C_S_AXI_DATA_WIDTH/8)-1 : 0] S_AXI_WSTRB,
		// Write valid. This signal indicates that valid write
    		// data and strobes are available.
		input wire  S_AXI_WVALID,
		// Write ready. This signal indicates that the slave
    		// can accept the write data.
		output wire  S_AXI_WREADY,
		// Write response. This signal indicates the status
    		// of the write transaction.
		output wire [1 : 0] S_AXI_BRESP,
		// Write response valid. This signal indicates that the channel
    		// is signaling a valid write response.
		output wire  S_AXI_BVALID,
		// Response ready. This signal indicates that the master
    		// can accept a write response.
		input wire  S_AXI_BREADY,
		// Read address (issued by master, acceped by Slave)
		input wire [C_S_AXI_ADDR_WIDTH-1 : 0] S_AXI_ARADDR,
		// Protection type. This signal indicates the privilege
    		// and security level of the transaction, and whether the
    		// transaction is a data access or an instruction access.
		input wire [2 : 0] S_AXI_ARPROT,
		// Read address valid. This signal indicates that the channel
    		// is signaling valid read address and control information.
		input wire  S_AXI_ARVALID,
		// Read address ready. This signal indicates that the slave is
    		// ready to accept an address and associated control signals.
		output wire  S_AXI_ARREADY,
		// Read data (issued by slave)
		output wire [C_S_AXI_DATA_WIDTH-1 : 0] S_AXI_RDATA,
		// Read response. This signal indicates the status of the
    		// read transfer.
		output wire [1 : 0] S_AXI_RRESP,
		// Read valid. This signal indicates that the channel is
    		// signaling the required read data.
		output wire  S_AXI_RVALID,
		// Read ready. This signal indicates that the master can
    		// accept the read data and response information.
		input wire  S_AXI_RREADY
	);

    initial begin
        if (NUM_PWM < 1 || NUM_PWM > 16)
            $error("PWM_AXI NUM_PWM valid range is 1..16.");
        if (C_S_AXI_DATA_WIDTH != 32)
            $error("PWM_AXI currently supports C_S_AXI_DATA_WIDTH = 32 only.");
        if (C_S_AXI_ADDR_WIDTH < 7)
            $error("PWM_AXI requires C_S_AXI_ADDR_WIDTH >= 7.");
    end

	// AXI4LITE signals
	reg [C_S_AXI_ADDR_WIDTH-1 : 0] 	axi_awaddr;
	reg  	axi_awready;
	reg  	axi_wready;
	reg [1 : 0] 	axi_bresp;
	reg  	axi_bvalid;
	reg [C_S_AXI_ADDR_WIDTH-1 : 0] 	axi_araddr;
	reg  	axi_arready;
	reg [C_S_AXI_DATA_WIDTH-1 : 0] 	axi_rdata;
	reg [1 : 0] 	axi_rresp;
	reg  	axi_rvalid;
	reg [C_S_AXI_DATA_WIDTH-1 : 0] axi_wdata_hold;
	reg [(C_S_AXI_DATA_WIDTH/8)-1 : 0] axi_wstrb_hold;
	reg aw_pending;
	reg w_pending;
	reg ar_pending;

	// Example-specific design signals
	// local parameter for addressing 32 bit / 64 bit C_S_AXI_DATA_WIDTH
	// ADDR_LSB is used for addressing 32/64 bit registers/memories
	// ADDR_LSB = 2 for 32 bits (n downto 2)
	// ADDR_LSB = 3 for 64 bits (n downto 3)
	localparam integer ADDR_LSB = (C_S_AXI_DATA_WIDTH/32) + 1;
	localparam integer OPT_MEM_ADDR_BITS = 4;
	//----------------------------------------------
	//-- Signals for user logic register space example
	//------------------------------------------------
	//-- Number of Slave Registers 4


	reg [C_S_AXI_DATA_WIDTH-1:0]	ctrl_reg = 0;
	reg [C_S_AXI_DATA_WIDTH-1:0]	status_reg = 0;
	reg [C_S_AXI_DATA_WIDTH-1:0]	period_reg = 4096;
    reg [C_S_AXI_DATA_WIDTH-1:0]	duty_reg[0:NUM_PWM-1];
	
	wire	 slv_reg_rden;
	wire	 slv_reg_wren;
	reg [C_S_AXI_DATA_WIDTH-1:0]	 reg_data_out;
	integer	 byte_index;
	integer pwm_i;

	// I/O Connections assignments
	
    genvar i;
    generate
    for (i = 0; i < NUM_PWM ; i = i + 1) begin 
        assign duty_reg_out[i] = duty_reg[i];
    end
    endgenerate
    
	assign period_reg_out = period_reg;
	assign ctrl_reg_out = ctrl_reg;
	assign status_reg_out = status_reg;

	assign S_AXI_AWREADY	= axi_awready;
	assign S_AXI_WREADY	= axi_wready;
	assign S_AXI_BRESP	= axi_bresp;
	assign S_AXI_BVALID	= axi_bvalid;
	assign S_AXI_ARREADY	= axi_arready;
	assign S_AXI_RDATA	= axi_rdata;
	assign S_AXI_RRESP	= axi_rresp;
	assign S_AXI_RVALID	= axi_rvalid;
	// Capture AW independently from W so a standards-compliant AXI-Lite master
	// does not need to present address and data on the same cycle.
	always @( posedge S_AXI_ACLK )
	begin
	  if ( S_AXI_ARESETN == 1'b0 )
	    begin
	      axi_awready <= 1'b0;
	      aw_pending  <= 1'b0;
	    end 
	  else
	    begin
	      axi_awready <= (~aw_pending && ~axi_bvalid);
	      if (~aw_pending && ~axi_bvalid && S_AXI_AWVALID)
	        begin
	          axi_awready <= 1'b0;
	          aw_pending  <= 1'b1;
	        end
	      else if (~axi_bvalid && aw_pending && w_pending)
	        begin
	          aw_pending <= 1'b0;
	        end
	    end 
	end       

	always @( posedge S_AXI_ACLK )
	begin
	  if ( S_AXI_ARESETN == 1'b0 )
	    begin
	      axi_awaddr <= 0;
	    end 
	  else
	    begin    
	      if (~aw_pending && S_AXI_AWVALID)
	        begin
	          // Write address is held until the matching W channel arrives.
	          axi_awaddr <= S_AXI_AWADDR;
	        end
	    end 
	end       

	// Capture W independently from AW and hold payload/strobes until commit.
	always @( posedge S_AXI_ACLK )
	begin
	  if ( S_AXI_ARESETN == 1'b0 )
	    begin
	      axi_wready    <= 1'b0;
	      w_pending     <= 1'b0;
	      axi_wdata_hold<= 0;
	      axi_wstrb_hold<= 0;
	    end 
	  else
	    begin
	      axi_wready <= (~w_pending && ~axi_bvalid);
	      if (~w_pending && ~axi_bvalid && S_AXI_WVALID)
	        begin
	          axi_wready     <= 1'b0;
	          w_pending      <= 1'b1;
	          axi_wdata_hold <= S_AXI_WDATA;
	          axi_wstrb_hold <= S_AXI_WSTRB;
	        end
	      else if (~axi_bvalid && aw_pending && w_pending)
	        begin
	          w_pending <= 1'b0;
	        end
	    end 
	end       

	// Implement memory mapped register select and write logic generation.
	// Write payload/strobes are held until both AW and W have been accepted,
	// then committed in a single beat.
	// Commit a write only after both AW and W have been accepted.
	assign slv_reg_wren = ~axi_bvalid && aw_pending && w_pending;

	always @( posedge S_AXI_ACLK )
	begin
	  if ( S_AXI_ARESETN == 1'b0 )
	    begin
	      for (pwm_i = 0; pwm_i < NUM_PWM; pwm_i = pwm_i + 1)
	           duty_reg[pwm_i] <= 0;
	      period_reg <= 4096;
	      ctrl_reg <= 0;
	      status_reg <= 0;
	    end 
	  else begin
	    if (slv_reg_wren)
	      begin
	        case ( axi_awaddr[ADDR_LSB+OPT_MEM_ADDR_BITS:ADDR_LSB] )
	          5'h0:
	            for ( byte_index = 0; byte_index <= (C_S_AXI_DATA_WIDTH/8)-1; byte_index = byte_index+1 )
	              if ( axi_wstrb_hold[byte_index] == 1 ) begin
	                // Respective byte enables are asserted as per write strobes 
	                // Slave register 0
	                ctrl_reg[(byte_index*8) +: 8] <= axi_wdata_hold[(byte_index*8) +: 8];
	              end  
	          5'h1: begin
                  // status_reg is read-only (hardware status), writes are ignored.
              end
	          5'h2:
	            for ( byte_index = 0; byte_index <= (C_S_AXI_DATA_WIDTH/8)-1; byte_index = byte_index+1 )
	              if ( axi_wstrb_hold[byte_index] == 1 ) begin
	                // Respective byte enables are asserted as per write strobes 
	                // Slave register 2
	                period_reg[(byte_index*8) +: 8] <= axi_wdata_hold[(byte_index*8) +: 8];
	              end  
	          5'h10, 5'h11, 5'h12, 5'h13, 5'h14, 5'h15, 5'h16, 5'h17, 5'h18, 5'h19, 5'h1A, 5'h1B, 5'h1C, 5'h1D, 5'h1E, 5'h1F:
	             for (pwm_i = 0; pwm_i < NUM_PWM; pwm_i = pwm_i + 1)
	               if ( axi_awaddr[ADDR_LSB+OPT_MEM_ADDR_BITS-1:ADDR_LSB] == pwm_i ) begin
                    for ( byte_index = 0; byte_index <= (C_S_AXI_DATA_WIDTH/8)-1; byte_index = byte_index+1 )
                      if ( axi_wstrb_hold[byte_index] == 1 ) begin
                        // Respective byte enables are asserted as per write strobes 
                        // Slave register 3
                        duty_reg[pwm_i][(byte_index*8) +: 8] <= axi_wdata_hold[(byte_index*8) +: 8];
                      end
                  end  
	          default : begin
                  // no-op
              end
	        endcase
	      end
          // bit0: enable_cfg
          // bit1: period_is_zero_fault
          // bit2: pwm_active (enable && period_valid)
          status_reg <= {29'd0, (ctrl_reg[0] && (period_reg != 0)), (period_reg == 0), ctrl_reg[0]};
	  end
	end    

	// Implement write response logic generation
	// The write response and response valid signals are asserted by the slave 
	// when axi_wready, S_AXI_WVALID, axi_wready and S_AXI_WVALID are asserted.  
	// This marks the acceptance of address and indicates the status of 
	// write transaction.

	always @( posedge S_AXI_ACLK )
	begin
	  if ( S_AXI_ARESETN == 1'b0 )
	    begin
	      axi_bvalid  <= 0;
	      axi_bresp   <= 2'b0;
	    end 
	  else
	    begin    
	      if (~axi_bvalid && aw_pending && w_pending)
	        begin
	          // indicates a valid write response is available
	          axi_bvalid <= 1'b1;
	          axi_bresp  <= 2'b0; // 'OKAY' response 
	        end                   // work error responses in future
	      else
	        begin
	          if (S_AXI_BREADY && axi_bvalid) 
	            //check if bready is asserted while bvalid is high) 
	            //(there is a possibility that bready is always asserted high)   
	            begin
	              axi_bvalid <= 1'b0; 
	            end  
	        end
	    end
	end   

	always @( posedge S_AXI_ACLK )
	begin
	  if ( S_AXI_ARESETN == 1'b0 )
	    begin
	      axi_arready <= 1'b0;
	      axi_araddr  <= 0;
	      ar_pending  <= 1'b0;
	    end 
	  else
	    begin    
	      axi_arready <= (~ar_pending && ~axi_rvalid);
	      if (~ar_pending && ~axi_rvalid && S_AXI_ARVALID)
	        begin
	          axi_arready <= 1'b0;
	          // Read address latching
	          axi_araddr  <= S_AXI_ARADDR;
	          ar_pending  <= 1'b1;
	        end
	      else if (~axi_rvalid && ar_pending)
	        begin
	          ar_pending <= 1'b0;
	        end
	    end 
	end       

	// Implement axi_rvalid generation from the captured read address.
	always @( posedge S_AXI_ACLK )
	begin
	  if ( S_AXI_ARESETN == 1'b0 )
	    begin
	      axi_rvalid <= 0;
	      axi_rresp  <= 0;
	    end 
	  else
	    begin    
	      if (~axi_rvalid && ar_pending)
	        begin
	          // Valid read data is available at the read data bus
	          axi_rvalid <= 1'b1;
	          axi_rresp  <= 2'b0; // 'OKAY' response
	        end   
	      else if (axi_rvalid && S_AXI_RREADY)
	        begin
	          // Read data is accepted by the master
	          axi_rvalid <= 1'b0;
	        end                
	    end
	end    

	// Decode the captured read address and return stable register data.
	assign slv_reg_rden = ~axi_rvalid & ar_pending;
	always @(*)
	begin
	      // Address decoding for reading registers
	      case ( axi_araddr[ADDR_LSB+OPT_MEM_ADDR_BITS:ADDR_LSB] )
	        5'h0   : reg_data_out = ctrl_reg;
	        5'h1   : reg_data_out = status_reg;
	        5'h2   : reg_data_out = period_reg;
            5'h10, 5'h11, 5'h12, 5'h13, 5'h14, 5'h15, 5'h16, 5'h17, 5'h18, 5'h19, 5'h1A, 5'h1B, 5'h1C, 5'h1D, 5'h1E, 5'h1F: begin
               reg_data_out = 0;
               for (pwm_i = 0; pwm_i < NUM_PWM; pwm_i = pwm_i + 1)
                  if ( axi_araddr[ADDR_LSB+OPT_MEM_ADDR_BITS-1:ADDR_LSB] == pwm_i ) 
                     reg_data_out = duty_reg[pwm_i];
            end
	        default : reg_data_out = 0;
	      endcase
	end

	// Output register or memory read data
	always @( posedge S_AXI_ACLK )
	begin
	  if ( S_AXI_ARESETN == 1'b0 )
	    begin
	      axi_rdata  <= 0;
	    end 
	  else
	    begin    
	      // Output data from the captured read address.
	      if (slv_reg_rden)
	        begin
	          axi_rdata <= reg_data_out;     // register read data
	        end   
	    end
	end    

	// Add user logic here

	// User logic ends

	endmodule
