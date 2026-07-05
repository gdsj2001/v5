library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity axi_dynclk_S00_AXI is
	generic (
		-- Users to add parameters here

		-- User parameters ends
		-- Do not modify the parameters beyond this line

		-- Width of S_AXI data bus
		C_S_AXI_DATA_WIDTH	: integer	:= 32;
		-- Width of S_AXI address bus
		C_S_AXI_ADDR_WIDTH	: integer	:= 5
	);
	port (
		-- Users to add ports here
		CTRL_REG                       :out std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
        STAT_REG                       :in  std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
        CLK_O_REG                      :out std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
        CLK_FB_REG                     :out std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
        CLK_FRAC_REG                   :out std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
        CLK_DIV_REG                    :out std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
        CLK_LOCK_REG                   :out std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
        CLK_FLTR_REG                   :out std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
		-- User ports ends
		-- Do not modify the ports beyond this line

		-- Global Clock Signal
		S_AXI_ACLK	: in std_logic;
		-- Global Reset Signal. This Signal is Active LOW
		S_AXI_ARESETN	: in std_logic;
		-- Write address (issued by master, acceped by Slave)
		S_AXI_AWADDR	: in std_logic_vector(C_S_AXI_ADDR_WIDTH-1 downto 0);
		-- Write channel Protection type. This signal indicates the
    		-- privilege and security level of the transaction, and whether
    		-- the transaction is a data access or an instruction access.
		S_AXI_AWPROT	: in std_logic_vector(2 downto 0);
		-- Write address valid. This signal indicates that the master signaling
    		-- valid write address and control information.
		S_AXI_AWVALID	: in std_logic;
		-- Write address ready. This signal indicates that the slave is ready
    		-- to accept an address and associated control signals.
		S_AXI_AWREADY	: out std_logic;
		-- Write data (issued by master, acceped by Slave)
		S_AXI_WDATA	: in std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
		-- Write strobes. This signal indicates which byte lanes hold
    		-- valid data. There is one write strobe bit for each eight
    		-- bits of the write data bus.
		S_AXI_WSTRB	: in std_logic_vector((C_S_AXI_DATA_WIDTH/8)-1 downto 0);
		-- Write valid. This signal indicates that valid write
    		-- data and strobes are available.
		S_AXI_WVALID	: in std_logic;
		-- Write ready. This signal indicates that the slave
    		-- can accept the write data.
		S_AXI_WREADY	: out std_logic;
		-- Write response. This signal indicates the status
    		-- of the write transaction.
		S_AXI_BRESP	: out std_logic_vector(1 downto 0);
		-- Write response valid. This signal indicates that the channel
    		-- is signaling a valid write response.
		S_AXI_BVALID	: out std_logic;
		-- Response ready. This signal indicates that the master
    		-- can accept a write response.
		S_AXI_BREADY	: in std_logic;
		-- Read address (issued by master, acceped by Slave)
		S_AXI_ARADDR	: in std_logic_vector(C_S_AXI_ADDR_WIDTH-1 downto 0);
		-- Protection type. This signal indicates the privilege
    		-- and security level of the transaction, and whether the
    		-- transaction is a data access or an instruction access.
		S_AXI_ARPROT	: in std_logic_vector(2 downto 0);
		-- Read address valid. This signal indicates that the channel
    		-- is signaling valid read address and control information.
		S_AXI_ARVALID	: in std_logic;
		-- Read address ready. This signal indicates that the slave is
    		-- ready to accept an address and associated control signals.
		S_AXI_ARREADY	: out std_logic;
		-- Read data (issued by slave)
		S_AXI_RDATA	: out std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
		-- Read response. This signal indicates the status of the
    		-- read transfer.
		S_AXI_RRESP	: out std_logic_vector(1 downto 0);
		-- Read valid. This signal indicates that the channel is
    		-- signaling the required read data.
		S_AXI_RVALID	: out std_logic;
		-- Read ready. This signal indicates that the master can
    		-- accept the read data and response information.
		S_AXI_RREADY	: in std_logic
	);
end axi_dynclk_S00_AXI;

architecture arch_imp of axi_dynclk_S00_AXI is

	-- AXI4LITE signals
	signal axi_awaddr	     : std_logic_vector(C_S_AXI_ADDR_WIDTH-1 downto 0);
	signal axi_awready	     : std_logic;
	signal axi_wready	     : std_logic;
	signal axi_bresp	     : std_logic_vector(1 downto 0);
	signal axi_bvalid	     : std_logic;
	signal axi_araddr	     : std_logic_vector(C_S_AXI_ADDR_WIDTH-1 downto 0);
	signal axi_arready	     : std_logic;
	signal axi_rdata	     : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
	signal axi_rresp	     : std_logic_vector(1 downto 0);
	signal axi_rvalid	     : std_logic;
    signal axi_wdata_hold    : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
    signal axi_wstrb_hold    : std_logic_vector((C_S_AXI_DATA_WIDTH/8)-1 downto 0);
    signal aw_pending        : std_logic;
    signal w_pending         : std_logic;
    signal ar_pending        : std_logic;

	-- local parameter for addressing 32 bit / 64 bit C_S_AXI_DATA_WIDTH
	constant ADDR_LSB          : integer := (C_S_AXI_DATA_WIDTH/32)+ 1;
	constant OPT_MEM_ADDR_BITS : integer := 2;

	-- Number of Slave Registers 8
	signal slv_reg0	    : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
	signal slv_reg1	    : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
	signal slv_reg2	    : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
	signal slv_reg3	    : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
	signal slv_reg4	    : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
	signal slv_reg5	    : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
	signal slv_reg6	    : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
	signal slv_reg7	    : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
	signal slv_reg_rden	: std_logic;
	signal slv_reg_wren	: std_logic;
	signal reg_data_out	: std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);

begin
	-- I/O Connections assignments

	S_AXI_AWREADY	<= axi_awready;
	S_AXI_WREADY	<= axi_wready;
	S_AXI_BRESP	    <= axi_bresp;
	S_AXI_BVALID	<= axi_bvalid;
	S_AXI_ARREADY	<= axi_arready;
	S_AXI_RDATA	    <= axi_rdata;
	S_AXI_RRESP	    <= axi_rresp;
	S_AXI_RVALID	<= axi_rvalid;

    -- Capture AW independently from W to support decoupled AXI-Lite masters.
    process (S_AXI_ACLK)
    begin
      if rising_edge(S_AXI_ACLK) then
        if S_AXI_ARESETN = '0' then
          axi_awready <= '0';
          aw_pending  <= '0';
          axi_awaddr  <= (others => '0');
        else
          axi_awready <= '0';
          if (aw_pending = '0' and axi_bvalid = '0') then
            axi_awready <= '1';
            if (S_AXI_AWVALID = '1') then
              axi_awready <= '0';
              aw_pending  <= '1';
              axi_awaddr  <= S_AXI_AWADDR;
            end if;
          elsif (axi_bvalid = '0' and aw_pending = '1' and w_pending = '1') then
            aw_pending <= '0';
          end if;
        end if;
      end if;
    end process;

    -- Capture W independently from AW and hold payload/strobes until commit.
    process (S_AXI_ACLK)
    begin
      if rising_edge(S_AXI_ACLK) then
        if S_AXI_ARESETN = '0' then
          axi_wready    <= '0';
          w_pending     <= '0';
          axi_wdata_hold<= (others => '0');
          axi_wstrb_hold<= (others => '0');
        else
          axi_wready <= '0';
          if (w_pending = '0' and axi_bvalid = '0') then
            axi_wready <= '1';
            if (S_AXI_WVALID = '1') then
              axi_wready     <= '0';
              w_pending      <= '1';
              axi_wdata_hold <= S_AXI_WDATA;
              axi_wstrb_hold <= S_AXI_WSTRB;
            end if;
          elsif (axi_bvalid = '0' and aw_pending = '1' and w_pending = '1') then
            w_pending <= '0';
          end if;
        end if;
      end if;
    end process;

	-- Write payload/strobes are committed only after both AW and W were accepted.
	slv_reg_wren <= '1' when (axi_bvalid = '0' and aw_pending = '1' and w_pending = '1') else '0';

	process (S_AXI_ACLK)
	variable loc_addr : std_logic_vector(OPT_MEM_ADDR_BITS downto 0);
	begin
	  if rising_edge(S_AXI_ACLK) then
	    if S_AXI_ARESETN = '0' then
	      slv_reg0 <= (others => '0');
	      slv_reg2 <= (others => '0');
	      slv_reg3 <= (others => '0');
	      slv_reg4 <= (others => '0');
	      slv_reg5 <= (others => '0');
	      slv_reg6 <= (others => '0');
	      slv_reg7 <= (others => '0');
	    else
	      loc_addr := axi_awaddr(ADDR_LSB + OPT_MEM_ADDR_BITS downto ADDR_LSB);
	      if (slv_reg_wren = '1') then
	        case loc_addr is
	          when b"000" =>
	            for byte_index in 0 to (C_S_AXI_DATA_WIDTH/8-1) loop
	              if (axi_wstrb_hold(byte_index) = '1') then
	                slv_reg0(byte_index*8+7 downto byte_index*8) <= axi_wdata_hold(byte_index*8+7 downto byte_index*8);
	              end if;
	            end loop;
	          when b"010" =>
	            for byte_index in 0 to (C_S_AXI_DATA_WIDTH/8-1) loop
	              if (axi_wstrb_hold(byte_index) = '1') then
	                slv_reg2(byte_index*8+7 downto byte_index*8) <= axi_wdata_hold(byte_index*8+7 downto byte_index*8);
	              end if;
	            end loop;
	          when b"011" =>
	            for byte_index in 0 to (C_S_AXI_DATA_WIDTH/8-1) loop
	              if (axi_wstrb_hold(byte_index) = '1') then
	                slv_reg3(byte_index*8+7 downto byte_index*8) <= axi_wdata_hold(byte_index*8+7 downto byte_index*8);
	              end if;
	            end loop;
	          when b"100" =>
	            for byte_index in 0 to (C_S_AXI_DATA_WIDTH/8-1) loop
	              if (axi_wstrb_hold(byte_index) = '1') then
	                slv_reg4(byte_index*8+7 downto byte_index*8) <= axi_wdata_hold(byte_index*8+7 downto byte_index*8);
	              end if;
	            end loop;
	          when b"101" =>
	            for byte_index in 0 to (C_S_AXI_DATA_WIDTH/8-1) loop
	              if (axi_wstrb_hold(byte_index) = '1') then
	                slv_reg5(byte_index*8+7 downto byte_index*8) <= axi_wdata_hold(byte_index*8+7 downto byte_index*8);
	              end if;
	            end loop;
	          when b"110" =>
	            for byte_index in 0 to (C_S_AXI_DATA_WIDTH/8-1) loop
	              if (axi_wstrb_hold(byte_index) = '1') then
	                slv_reg6(byte_index*8+7 downto byte_index*8) <= axi_wdata_hold(byte_index*8+7 downto byte_index*8);
	              end if;
	            end loop;
	          when b"111" =>
	            for byte_index in 0 to (C_S_AXI_DATA_WIDTH/8-1) loop
	              if (axi_wstrb_hold(byte_index) = '1') then
	                slv_reg7(byte_index*8+7 downto byte_index*8) <= axi_wdata_hold(byte_index*8+7 downto byte_index*8);
	              end if;
	            end loop;
	          when others =>
                null;
	        end case;
	      end if;
	    end if;
	  end if;
	end process;

	-- Write response after a committed AW+W pair.
	process (S_AXI_ACLK)
	begin
	  if rising_edge(S_AXI_ACLK) then
	    if S_AXI_ARESETN = '0' then
	      axi_bvalid  <= '0';
	      axi_bresp   <= "00";
	    else
	      if (axi_bvalid = '0' and aw_pending = '1' and w_pending = '1') then
	        axi_bvalid <= '1';
	        axi_bresp  <= "00";
	      elsif (S_AXI_BREADY = '1' and axi_bvalid = '1') then
	        axi_bvalid <= '0';
	      end if;
	    end if;
	  end if;
	end process;

	-- Capture AR independently from R channel backpressure.
	process (S_AXI_ACLK)
	begin
	  if rising_edge(S_AXI_ACLK) then
	    if S_AXI_ARESETN = '0' then
	      axi_arready <= '0';
	      axi_araddr  <= (others => '0');
          ar_pending  <= '0';
	    else
	      axi_arready <= '0';
	      if (ar_pending = '0' and axi_rvalid = '0') then
	        axi_arready <= '1';
            if (S_AXI_ARVALID = '1') then
              axi_arready <= '0';
              axi_araddr  <= S_AXI_ARADDR;
              ar_pending  <= '1';
            end if;
          elsif (axi_rvalid = '0' and ar_pending = '1') then
            ar_pending <= '0';
	      end if;
	    end if;
	  end if;
	end process;

	-- Read response generation from captured read address.
	process (S_AXI_ACLK)
	begin
	  if rising_edge(S_AXI_ACLK) then
	    if S_AXI_ARESETN = '0' then
	      axi_rvalid <= '0';
	      axi_rresp  <= "00";
	    else
	      if (axi_rvalid = '0' and ar_pending = '1') then
	        axi_rvalid <= '1';
	        axi_rresp  <= "00";
	      elsif (axi_rvalid = '1' and S_AXI_RREADY = '1') then
	        axi_rvalid <= '0';
	      end if;
	    end if;
	  end if;
	end process;

	-- Slave register read enable is asserted when a captured read is about to be served.
	slv_reg_rden <= '1' when (axi_rvalid = '0' and ar_pending = '1') else '0';

	process (slv_reg0, slv_reg1, slv_reg2, slv_reg3, slv_reg4, slv_reg5, slv_reg6, slv_reg7, axi_araddr)
	variable loc_addr : std_logic_vector(OPT_MEM_ADDR_BITS downto 0);
	begin
	    loc_addr := axi_araddr(ADDR_LSB + OPT_MEM_ADDR_BITS downto ADDR_LSB);
	    case loc_addr is
	      when b"000" =>
	        reg_data_out <= slv_reg0;
	      when b"001" =>
	        reg_data_out <= slv_reg1;
	      when b"010" =>
	        reg_data_out <= slv_reg2;
	      when b"011" =>
	        reg_data_out <= slv_reg3;
	      when b"100" =>
	        reg_data_out <= slv_reg4;
	      when b"101" =>
	        reg_data_out <= slv_reg5;
	      when b"110" =>
	        reg_data_out <= slv_reg6;
	      when b"111" =>
	        reg_data_out <= slv_reg7;
	      when others =>
	        reg_data_out <= (others => '0');
	    end case;
	end process;

	process (S_AXI_ACLK)
	begin
	  if rising_edge(S_AXI_ACLK) then
	    if S_AXI_ARESETN = '0' then
	      axi_rdata <= (others => '0');
	    else
	      if (slv_reg_rden = '1') then
	        axi_rdata <= reg_data_out;
	      end if;
	    end if;
	  end if;
	end process;

	-- Add user logic here
	CTRL_REG    <= slv_reg0;
    slv_reg1    <= STAT_REG;
    CLK_O_REG   <= slv_reg2;
    CLK_FB_REG  <= slv_reg3;
    CLK_FRAC_REG<= slv_reg4;
    CLK_DIV_REG <= slv_reg5;
    CLK_LOCK_REG<= slv_reg6;
    CLK_FLTR_REG<= slv_reg7;
	-- User logic ends

end arch_imp;

