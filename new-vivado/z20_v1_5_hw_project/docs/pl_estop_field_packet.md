# PL E-Stop Field Evidence Packet

This generated packet is the field-facing intake checklist for PL E-stop wiring and board evidence. Fill the CSV inputs and evidence files; do not hand-edit this generated packet.

## Inputs

- Wiring CSV: `docs/pl_estop_wiring_evidence.csv`
- Board validation CSV: `docs/pl_estop_board_validation_evidence.csv`
- Evidence-file root: `docs/evidence/pl_estop`
- Packet state: `open`

## Counts

| Item | Count |
| --- | ---: |
| Wiring rows | 22 |
| Wiring rows not board-verified | 22 |
| Board validation rows | 14 |
| Board validation rows not verified | 14 |
| DO/PWM rows | 16 |
| Bus TX rows | 2 |

## Wiring CSV Intake Rows

Each row must stay `pending` until real wiring is known. A row can move to `ready_for_rtl_xdc` only after polarity, normal owner, PL gate point, schematic evidence, and wiring evidence are filled. A row can move to `board_verified` only after bench and board evidence files exist under the evidence-file root.

For `board_verified`, `bench_evidence` and `board_evidence` must point to non-placeholder `.md` evidence records. Raw scope captures, photos, logic-analyzer exports, or logs should be referenced from those `.md` records instead of being used as the CSV evidence path directly.

| Signal | Group | Candidate | Connector | Required CSV fields before promotion | Suggested evidence paths | Next action |
| --- | --- | --- | --- | --- | --- | --- |
| physical_nc_input | estop_input | EGS_DI AA19 | CN4-56 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/physical_nc_input.md; bench=docs/evidence/pl_estop/bench/physical_nc_input.md; board=docs/evidence/pl_estop/board/physical_nc_input.md` | Board evidence still required before board_verified. |
| sto_or_drive_enable | sto_enable_output |  |  | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/sto_or_drive_enable.md; bench=docs/evidence/pl_estop/bench/sto_or_drive_enable.md; board=docs/evidence/pl_estop/board/sto_or_drive_enable.md` | Identify STO or drive-enable hardware output and fail-safe polarity before connecting any real enable or STO gate. |
| z_or_vertical_axis_brake | brake_output |  |  | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/z_or_vertical_axis_brake.md; bench=docs/evidence/pl_estop/bench/z_or_vertical_axis_brake.md; board=docs/evidence/pl_estop/board/z_or_vertical_axis_brake.md` | Identify vertical-axis brake output and brake-active polarity before connecting brake_z_out. |
| step_enable_gate | axis_gate |  |  | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/step_enable_gate.md; bench=docs/evidence/pl_estop/bench/step_enable_gate.md; board=docs/evidence/pl_estop/board/step_enable_gate.md` | Identify final motion enable/STO/brake owner and keep any future output path upstream of the PL E-stop gate. |
| DO1 | general_output | DO1 A21 | CN3-4 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/DO1.md; bench=docs/evidence/pl_estop/bench/DO1.md; board=docs/evidence/pl_estop/board/DO1.md` | Board evidence still required for DO1 through pl_estop_general_output_gate before board_verified. |
| DO2 | general_output | DO2 A22 | CN3-5 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/DO2.md; bench=docs/evidence/pl_estop/bench/DO2.md; board=docs/evidence/pl_estop/board/DO2.md` | Board evidence still required for DO2 through pl_estop_general_output_gate before board_verified. |
| DO3 | general_output | DO3 B21 | CN3-6 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/DO3.md; bench=docs/evidence/pl_estop/bench/DO3.md; board=docs/evidence/pl_estop/board/DO3.md` | Board evidence still required for DO3 through pl_estop_general_output_gate before board_verified. |
| DO4 | general_output | DO4 B22 | CN3-7 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/DO4.md; bench=docs/evidence/pl_estop/bench/DO4.md; board=docs/evidence/pl_estop/board/DO4.md` | Board evidence still required for DO4 through pl_estop_general_output_gate before board_verified. |
| DO5 | general_output | DO5 C22 | CN3-9 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/DO5.md; bench=docs/evidence/pl_estop/bench/DO5.md; board=docs/evidence/pl_estop/board/DO5.md` | Board evidence still required for DO5 through pl_estop_general_output_gate before board_verified. |
| DO6 | general_output | DO6 D22 | CN3-10 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/DO6.md; bench=docs/evidence/pl_estop/bench/DO6.md; board=docs/evidence/pl_estop/board/DO6.md` | Board evidence still required for DO6 through pl_estop_general_output_gate before board_verified. |
| DO7 | general_output | DO7 F21 | CN3-11 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/DO7.md; bench=docs/evidence/pl_estop/bench/DO7.md; board=docs/evidence/pl_estop/board/DO7.md` | Board evidence still required for DO7 through pl_estop_general_output_gate before board_verified. |
| DO8 | general_output | DO8 F22 | CN3-12 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/DO8.md; bench=docs/evidence/pl_estop/bench/DO8.md; board=docs/evidence/pl_estop/board/DO8.md` | Board evidence still required for DO8 through pl_estop_general_output_gate before board_verified. |
| DO9 | general_output | DO9 F19 | CN3-72 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/DO9.md; bench=docs/evidence/pl_estop/bench/DO9.md; board=docs/evidence/pl_estop/board/DO9.md` | Board evidence still required for DO9 through pl_estop_general_output_gate before board_verified. |
| DO10 | general_output | DO10 G19 | CN3-71 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/DO10.md; bench=docs/evidence/pl_estop/bench/DO10.md; board=docs/evidence/pl_estop/board/DO10.md` | Board evidence still required for DO10 through pl_estop_general_output_gate before board_verified. |
| DO11 | general_output | DO11 F17 | CN3-70 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/DO11.md; bench=docs/evidence/pl_estop/bench/DO11.md; board=docs/evidence/pl_estop/board/DO11.md` | Board evidence still required for DO11 through pl_estop_general_output_gate before board_verified. |
| DO12 | general_output | DO12 G17 | CN3-69 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/DO12.md; bench=docs/evidence/pl_estop/bench/DO12.md; board=docs/evidence/pl_estop/board/DO12.md` | Board evidence still required for DO12 through pl_estop_general_output_gate before board_verified. |
| DO13 | general_output | DO13 G21 | CN3-68 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/DO13.md; bench=docs/evidence/pl_estop/bench/DO13.md; board=docs/evidence/pl_estop/board/DO13.md` | Board evidence still required for DO13 through pl_estop_general_output_gate before board_verified. |
| DO14 | general_output | DO14 G20 | CN3-67 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/DO14.md; bench=docs/evidence/pl_estop/bench/DO14.md; board=docs/evidence/pl_estop/board/DO14.md` | Board evidence still required for DO14 through pl_estop_general_output_gate before board_verified. |
| PWM1 | general_output | PWM1 G22 | CN3-13 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/PWM1.md; bench=docs/evidence/pl_estop/bench/PWM1.md; board=docs/evidence/pl_estop/board/PWM1.md` | Board evidence still required for PWM1 through pl_estop_general_output_gate before board_verified. |
| PWM2 | general_output | PWM2 H22 | CN3-14 | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/PWM2.md; bench=docs/evidence/pl_estop/bench/PWM2.md; board=docs/evidence/pl_estop/board/PWM2.md` | Board evidence still required for PWM2 through pl_estop_general_output_gate before board_verified. |
| bus_tx_driver_enable | bus_tx_gate |  |  | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/bus_tx_driver_enable.md; bench=docs/evidence/pl_estop/bench/bus_tx_driver_enable.md; board=docs/evidence/pl_estop/board/bus_tx_driver_enable.md` | Board evidence still required before board_verified. |
| bus_tx_queue_flush | bus_tx_gate |  |  | polarity_or_safe_level; normal_owner; pl_gate_point; schematic_evidence; wiring_evidence; bench_evidence; board_evidence | `wiring=docs/evidence/pl_estop/wiring/bus_tx_queue_flush.md; bench=docs/evidence/pl_estop/bench/bus_tx_queue_flush.md; board=docs/evidence/pl_estop/board/bus_tx_queue_flush.md` | Board evidence still required before board_verified. |

## Board Validation Intake Rows

Board validation rows stay `pending` until the real board or bench measurement file exists under the evidence-file root and the CSV row records measured value, operator, and date.

For `board_verified`, `evidence_path` must point to a non-placeholder `.md` evidence record. Raw captures or logs should be referenced from that record.

| Test | Group | Instrument | Acceptance | Suggested evidence_path | Next action |
| --- | --- | --- | --- | --- | --- |
| BV01 | physical_nc_input | oscilloscope | <=20ms input-to-hard-output block | `docs/evidence/pl_estop/board_validation/BV01.md` | Capture input and hard-output traces after real NC input and output gates are wired. |
| BV02 | physical_nc_input | oscilloscope | latched fail-safe on disconnect | `docs/evidence/pl_estop/board_validation/BV02.md` | Measure disconnect behavior on the real input circuit. |
| BV03 | debounce_filter | oscilloscope_or_pattern_generator | short pulse rejected per configured debounce | `docs/evidence/pl_estop/board_validation/BV03.md` | Measure debounce behavior around the 10ms to 20ms design window. |
| BV04 | debounce_filter | oscilloscope_or_pattern_generator | latched after documented debounce | `docs/evidence/pl_estop/board_validation/BV04.md` | Measure trigger threshold with the real input path. |
| BV05 | reset_interlock | PS_AXI_status_readback | reset rejected while input low | `docs/evidence/pl_estop/board_validation/BV05.md` | Record AXI readback before and after reset attempt. |
| BV06 | reset_interlock | PS_AXI_status_readback | two-step reset accepted | `docs/evidence/pl_estop/board_validation/BV06.md` | Record AXI readback and physical input trace. |
| BV07 | brake_and_axis_gate | oscilloscope | brake lead matches configured BRAKE_LEAD_US | `docs/evidence/pl_estop/board_validation/BV07.md` | Capture brake and axis gate traces. |
| BV08 | axis_gate | oscilloscope | hard gate within required safety latency | `docs/evidence/pl_estop/board_validation/BV08.md` | Capture input and axis output traces. |
| BV09 | general_outputs | oscilloscope_or_logic_analyzer | all confirmed DO/PWM outputs off | `docs/evidence/pl_estop/board_validation/BV09.md` | Capture all confirmed DO/PWM output states. |
| BV10 | bus_tx_gate | oscilloscope_or_logic_analyzer | TX idle/off while link remains powered | `docs/evidence/pl_estop/board_validation/BV10.md` | Capture TX gate signal and link status. |
| BV11 | bus_tx_link | link_status_probe_and_scope | link and RX/status remain observable | `docs/evidence/pl_estop/board_validation/BV11.md` | Record link status and RX/status observation during E-stop. |
| BV12 | bus_tx_queue | protocol_log_or_logic_analyzer | queued entries flushed or invalidated | `docs/evidence/pl_estop/board_validation/BV12.md` | Record queue owner evidence and post-reset bus trace. |
| BV13 | power_on_fail_closed | oscilloscope_and_AXI_status | power-on safe state | `docs/evidence/pl_estop/board_validation/BV13.md` | Capture power-on output state and STATUS. |
| BV14 | status_irq | PS_AXI_status_readback | STATUS bits and IRQ semantics match spec | `docs/evidence/pl_estop/board_validation/BV14.md` | Record AXI/IRQ readback around trigger and clear. |

## Non-Negotiable Field Rules

- Use only project-relative evidence paths. Do not put drive-letter absolute paths in CSV fields.
- Evidence files used by `board_verified` rows must be non-placeholder `.md` records under `docs/evidence/pl_estop/`; raw captures or logs are attachments referenced by the `.md` record.
- Each `board_verified` evidence record must contain `Evidence State: board_verified`, the matching signal name or test ID, operator, date, instrument, result, and attachment references.
- The `Attachments:` line in a `board_verified` evidence record must list one or more existing comma- or semicolon-separated project-relative files under `docs/evidence/pl_estop/`; the evidence record must not list itself as an attachment.
- Evidence records must not contain `TODO`, `TBD`, `PLACEHOLDER`, or `DRAFT` markers.
- DO1-DO14 and PWM1-PWM2 must be connected through `pl_estop_general_output_gate` after the normal output owner and must include confirmed off polarity before real pin promotion.
- Bus gating must block only TX send-enable, driver-enable, TX idle, or TX_CTL; it must not reset PHY, cut link clock or power, or disable RX/status observation.
- Current production drive transport is EtherCAT over PS GEM1/EMIO; the current generated synth gate is locally verified on that PS GEM1/EMIO RGMII TX path, but BV10-BV12 board evidence is still required before claiming production drive-bus safety.
- Until CSV board evidence closes, the active E-stop input, DO/PWM outputs, and PS GEM1/EMIO RGMII TX gate remain local_verified_only; STO, brake, axis, and any additional bus TX owner stay fail-closed or unpromoted.

## Evidence Record Contract

Each `.md` evidence record used by a `board_verified` CSV row must include these machine-checkable lines:

```text
Evidence State: board_verified
Subject: <signal_name or test_id>
Operator: <person>
Date: <YYYY-MM-DD>
Instrument: <scope logic_analyzer ps_axi_readback photo log other>
Result: <measured result and pass/fail statement>
Attachments: <comma- or semicolon-separated project-relative files under docs/evidence/pl_estop/>
```
