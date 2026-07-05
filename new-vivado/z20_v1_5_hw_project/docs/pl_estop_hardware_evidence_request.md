# PL E-Stop Hardware Evidence Request

This generated file is the field evidence request for promoting PL E-stop wiring from placeholder-only to real pins. Fill the CSV inputs, not this generated report.

## Inputs To Fill

- Wiring evidence CSV: `docs/pl_estop_wiring_evidence.csv`
- Board validation CSV: `docs/pl_estop_board_validation_evidence.csv`
- Evidence-file root: `docs/evidence/pl_estop`

## Current Request State

| Item | Count |
| --- | ---: |
| Wiring evidence requests | 3 |
| Board validation requests | 14 |
| DO/PWM output requests | 0 |
| Bus TX gate requests | 0 |

## Wiring Evidence To Collect

A row may move toward real RTL/XDC only after the CSV records real wiring, safe polarity, normal owner, PL gate point, and evidence paths. Connector/package evidence copied from XDC is only a candidate.

| Signal | Group | Candidate | Connector | Collection Request | CSV Row Next Action |
| --- | --- | --- | --- | --- | --- |
| sto_or_drive_enable | sto_enable_output |  |  | Identify the real STO or drive-enable output, confirm fail-safe polarity, and record where the PL gate must sit after the normal owner. | Identify STO or drive-enable hardware output and fail-safe polarity before connecting any real enable or STO gate. |
| z_or_vertical_axis_brake | brake_output |  |  | Identify the vertical-axis brake output, active polarity, required lead time, and the gate point relative to the drive block. | Identify vertical-axis brake output and brake-active polarity before connecting brake_z_out. |
| step_enable_gate | axis_gate |  |  | Identify the real axis-output owner and final PL gate point for step, PWM, ENA, or future motion-output paths. | Identify final motion enable/STO/brake owner and keep any future output path upstream of the PL E-stop gate. |

## Board Measurements To Collect

These tests are required after real wiring is connected. They are not satisfied by local RTL simulation, bitstream generation, or XSA export.

| Test | Group | Trigger | Expected Result | Instrument | Acceptance | Next Action |
| --- | --- | --- | --- | --- | --- | --- |
| BV01 | physical_nc_input | drive confirmed NC input from healthy high to E-stop low | PL STATUS estop_latched asserts and selected hard outputs enter safe/off state | oscilloscope | <=20ms input-to-hard-output block | Capture input and hard-output traces after real NC input and output gates are wired. |
| BV02 | physical_nc_input | open or disconnect confirmed NC safety-chain input | PL treats disconnect as E-stop and remains latched | oscilloscope | latched fail-safe on disconnect | Measure disconnect behavior on the real input circuit. |
| BV03 | debounce_filter | inject low pulse shorter than debounce window | PL does not falsely release or false-trigger outside documented debounce behavior | oscilloscope_or_pattern_generator | short pulse rejected per configured debounce | Measure debounce behavior around the 10ms to 20ms design window. |
| BV04 | debounce_filter | hold input low longer than debounce window | PL latches E-stop after debounce and stays latched | oscilloscope_or_pattern_generator | latched after documented debounce | Measure trigger threshold with the real input path. |
| BV05 | reset_interlock | write software reset while physical input remains low | PL rejects reset and STATUS estop_latched remains asserted | PS_AXI_status_readback | reset rejected while input low | Record AXI readback before and after reset attempt. |
| BV06 | reset_interlock | restore physical input high then issue software reset | PL clears latch only after physical recovery plus software reset | PS_AXI_status_readback | two-step reset accepted | Record AXI readback and physical input trace. |
| BV07 | brake_and_axis_gate | trigger E-stop with confirmed vertical-axis brake and axis gate connected | brake output reaches active state before vertical-axis drive output is blocked | oscilloscope | brake lead matches configured BRAKE_LEAD_US | Capture brake and axis gate traces. |
| BV08 | axis_gate | trigger E-stop with confirmed step or enable gate connected | step or enable output is blocked by PL without waiting for PS/UI | oscilloscope | hard gate within required safety latency | Capture input and axis output traces. |
| BV09 | general_outputs | trigger E-stop with DO1-DO14 and PWM1-PWM2 connected through PL gate | every confirmed general output reaches documented off/safe level | oscilloscope_or_logic_analyzer | all confirmed DO/PWM outputs off | Capture all confirmed DO/PWM output states. |
| BV10 | bus_tx_gate | trigger E-stop with selected bus TX send-enable or driver-enable gate connected | selected TX gate reaches idle/off state without resetting PHY/link | oscilloscope_or_logic_analyzer | TX idle/off while link remains powered | Capture TX gate signal and link status. |
| BV11 | bus_tx_link | hold E-stop latched while bus physical link is connected | physical link clock/reset/power/RX observation remain alive | link_status_probe_and_scope | link and RX/status remain observable | Record link status and RX/status observation during E-stop. |
| BV12 | bus_tx_queue | queue or simulate bus TX/control frames during E-stop latch | no stale TX/control frame is replayed after reset release | protocol_log_or_logic_analyzer | queued entries flushed or invalidated | Record queue owner evidence and post-reset bus trace. |
| BV13 | power_on_fail_closed | power cycle board with confirmed E-stop path | PL starts latched or in safe state until physical input and reset conditions are satisfied | oscilloscope_and_AXI_status | power-on safe state | Capture power-on output state and STATUS. |
| BV14 | status_irq | trigger and clear E-stop IRQ path | PS can observe STATUS bits and IRQ clear does not clear E-stop latch | PS_AXI_status_readback | STATUS bits and IRQ semantics match spec | Record AXI/IRQ readback around trigger and clear. |

## Non-Negotiable Boundaries

- DO1-DO14 and PWM1-PWM2 must not be promoted as ordinary outputs first; promotion must include the PL general-output gate, confirmed off polarity, AXI/status coverage, simulation update, and bench or board forced-off evidence.
- Bus gating must block only TX send-enable, driver-enable, TX idle, or TX_CTL while leaving the physical link, clock, power, and RX observation alive.
- Current production drive transport is EtherCAT over PS GEM1/EMIO; the current generated synth gate is locally verified on that PS GEM1/EMIO RGMII TX path, but BV10-BV12 board evidence is still required before claiming production bus cut capability.
- Any `board_verified` wiring or board-validation evidence file must be stored under `docs/evidence/pl_estop/` and referenced by project-relative path.
- Any `board_verified` evidence path must point to a non-placeholder `.md` evidence record with `Evidence State: board_verified` and the matching signal name or test ID; raw captures and logs are attachments referenced by that record.
- Each `board_verified` evidence record `Attachments:` line must list one or more existing comma- or semicolon-separated project-relative files under `docs/evidence/pl_estop/`; the record cannot list itself as an attachment.
- Until the CSV board evidence closes, the active E-stop input, DO/PWM outputs, and PS GEM1/EMIO RGMII TX gate remain local_verified_only; STO, brake, axis, and any additional bus TX owner stay fail-closed or unpromoted.
