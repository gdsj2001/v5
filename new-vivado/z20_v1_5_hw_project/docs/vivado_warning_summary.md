# Vivado Warning Summary

This generated summary classifies the current `synth_1/runme.log` and `impl_1/runme.log` warnings.

## Gate Result

- `vivado_warning_summary=classified`
- `vivado_warning_lines=457`
- `unexpected_warning_codes=0`
- `constraint_truth_warning_lines=0`
- `retired_hdmi_warning_lines=0`

`constraint_truth_warning_lines=0` is the requirement that proves the v1.5 source XDC and old `system.xdc` warning chain is not present in the active Vivado run. Remaining warning lines are classified below and are not board safety proof.

## Summary

| Count | Code | Phases | Classification | Owner | Next Action |
| ---: | --- | --- | --- | --- | --- |
| 100 | `Synth 8-5396` | `synth:100` | `xpm_cdc_keep_attribute` | `vivado_xpm_ip` | `monitor` |
| 100 | `Synth 8-7071` | `synth:100` | `generated_ip_unconnected_port` | `generated_bd_ip` | `monitor_or_remove_retired_ip_owner` |
| 100 | `Synth 8-7129` | `synth:100` | `generated_ip_unloaded_port` | `generated_bd_ip` | `monitor_or_remove_retired_ip_owner` |
| 60 | `Synth 8-7023` | `synth:60` | `generated_ip_partial_instance_connections` | `generated_bd_ip` | `monitor_or_remove_retired_ip_owner` |
| 40 | `Common 17-1361` | `impl:20;synth:20` | `vivado_message_config_duplicate` | `generated_bd_ip_flow` | `cleanup_optional` |
| 38 | `Synth 8-3332` | `synth:38` | `generated_ip_trimmed_unused_sequential` | `generated_bd_ip` | `monitor` |
| 5 | `Vivado 12-180` | `synth:5` | `xpm_memory_xdc_no_cells` | `vivado_xpm_ip` | `monitor` |
| 2 | `Synth 8-3936` | `synth:2` | `hls_frozen_internal_register_trim` | `hls_frozen_ip` | `monitor` |
| 2 | `Synth 8-589` | `synth:2` | `hls_frozen_case_equality_rewrite` | `hls_frozen_ip` | `monitor` |
| 2 | `Timing 38-436` | `impl:2` | `bus_skew_report_reminder` | `vivado_timing_report` | `run_bus_skew_before_board_release` |
| 2 | `Vivado 12-2489` | `impl:2` | `ps7_generated_clock_jitter_rounding` | `generated_ps7_xdc` | `monitor` |
| 1 | `DRC RTSTAT-10` | `impl:1` | `allowed_drc_no_routable_loads` | `generated_bd_unused_internal_nets` | `monitor` |
| 1 | `Opt 31-32` | `synth:1` | `redundant_ibuf_removed` | `generated_bd_ip` | `monitor_or_remove_retired_ip_owner` |
| 1 | `Power 33-332` | `impl:1` | `power_analysis_high_fanout_reset_activity` | `vivado_power_estimator` | `monitor` |
| 1 | `Synth 8-2048` | `synth:1` | `generated_axi_iic_function_return` | `vivado_axi_iic_ip` | `monitor` |
| 1 | `Synth 8-3295` | `synth:1` | `xpm_cdc_undriven_pin_tied_zero` | `vivado_xpm_ip` | `monitor` |
| 1 | `Synth 8-5785` | `synth:1` | `constant_propagation_removed_ram` | `generated_bd_or_tx_gate_logic` | `monitor` |

## Boundary

- This file does not claim all Vivado warnings are removed.
- It proves that current warning codes are known and that source/old constraint warning lines are absent.
- Retired HDMI/DVI warning residue is 0 after BD-level HDMI removal; MPG remains the owner of the shared pins.
