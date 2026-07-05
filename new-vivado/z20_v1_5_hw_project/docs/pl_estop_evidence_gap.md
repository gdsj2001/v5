# PL E-Stop Evidence Gap

This report is generated from project-local evidence CSV files. It is a handoff checklist, not board proof.

## Inputs

- Wiring evidence: `docs/pl_estop_wiring_evidence.csv`
- Board validation evidence: `docs/pl_estop_board_validation_evidence.csv`

## Current Gate State

| Gate | Ready | Pending | Required |
| --- | ---: | ---: | ---: |
| Wiring evidence rows | 19 | 3 | 22 |
| Board validation tests | 0 | 14 | 14 |

## Wiring Evidence Gaps

Rows below must not be promoted to active XDC or real PL outputs until the missing fields are backed by actual wiring evidence.

| Signal | Group | Candidate | Pin | Connector | Missing Before ready_for_rtl_xdc | Next Action |
| --- | --- | --- | --- | --- | --- | --- |
| sto_or_drive_enable | sto_enable_output |  |  |  | polarity_or_safe_level, normal_owner, pl_gate_point, wiring_evidence | Identify STO or drive-enable hardware output and fail-safe polarity before connecting any real enable or STO gate. |
| z_or_vertical_axis_brake | brake_output |  |  |  | polarity_or_safe_level, normal_owner, pl_gate_point, wiring_evidence | Identify vertical-axis brake output and brake-active polarity before connecting brake_z_out. |
| step_enable_gate | axis_gate |  |  |  | polarity_or_safe_level, normal_owner, pl_gate_point, wiring_evidence | Identify final motion enable/STO/brake owner and keep any future output path upstream of the PL E-stop gate. |

## Board Validation Gaps

Rows below need bench or board measurements after real wiring is connected. Keeping them pending is the correct fail-closed state until then.

| Test | Group | Instrument | Acceptance | Missing Before board_verified | Next Action |
| --- | --- | --- | --- | --- | --- |
| BV01 | physical_nc_input | oscilloscope | <=20ms input-to-hard-output block | measured_value, evidence_path, operator, date | Capture input and hard-output traces after real NC input and output gates are wired. |
| BV02 | physical_nc_input | oscilloscope | latched fail-safe on disconnect | measured_value, evidence_path, operator, date | Measure disconnect behavior on the real input circuit. |
| BV03 | debounce_filter | oscilloscope_or_pattern_generator | short pulse rejected per configured debounce | measured_value, evidence_path, operator, date | Measure debounce behavior around the 10ms to 20ms design window. |
| BV04 | debounce_filter | oscilloscope_or_pattern_generator | latched after documented debounce | measured_value, evidence_path, operator, date | Measure trigger threshold with the real input path. |
| BV05 | reset_interlock | PS_AXI_status_readback | reset rejected while input low | measured_value, evidence_path, operator, date | Record AXI readback before and after reset attempt. |
| BV06 | reset_interlock | PS_AXI_status_readback | two-step reset accepted | measured_value, evidence_path, operator, date | Record AXI readback and physical input trace. |
| BV07 | brake_and_axis_gate | oscilloscope | brake lead matches configured BRAKE_LEAD_US | measured_value, evidence_path, operator, date | Capture brake and axis gate traces. |
| BV08 | axis_gate | oscilloscope | hard gate within required safety latency | measured_value, evidence_path, operator, date | Capture input and axis output traces. |
| BV09 | general_outputs | oscilloscope_or_logic_analyzer | all confirmed DO/PWM outputs off | measured_value, evidence_path, operator, date | Capture all confirmed DO/PWM output states. |
| BV10 | bus_tx_gate | oscilloscope_or_logic_analyzer | TX idle/off while link remains powered | measured_value, evidence_path, operator, date | Capture TX gate signal and link status. |
| BV11 | bus_tx_link | link_status_probe_and_scope | link and RX/status remain observable | measured_value, evidence_path, operator, date | Record link status and RX/status observation during E-stop. |
| BV12 | bus_tx_queue | protocol_log_or_logic_analyzer | queued entries flushed or invalidated | measured_value, evidence_path, operator, date | Record queue owner evidence and post-reset bus trace. |
| BV13 | power_on_fail_closed | oscilloscope_and_AXI_status | power-on safe state | measured_value, evidence_path, operator, date | Capture power-on output state and STATUS. |
| BV14 | status_irq | PS_AXI_status_readback | STATUS bits and IRQ semantics match spec | measured_value, evidence_path, operator, date | Record AXI/IRQ readback around trigger and clear. |

## Fail-Closed Notes

- XDC connector/package evidence is not enough to connect a real safety input or output.
- DO1-DO14 and PWM1-PWM2 must stay behind the PL E-stop general-output gate when promoted.
- The bus TX gate may only target a confirmed TX send-enable, driver-enable, TX idle, or TX_CTL path; it must not break PHY reset, link clock, link power, or RX observation.
- Current production drive transport is EtherCAT over PS GEM1/EMIO; the current generated synth gate is locally verified on that PS GEM1/EMIO RGMII TX path, but BV10-BV12 board evidence is still required before claiming the production EtherCAT bus can be cut safely by PL.
