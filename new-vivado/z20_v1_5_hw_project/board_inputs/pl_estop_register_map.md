# PL E-Stop Register Map

This file is the board-software-facing register map for the local Vivado PL E-stop AXI-Lite block. It is a hardware handoff aid only; it is not board safety proof.

## Block Identity

- RTL source: `rtl/pl_estop_axi_lite.v`
- AXI block: `pl_estop/S_AXI`
- Base address: `0x41260000`
- Range: `64K`
- AXI address width: `C_S_AXI_ADDR_WIDTH=6`
- IRQ route: `pl_estop/estop_irq` -> `xlconcat_0/In14`
- Current closure state: `local_verified_only`
- Current readiness state: `pl_estop_readiness=not_ready`
- Timing parameter check: `pl_estop_timing_params=ok`

## Timing And Axis Constants

- `CLK_HZ=100000000`
- `DEBOUNCE_MS=10`, so `debounce_cycles=1000000`
- `BRAKE_LEAD_US=50`, so `brake_cycles=5000`
- `AXIS_COUNT=8`
- `Z_AXIS_INDEX=2`
- BD clock net: `processing_system7_0/FCLK_CLK0`

## Register Table

| Offset | Name | Access | Reset/read value | Description |
| --- | --- | --- | --- | --- |
| `0x00` | `MAGIC` | RO | `0x45535450` | ASCII `ESTP`. |
| `0x04` | `VERSION` | RO | `0x00010001` | PL E-stop register ABI version. |
| `0x08` | `STATUS` | RO | dynamic | Latched E-stop, input, reset, IRQ, DO/PWM, and bus TX status bits. |
| `0x0C` | `CONTROL` | WO / read zero | `0x00000000` | Write-one pulse controls. Reads always return zero. |
| `0x10` | `TIMING` | RO | `{DEBOUNCE_MS, BRAKE_LEAD_US}` | `[31:16]` debounce milliseconds, `[15:0]` brake lead microseconds. |
| `0x14` | `AXIS_CONFIG` | RO | `{AXIS_COUNT, Z_AXIS_INDEX}` | `[31:16]` axis count, `[15:0]` Z-axis index. |
| `0x18` | `BUILD_ID` | RO | `0x20260629` | Build identifier parameter from RTL. |
| `0x1C` | `GENERAL_CONFIG` | RO | `{16, 0x0000}` | `[31:16]` general output count, `[15:0]` safe levels. |
| `0x20` | `BUS_TX_CONFIG` | RO | `{1, 0x0000}` | `[31:16]` bus TX gate count, `[15:0]` idle levels. |

## STATUS Bits

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `estop_latched` | PL E-stop latch is active. This bit is set after reset and after a filtered low NC input event. |
| 1 | `estop_input_raw` | Synchronized raw NC input; `1` means the NC input is electrically healthy/high after synchronizer. |
| 2 | `estop_input_filtered` | Debounced NC input; `1` means the input stayed healthy/high long enough to pass debounce. |
| 3 | `reset_allowed` | A software reset request can clear the latch only when raw and filtered input are high, brake delay is done, and bus TX queue is flushed. |
| 4 | `brake_delay_active` | Z-axis brake lead delay is still active after a latch event. |
| 5 | `estop_irq` | IRQ latch is active. It is set by a latch event and cleared by `CONTROL[1]`. |
| 6 | `general_output_forced_off` | DO/PWM general-output gate is forcing outputs to safe levels. |
| 7 | `bus_tx_gate_active` | Bus TX gate is active. The current generated hardware gates PS GEM1/EMIO GMII TX before `gmii2rgmii`. |
| 8 | `bus_tx_queue_flushed` | Bus TX queue flushed/invalidated input observed by PL. The current BD placeholder is high; real board software/owner evidence is still pending. |
| 31:9 | `reserved` | Reserved, read as zero in the current RTL. |

## CONTROL Bits

`CONTROL` reads as zero. The current RTL samples writes only when `WSTRB[0]` is set.

| Bit | Name | Access | Meaning |
| ---: | --- | --- | --- |
| 0 | `sw_reset_req` | W1P | Request latch clear. It only clears the latch when `STATUS[3].reset_allowed` is already `1`. |
| 1 | `irq_clear` | W1P | Clear `STATUS[5].estop_irq`. This does not clear `STATUS[0].estop_latched`. |
| 31:2 | `reserved` | write zero | Reserved. Board software should write zero. |

## Readback Rules

- Poll `MAGIC` and `VERSION` before treating the block as the expected PL E-stop ABI.
- Treat `STATUS[0]` as the latched safety-state observation. Do not infer board safety behavior from this bit alone.
- Only issue `CONTROL[0]` after `STATUS[3]` is `1`; a write while reset is not allowed is ignored by the core.
- `CONTROL[1]` is IRQ acknowledgement only. It does not release E-stop.
- `STATUS[6]` and `STATUS[7]` report local PL gate state only. Physical DO/PWM load/off-polarity tests and bus TX Link/RX/release measurements are outside the current plan.
- The current XSA remains `local_verified_only`; board software must not report PL E-stop, DO/PWM shutdown, or bus TX gate behavior as `board_verified` from this code-review-only handoff.

## Verification Command

Run this from `new-vivado/z20_v1_5_hw_project`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify_pl_estop_register_map.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify_pl_estop_timing_params.ps1
```

Required output:

- `pl_estop_register_map=ok`
- `registers=9`
- `status_bits=9`
- `control_bits=2`
- `base_address=0x41260000`
- `irq_route=xlconcat_0/In14`
- `pl_estop_timing_params=ok`
- `bd_clock_net=processing_system7_0_FCLK_CLK0`
