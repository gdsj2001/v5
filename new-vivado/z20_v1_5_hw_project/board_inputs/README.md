# Z20 v1.5 Board Input Handoff

This generated handoff is for the local Vivado board-input artifacts in this directory. It is not board safety proof.

## Current State

- Board closure state: `local_verified_only`
- Active XDC traceability: `ok`
- Active XDC traced assignments: `180`
- Active XDC electrical contract: `ok`
- Active XDC IOSTANDARD assignments: `180`
- Active XDC LVCMOS33 assignments: `180`
- Vivado/XSA cleanliness: `ok`
- Active constraints loaded: `mapped_only`
- Truth/source XDC loaded by Vivado: `no`
- Old project XDC loaded by Vivado: `no`
- DRC blocking rules: `0`
- DRC allowed warning rules: `RTSTAT-10`
- Hardware ABI header: `ok`
- Hardware ABI header output: `board_inputs/v3_hardware_abi.h`
- Hardware ABI address anchors: step_ip `0x43CB0000` / pl_estop `0x41260000` / io_owner `0x41270000`
- Driver contract: `ok`
- Driver contract header: `board_inputs/z20_v1_5_hardware_abi.h`
- Driver contract DTSI: `board_inputs/z20_v1_5_axi_lite.dtsi`
- PL E-stop DTS interrupt: IRQ_F2P[`14`] / GIC SPI cell `43`
- Vivado warning summary: `classified`
- Vivado warning summary verify: `ok`
- Vivado warning lines: `457`
- Vivado warning codes: `17`
- Constraint/source warning lines: `0`
- Retired HDMI warning lines: `0`
- Project portability: `ok`
- Absolute path scan: `ok`
- Manifest relative paths: `ok`
- ADC mapping: `ok`
- ADC owner: `XADC_VP_VN_ONE_CHANNEL`
- ADC XADC pins: `L11,M12`
- ADC SPI mapping: `retired`
- Legacy axis/ADC external boundary: `retired`
- Wrapper axis boundary: `current_8bit`
- Axis functional completion: `vivado_io_owner_connected`
- Axis motion owner: `step_ip_8axis_stepdir_encoder_direct`
- Axis ENA owner: `z20_v15_io_owner_axi_lite`
- Axis 7/8 encoder processing: `connected_to_step_ip`
- DI/MPG/ALM processing: `z20_v15_io_owner_input_registers`
- DO/PWM normal owner: `z20_v15_io_owner_do_pwm`
- RS485 boundary: `exported_ps_uart1_emio`
- Touch INT/RST boundary: `z20_v15_io_owner_tp_int_rst`
- Z20 v1.5 IO owner simulation: `ok`
- PL E-stop simulation: `ok`
- PL E-stop core testbench: `pass`
- PL E-stop AXI testbench: `pass`
- PL E-stop timing params: `ok`
- PL E-stop timing clock/debounce/brake: `100000000` / `10` ms / `50` us
- PL E-stop derived cycles: `1000000` debounce / `5000` brake
- PL E-stop axis config: `8` axes / Z index `2`
- PL E-stop readiness: `not_ready`
- Readiness blockers: `pl_estop_wiring_evidence_not_ready,pl_estop_board_evidence_not_ready,pl_estop_board_validation_not_ready`
- Hardware evidence request verify: `ok`
- Hardware evidence request state: `open`
- Field evidence packet verify: `ok`
- Field evidence packet state: `open`
- Field execution runbook verify: `ok`
- Field execution runbook state: `open`
- Evidence record templates verify: `ok`
- Evidence record templates state: `open`
- Evidence root verify: `ok`
- Orphan board-verified records: `0`
- Field intake gate: `not_ready`
- Field intake structural contract: `ok`
- Field intake blockers: `wiring_not_ready_for_real_pins,wiring_board_evidence_not_ready,board_validation_not_ready`
- Real pin promotion gate: `local_hard_gate_promoted`
- Output shutdown contract: `code_review_only`
- Output shutdown DO/PWM rows: `16`
- Output shutdown bus TX rows: `2`
- Bus gate owner: `ps_gem1_emio_rgmii_local_verified`
- Bus gate transport: `EtherCAT over PS GEM1/EMIO`
- Bus gate board evidence: `pending`
- PL E-stop register map: `ok`
- PL E-stop register count: `9`
- PL E-stop status/control bits: `9` / `2`
- Active promoted wiring assignments: `17`
- Active promoted DO/PWM assignments: `16`
- Active promoted bus TX assignments: `0`
- Promotion requires E11: `no`
- PL E-stop safety boundary: `ok`
- Active pending safety-pin assignments: `0`

## Timing Snapshot

- Timestamp: `2026-07-01 10:04:04`
- Build status: `bitstream_generated`
- Timing status: `timing_met`
- WNS/WHS: `0.193` / `0.034`
- Bit file: `z20_v1_5_hw_project.runs/impl_1/system_top.bit`

## Artifacts

| Item | Project-relative path | Bytes | SHA256 |
| --- | --- | ---: | --- |
| XSA with bitstream | `board_inputs/system.xsa` | 1144361 | `61ce7bd06c67d6f1611f72a855c761133340df6efd253b617d87364fceeb95cc` |
| Board software handoff | `board_inputs/software_handoff.md` | 9456 | `7b3abe12524da5f03cab8884a8afa645ce0ff48c7e644a2141537fa18820f84f` |
| Driver contract C header | `board_inputs/z20_v1_5_hardware_abi.h` | 10338 | `135cdd2a4359dd096ea53121eadbf2f5dcff8ac45a4c0b94e08d2427a2117ca6` |
| Driver contract DTSI | `board_inputs/z20_v1_5_axi_lite.dtsi` | 938 | `aff297499a5657b2df3c6690306d2fa6a38ef1c565b1338ad25550fdb8402d2f` |
| Compatibility C hardware ABI header | `board_inputs/v3_hardware_abi.h` | 10306 | `fbd565074753f4622667ddc09f9bd5f245d6199cab3c7b76a12b65c5da32776f` |
| PL E-stop register map | `board_inputs/pl_estop_register_map.md` | 4832 | `d5be4b333537df188f0f3404a0605b5b112f8bd0229b5637b8597ec97ee4dd68` |
| Z20 v1.5 IO owner register map | `docs/io_owner_register_map.md` | 6931 | `dcabbf54e645d503f13f51bf974550baf1151b96060ee500a2368fe16a3964f2` |
| Bitstream | `z20_v1_5_hw_project.runs/impl_1/system_top.bit` | 4045670 | `cf84bf001dac2a3ee92ea79f316e4b9e02f46dd57835d0a3cc25d150138914d6` |
| Active XDC | `constraints/z20_v1_5_active_mapped.xdc` | 38143 | `83174b9f49eb21906b644dadaed4f5abfa82c64499359e99447b871e3141bd94` |
| Hardware evidence request | `docs/pl_estop_hardware_evidence_request.md` | 7433 | `c6e10e13878b2a33bdcb84ac98aa3d625d462afa41b7cae3839f53cbc4713497` |
| Field evidence packet | `docs/pl_estop_field_packet.md` | 15905 | `af5b0ad01a63cc5c7f979caaf4e0d5d20bd893444c3450ca14ee4307e286bc93` |
| Field execution runbook | `docs/pl_estop_field_execution_runbook.md` | 5044 | `23005c987e0ffbc124f379dc1c71416844391ac8000a1572b322267ef67b968b` |
| Evidence record templates | `docs/pl_estop_evidence_record_templates.md` | 11090 | `28a75ffa9eba4b8d8c9056dbc724971dcb4eb9f4cf3bcb4a6706f4e6d9ce21f1` |
| Vivado warning summary CSV | `docs/vivado_warning_summary.csv` | 8566 | `85ef4c6866908cbc2e818eca765fc726bcae6548db9b3a0b38d1a4a9d1f2ae76` |
| Vivado warning summary report | `docs/vivado_warning_summary.md` | 2955 | `0327cc96be8e31148c11daa0468db6afa87b99ca624d060376fd88005fc4b7ac` |
| Evidence gap report | `docs/pl_estop_evidence_gap.md` | 5103 | `32e665be02196ec4e4817774333d6883c47eddf8c72c5350ea33b6a95d91f0d7` |
| Evidence-file root policy | `docs/evidence/pl_estop/README.md` | 2116 | `9ca1468651b9b314be395e915fecae17ead958b3f9da35edc85057ea190095af` |

## Before Board Use

- Run `scripts/verify_board_input_manifest.ps1` from the project directory and require `hashes=ok`.
- Use `board_inputs/software_handoff.md` as the board-software-facing summary; it stays inside the Vivado project and contains only project-relative paths.
- Include `board_inputs/z20_v1_5_hardware_abi.h` from C-side driver or HAL code after `scripts/verify_driver_contract.ps1` passes; do not hand-copy AXI bases or register offsets.
- Copy `board_inputs/z20_v1_5_axi_lite.dtsi` into the Linux device-tree overlay or `system-user.dtsi` after `scripts/verify_driver_contract.ps1` passes.
- Keep `board_inputs/v3_hardware_abi.h` only as the compatibility ABI header generated from the same XSA/register-map source.
- Use `board_inputs/pl_estop_register_map.md` as the PL E-stop AXI register map; run `scripts/verify_pl_estop_register_map.ps1` before wiring board software to the block.
- Use `docs/io_owner_register_map.md` as the Z20 v1.5 IO owner AXI register map before wiring LinuxCNC/HAL to DI, FR_DI, MPG, ALM, DO, PWM, ENA, TP_INT, or TP_RST.
- Run `scripts/verify_pl_estop_timing_params.ps1` before changing PL E-stop debounce, brake, axis count, or AXI clock assumptions; current expected state is `pl_estop_timing_params=ok` at 100 MHz.
- Run `scripts/verify_new_vivado_local_closure.ps1` as the local handoff gate; require `new_vivado_local_closure=local_verified_only` under the current code-review-only scope.
- Run `scripts/verify_project_portability.ps1` before moving or handing off the folder; require `project_portability=ok` and `absolute_path_scan=ok`.
- Run `scripts/verify_adc_spi_mapping.ps1` before any ADC-related handoff; require `adc_mapping=ok`, `adc_xadc_pins=L11,M12`, `adc_spi_mapping=retired`, and `active_adc_spi_assignments=0`.
- Run `scripts/verify_no_legacy_axis_adc_boundary.ps1` before any wrapper regeneration handoff; require `legacy_axis_adc_boundary=retired`, `wrapper_axis_boundary=current_8bit`, `axis_functional_completion=vivado_io_owner_connected`, `axis_ena_owner=z20_v15_io_owner_axi_lite`, `do_pwm_normal_owner=z20_v15_io_owner_do_pwm`, `rs485_boundary=exported_ps_uart1_emio`, and `touch_int_rst_boundary=z20_v15_io_owner_tp_int_rst`.
- Run `scripts/verify_z20_v15_io_owner_sim.ps1` before treating DO/PWM/ENA/input/touch owner RTL as locally closed; require `z20_v15_io_owner_sim=ok`.
- Run `scripts/verify_vivado_warning_summary.ps1` before treating the XSA log state as clean; require `constraint_truth_warning_lines=0` and `unexpected_warning_codes=0`.
- Run `scripts/verify_pl_estop_real_pin_promotion_gate.ps1` before and after any real E-stop, STO, brake, DO/PWM, axis, or bus TX active-XDC promotion; current expected state is `local_hard_gate_promoted` for the E-stop input plus 16 DO/PWM outputs.
- Run `scripts/verify_pl_estop_bus_gate_owner.ps1` before board handoff; current expected state is `pl_estop_bus_gate_owner=ps_gem1_emio_rgmii_local_verified` and `board_evidence_state=pending` because physical measurement is outside this plan.
- Do not treat `system.xsa` or `system_top.bit` as board-verified safety behavior.
- Do not claim real E-stop, DO/PWM, or bus TX safety behavior from this handoff; the current plan is code review only and does not execute physical measurement.
- Do not promote remaining STO, brake, axis, or any additional bus TX owner in this plan; keep unreviewed external safety outputs out of scope.
- Treat `docs/pl_estop_hardware_evidence_request.md` as reference material only under the current code-review-only plan.
- Use `docs/pl_estop_field_packet.md` as the field intake packet for exact CSV fields and suggested evidence file names.
- Use `docs/pl_estop_field_execution_runbook.md` as the step-by-step field sequence before changing real RTL/XDC wiring.
- Use `docs/pl_estop_evidence_record_templates.md` to create real `.md` evidence records under `docs/evidence/pl_estop/`; do not reference the generated template file itself from CSV evidence fields.
- Store future PL E-stop bench and board proof files under `docs/evidence/pl_estop/`; verified CSV rows must reference files under that directory.
- Do not use `scripts/verify_pl_estop_field_intake.ps1` to promote physical measurement scope in this plan.

## Safety Boundaries

- Real E-stop input: active XDC top input, local hard-gate only, board validation not run.
- Axis interface: wrapper external boundary is current 8-bit axis naming; PULS/DIR/ABZ are owned by `step_ip`, and ENA1-8 are owned by `z20_v15_io_owner_axi_lite` before the top E-stop gate. Board motion validation is not run.
- DI/FR_DI/TS_DI/MPG/SCALE/ALM: top pins feed `z20_v15_io_owner_axi_lite` input synchronizers/status registers for code-review-level IO closure. Board input validation is not run.
- RS485: PS UART1 EMIO is exported to `RS485_FPGA_RX`/`RS485_FPGA_TX` and constrained from the v1.5 source XDC. Board serial validation is not run.
- TP_INT/TP_RST: `TP_INT` feeds the IO owner status path, and `TP_RST` is driven by the IO owner touch reset output defaulting released high. Board touch validation is not run.
- Real STO, brake, and axis outputs: not connected.
- Real DO/PWM outputs: active XDC top outputs, forced-off local build, board validation not run.
- Real bus TX or driver-enable gate: PS GEM1/EMIO GMII TX_EN/TX_ER/TXD is locally gated before gmii2rgmii; RGMII Link/RX/MDIO/clock are preserved by design, board validation not run.
- Board deploy: not run.
- Board safety validation: not run.
