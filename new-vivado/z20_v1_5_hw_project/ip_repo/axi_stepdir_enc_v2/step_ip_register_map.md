# axi_stepdir_enc_v2 Register Map (Slice Executor v3.5.0)

状态：`已修复（与当前 v3.5.0 RTL 对齐）`

Current Z20 v1.5 AXI base: `0x43CB0000`, range `64K`, 32-bit word access.

All register addresses below are byte offsets from the `step_ip/s_axi` block base. For other integrations, regenerate or recheck the base address from the current XSA/BD address map before writing Linux/LinuxCNC code.

## Global Registers

| Offset | Name | R/W | Description |
|---|---|---|---|
| `0x000` | `ID` | R | Constant `0x53544550` (`"STEP"`) |
| `0x004` | `VERSION` | R | RTL version (`0x00030500`) |
| `0x008` | `CLK_FREQ` | R | FPGA clock frequency in Hz |
| `0x00C` | `N_AXES` | R | Supported axis count |
| `0x010` | `EXEC_STATUS` | R/W | Sticky execution status (only write bit0=1 is defined; clears all sticky flags) |
| `0x014` | `GLOBAL_APPLY` | W | Write bit0=1 to latch shadow config and start one slice |
| `0x018` | `SLICE_TICKS` | R/W | Slice duration in FPGA clocks |
| `0x01C` | `EVENT_CNT` | R | Current slice event counter |
| `0x020` | `GLOBAL_CFG` | R/W | Global config bits |
| `0x024` | `SLICE_SEQ_SHADOW` | R/W | Software-provided sequence number for next apply |
| `0x028` | `SLICE_SEQ_ACTIVE` | R | Sequence latched on current/last started slice |
| `0x02C` | `SLICE_SEQ_DONE` | R | Sequence for last completed/faulted slice, including apply-while-busy fault source |
| `0x030` | `EXEC_LIVE_STATUS` | R | Live execution status (`busy/barrier/fault class`) |
| `0x034` | `RESET_STATUS` | R | Reset/initialization telemetry |
| `0x038` | `RESET_COUNT` | R | Reset release counter |
| `0x03C` | `LAST_RESET_CAUSE` | R | Last reset/fault cause code |
| `0x040` | `LAST_RESET_SEQ` | R | Slice sequence associated with last reset/fault cause |

### `EXEC_STATUS` write semantics

- This register is **not** per-bit W1C.
- Only `EXEC_STATUS[0]=1` is defined as a command and clears all sticky status bits (`done/fault/overrun/apply_while_busy/invalid_slice`).
- Writes to other bits are ignored.

### `EXEC_STATUS` bits

- `bit0`: `busy`
- `bit1`: `done` (sticky)
- `bit2`: `fault` (sticky)
- `bit3`: `overrun` (sticky)
- `bit4`: `apply_while_busy` (sticky)
- `bit5`: `invalid_slice` (sticky)

### `GLOBAL_CFG` bits

- `bit0`: `first_step_sync` (enable global first-step barrier)
- `bit1`: `enc_loopback_debug` (debug-only: step/dir synthetic encoder source)

### `EXEC_LIVE_STATUS` bits

- `bit0`: `busy`
- `bit1`: `start_barrier_active`
- `bit2`: `fault`
- `bit3`: `overrun`
- `bit4`: `invalid_slice`
- `bit5`: `apply_while_busy`

### `RESET_STATUS` bits

- `bit0`: `axi_reset_seen`
- `bit1`: `exec_reset_seen`
- `bit2`: `init_done`
- `bit3`: `reset_released`
- `bit4`: `apply_since_reset`

### `LAST_RESET_CAUSE` values

- `0`: none / uninitialized
- `1`: AXI reset asserted
- `2`: reset release observed
- `3`: apply-while-busy fault
- `4`: invalid-slice fault
- `5`: overrun fault

### `LAST_RESET_SEQ` semantics

- Cause `3/4`: records `SLICE_SEQ_SHADOW`
- Cause `5`: records `SLICE_SEQ_ACTIVE`
- Reset-related causes (`1/2`): records active sequence snapshot at event time

### `invalid_slice` precheck semantics

- `invalid_slice` is a **fast precheck**, not a complete admission check.
- It only blocks slices that are clearly impossible under pulse-shaping launch constraints (step opportunities + optional `dir_setup`).
- It intentionally does **not** fully prove DDA distribution feasibility for all step patterns.
- Therefore, some slices can still pass precheck and later fail with runtime `overrun`.

## Per-Axis Registers

Per-axis base: `0x100 + axis * 0x20`, axis in `[0, N_AXES-1]`.

| Offset (axis base +) | Name | R/W | Description |
|---|---|---|---|
| `0x00` | `CONTROL` | R/W | Control bits (enable/dir invert/index enable, pulse commands) |
| `0x04` | `DELTA_STEPS` | R/W | Signed steps for this slice |
| `0x08` | `STEP_WIDTH` | R/W | Pulse width in clocks (`0` treated as `1`) |
| `0x0C` | `STEP_SPACE` | R/W | Minimum extra gap in clocks |
| `0x10` | `ENC_COUNT` | R | Quadrature encoder count |
| `0x14` | `STATUS` | R | Per-axis runtime status bits |
| `0x18` | `DIR_SETUP` | R/W | Direction setup time in clocks |
| `0x1C` | `DIR_HOLD` | R/W | Direction hold time in clocks |

### Axis `STATUS` bits

- `bit0`: `enable_cfg`
- `bit1`: `step_out`
- `bit2`: `width_active`
- `bit3`: `space_wait`
- `bit4`: `dir_setup_wait`
- `bit5`: `axis_busy`
- `bit6`: `enc_z_seen_latched`
- `bit7`: `slice_busy`
- `bit8`: `dir_out`
- `bit9`: `axis_active_in_slice`
- `bit10`: `axis_dir_changed`
- `bit11`: `axis_first_step_pending`

## Axis Debug Counters

Base: `0x300 + axis * 0x08`.

| Offset | Name | R/W | Description |
|---|---|---|---|
| `+0x00` | `EMITTED_STEPS` | R | Steps already emitted in current/last slice |
| `+0x04` | `STEPS_REMAIN` | R | Steps still waiting for scheduler/emission |

## Axis Live Telemetry (P1 Debug)

Base: `0x380 + axis * 0x20`.

| Offset | Name | R/W | Description |
|---|---|---|---|
| `+0x00` | `AXIS_LIVE_STATUS` | R | Live per-axis gate status |
| `+0x04` | `PULSE_RISE_COUNT` | R | Observed `step_o` rising edges |
| `+0x08` | `PULSE_FALL_COUNT` | R | Observed `step_o` falling edges |
| `+0x0C` | `ENC_RAW_EDGE_COUNT` | R | Raw synchronized encoder edge count |
| `+0x10` | `ENC_FILT_EDGE_COUNT` | R | Filtered encoder edge count |
| `+0x14` | `ENC_GLITCH_COUNT` | R | Raw edge with no same-cycle filtered edge |

### `AXIS_LIVE_STATUS` bits

- `bit0`: `step_out_live`
- `bit1`: `dir_out_live`
- `bit2`: `pending_step`
- `bit3`: `width_active`
- `bit4`: `space_active`
- `bit5`: `dir_setup_wait`
- `bit6`: `first_step_barrier_wait`
- `bit7`: `axis_active_in_slice`
