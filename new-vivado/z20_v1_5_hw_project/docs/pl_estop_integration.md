# PL E-Stop Integration

## Source

- Requirement input: `../../pl急停.md`
- Active board constraint source: `../../z20-v1_5_20260623.xdc`
- Port mapping: `port_mapping.md`
- Current project status: `local_verified_only` for PL E-stop RTL, top-level `estop_nc_in` hard gate, DO/PWM general-output forced-off gate, GMII pre-ODDR TX send gate, generated AXI status observation from the same top-level E-stop input, simulation, internal BD AXI/IRQ integration, current-top synthesis, routed timing, bitstream generation, XSA export, board-input manifest export, hardware evidence request export and verify, field evidence packet export and verify, evidence-record template export and verify, evidence-root verify, project-independence checks, and PL E-stop safety-boundary checks. Board wiring proof, external STO/brake/axis outputs, board deployment, software readback, and board safety validation remain open.

## Safety Intent

The PL side must own the physical emergency-stop action. PS/LinuxCNC/UI may observe, report, and request reset, but they must not be required for the first hardware stop action.

## Required Hardware Behavior

- NC input: physical E-stop and safety chain are normally closed, high when healthy, low on E-stop, cable break, power loss, or optocoupler failure.
- Debounce: PL implements a 10ms - 20ms integrator/filter before latching E-stop to reject EMC spikes.
- Latch: once triggered, E-stop remains latched until the physical input is healthy and software reset is accepted.
- Hard stop: PL directly blocks step/STO/enable outputs without waiting for PS.
- General output shutdown: when E-stop is latched, PL must also force general-purpose outputs `DO1` - `DO14` and `PWM1` - `PWM2` to their confirmed off/safe state without waiting for PS, LinuxCNC, or UI. Output polarity is not assumed; each DO/PWM channel needs schematic/load evidence before active use.
- Bus transmit gate: for PL-connected bus drive/control outputs, E-stop must block TX send enable or driver-enable in PL while keeping the physical link alive. Do not reset the PHY/transceiver, remove link clock, drop link power, or disable RX observation merely to satisfy E-stop. After the physical input recovers and the reset interlock accepts reset, TX may re-enable without rebuilding the physical link. Any TX FIFO, queued command, or control frame accumulated while E-stop is latched must be flushed or invalidated before release.
- Current hardware-profile boundary: `config/hardware_profile.json` currently marks the production motion transport as EtherCAT over PS GEM1/EMIO and marks STEP-DIR / PL Ethernet DMA as future-not-implemented. The current generated synth gate is now locally traced to that PS GEM1/EMIO RGMII path: `scripts/verify_pl_estop_bus_gate_owner.ps1` confirms PS ENET1 EMIO is connected through `gmii2rgmii` to external RGMII, and that E-stop gates GMII `TX_EN`, GMII `TXD`, and GMII `TX_ER` before `gmii2rgmii` while preserving RX, MDIO, link reset, link power, and clocks by design. This is still local RTL/generated-netlist evidence, not board proof. BV10-BV12 must measure the actual board/PHY path, TX idle/off behavior, Link/RX/MDIO continuity, release recovery, and stale TX/control-frame behavior. Physical safety still must also be confirmed through the E-stop/STO/enable/brake/DO-PWM chain.
- Brake timing: vertical-axis brake output must assert before the corresponding vertical-axis drive is disabled, with a hardware timer delay in the tens of microseconds.
- PS visibility: PL exposes E-stop status through AXI status registers and/or IRQ for PS/Broker/SHM/UI reporting.
- Reset interlock: when the physical input is still low, PL rejects all PS reset/enable writes.

## Current A4 Result

`EGS_DI/AA19` is now the active top-level `estop_nc_in` for the local hard gate and generated PL E-stop AXI status/IRQ observation. HDMI is abandoned and the former HDMI conflict pins are assigned to MPG. `DO1` - `DO14` and `PWM1` - `PWM2` are active top-level outputs driven only through the PL E-stop forced-off gate, with current normal owner tied to zero. STO, brake, and axis gate outputs are still unassigned.

`docs/pl_estop_wiring_evidence.csv` is the machine-readable evidence gate for E1/E2. Current expected state is `pl_estop_wiring_evidence=not_ready`, `ready_for_real_pins=no`, `pending_rows=3`, `ready_rows=19`, `do_pwm_ready_rows=16`, and `bus_tx_ready_rows=2`; no remaining STO/brake/axis output or additional bus TX owner may be promoted until the relevant rows carry schematic evidence, wiring evidence, owner, safe/off or idle polarity, and the PL gate point.

When any row moves to `board_verified`, its CSV evidence path must point to a non-placeholder `.md` evidence record under `docs/evidence/pl_estop/`. That record must include `Evidence State: board_verified` plus the matching signal name or test ID, operator, date, instrument, result, and attachment references. The `Attachments:` line must list one or more existing comma- or semicolon-separated project-relative files under `docs/evidence/pl_estop/`, and the evidence record must not list itself as an attachment. Raw scope captures, photos, logic-analyzer exports, and readback logs are attachments referenced from the `.md` record, not the CSV evidence path itself.

`scripts/verify_pl_estop_evidence_root.ps1` also scans the evidence root as a whole. It rejects process logs, absolute paths, old-project references, and orphan `board_verified` records that are not referenced by a `board_verified` CSV row.

`docs/pl_estop_evidence_record_templates.md` is generated from the same CSV inputs as a field helper for creating real `.md` records under `docs/evidence/pl_estop/`. It is verified by `scripts/verify_pl_estop_evidence_templates.ps1` and currently covers 22 wiring templates, 14 board-validation templates, 16 DO/PWM templates, and 2 bus-TX templates. The template file itself is not measurement evidence and must not be used as a CSV evidence path.

Local evidence review on 2026-06-30 populated the CSV with v1.5 XDC connector/package evidence for `EGS_DI`, `DO1` - `DO14`, `PWM1` - `PWM2`, and the GMII pre-ODDR TX send-gate boundary. These rows are now `ready_for_rtl_xdc` and the local top-level hard gate is promoted. This is still not board evidence: it proves the named net, connector, package pin, and local generated-netlist gate point in `../../z20-v1_5_20260623.xdc` and the current XSA build, but it does not prove the actual machine NC safety-chain wiring, STO/brake wiring, load wiring, off polarity, actual board/PHY TX idle behavior, Link/RX/MDIO continuity, PS-side queue flush or stale-frame behavior, or board measurement. A local scan of `0硬件资料/_z20_base_schematic_text.txt` and the base-board pin spreadsheet found base-board RS485/PL PHY concepts such as SP3485 `RS485_DE` and RGMII `PHY2_TXCTL`, but those files do not close the v1.5 machine safety wiring evidence.

## Current A5/A6 RTL Result

Standalone PL E-stop RTL now exists and is connected internally to BD for AXI/IRQ observation. It is still not connected to external E-stop/STO/brake/axis pins:

- Core RTL: `../rtl/pl_estop_core.v`
- AXI-Lite wrapper: `../rtl/pl_estop_axi_lite.v`
- Core testbench: `../sim/pl_estop_core_tb.v`
- AXI wrapper testbench: `../sim/pl_estop_axi_lite_tb.v`

Local checks run from `PROJECT_ROOT`:

```powershell
iverilog -g2012 -s pl_estop_core_tb -o repo_ignored/vivado_new_project_plan/pl_estop_core_tb.vvp new-vivado/z20_v1_5_hw_project/rtl/pl_estop_core.v new-vivado/z20_v1_5_hw_project/rtl/pl_estop_axi_lite.v new-vivado/z20_v1_5_hw_project/sim/pl_estop_core_tb.v
vvp repo_ignored/vivado_new_project_plan/pl_estop_core_tb.vvp
iverilog -g2012 -s pl_estop_axi_lite_tb -o repo_ignored/vivado_new_project_plan/pl_estop_axi_lite_tb.vvp new-vivado/z20_v1_5_hw_project/rtl/pl_estop_core.v new-vivado/z20_v1_5_hw_project/rtl/pl_estop_axi_lite.v new-vivado/z20_v1_5_hw_project/sim/pl_estop_axi_lite_tb.v
vvp repo_ignored/vivado_new_project_plan/pl_estop_axi_lite_tb.vvp
```

Observed result:

- `PASS: pl_estop_core_tb`
- `PASS: pl_estop_axi_lite_tb`

Covered locally: reset-safe latch, NC low trigger, short-low bounce rejection, software reset rejection while input is low, Z-axis brake lead delay, non-Z immediate output block, 16-channel general output forced-off gate with default all-zero safe level, one-channel bus TX gate with default idle level 0, reset rejection while the bus TX queue-flushed input is false, AXI status read including `STATUS[6].general_output_forced_off`, `STATUS[7].bus_tx_gate_active`, and `STATUS[8].bus_tx_queue_flushed`, AXI reset request, and IRQ clear without latch clear.

Not yet covered: the top-level DO/PWM gate is connected to real `DO1` - `DO14` and `PWM1` - `PWM2` pins, and current normal-output owner is now `z20_v15_io_owner_axi_lite` before that gate. Each real channel still needs confirmed off polarity, load wiring, and bench/board forced-off evidence before active use. The bus TX gate is connected before `gmii2rgmii` by gating GMII `TX_EN`, zeroing GMII `TXD`, and clearing GMII `TX_ER`; RGMII `TX_CTL/TXD/TXC` stay directly driven by `gmii2rgmii`/ODDR. The generated AXI status/IRQ observation now uses the same top `estop_nc_in`, but still needs software readback and board evidence. Bus TX still needs board proof that TX stops, Link/RX stay alive, release restores cleanly, and no stale TX/control frames replay. STO, brake, and dedicated axis safety gate points remain unconnected.

Static safety-boundary check:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify_pl_estop_safety_boundary.ps1
```

Current expected output includes `pl_estop_safety_boundary=ok`, `do_pwm_gate=top_hard_gate_local_unverified`, `bus_tx_gate=top_rgmii_tx_gate_local_unverified`, `gmii_pre_oddr_patch_flow=ok`, `active_do_pwm_pin_assignments=16`, `active_estop_input_pin_assignments=1`, `active_pending_wiring_pin_assignments=0`, and `active_estop_gate_output_ports=0`.

Wiring evidence check:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify_pl_estop_wiring_evidence.ps1
```

Current expected output includes `pl_estop_wiring_evidence=not_ready`, `ready_for_real_pins=no`, `board_evidence_ready=no`, `required_rows=22`, `pending_rows=3`, `ready_rows=19`, `do_pwm_ready_rows=16`, `bus_tx_ready_rows=2`, `board_verified_evidence_contract=md_non_placeholder`, and `board_verified_attachment_contract=project_relative_existing_files`.

Board validation evidence check:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify_pl_estop_board_validation.ps1
```

Current expected output includes `pl_estop_board_validation=not_ready`, `board_validation_ready=no`, `required_tests=14`, `verified_tests=0`, `pending_tests=14`, `board_verified_evidence_contract=md_non_placeholder`, and `board_verified_attachment_contract=project_relative_existing_files`. The evidence table is `docs/pl_estop_board_validation_evidence.csv`; it is intentionally all pending until the real NC input, STO/brake/axis gates, `DO1` - `DO14`, `PWM1` - `PWM2`, and bus TX gate are wired and measured on bench or board.

E11/A11 readiness check:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify_pl_estop_readiness.ps1
```

Current expected output includes `pl_estop_readiness=not_ready`, `pl_estop_hardware_evidence_request_verify=ok`, `pl_estop_evidence_templates_verify=ok`, `board_verified_template_records=0`, `e11_rtl_xdc_ready=no`, `a11_board_validation_ready=no`, and blockers `pl_estop_wiring_evidence_not_ready,pl_estop_board_evidence_not_ready,pl_estop_board_validation_not_ready`.

## Current A7/A8 BD Result

Internal BD integration has been added by `../scripts/vivado/add_pl_estop_axi_lite.tcl`.

Actual BD connections:

| Signal or interface | Connection | Current safety meaning |
| --- | --- | --- |
| AXI-Lite | `ps7_0_axi_periph/M21_AXI` -> `pl_estop/S_AXI` | PS can read/write the PL E-stop register block after software support exists. |
| Address | `SEG_pl_estop_reg0` offset `0x41260000`, range `64K` | Dedicated 64K AXI window for PL E-stop registers. |
| Clock | `processing_system7_0/FCLK_CLK0` -> `pl_estop/S_AXI_ACLK` | Uses existing PS fabric clock path. |
| Reset | `rst_ps7_0_100M/peripheral_aresetn` -> `pl_estop/S_AXI_ARESETN` | Uses existing PS peripheral reset path. |
| E-stop NC input | Saved BD source placeholder: `cnc_const_zero/dout` -> `pl_estop/estop_nc_in`; generated synth/XSA patch: top `estop_nc_in` -> `pl_estop/estop_nc_in` | The built local hardware status path observes the same top physical input as the DO/PWM and GMII TX hard gates. The saved BD still records the fail-closed placeholder and the generated patch must run before synth/impl. This is not board proof of the real NC chain. |
| Axis placeholders | `pl_estop_axis_zero/dout` -> `pl_estop/step_in` and `pl_estop/enable_in` | The PL E-stop AXI module keeps internal status placeholders. Real `PULS/DIR/ENA` hard gating for the current handoff is implemented in `system_top.v`; `ENA1-8` normal owner is `z20_v15_io_owner_axi_lite`. |
| General output placeholder | `pl_estop_do_zero/dout` -> `pl_estop/general_output_in` | The PL E-stop AXI module keeps a 16-bit zero status placeholder. Real `DO1-14/PWM1-2` normal owner is `z20_v15_io_owner_axi_lite`, and the actual hard shutdown path is the top-level `pl_estop_general_output_gate` after that owner. |
| Bus TX status placeholder | `pl_estop_tx_zero/dout` -> `pl_estop/bus_tx_enable_in`; `pl_estop_tx_flushed/dout` -> `pl_estop/bus_tx_queue_flushed_in` | Provides AXI status/reset-interlock placeholders inside `pl_estop`. The actual local hardware TX block for this XSA is the generated-synth PS GEM1/EMIO GMII gate before `gmii2rgmii`, verified by `scripts/verify_pl_estop_bus_gate_owner.ps1`. PS-side queue flush and stale-frame behavior still require BV12 board evidence before any safety claim. |
| IRQ | `pl_estop/estop_irq` -> `xlconcat_0/In14` | PS IRQ path is wired for future software observation. |
| Outputs | `pl_estop/step_out`, `pl_estop/enable_out`, `pl_estop/brake_z_out`, `pl_estop/general_output_out`, `pl_estop/bus_tx_enable_out`, and `pl_estop/bus_tx_queue_flush_req` unconnected to external pins or real bus owners | No real STO/brake/axis/general/bus transmit output is controlled until wiring, signal owner, and safe polarity or idle state are confirmed. |

Local Vivado checks run:

```powershell
vivado.bat -mode batch -source scripts/vivado/add_pl_estop_axi_lite.tcl
vivado.bat -mode batch -source scripts/vivado/add_pl_estop_axi_lite.tcl
vivado.bat -mode batch -source scripts/vivado_validate_current.tcl
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\patch_gmii_pre_oddr_estop_gate.ps1 -CheckOnly
vivado.bat -mode batch -source scripts/vivado_synth_current.tcl
```

Observed result:

- The add script was run twice and remained idempotent: no duplicate `pl_estop` cell, no duplicate `pl_estop_axis_zero`, no duplicate AXI/IRQ connection.
- The current add script uses BD module reference `pl_estop_axi_lite_v3`, keeps cell name `pl_estop`, connects 16-bit `pl_estop_do_zero/dout` to `pl_estop/general_output_in`, connects `pl_estop_tx_zero/dout` to `pl_estop/bus_tx_enable_in`, and connects `pl_estop_tx_flushed/dout` to `pl_estop/bus_tx_queue_flushed_in`.
- `scripts/patch_gmii_pre_oddr_estop_gate.ps1` is the repeatable generated-file patch for `system_wrapper.v` and BD synth `system.v`; it is checkable with `-CheckOnly` and is called by `scripts/vivado_synth_current.tcl` and `scripts/vivado_impl_current.tcl` before Vivado runs.
- `validate_bd_design` completed for the current BD.
- Current top synthesis completed with `SYNTH_STATUS:synth_design Complete!`.
- DVI reset async timing exceptions were completed for the current hierarchy; implementation routed timing is now `TIMING_STATUS=timing_met`, `TIMING_WNS=0.081`, `TIMING_WHS=0.041` in the `2026-06-30 10:31:52` timing-history row.
- Bitstream generation succeeds for the current top. Routed DRC has no `NSTD-1` or `UCIO-1` errors and only the existing `RTSTAT-10` no-routable-load warning.
- `board_inputs/manifest.json` records the current bitstream, XSA, active XDC, project file, wiring evidence CSV, board-validation evidence CSV, hardware evidence request, field evidence packet, evidence record templates, routed DRC report, and timing history with relative paths and SHA256 hashes. It also records the latest active XDC, project-independence, project-portability, one-channel XADC ADC mapping, `z20_v15_io_owner_axi_lite` simulation, PL E-stop safety-boundary, PL E-stop wiring-evidence, PL E-stop board-validation, PL E-stop field-packet, PL E-stop evidence-template, and PL E-stop readiness checks, and keeps `board_closure_state=local_verified_only`; `scripts/verify_board_input_manifest.ps1` verifies the recorded artifact sizes and hashes still match the current local files.
- `scripts/verify_new_vivado_local_closure.ps1` is the one-command local handoff gate. It verifies the current local build stays movable, independent from the old Vivado project, ADC_IN1 assigned to XADC VP/VN on `L11/M12`, ADC SPI board-level pins retired, DRC closed, active pin conflicts closed, `z20_v15_io_owner_axi_lite` owns ENA/input/DO/PWM/touch status locally, RS485 and TP_INT/RST are exported, field-intake/readiness still honestly `not_ready`, and board-input manifest hashes still match. Current expected output is `new_vivado_local_closure=local_verified_only`.

This proves the internal AXI/IRQ/fail-closed BD integration, top-level DO/PWM forced-off gate, GMII pre-ODDR TX send gate, wiring-evidence gate, board-validation evidence gate, field evidence packet, and board-input artifact identity can be opened, validated, synthesized, implemented, exported, and recorded locally. It does not prove the physical E-stop hardware path because the NC chain, STO/brake outputs, axis gate points, DO/PWM load/off polarity, and RGMII TX Link/RX/release behavior are still unmeasured on the board.

## Candidate E-Stop / Safety Inputs

`EGS_DI/AA19` is the current active top-level E-stop input for the local hard gate because the user selected it for this implementation. It is still not board-verified until the schematic, wiring, and field test identify it as the NC safety chain and prove the expected high-healthy/low-fault polarity.

| v1.5 net | Pin | XDC evidence | Status | Action |
| --- | --- | --- | --- | --- |
| DI1 | V13 | DI1: source CN4-9, core CN4-9 B33_L20_P, package V13 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI2 | W13 | DI2: source CN4-10, core CN4-10 B33_L20_N, package W13 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI3 | W17 | DI3: source CN4-11, core CN4-11 B33_L13_P, package W17 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI4 | W18 | DI4: source CN4-12, core CN4-12 B33_L13_N, package W18 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI5 | V15 | DI5: source CN4-14, core CN4-14 B33_L19_N, package V15 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI6 | V14 | DI6: source CN4-15, core CN4-15 B33_L19_P, package V14 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI7 | U15 | DI7: source CN4-16, core CN4-16 B33_L15_P, package U15 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI8 | U16 | DI8: source CN4-17, core CN4-17 B33_L15_N, package U16 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI9 | U21 | DI9: source CN4-18, core CN4-18 B33_L1_N, package U21 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI10 | T21 | DI10: source CN4-19, core CN4-19 B33_L1_P, package T21 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI11 | V18 | DI11: source CN4-21, core CN4-21 B33_L6_P, package V18 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI12 | V19 | DI12: source CN4-22, core CN4-22 B33_L6_N, package V19 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI13 | Y18 | DI13: source CN4-23, core CN4-23 B33_L12_P, package Y18 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI14 | AA18 | DI14: source CN4-24, core CN4-24 B33_L12_N, package AA18 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI15 | Y13 | DI15: source CN4-25, core CN4-25 B33_L23_P, package Y13 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI16 | AA13 | DI16: source CN4-26, core CN4-26 B33_L23_N, package AA13 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI17 | Y21 | DI17: source CN4-28, core CN4-28 B33_L9_N, package Y21 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| DI18 | Y20 | DI18: source CN4-29, core CN4-29 B33_L9_P, package Y20 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| EGS_DI | AA19 | EGS_DI: source CN4-56, core CN4-56 B33_L11_N, package AA19 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| FR_DI1 | W15 | FR_DI1: source CN4-30, core CN4-30 B33_L21_P, package W15 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| FR_DI2 | Y15 | FR_DI2: source CN4-31, core CN4-31 B33_L21_N, package Y15 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| FR_DI3 | AA14 | FR_DI3: source CN4-72, core CN4-72 B33_L22_N, package AA14 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| FR_DI4 | Y14 | FR_DI4: source CN4-71, core CN4-71 B33_L22_P, package Y14 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| FR_DI5 | W16 | FR_DI5: source CN4-70, core CN4-70 B33_L14_P, package W16 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| FR_DI6 | Y16 | FR_DI6: source CN4-69, core CN4-69 B33_L14_N, package Y16 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| FR_DI7 | V17 | FR_DI7: source CN4-67, core CN4-67 B33_L16_N, package V17 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| FR_DI8 | U17 | FR_DI8: source CN4-66, core CN4-66 B33_L16_P, package U17 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| FR_DI9 | AB14 | FR_DI9: source CN4-65, core CN4-65 B33_L24_P, package AB14 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| FR_DI10 | AB15 | FR_DI10: source CN4-64, core CN4-64 B33_L24_N, package AB15 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| FR_DI11 | AB16 | FR_DI11: source CN4-63, core CN4-63 B33_L18_N, package AB16 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| FR_DI12 | AA16 | FR_DI12: source CN4-62, core CN4-62 B33_L18_P, package AA16 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| FR_DI13 | AB17 | FR_DI13: source CN4-60, core CN4-60 B33_L17_N, package AB17 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| FR_DI14 | AA17 | FR_DI14: source CN4-59, core CN4-59 B33_L17_P, package AA17 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| FR_DI15 | AB19 | FR_DI15: source CN4-58, core CN4-58 B33_L10_P, package AB19 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| FR_DI16 | AB20 | FR_DI16: source CN4-57, core CN4-57 B33_L10_N, package AB20 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |
| TS_DI | Y19 | TS_DI: source CN4-55, core CN4-55 B33_L11_P, package Y19 | candidate only | Confirm actual safety-chain wiring and NC polarity before active use. |

## Candidate Drive, STO, Brake, And Control Outputs

No dedicated `STO` or `BRAKE` net appears in `../../z20-v1_5_20260623.xdc`. The following outputs can only become STO/brake/control outputs after schematic and drive wiring confirmation.

User requirement recorded on 2026-06-29: all general outputs in this group, specifically `DO1` - `DO14` and `PWM1` - `PWM2`, must be shut off by the PL E-stop path. The close condition for these outputs is therefore stronger than ordinary pin promotion: each channel must have a confirmed off polarity, a PL gate point after any normal-output generator, simulation coverage that E-stop forces the off state, and board evidence before it may be called safe.

Implementation rule: the generic `pl_estop_general_output_gate` exists as a 16-channel local top-level gate for `DO1` - `DO14` and `PWM1` - `PWM2`. These outputs must not bypass this gate. The current normal-output owner is `z20_v15_io_owner_axi_lite`: `do_o[13:0]` supplies `DO1-14` and its two PWM generators supply `PWM1-2`; the gate output remains the only path to the package pins. Each channel still needs confirmed off polarity, maintained status/software alignment, simulation updates if polarity differs from zero, and bench/board verification.

| v1.5 net | Pin | XDC evidence | Status | Action |
| --- | --- | --- | --- | --- |
| BEEP_EN | H18 | BEEP_EN: source CN3-16, core CN2-16 B35_IO_25, package H18 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |
| DO1 | A21 | DO1: source CN3-4, core CN2-4 B35_L15_P, package A21 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |
| DO2 | A22 | DO2: source CN3-5, core CN2-5 B35_L15_N, package A22 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |
| DO3 | B21 | DO3: source CN3-6, core CN2-6 B35_L18_P, package B21 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |
| DO4 | B22 | DO4: source CN3-7, core CN2-7 B35_L18_N, package B22 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |
| DO5 | C22 | DO5: source CN3-9, core CN2-9 B35_L16_N, package C22 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |
| DO6 | D22 | DO6: source CN3-10, core CN2-10 B35_L16_P, package D22 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |
| DO7 | F21 | DO7: source CN3-11, core CN2-11 B35_L23_P, package F21 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |
| DO8 | F22 | DO8: source CN3-12, core CN2-12 B35_L23_N, package F22 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |
| DO9 | F19 | DO9: source CN3-72, core CN2-72 B35_L20_N, package F19 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |
| DO10 | G19 | DO10: source CN3-71, core CN2-71 B35_L20_P, package G19 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |
| DO11 | F17 | DO11: source CN3-70, core CN2-70 B35_L6_N, package F17 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |
| DO12 | G17 | DO12: source CN3-69, core CN2-69 B35_L6_P, package G17 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |
| DO13 | G21 | DO13: source CN3-68, core CN2-68 B35_L22_N, package G21 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |
| DO14 | G20 | DO14: source CN3-67, core CN2-67 B35_L22_P, package G20 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |
| PWM1 | G22 | PWM1: source CN3-13, core CN2-13 B35_L24_N, package G22 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |
| PWM2 | H22 | PWM2: source CN3-14, core CN2-14 B35_L24_P, package H22 | candidate only | Confirm load wiring; possible brake/STO/control output only after schematic review. |

## Bus TX / Driver Gate Requirement

User requirement recorded on 2026-06-29: PL-connected physical E-stop should also stop bus drive/control transmission while allowing fast recovery after release. The acceptable implementation is to gate only the bus TX send enable or driver-enable path in PL, not to break the physical link.

Required behavior for the later implementation:

- On `estop_latched`, force the selected bus TX output into its documented idle or disabled-drive state in hardware, without waiting for PS/LinuxCNC/UI.
- Keep physical link ownership alive: do not reset the PHY/transceiver, disable link clock, remove link power, or disable RX/status observation as the E-stop action.
- Keep the local hard safety path separate: this bus TX gate does not replace step/STO/enable/brake or DO/PWM forced-off gating.
- On release, require the same physical-input-healthy plus software-reset interlock as the rest of PL E-stop.
- Before re-enabling TX, flush or invalidate any queued TX FIFO entry, buffered command, or control frame that accumulated while E-stop was latched. Stale motion/control traffic must not be replayed after release.

The exact signal must be chosen per bus owner, for example TX enable/driver-enable, CAN TX idle gating, RS485 DE/TX gating, or Ethernet/RGMII TX enable/TX_CTL idle gating. Link, clock, reset, MDIO/configuration, and RX paths are not the gate target.

## Axis Output / Feedback Candidates

Current Vivado boundary:

| Signal group | Current connection | PL E-stop status | Remaining owner gap |
| --- | --- | --- | --- |
| `PULS1-8` | wrapper `axis_puls_o[7:0]` drives top `PULS*_IO` | Top-level E-stop gate forces safe low when `estop_hw_active` is true | Motion software/register owner and axis polarity still need review. |
| `DIR1-8` | wrapper `axis_dir_o[7:0]` drives top `DIR*_IO` | Top-level E-stop gate forces safe low when `estop_hw_active` is true | Motion software/register owner and axis polarity still need review. |
| `ENA1-8` | wrapper `axis_ena_o[7:0]` drives top `ENA*_IO` | Wrapper source is `z20_v15_io_owner_axi_lite`; top E-stop gate forces safe low when `estop_hw_active` is true | Enable polarity and board behavior are not board-verified. |
| `A/B/Z1-8` | top `A*_IO/B*_IO/Z*_IO` feed wrapper `axis_enc_*_i[7:0]` and BD `step_ip` | Inputs are not forced by E-stop; they are feedback inputs | Software/register readout, axis numbering, direction, and validation still need review. |
| `ALM1-8` | top-level inputs feed `z20_v15_io_owner_axi_lite` synchronizers/status registers | Not an E-stop output path | Alarm polarity and board behavior are not board-verified. |

Do not add a second axis wrapper or restore retired axis buses. Future work must extend the current 8-axis DB15 boundary and keep every real output path upstream of the PL E-stop gate.

## Implementation Boundary

- `pl_estop_filter`: NC input synchronizer and debounce/integrator.
- `pl_estop_latch`: latched state, physical-low override, software reset acceptance.
- `pl_estop_gate`: step/STO/enable output gating.
- `pl_estop_general_output_gate`: DO/PWM output gating for `DO1` - `DO14` and `PWM1` - `PWM2`; on `estop_latched`, each output must be forced to its documented off polarity before leaving PL.
- `pl_estop_bus_tx_gate`: bus transmit/driver-enable gate; on `estop_latched`, force TX idle or driver disabled while preserving PHY/transceiver link, clock, reset, and RX observation.
- `pl_estop_brake_timer`: vertical-axis brake lead timing.
- `pl_estop_axi`: AXI status/control bits and optional IRQ source.

Do not implement this as ad hoc wrapper-only combinational logic. If it lands in `axi_stepdir_enc_v2`, keep the E-stop sub-blocks separable and documented.

## Proposed Register Contract

| Register bit | Direction | Meaning | Required behavior |
| --- | --- | --- | --- |
| `STATUS.estop_latched` | PL to PS | PL has latched E-stop | Set by filtered low input; cleared only by allowed reset |
| `STATUS.estop_input_raw` | PL to PS | Synchronized physical input | Low means physical chain is not healthy |
| `STATUS.estop_input_filtered` | PL to PS | Debounced physical input | Low after 10ms - 20ms low window |
| `STATUS.reset_allowed` | PL to PS | Physical input is healthy and reset can be accepted | False while input is low, brake delay is active, or bus TX queue-flushed input is false |
| `STATUS.brake_delay_active` | PL to PS | Vertical-axis brake lead timer running | True between brake action and drive cutoff |
| `CONTROL.reset_request` | PS to PL | Software asks to clear latch | Ignored while physical input remains low |
| `CONTROL.irq_clear` | PS to PL | Clear IRQ indication | Must not clear `estop_latched` |
| `STATUS.general_output_forced_off` | PL to PS | DO/PWM E-stop gate is actively forcing off state | Implemented as `STATUS[6]` in the AXI module; generated synth now observes top `estop_nc_in`, but board/software readback remains open before closure |
| `STATUS.bus_tx_gate_active` | PL to PS | Bus TX send enable or driver-enable is being held idle/off by E-stop | Implemented as `STATUS[7]` in the AXI module; generated synth now observes top `estop_nc_in`, but board/software readback and real bus owner evidence remain open before closure |
| `STATUS.bus_tx_queue_flushed` | PL to PS | Buffered TX/control entries from the latched interval have been flushed or invalidated | Implemented as `STATUS[8]`; currently tied true by BD placeholder, and must be driven by the real bus queue/FIFO owner before any real TX re-enable path is connected |

## Next Required Actions

Vivado flow current status:

1. Current active-top DRC is closed for this local build: `unassigned_top_ports_count=0`, `csv_rows=0`, `active_pin_conflicts=0`, `project_independence=ok`, `project_portability=ok`, `adc_mapping=ok`, `pl_estop_safety_boundary=ok`, `pl_estop_wiring_evidence=not_ready`, `pl_estop_board_validation=not_ready`, `board_verified_evidence_contract=md_non_placeholder`, `board_verified_attachment_contract=project_relative_existing_files`, `pl_estop_field_packet_verify=ok`, `pl_estop_evidence_templates_verify=ok`, `board_verified_template_records=0`, `pl_estop_evidence_root_verify=ok`, `orphan_board_verified_records=0`, `pl_estop_readiness=not_ready`, `new_vivado_local_closure=local_verified_only`, routed timing is met, bitstream is generated, `board_inputs/system.xsa` has been exported with the fresh bit, and `board_inputs/manifest.json` records the current board-input artifact identity with hash verification available through `scripts/verify_board_input_manifest.ps1`.

Safety-chain next actions before any real output connection:

1. Confirm whether `EGS_DI` is the physical NC emergency-stop chain.
2. Confirm whether STO is wired to a dedicated output, one of `DO1` - `DO14`, `PWM1/PWM2`, or axis `ENA*_IO`.
3. Confirm the Z-axis or vertical-axis brake output, polarity, and required lead time.
4. Confirm every general output off polarity for `DO1` - `DO14` and `PWM1` - `PWM2`, then connect each confirmed channel through the existing PL-side gate point so E-stop forces those outputs off independent of PS/software.
5. Identify the bus path that must be inhibited by physical E-stop, then choose the exact TX send enable, driver-enable, TX idle, or TX_CTL gate point. The implementation must preserve link/clock/reset/RX and must define how queued TX entries are flushed.
6. Keep the current 8-axis DB15 boundary and decide any remaining enable/STO/brake gate points before inserting new output owners.
7. Only after 1 - 6 are closed, promote confirmed E-stop/STO/brake/DO/PWM/bus-gate nets into active XDC and RTL.

## Verification Required Before Any Pass Claim

- Simulation or focused hardware test for NC low trigger, cable-break low trigger, debounce window, latch behavior, and reset rejection while low.
- Vivado synthesis/implementation with no new timing or DRC blockers.
- Board test with oscilloscope measurement from physical input transition to pulse/STO block.
- Board or bench test that every confirmed `DO1` - `DO14` and `PWM1` - `PWM2` output reaches its documented off state when E-stop latches.
- Board or bench test that the selected bus TX enable/driver-enable reaches idle/off when E-stop latches, the physical link remains up, RX/status remains observable, release does not require link renegotiation, and no queued stale TX/control frame is replayed after reset.
- Cable-break simulation test.
- EMC/noise or forced bounce test for debounce behavior.

Until those checks run, the PL E-stop work remains `source_only` or `local_verified_only` according to the actual evidence.
