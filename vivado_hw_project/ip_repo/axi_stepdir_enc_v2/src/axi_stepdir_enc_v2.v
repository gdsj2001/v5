`timescale 1ns/1ps

// 6-axis step/dir + encoder IP (slice executor mode)
// LinuxCNC/PS writes per-servo-slice delta_steps[axis] and slice_ticks, then pulses GLOBAL_APPLY.
// FPGA executes the whole slice with deterministic scheduling and hardware pulse shaping.
module axi_stepdir_enc_v2 #(
    parameter integer C_S_AXI_ADDR_WIDTH = 12,
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer N_AXES = 6,
    parameter integer CLK_FREQ_HZ = 100_000_000
)(
    input  wire                           s_axi_aclk,
    input  wire                           s_axi_aresetn,
    input  wire [C_S_AXI_ADDR_WIDTH-1:0]  s_axi_awaddr,
    input  wire                           s_axi_awvalid,
    output reg                            s_axi_awready,
    input  wire [C_S_AXI_DATA_WIDTH-1:0]  s_axi_wdata,
    input  wire [(C_S_AXI_DATA_WIDTH/8)-1:0] s_axi_wstrb,
    input  wire                           s_axi_wvalid,
    output reg                            s_axi_wready,
    output reg [1:0]                      s_axi_bresp,
    output reg                            s_axi_bvalid,
    input  wire                           s_axi_bready,
    input  wire [C_S_AXI_ADDR_WIDTH-1:0]  s_axi_araddr,
    input  wire                           s_axi_arvalid,
    output reg                            s_axi_arready,
    output reg [C_S_AXI_DATA_WIDTH-1:0]   s_axi_rdata,
    output reg [1:0]                      s_axi_rresp,
    output reg                            s_axi_rvalid,
    input  wire                           s_axi_rready,
    output wire [N_AXES-1:0]              step_o,
    output wire [N_AXES-1:0]              dir_o,
    input  wire [N_AXES-1:0]              enc_a_i,
    input  wire [N_AXES-1:0]              enc_b_i,
    input  wire [N_AXES-1:0]              enc_z_i,
    output wire                           irq_o
);
    localparam [31:0] REG_ID      = 32'h5354_4550; // 'STEP'
    localparam [31:0] REG_VERSION = 32'h0003_0507; // v3.5.7: pipeline DDA issue before pending queue update

    localparam [9:0] ADDR_ID           = 10'h000; // 0x000
    localparam [9:0] ADDR_VERSION      = 10'h001; // 0x004
    localparam [9:0] ADDR_CLK_FREQ     = 10'h002; // 0x008
    localparam [9:0] ADDR_N_AXES       = 10'h003; // 0x00C
    localparam [9:0] ADDR_EXEC_STATUS  = 10'h004; // 0x010
    localparam [9:0] ADDR_GLOBAL_APPLY = 10'h005; // 0x014
    localparam [9:0] ADDR_SLICE_TICKS  = 10'h006; // 0x018
    localparam [9:0] ADDR_EVENT_CNT        = 10'h007; // 0x01C
    localparam [9:0] ADDR_GLOBAL_CFG       = 10'h008; // 0x020
    localparam [9:0] ADDR_SLICE_SEQ_SHADOW = 10'h009; // 0x024
    localparam [9:0] ADDR_SLICE_SEQ_ACTIVE = 10'h00A; // 0x028
    localparam [9:0] ADDR_SLICE_SEQ_DONE   = 10'h00B; // 0x02C
    localparam [9:0] ADDR_EXEC_LIVE_STATUS = 10'h00C; // 0x030
    localparam [9:0] ADDR_RESET_STATUS     = 10'h00D; // 0x034
    localparam [9:0] ADDR_RESET_COUNT      = 10'h00E; // 0x038
    localparam [9:0] ADDR_LAST_RESET_CAUSE = 10'h00F; // 0x03C
    localparam [9:0] ADDR_LAST_RESET_SEQ   = 10'h010; // 0x040

    localparam [11:0] ADDR_AXIS_DBG_BASE  = 12'h300; // + axis*8: [0] emitted_steps, [4] steps_remain
    localparam [11:0] ADDR_AXIS_LIVE_BASE = 12'h380; // + axis*0x20: status + pulse/encoder counters

    // EXEC_STATUS bits
    localparam integer ST_BUSY             = 0;
    localparam integer ST_DONE             = 1;
    localparam integer ST_FAULT            = 2;
    localparam integer ST_OVERRUN          = 3;
    localparam integer ST_APPLY_WHILE_BUSY = 4;
    localparam integer ST_INVALID_SLICE    = 5; // fast precheck fail (not full admission proof)

    // GLOBAL_CFG bits
    localparam integer CFG_FIRST_STEP_SYNC = 0;
    localparam integer CFG_ENC_LOOPBACK    = 1; // debug-only: step/dir -> encoder count loopback

    // RESET_STATUS bits
    localparam integer RST_AXI_SEEN        = 0;
    localparam integer RST_EXEC_SEEN       = 1;
    localparam integer RST_INIT_DONE       = 2;
    localparam integer RST_RELEASED        = 3;
    localparam integer RST_APPLY_SINCE_RST = 4;

    // LAST_RESET_CAUSE values
    localparam [31:0] RST_CAUSE_NONE             = 32'd0;
    localparam [31:0] RST_CAUSE_AXI_RESET        = 32'd1;
    localparam [31:0] RST_CAUSE_RESET_RELEASE    = 32'd2;
    localparam [31:0] RST_CAUSE_APPLY_WHILE_BUSY = 32'd3;
    localparam [31:0] RST_CAUSE_INVALID_SLICE    = 32'd4;
    localparam [31:0] RST_CAUSE_OVERRUN          = 32'd5;

    initial begin
        if (C_S_AXI_ADDR_WIDTH != 12)
            $error("axi_stepdir_enc_v2 currently supports C_S_AXI_ADDR_WIDTH = 12 only.");
        if (C_S_AXI_DATA_WIDTH != 32)
            $error("axi_stepdir_enc_v2 currently supports C_S_AXI_DATA_WIDTH = 32 only.");
        if (CLK_FREQ_HZ <= 0)
            $error("axi_stepdir_enc_v2 requires CLK_FREQ_HZ > 0.");
        if (N_AXES < 1)
            $error("axi_stepdir_enc_v2 requires N_AXES >= 1.");
        if (N_AXES > 16)
            $error("axi_stepdir_enc_v2 supports N_AXES <= 16.");
    end

    // AXI-Lite front-end
    reg [C_S_AXI_ADDR_WIDTH-1:0] awaddr_hold;
    reg [C_S_AXI_ADDR_WIDTH-1:0] araddr_hold;
    reg [C_S_AXI_DATA_WIDTH-1:0] wdata_hold;
    reg [(C_S_AXI_DATA_WIDTH/8)-1:0] wstrb_hold;
    reg aw_pending;
    reg w_pending;
    reg ar_pending;

    // Command pulses from AXI writes
    reg sync_request;
    reg status_clear_pulse;
    reg [N_AXES-1:0] clear_count_pulse;
    reg [N_AXES-1:0] clear_z_seen_pulse;

    // Shadow registers (PS-writable)
    reg [31:0] control_reg      [0:N_AXES-1]; // bit0 enable, bit1 dir invert, bit3 index enable
    reg [31:0] delta_steps_shadow [0:N_AXES-1];
    reg [31:0] step_width       [0:N_AXES-1];
    reg [31:0] step_space       [0:N_AXES-1];
    reg [31:0] dir_setup        [0:N_AXES-1];
    reg [31:0] dir_hold         [0:N_AXES-1];

    reg [31:0] slice_ticks_shadow;
    reg [31:0] global_cfg_shadow;

    // Active registers (latched on GLOBAL_APPLY)
    reg [31:0] control_active      [0:N_AXES-1];
    reg [31:0] delta_steps_active  [0:N_AXES-1];
    reg [31:0] step_width_active   [0:N_AXES-1];
    reg [31:0] step_space_active   [0:N_AXES-1];
    reg [31:0] dir_setup_active    [0:N_AXES-1];
    reg [31:0] dir_hold_active     [0:N_AXES-1];

    reg [31:0] slice_ticks_active;
    reg [31:0] global_cfg_active;
    reg [31:0] slice_seq_shadow;
    reg [31:0] slice_seq_active;
    reg [31:0] slice_seq_done;

    // Executor state
    reg exec_busy;
    reg exec_done;
    reg exec_fault;
    reg exec_overrun;
    reg exec_apply_while_busy;
    reg exec_invalid_slice;
    reg exec_start_pending;
    reg exec_flush_pending;
    reg invalid_slice_pending;
    reg [31:0] invalid_slice_seq_pending;

    reg [31:0] event_cnt;
    reg [31:0] tick_ctr;
    reg [31:0] sched_denom;
    reg        start_barrier_wait;
    reg        release_first_steps_q;

    reg [31:0] steps_abs     [0:N_AXES-1];
    reg [31:0] steps_remain  [0:N_AXES-1];
    reg [31:0] dda_num       [0:N_AXES-1];
    reg [31:0] accum         [0:N_AXES-1];
    reg [31:0] pending_steps [0:N_AXES-1];
    reg [31:0] emitted_steps [0:N_AXES-1];

    reg        target_dir    [0:N_AXES-1];
    reg        eff_dir       [0:N_AXES-1];
    reg [31:0] width_cnt     [0:N_AXES-1];
    reg [31:0] space_cnt         [0:N_AXES-1];
    reg [31:0] dsetup_cnt        [0:N_AXES-1];
    reg [31:0] dhold_cnt         [0:N_AXES-1];
    reg        step_q            [0:N_AXES-1];
    reg        step_q_prev       [0:N_AXES-1];
    reg        axis_active_in_slice [0:N_AXES-1];
    reg        axis_dir_changed     [0:N_AXES-1];
    reg        first_step_armed     [0:N_AXES-1];
    reg        issue_step_pipe      [0:N_AXES-1];
    reg [31:0] pulse_rise_count     [0:N_AXES-1];
    reg [31:0] pulse_fall_count     [0:N_AXES-1];

    // Encoder state
    reg [31:0] enc_count           [0:N_AXES-1];
    reg        enc_z_seen          [0:N_AXES-1];
    reg [31:0] enc_raw_edge_count  [0:N_AXES-1];
    reg [31:0] enc_filt_edge_count [0:N_AXES-1];
    reg [31:0] enc_glitch_count    [0:N_AXES-1];

    // Reset telemetry
    reg [31:0] reset_status;
    reg [31:0] reset_count;
    reg        reset_release_seen;
    reg [31:0] last_reset_cause;
    reg [31:0] last_reset_seq;

    // Common loop/temp vars
    integer rst_i;
    integer exec_i;
    integer chk_i;
    reg issue_step;
    reg [31:0] pending_next;
    reg [31:0] denom_ticks;
    reg [32:0] accum_sum;
    reg [31:0] pulse_width_reload;
    reg [32:0] pulse_gap_sum;
    reg [31:0] pulse_gap_reload;
    reg [31:0] width_next;
    reg [31:0] space_next;
    reg [31:0] dsetup_next;
    reg pulse_ready;
    reg pulse_launch;
    reg all_counts_done;
    reg all_quiet;
    reg tail_backlog_overrun;
    reg barrier_ready;
    reg release_first_steps;
    reg any_axis_with_steps;
    reg legal_check_fail;
    reg [31:0] delta_masked_init;
    reg [31:0] abs_steps_init;
    reg [31:0] abs_steps_init_minus1;
    reg        target_dir_init;
    reg pending_release_step;
    reg pending_issue_step;
    reg [1:0] pending_enqueue_count;

    // Per-axis shadow predecode to avoid deep cross-axis combinational chains
    // in the GLOBAL_APPLY admission path.
    wire [31:0] delta_masked_shadow_w [0:N_AXES-1];
    wire [31:0] abs_steps_shadow_w    [0:N_AXES-1];
    wire        target_dir_shadow_w   [0:N_AXES-1];
    genvar pre_i;
    generate
        for (pre_i = 0; pre_i < N_AXES; pre_i = pre_i + 1) begin : PREDECODE
            assign delta_masked_shadow_w[pre_i] = control_reg[pre_i][0] ? delta_steps_shadow[pre_i] : 32'd0;
            assign abs_steps_shadow_w[pre_i] = delta_masked_shadow_w[pre_i][31] ?
                                               ((~delta_masked_shadow_w[pre_i]) + 32'd1) :
                                               delta_masked_shadow_w[pre_i];
            assign target_dir_shadow_w[pre_i] = delta_masked_shadow_w[pre_i][31] ^ control_reg[pre_i][1];
        end
    endgenerate

    // AXI-Lite protocol shell
    always @(posedge s_axi_aclk) begin
        if (!s_axi_aresetn) begin
            s_axi_awready <= 1'b0;
            s_axi_wready  <= 1'b0;
            s_axi_bvalid  <= 1'b0;
            s_axi_bresp   <= 2'b00;
            s_axi_arready <= 1'b0;
            s_axi_rvalid  <= 1'b0;
            s_axi_rresp   <= 2'b00;
            s_axi_rdata   <= 32'd0;

            awaddr_hold <= {C_S_AXI_ADDR_WIDTH{1'b0}};
            araddr_hold <= {C_S_AXI_ADDR_WIDTH{1'b0}};
            wdata_hold  <= 32'd0;
            wstrb_hold  <= 4'd0;
            aw_pending  <= 1'b0;
            w_pending   <= 1'b0;
            ar_pending  <= 1'b0;

            sync_request <= 1'b0;
            status_clear_pulse <= 1'b0;
            clear_count_pulse <= {N_AXES{1'b0}};
            clear_z_seen_pulse <= {N_AXES{1'b0}};

            slice_ticks_shadow <= 32'd1;
            global_cfg_shadow <= 32'h0000_0001;
            slice_seq_shadow <= 32'd0;

            for (rst_i = 0; rst_i < N_AXES; rst_i = rst_i + 1) begin
                control_reg[rst_i] <= 32'd0;
                delta_steps_shadow[rst_i] <= 32'd0;
                step_width[rst_i] <= 32'd0;
                step_space[rst_i] <= 32'd0;
                dir_setup[rst_i] <= 32'd0;
                dir_hold[rst_i] <= 32'd0;
            end
        end else begin
            s_axi_awready <= 1'b0;
            s_axi_wready  <= 1'b0;
            s_axi_arready <= 1'b0;

            sync_request <= 1'b0;
            status_clear_pulse <= 1'b0;
            clear_count_pulse <= {N_AXES{1'b0}};
            clear_z_seen_pulse <= {N_AXES{1'b0}};

            if (!aw_pending && !s_axi_bvalid) begin
                s_axi_awready <= 1'b1;
                if (s_axi_awvalid) begin
                    aw_pending <= 1'b1;
                    awaddr_hold <= s_axi_awaddr;
                end
            end

            if (!w_pending && !s_axi_bvalid) begin
                s_axi_wready <= 1'b1;
                if (s_axi_wvalid) begin
                    w_pending <= 1'b1;
                    wdata_hold <= s_axi_wdata;
                    wstrb_hold <= s_axi_wstrb;
                end
            end

            if (!s_axi_bvalid && aw_pending && w_pending) begin
                axi_write(awaddr_hold, wdata_hold, wstrb_hold);
                s_axi_bvalid <= 1'b1;
                s_axi_bresp <= 2'b00;
                aw_pending <= 1'b0;
                w_pending <= 1'b0;
            end else if (s_axi_bvalid && s_axi_bready) begin
                s_axi_bvalid <= 1'b0;
            end

            if (!ar_pending && !s_axi_rvalid) begin
                s_axi_arready <= 1'b1;
                if (s_axi_arvalid) begin
                    ar_pending <= 1'b1;
                    araddr_hold <= s_axi_araddr;
                end
            end

            if (!s_axi_rvalid && ar_pending) begin
                s_axi_rvalid <= 1'b1;
                s_axi_rresp <= 2'b00;
                s_axi_rdata <= axi_read(araddr_hold);
                ar_pending <= 1'b0;
            end else if (s_axi_rvalid && s_axi_rready) begin
                s_axi_rvalid <= 1'b0;
            end
        end
    end

    // Core executor
    always @(posedge s_axi_aclk) begin
        if (!s_axi_aresetn) begin
            exec_busy <= 1'b0;
            exec_done <= 1'b0;
            exec_fault <= 1'b0;
            exec_overrun <= 1'b0;
            exec_apply_while_busy <= 1'b0;
            exec_invalid_slice <= 1'b0;
            exec_start_pending <= 1'b0;
            exec_flush_pending <= 1'b0;
            invalid_slice_pending <= 1'b0;
            invalid_slice_seq_pending <= 32'd0;
            reset_status <= (32'd1 << RST_AXI_SEEN) | (32'd1 << RST_EXEC_SEEN);
            reset_release_seen <= 1'b0;
            reset_count <= 32'd0;
            last_reset_cause <= RST_CAUSE_AXI_RESET;
            last_reset_seq <= 32'd0;

            event_cnt <= 32'd0;
            tick_ctr <= 32'd0;
            sched_denom <= 32'd0;
            slice_ticks_active <= 32'd0;
            global_cfg_active <= 32'd1;
            slice_seq_active <= 32'd0;
            slice_seq_done <= 32'd0;
            start_barrier_wait <= 1'b0;
            release_first_steps_q <= 1'b0;

            for (rst_i = 0; rst_i < N_AXES; rst_i = rst_i + 1) begin
                control_active[rst_i] <= 32'd0;
                delta_steps_active[rst_i] <= 32'd0;
                step_width_active[rst_i] <= 32'd0;
                step_space_active[rst_i] <= 32'd0;
                dir_setup_active[rst_i] <= 32'd0;
                dir_hold_active[rst_i] <= 32'd0;

                steps_abs[rst_i] <= 32'd0;
                steps_remain[rst_i] <= 32'd0;
                dda_num[rst_i] <= 32'd0;
                accum[rst_i] <= 32'd0;
                pending_steps[rst_i] <= 32'd0;
                emitted_steps[rst_i] <= 32'd0;

                target_dir[rst_i] <= 1'b0;
                eff_dir[rst_i] <= 1'b0;
                width_cnt[rst_i] <= 32'd0;
                space_cnt[rst_i] <= 32'd0;
                dsetup_cnt[rst_i] <= 32'd0;
                dhold_cnt[rst_i] <= 32'd0;
                step_q[rst_i] <= 1'b0;
                step_q_prev[rst_i] <= 1'b0;
                axis_active_in_slice[rst_i] <= 1'b0;
                axis_dir_changed[rst_i] <= 1'b0;
                first_step_armed[rst_i] <= 1'b0;
                issue_step_pipe[rst_i] <= 1'b0;
                pulse_rise_count[rst_i] <= 32'd0;
                pulse_fall_count[rst_i] <= 32'd0;
            end
        end else begin
            if (!reset_release_seen) begin
                reset_release_seen <= 1'b1;
                reset_count <= reset_count + 1'b1;
                reset_status[RST_AXI_SEEN] <= 1'b1;
                reset_status[RST_EXEC_SEEN] <= 1'b1;
                reset_status[RST_INIT_DONE] <= 1'b1;
                reset_status[RST_RELEASED] <= 1'b1;
                reset_status[RST_APPLY_SINCE_RST] <= 1'b0;
                last_reset_cause <= RST_CAUSE_RESET_RELEASE;
                last_reset_seq <= slice_seq_active;
            end

            for (chk_i = 0; chk_i < N_AXES; chk_i = chk_i + 1) begin
                if (clear_count_pulse[chk_i]) begin
                    pulse_rise_count[chk_i] <= 32'd0;
                    pulse_fall_count[chk_i] <= 32'd0;
                end else if (step_q_prev[chk_i] && !step_q[chk_i]) begin
                    pulse_fall_count[chk_i] <= pulse_fall_count[chk_i] + 1'b1;
                end
                step_q_prev[chk_i] <= step_q[chk_i];
            end

            if (status_clear_pulse) begin
                exec_done <= 1'b0;
                exec_fault <= 1'b0;
                exec_overrun <= 1'b0;
                exec_apply_while_busy <= 1'b0;
                exec_invalid_slice <= 1'b0;
            end

            if (invalid_slice_pending) begin
                invalid_slice_pending <= 1'b0;
                slice_seq_done <= invalid_slice_seq_pending;
            end

            if (exec_flush_pending) begin
                exec_start_pending <= 1'b0;
                exec_flush_pending <= 1'b0;
                for (chk_i = 0; chk_i < N_AXES; chk_i = chk_i + 1) begin
                    steps_remain[chk_i] <= 32'd0;
                    pending_steps[chk_i] <= 32'd0;
                    width_cnt[chk_i] <= 32'd0;
                    space_cnt[chk_i] <= 32'd0;
                    dsetup_cnt[chk_i] <= 32'd0;
                    dhold_cnt[chk_i] <= 32'd0;
                    step_q[chk_i] <= 1'b0;
                    first_step_armed[chk_i] <= 1'b0;
                    issue_step_pipe[chk_i] <= 1'b0;
                end
            end

            if (sync_request) begin
                exec_done <= 1'b0;
                exec_flush_pending <= 1'b0;
                release_first_steps_q <= 1'b0;
                invalid_slice_pending <= 1'b0;

                if (exec_busy || exec_start_pending) begin
                    exec_fault <= 1'b1;
                    exec_apply_while_busy <= 1'b1;
                    // Record the apply sequence that caused this fault.
                    slice_seq_done <= slice_seq_shadow;
                    last_reset_cause <= RST_CAUSE_APPLY_WHILE_BUSY;
                    last_reset_seq <= slice_seq_shadow;
                end else if (exec_fault) begin
                    // Sticky-fault gate: GLOBAL_APPLY is ignored until SW writes EXEC_STATUS[0] to clear status.
                end else begin
                    reset_status[RST_APPLY_SINCE_RST] <= 1'b1;
                    slice_ticks_active <= slice_ticks_shadow;
                    global_cfg_active <= global_cfg_shadow;
                    slice_seq_active <= slice_seq_shadow;
                    invalid_slice_seq_pending <= slice_seq_shadow;
                    tick_ctr <= 32'd0;
                    event_cnt <= 32'd0;
                    sched_denom <= (global_cfg_shadow[CFG_FIRST_STEP_SYNC] && (slice_ticks_shadow != 0)) ?
                                   (slice_ticks_shadow - 1) : slice_ticks_shadow;
                    start_barrier_wait <= 1'b0;
                    for (exec_i = 0; exec_i < N_AXES; exec_i = exec_i + 1) begin
                        control_active[exec_i] <= control_reg[exec_i];
                        delta_steps_active[exec_i] <= delta_steps_shadow[exec_i];
                        step_width_active[exec_i] <= step_width[exec_i];
                        step_space_active[exec_i] <= step_space[exec_i];
                        dir_setup_active[exec_i] <= dir_setup[exec_i];
                        dir_hold_active[exec_i] <= dir_hold[exec_i];
                    end
                    exec_start_pending <= 1'b1;
                end
            end else if (exec_start_pending) begin
                legal_check_fail = (slice_ticks_active == 0);
                any_axis_with_steps = 1'b0;
                exec_start_pending <= 1'b0;

                for (exec_i = 0; exec_i < N_AXES; exec_i = exec_i + 1) begin
                    delta_masked_init = control_active[exec_i][0] ? delta_steps_active[exec_i] : 32'd0;
                    if (delta_masked_init[31])
                        abs_steps_init = (~delta_masked_init) + 32'd1;
                    else
                        abs_steps_init = delta_masked_init;
                    if (abs_steps_init != 0)
                        abs_steps_init_minus1 = abs_steps_init - 32'd1;
                    else
                        abs_steps_init_minus1 = 32'd0;
                    target_dir_init = delta_masked_init[31] ^ control_active[exec_i][1];

                    if (abs_steps_init != 0) begin
                        target_dir[exec_i] <= target_dir_init;
                        axis_dir_changed[exec_i] <= (eff_dir[exec_i] != target_dir_init);
                        if (eff_dir[exec_i] != target_dir_init) begin
                            eff_dir[exec_i] <= target_dir_init;
                            dsetup_cnt[exec_i] <= dir_setup_active[exec_i];
                        end else begin
                            dsetup_cnt[exec_i] <= 32'd0;
                        end
                    end else begin
                        axis_dir_changed[exec_i] <= 1'b0;
                        dsetup_cnt[exec_i] <= 32'd0;
                    end

                    steps_abs[exec_i] <= abs_steps_init;
                    accum[exec_i] <= 32'd0;
                    emitted_steps[exec_i] <= 32'd0;
                    width_cnt[exec_i] <= 32'd0;
                    space_cnt[exec_i] <= 32'd0;
                    dhold_cnt[exec_i] <= 32'd0;
                    step_q[exec_i] <= 1'b0;
                    pending_steps[exec_i] <= 32'd0;
                    issue_step_pipe[exec_i] <= 1'b0;

                    axis_active_in_slice[exec_i] <= (abs_steps_init != 0);

                    if (global_cfg_active[CFG_FIRST_STEP_SYNC] && (abs_steps_init != 0)) begin
                        first_step_armed[exec_i] <= 1'b1;
                        steps_remain[exec_i] <= abs_steps_init_minus1;
                        dda_num[exec_i] <= abs_steps_init_minus1;
                    end else begin
                        first_step_armed[exec_i] <= 1'b0;
                        steps_remain[exec_i] <= abs_steps_init;
                        dda_num[exec_i] <= abs_steps_init;
                    end

                    if (abs_steps_init != 0)
                        any_axis_with_steps = 1'b1;
                end

                if (legal_check_fail) begin
                    exec_busy <= 1'b0;
                    exec_fault <= 1'b1;
                    exec_invalid_slice <= 1'b1;
                    invalid_slice_pending <= 1'b1;
                    last_reset_cause <= RST_CAUSE_INVALID_SLICE;
                    start_barrier_wait <= 1'b0;
                    release_first_steps_q <= 1'b0;
                    exec_flush_pending <= 1'b1;
                end else begin
                    exec_busy <= 1'b1;
                    start_barrier_wait <= global_cfg_active[CFG_FIRST_STEP_SYNC] && any_axis_with_steps;
                end
            end else if (exec_busy) begin
                release_first_steps = release_first_steps_q;
                release_first_steps_q <= 1'b0;
                if (start_barrier_wait) begin
                    barrier_ready = 1'b1;
                    for (chk_i = 0; chk_i < N_AXES; chk_i = chk_i + 1) begin
                        // Next-state check: dsetup_cnt==1 decrements to 0 in this cycle and should not stall release.
                        if (first_step_armed[chk_i] && (dsetup_cnt[chk_i] > 32'd1))
                            barrier_ready = 1'b0;
                    end
                    if (barrier_ready) begin
                        release_first_steps_q <= 1'b1;
                        start_barrier_wait <= 1'b0;
                    end
                end

                for (exec_i = 0; exec_i < N_AXES; exec_i = exec_i + 1) begin
                    issue_step = 1'b0;

                    // Scheduler: deterministic DDA distribution inside one slice.
                    if (!start_barrier_wait && (tick_ctr < slice_ticks_active)) begin
                        if (!(global_cfg_active[CFG_FIRST_STEP_SYNC] && (tick_ctr == 0))) begin
                            denom_ticks = global_cfg_active[CFG_FIRST_STEP_SYNC] ? sched_denom : slice_ticks_active;
                            if ((denom_ticks != 0) && (steps_remain[exec_i] != 0) && (dda_num[exec_i] != 0)) begin
                                accum_sum = {1'b0, accum[exec_i]} + {1'b0, dda_num[exec_i]};
                                if (accum_sum >= {1'b0, denom_ticks}) begin
                                    accum[exec_i] <= accum_sum[31:0] - denom_ticks;
                                    steps_remain[exec_i] <= steps_remain[exec_i] - 1'b1;
                                    issue_step = 1'b1;
                                end else begin
                                    accum[exec_i] <= accum_sum[31:0];
                                end
                            end
                        end
                    end

                    // One-shot pulse shaper (kept from v2 concept, source is pending queue).
                    width_next = width_cnt[exec_i];
                    space_next = space_cnt[exec_i];
                    dsetup_next = dsetup_cnt[exec_i];

                    if (width_next != 0)
                        width_next = width_next - 1'b1;
                    if (space_next != 0)
                        space_next = space_next - 1'b1;
                    if (dsetup_next != 0)
                        dsetup_next = dsetup_next - 1'b1;

                    if (width_cnt[exec_i] == 1)
                        step_q[exec_i] <= 1'b0;

                    pending_release_step = release_first_steps && first_step_armed[exec_i];
                    pending_issue_step = issue_step_pipe[exec_i];
                    pending_enqueue_count = {1'b0, pending_release_step} + {1'b0, pending_issue_step};
                    pending_next = pending_steps[exec_i];

                    pulse_width_reload = (step_width_active[exec_i] == 0) ? 32'd1 : step_width_active[exec_i];
                    pulse_gap_sum = {1'b0, pulse_width_reload} + {1'b0, step_space_active[exec_i]};
                    pulse_gap_reload = pulse_gap_sum[32] ? 32'hFFFF_FFFF : pulse_gap_sum[31:0];

                    pulse_ready = (width_cnt[exec_i] == 0) &&
                                  (space_cnt[exec_i] == 0) &&
                                  ((dsetup_cnt[exec_i] == 0) ||
                                   (release_first_steps && first_step_armed[exec_i] && (dsetup_cnt[exec_i] == 32'd1))) &&
                                  control_active[exec_i][0];
                    pulse_launch = (pending_steps[exec_i] != 0) && pulse_ready;

                    if (pulse_launch) begin
                        if (!step_q[exec_i])
                            pulse_rise_count[exec_i] <= pulse_rise_count[exec_i] + 1'b1;
                        step_q[exec_i] <= 1'b1;
                        width_next = pulse_width_reload;
                        space_next = pulse_gap_reload;
                        emitted_steps[exec_i] <= emitted_steps[exec_i] + 1'b1;
                    end

                    // Queue update uses one net-delta operation to keep carry depth shallow.
                    if (pulse_launch) begin
                        case (pending_enqueue_count)
                            2'd0: pending_next = pending_steps[exec_i] - 1'b1;
                            2'd1: pending_next = pending_steps[exec_i];
                            default: begin // 2'd2
                                if (pending_steps[exec_i] != 32'hFFFF_FFFF)
                                    pending_next = pending_steps[exec_i] + 1'b1;
                                else
                                    pending_next = 32'hFFFF_FFFF;
                            end
                        endcase
                    end else begin
                        case (pending_enqueue_count)
                            2'd0: pending_next = pending_steps[exec_i];
                            2'd1: begin
                                if (pending_steps[exec_i] != 32'hFFFF_FFFF)
                                    pending_next = pending_steps[exec_i] + 1'b1;
                                else
                                    pending_next = 32'hFFFF_FFFF;
                            end
                            default: begin // 2'd2
                                if (pending_steps[exec_i] >= 32'hFFFF_FFFE)
                                    pending_next = 32'hFFFF_FFFF;
                                else
                                    pending_next = pending_steps[exec_i] + 32'd2;
                            end
                        endcase
                    end

                    width_cnt[exec_i] <= width_next;
                    space_cnt[exec_i] <= space_next;
                    dsetup_cnt[exec_i] <= dsetup_next;

                    if (release_first_steps && first_step_armed[exec_i])
                        first_step_armed[exec_i] <= 1'b0;
                    issue_step_pipe[exec_i] <= issue_step;
                    pending_steps[exec_i] <= pending_next;
                end

                if (tick_ctr < slice_ticks_active) begin
                    tick_ctr <= tick_ctr + 1'b1;
                    event_cnt <= tick_ctr + 1'b1;
                end

                all_counts_done = 1'b1;
                all_quiet = 1'b1;
                tail_backlog_overrun = 1'b0;
                for (chk_i = 0; chk_i < N_AXES; chk_i = chk_i + 1) begin
                    if ((steps_remain[chk_i] != 0) || first_step_armed[chk_i])
                        all_counts_done = 1'b0;
                    if (pending_steps[chk_i] > 32'd1)
                        tail_backlog_overrun = 1'b1;
                    if (issue_step_pipe[chk_i] ||
                        (pending_steps[chk_i] != 0) ||
                        step_q[chk_i] ||
                        (width_cnt[chk_i] != 0) ||
                        (space_cnt[chk_i] != 0) ||
                        (dsetup_cnt[chk_i] != 0))
                        all_quiet = 1'b0;
                end

                if (tick_ctr >= slice_ticks_active) begin
                    // Slice budget exhausted:
                    // 1) remaining scheduled steps means true overrun (cannot fit in slice).
                    // 2) no steps left but shaping counters active => keep draining without fault.
                    if (!all_counts_done || tail_backlog_overrun) begin
                        exec_busy <= 1'b0;
                        exec_fault <= 1'b1;
                        exec_overrun <= 1'b1;
                        slice_seq_done <= slice_seq_active;
                        last_reset_cause <= RST_CAUSE_OVERRUN;
                        start_barrier_wait <= 1'b0;
                        release_first_steps_q <= 1'b0;
                        exec_flush_pending <= 1'b1;
                        for (chk_i = 0; chk_i < N_AXES; chk_i = chk_i + 1) begin
                            step_q[chk_i] <= 1'b0;
                        end
                    end else if (all_quiet) begin
                        exec_busy <= 1'b0;
                        exec_done <= 1'b1;
                        slice_seq_done <= slice_seq_active;
                        start_barrier_wait <= 1'b0;
                        release_first_steps_q <= 1'b0;
                    end
                end
            end
        end
    end

    // Encoder logic
    genvar i;
    generate
        for (i = 0; i < N_AXES; i = i + 1) begin : AX
            reg qa0, qb0, qa1, qb1;
            reg za0, za1;
            (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg a_meta, b_meta, z_meta;
            (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg a_sync, b_sync, z_sync;
            reg a_filt, b_filt, z_filt;
            reg a_sync_d, b_sync_d, z_sync_d;
            reg a_filt_d, b_filt_d, z_filt_d;
            reg index_zero_armed;
            reg index_enable_d;
            reg loop_step_prev;
            wire raw_edge_any;
            wire filt_edge_any;
            wire z_rise = za0 & ~za1;
            assign raw_edge_any = (a_sync ^ a_sync_d) | (b_sync ^ b_sync_d) | (z_sync ^ z_sync_d);
            assign filt_edge_any = (a_filt ^ a_filt_d) | (b_filt ^ b_filt_d) | (z_filt ^ z_filt_d);

            always @(posedge s_axi_aclk) begin
                if (!s_axi_aresetn) begin
                    qa0 <= 1'b0; qb0 <= 1'b0; qa1 <= 1'b0; qb1 <= 1'b0;
                    za0 <= 1'b0; za1 <= 1'b0;
                    a_meta <= 1'b0; b_meta <= 1'b0; z_meta <= 1'b0;
                    a_sync <= 1'b0; b_sync <= 1'b0; z_sync <= 1'b0;
                    a_filt <= 1'b0; b_filt <= 1'b0; z_filt <= 1'b0;
                    a_sync_d <= 1'b0; b_sync_d <= 1'b0; z_sync_d <= 1'b0;
                    a_filt_d <= 1'b0; b_filt_d <= 1'b0; z_filt_d <= 1'b0;
                    index_zero_armed <= 1'b0;
                    index_enable_d <= 1'b0;
                    loop_step_prev <= 1'b0;
                    enc_count[i] <= 32'sd0;
                    enc_z_seen[i] <= 1'b0;
                    enc_raw_edge_count[i] <= 32'd0;
                    enc_filt_edge_count[i] <= 32'd0;
                    enc_glitch_count[i] <= 32'd0;
                end else begin
                    a_meta <= enc_a_i[i];
                    b_meta <= enc_b_i[i];
                    z_meta <= enc_z_i[i];
                    a_sync <= a_meta;
                    b_sync <= b_meta;
                    z_sync <= z_meta;

                    if (a_sync == a_meta) a_filt <= a_sync;
                    if (b_sync == b_meta) b_filt <= b_sync;
                    if (z_sync == z_meta) z_filt <= z_sync;

                    qa0 <= a_filt; qb0 <= b_filt; qa1 <= qa0; qb1 <= qb0;
                    za0 <= z_filt; za1 <= za0;
                    a_sync_d <= a_sync; b_sync_d <= b_sync; z_sync_d <= z_sync;
                    a_filt_d <= a_filt; b_filt_d <= b_filt; z_filt_d <= z_filt;

                    index_enable_d <= control_active[i][3];
                    if (!control_active[i][3])
                        index_zero_armed <= 1'b0;
                    else if (!index_enable_d)
                        index_zero_armed <= 1'b1;

                    if (global_cfg_active[CFG_ENC_LOOPBACK]) begin
                        if (clear_count_pulse[i]) begin
                            enc_count[i] <= 32'sd0;
                            enc_z_seen[i] <= 1'b0;
                            enc_raw_edge_count[i] <= 32'd0;
                            enc_filt_edge_count[i] <= 32'd0;
                            enc_glitch_count[i] <= 32'd0;
                            index_zero_armed <= 1'b0;
                        end else begin
                            if (clear_z_seen_pulse[i])
                                enc_z_seen[i] <= 1'b0;
                            if (!loop_step_prev && step_q[i]) begin
                                if (eff_dir[i])
                                    enc_count[i] <= enc_count[i] - 1'b1;
                                else
                                    enc_count[i] <= enc_count[i] + 1'b1;
                                enc_raw_edge_count[i] <= enc_raw_edge_count[i] + 1'b1;
                                enc_filt_edge_count[i] <= enc_filt_edge_count[i] + 1'b1;
                            end
                        end
                        loop_step_prev <= step_q[i];
                    end else if (clear_count_pulse[i]) begin
                        enc_count[i] <= 32'sd0;
                        enc_z_seen[i] <= 1'b0;
                        enc_raw_edge_count[i] <= 32'd0;
                        enc_filt_edge_count[i] <= 32'd0;
                        enc_glitch_count[i] <= 32'd0;
                        index_zero_armed <= 1'b0;
                        loop_step_prev <= step_q[i];
                    end else begin
                        if (raw_edge_any)
                            enc_raw_edge_count[i] <= enc_raw_edge_count[i] + 1'b1;
                        if (filt_edge_any)
                            enc_filt_edge_count[i] <= enc_filt_edge_count[i] + 1'b1;
                        if (raw_edge_any && !filt_edge_any)
                            enc_glitch_count[i] <= enc_glitch_count[i] + 1'b1;

                        if (clear_z_seen_pulse[i]) begin
                            enc_z_seen[i] <= 1'b0;
                        end else if (index_zero_armed && z_rise) begin
                            enc_count[i] <= 32'sd0;
                            enc_z_seen[i] <= 1'b1;
                            index_zero_armed <= 1'b0;
                        end else begin
                            case ({qa1, qb1, qa0, qb0})
                                4'b0001, 4'b0111, 4'b1110, 4'b1000: enc_count[i] <= enc_count[i] + 1;
                                4'b0010, 4'b0100, 4'b1101, 4'b1011: enc_count[i] <= enc_count[i] - 1;
                                default: enc_count[i] <= enc_count[i];
                            endcase
                            if (z_rise)
                                enc_z_seen[i] <= 1'b1;
                        end
                        loop_step_prev <= step_q[i];
                    end
                end
            end

            assign step_o[i] = step_q[i];
            assign dir_o[i] = eff_dir[i];
        end
    endgenerate

    assign irq_o = exec_fault;

    function [31:0] axi_read;
        input [C_S_AXI_ADDR_WIDTH-1:0] a;
        reg [31:0] r;
        integer ax;
        begin
            r = 32'h0;
            case (a[11:2])
                ADDR_ID:          r = REG_ID;
                ADDR_VERSION:     r = REG_VERSION;
                ADDR_CLK_FREQ:    r = CLK_FREQ_HZ;
                ADDR_N_AXES:      r = N_AXES;
                ADDR_EXEC_STATUS: r = {26'd0,
                                       exec_invalid_slice,
                                       exec_apply_while_busy,
                                       exec_overrun,
                                       exec_fault,
                                       exec_done,
                                       exec_busy};
                ADDR_GLOBAL_APPLY: r = 32'd0;
                ADDR_SLICE_TICKS: r = slice_ticks_shadow;
                ADDR_EVENT_CNT:   r = event_cnt;
                ADDR_GLOBAL_CFG:  r = global_cfg_shadow;
                ADDR_SLICE_SEQ_SHADOW: r = slice_seq_shadow;
                ADDR_SLICE_SEQ_ACTIVE: r = slice_seq_active;
                ADDR_SLICE_SEQ_DONE:   r = slice_seq_done;
                ADDR_EXEC_LIVE_STATUS: r = {26'd0,
                                            exec_apply_while_busy,
                                            exec_invalid_slice,
                                            exec_overrun,
                                            exec_fault,
                                            start_barrier_wait,
                                            exec_busy};
                ADDR_RESET_STATUS: r = reset_status;
                ADDR_RESET_COUNT:  r = reset_count;
                ADDR_LAST_RESET_CAUSE: r = last_reset_cause;
                ADDR_LAST_RESET_SEQ:   r = last_reset_seq;
                default: if ((a >= ADDR_AXIS_DBG_BASE) && (a < (ADDR_AXIS_DBG_BASE + (N_AXES * 12'd8)))) begin
                    ax = (a - ADDR_AXIS_DBG_BASE) >> 3;
                    if (ax < N_AXES) begin
                        if (a[2])
                            r = steps_remain[ax];
                        else
                            r = emitted_steps[ax];
                    end
                end else if ((a >= ADDR_AXIS_LIVE_BASE) && (a < (ADDR_AXIS_LIVE_BASE + (N_AXES * 12'd32)))) begin
                    ax = (a - ADDR_AXIS_LIVE_BASE) >> 5;
                    if (ax < N_AXES) begin
                        case (a[4:2])
                            3'h0: r = {24'd0,
                                       axis_active_in_slice[ax],
                                       (first_step_armed[ax] && start_barrier_wait),
                                       (dsetup_cnt[ax] != 0),
                                       (space_cnt[ax] != 0),
                                       (width_cnt[ax] != 0),
                                       ((pending_steps[ax] != 0) || issue_step_pipe[ax]),
                                       eff_dir[ax],
                                       step_q[ax]};
                            3'h1: r = pulse_rise_count[ax];
                            3'h2: r = pulse_fall_count[ax];
                            3'h3: r = enc_raw_edge_count[ax];
                            3'h4: r = enc_filt_edge_count[ax];
                            3'h5: r = enc_glitch_count[ax];
                            default: r = 32'h0;
                        endcase
                    end
                end else if (a >= 12'h100) begin
                    ax = (a - 12'h100) >> 5;
                    if (ax < N_AXES) begin
                        case (a[4:2])
                            3'h0: r = control_reg[ax];
                            3'h1: r = delta_steps_shadow[ax];
                            3'h2: r = step_width[ax];
                            3'h3: r = step_space[ax];
                            3'h4: r = enc_count[ax];
                            // bit0: enable_cfg
                            // bit1: step_out
                            // bit2: width_active
                            // bit3: space_wait
                            // bit4: dir_setup_wait
                            // bit5: axis_busy
                            // bit6: enc_z_seen_latched
                            // bit7: slice_busy
                            // bit8: dir_out
                            // bit9: axis_active_in_slice
                            // bit10: axis_dir_changed
                            // bit11: axis_first_step_pending
                            3'h5: r = {20'd0,
                                       first_step_armed[ax],
                                       axis_dir_changed[ax],
                                       axis_active_in_slice[ax],
                                       eff_dir[ax],
                                       exec_busy,
                                       enc_z_seen[ax],
                                       ((steps_remain[ax] != 0) || issue_step_pipe[ax] ||
                                        (pending_steps[ax] != 0) || first_step_armed[ax] ||
                                        (width_cnt[ax] != 0) || (space_cnt[ax] != 0) ||
                                        (dsetup_cnt[ax] != 0) || (dhold_cnt[ax] != 0) || step_q[ax]),
                                       (dsetup_cnt[ax] != 0),
                                       (space_cnt[ax] != 0),
                                       (width_cnt[ax] != 0),
                                       step_q[ax],
                                       control_active[ax][0]};
                            3'h6: r = dir_setup[ax];
                            3'h7: r = dir_hold[ax];
                            default: r = 32'h0;
                        endcase
                    end
                end
            endcase
            axi_read = r;
        end
    endfunction

    task axi_write;
        input [C_S_AXI_ADDR_WIDTH-1:0] a;
        input [31:0] wd;
        input [3:0] wstrb;
        integer ax;
        reg [31:0] mask;
        reg [31:0] nv;
        begin
            mask = {{8{wstrb[3]}}, {8{wstrb[2]}}, {8{wstrb[1]}}, {8{wstrb[0]}}};

            if (a >= 12'h100) begin
                ax = (a - 12'h100) >> 5;
                if (ax < N_AXES) begin
                    case (a[4:2])
                        3'h0: begin
                            if (|mask) begin
                                nv = (control_reg[ax] & ~mask) | (wd & mask);
                                if (mask[2] && wd[2]) clear_count_pulse[ax] <= 1'b1;
                                if (mask[4] && wd[4]) clear_z_seen_pulse[ax] <= 1'b1;
                                nv[2] = 1'b0;
                                nv[4] = 1'b0;
                                control_reg[ax] <= nv;
                            end
                        end
                        3'h1: if (|mask) delta_steps_shadow[ax] <= (delta_steps_shadow[ax] & ~mask) | (wd & mask);
                        3'h2: if (|mask) step_width[ax] <= (step_width[ax] & ~mask) | (wd & mask);
                        3'h3: if (|mask) step_space[ax] <= (step_space[ax] & ~mask) | (wd & mask);
                        3'h6: if (|mask) dir_setup[ax] <= (dir_setup[ax] & ~mask) | (wd & mask);
                        3'h7: if (|mask) dir_hold[ax] <= (dir_hold[ax] & ~mask) | (wd & mask);
                        default: ;
                    endcase
                end
            end else if (a[11:10] == 0) begin
                if (a[11:2] == ADDR_EXEC_STATUS && mask[0] && wd[0])
                    status_clear_pulse <= 1'b1;
                if (a[11:2] == ADDR_GLOBAL_APPLY && mask[0] && wd[0]) begin
                    // Core executor is the single writer of fault/status flags.
                    // AXI write path only raises request pulse; core decides
                    // idle accept / busy fault / sticky-fault reject.
                    if (!exec_fault)
                        sync_request <= 1'b1;
                end
                if (a[11:2] == ADDR_SLICE_TICKS && |mask)
                    slice_ticks_shadow <= (slice_ticks_shadow & ~mask) | (wd & mask);
                if (a[11:2] == ADDR_GLOBAL_CFG && |mask)
                    global_cfg_shadow <= (global_cfg_shadow & ~mask) | (wd & mask);
                if (a[11:2] == ADDR_SLICE_SEQ_SHADOW && |mask)
                    slice_seq_shadow <= (slice_seq_shadow & ~mask) | (wd & mask);
            end
        end
    endtask

endmodule
