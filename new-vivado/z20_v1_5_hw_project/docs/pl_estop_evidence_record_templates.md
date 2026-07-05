# PL E-Stop Evidence Record Templates

This generated file gives field operators copy-ready `.md` evidence-record skeletons. It is not measurement evidence and must not be referenced by a `board_verified` CSV row.

## Inputs

- Wiring CSV: `docs/pl_estop_wiring_evidence.csv`
- Board validation CSV: `docs/pl_estop_board_validation_evidence.csv`
- Evidence-file root: `docs/evidence/pl_estop`
- Template state: `open`

## Counts

| Item | Count |
| --- | ---: |
| Wiring templates | 22 |
| Board validation templates | 14 |
| DO/PWM templates | 16 |
| Bus TX templates | 2 |

## How To Use

- Create real evidence records under the suggested project-relative path, then fill real operator/date/instrument/result/attachment values.
- Keep `Evidence State: pending_until_measured` while the record is only prepared or partially filled.
- Change the evidence record state to the board-verified state only after the corresponding CSV row is changed to `board_verified` and the evidence is complete.
- Do not reference this generated template file from a CSV evidence path.
- Do not store drive-letter absolute paths in records or attachments.
- For a `board_verified` record, list one or more existing comma- or semicolon-separated project-relative attachment files under `docs/evidence/pl_estop/`; do not list the evidence record itself as an attachment.

## Wiring Record Templates

| Signal | Group | Wiring record | Bench record | Board record | Required CSV fields |
| --- | --- | --- | --- | --- | --- |
| physical_nc_input | estop_input | `docs/evidence/pl_estop/wiring/physical_nc_input.md` | `docs/evidence/pl_estop/bench/physical_nc_input.md` | `docs/evidence/pl_estop/board/physical_nc_input.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| sto_or_drive_enable | sto_enable_output | `docs/evidence/pl_estop/wiring/sto_or_drive_enable.md` | `docs/evidence/pl_estop/bench/sto_or_drive_enable.md` | `docs/evidence/pl_estop/board/sto_or_drive_enable.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| z_or_vertical_axis_brake | brake_output | `docs/evidence/pl_estop/wiring/z_or_vertical_axis_brake.md` | `docs/evidence/pl_estop/bench/z_or_vertical_axis_brake.md` | `docs/evidence/pl_estop/board/z_or_vertical_axis_brake.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| step_enable_gate | axis_gate | `docs/evidence/pl_estop/wiring/step_enable_gate.md` | `docs/evidence/pl_estop/bench/step_enable_gate.md` | `docs/evidence/pl_estop/board/step_enable_gate.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| DO1 | general_output | `docs/evidence/pl_estop/wiring/DO1.md` | `docs/evidence/pl_estop/bench/DO1.md` | `docs/evidence/pl_estop/board/DO1.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| DO2 | general_output | `docs/evidence/pl_estop/wiring/DO2.md` | `docs/evidence/pl_estop/bench/DO2.md` | `docs/evidence/pl_estop/board/DO2.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| DO3 | general_output | `docs/evidence/pl_estop/wiring/DO3.md` | `docs/evidence/pl_estop/bench/DO3.md` | `docs/evidence/pl_estop/board/DO3.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| DO4 | general_output | `docs/evidence/pl_estop/wiring/DO4.md` | `docs/evidence/pl_estop/bench/DO4.md` | `docs/evidence/pl_estop/board/DO4.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| DO5 | general_output | `docs/evidence/pl_estop/wiring/DO5.md` | `docs/evidence/pl_estop/bench/DO5.md` | `docs/evidence/pl_estop/board/DO5.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| DO6 | general_output | `docs/evidence/pl_estop/wiring/DO6.md` | `docs/evidence/pl_estop/bench/DO6.md` | `docs/evidence/pl_estop/board/DO6.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| DO7 | general_output | `docs/evidence/pl_estop/wiring/DO7.md` | `docs/evidence/pl_estop/bench/DO7.md` | `docs/evidence/pl_estop/board/DO7.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| DO8 | general_output | `docs/evidence/pl_estop/wiring/DO8.md` | `docs/evidence/pl_estop/bench/DO8.md` | `docs/evidence/pl_estop/board/DO8.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| DO9 | general_output | `docs/evidence/pl_estop/wiring/DO9.md` | `docs/evidence/pl_estop/bench/DO9.md` | `docs/evidence/pl_estop/board/DO9.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| DO10 | general_output | `docs/evidence/pl_estop/wiring/DO10.md` | `docs/evidence/pl_estop/bench/DO10.md` | `docs/evidence/pl_estop/board/DO10.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| DO11 | general_output | `docs/evidence/pl_estop/wiring/DO11.md` | `docs/evidence/pl_estop/bench/DO11.md` | `docs/evidence/pl_estop/board/DO11.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| DO12 | general_output | `docs/evidence/pl_estop/wiring/DO12.md` | `docs/evidence/pl_estop/bench/DO12.md` | `docs/evidence/pl_estop/board/DO12.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| DO13 | general_output | `docs/evidence/pl_estop/wiring/DO13.md` | `docs/evidence/pl_estop/bench/DO13.md` | `docs/evidence/pl_estop/board/DO13.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| DO14 | general_output | `docs/evidence/pl_estop/wiring/DO14.md` | `docs/evidence/pl_estop/bench/DO14.md` | `docs/evidence/pl_estop/board/DO14.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| PWM1 | general_output | `docs/evidence/pl_estop/wiring/PWM1.md` | `docs/evidence/pl_estop/bench/PWM1.md` | `docs/evidence/pl_estop/board/PWM1.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| PWM2 | general_output | `docs/evidence/pl_estop/wiring/PWM2.md` | `docs/evidence/pl_estop/bench/PWM2.md` | `docs/evidence/pl_estop/board/PWM2.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| bus_tx_driver_enable | bus_tx_gate | `docs/evidence/pl_estop/wiring/bus_tx_driver_enable.md` | `docs/evidence/pl_estop/bench/bus_tx_driver_enable.md` | `docs/evidence/pl_estop/board/bus_tx_driver_enable.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |
| bus_tx_queue_flush | bus_tx_gate | `docs/evidence/pl_estop/wiring/bus_tx_queue_flush.md` | `docs/evidence/pl_estop/bench/bus_tx_queue_flush.md` | `docs/evidence/pl_estop/board/bus_tx_queue_flush.md` | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence |

## Board Validation Record Templates

| Test | Group | Evidence record | Instrument | Acceptance |
| --- | --- | --- | --- | --- |
| BV01 | physical_nc_input | `docs/evidence/pl_estop/board_validation/BV01.md` | oscilloscope | <=20ms input-to-hard-output block |
| BV02 | physical_nc_input | `docs/evidence/pl_estop/board_validation/BV02.md` | oscilloscope | latched fail-safe on disconnect |
| BV03 | debounce_filter | `docs/evidence/pl_estop/board_validation/BV03.md` | oscilloscope_or_pattern_generator | short pulse rejected per configured debounce |
| BV04 | debounce_filter | `docs/evidence/pl_estop/board_validation/BV04.md` | oscilloscope_or_pattern_generator | latched after documented debounce |
| BV05 | reset_interlock | `docs/evidence/pl_estop/board_validation/BV05.md` | PS_AXI_status_readback | reset rejected while input low |
| BV06 | reset_interlock | `docs/evidence/pl_estop/board_validation/BV06.md` | PS_AXI_status_readback | two-step reset accepted |
| BV07 | brake_and_axis_gate | `docs/evidence/pl_estop/board_validation/BV07.md` | oscilloscope | brake lead matches configured BRAKE_LEAD_US |
| BV08 | axis_gate | `docs/evidence/pl_estop/board_validation/BV08.md` | oscilloscope | hard gate within required safety latency |
| BV09 | general_outputs | `docs/evidence/pl_estop/board_validation/BV09.md` | oscilloscope_or_logic_analyzer | all confirmed DO/PWM outputs off |
| BV10 | bus_tx_gate | `docs/evidence/pl_estop/board_validation/BV10.md` | oscilloscope_or_logic_analyzer | TX idle/off while link remains powered |
| BV11 | bus_tx_link | `docs/evidence/pl_estop/board_validation/BV11.md` | link_status_probe_and_scope | link and RX/status remain observable |
| BV12 | bus_tx_queue | `docs/evidence/pl_estop/board_validation/BV12.md` | protocol_log_or_logic_analyzer | queued entries flushed or invalidated |
| BV13 | power_on_fail_closed | `docs/evidence/pl_estop/board_validation/BV13.md` | oscilloscope_and_AXI_status | power-on safe state |
| BV14 | status_irq | `docs/evidence/pl_estop/board_validation/BV14.md` | PS_AXI_status_readback | STATUS bits and IRQ semantics match spec |

## Record Skeleton

Use this skeleton for each real evidence file. The subject must exactly match a `signal_name` or `test_id` from the CSV row that will reference it.

```text
Evidence State: pending_until_measured
Subject: <signal_name or test_id>
Operator: <person>
Date: <YYYY-MM-DD>
Instrument: <scope logic_analyzer ps_axi_readback photo log other>
Result: <measured result and pass/fail statement>
Attachments: <comma- or semicolon-separated project-relative files under docs/evidence/pl_estop/>
Notes: <short measurement context>
```

## DO/PWM and Bus-Specific Notes

- `DO1` - `DO14` and `PWM1` - `PWM2` records must include confirmed load wiring, off polarity, and proof that the signal is driven through `pl_estop_general_output_gate` before the physical pin.
- Bus TX records must name the real PL-owned bus owner, the gated send-enable or driver-enable signal, the idle/off polarity, and the queue flush or invalidate owner.
- Bus TX evidence must show TX is blocked while Link, clock, reset, power, RX, and status observation remain alive.
