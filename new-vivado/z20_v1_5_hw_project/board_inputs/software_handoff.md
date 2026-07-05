# Board Software Handoff

This file is the board-software-facing entry point for the Vivado hardware handoff. It stays inside `new-vivado/z20_v1_5_hw_project/board_inputs/` and uses project-relative paths only.

## Use These Inputs

- `board_inputs/system.xsa`: hardware platform handoff; includes the current bitstream.
- `board_inputs/manifest.json`: machine-readable artifact identity, hashes, local gate output, and safety boundary state.
- `board_inputs/README.md`: generated board-input summary for humans.
- `board_inputs/z20_v1_5_hardware_abi.h`: generated C driver contract header from `board_inputs/system.xsa` and the Markdown register maps.
- `board_inputs/z20_v1_5_axi_lite.dtsi`: generated AXI-Lite DTSI fragment for the custom PL IP nodes.
- `board_inputs/v3_hardware_abi.h`: compatibility ABI header generated from the same sources.
- `board_inputs/pl_estop_register_map.md`: PL E-stop AXI register map for board software.
- `docs/io_owner_register_map.md`: Z20 v1.5 IO owner AXI register map for DI, FR_DI, MPG, ALM, DO, PWM, ENA, TP_INT, and TP_RST.
- `constraints/z20_v1_5_active_mapped.xdc`: active mapped constraints used by the current Vivado project.
- `artifacts/vivado/timing_history.csv`: timing history used by the manifest.

## Required Local Checks

Run these from `new-vivado/z20_v1_5_hw_project` before consuming the XSA:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify_board_input_manifest.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify_driver_contract.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify_hardware_abi_header.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify_new_vivado_local_closure.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify_project_portability.ps1
```

Required local state:

- `hashes=ok`
- `driver_contract=ok`
- `hardware_abi_header=ok`
- `new_vivado_local_closure=local_verified_only`
- `board_closure_state=local_verified_only`
- `project_portability=ok`
- `absolute_path_scan=ok`

This state is a Vivado local handoff only. It does not prove board safety behavior.

## Generated Driver Contract

- Generated C header: `board_inputs/z20_v1_5_hardware_abi.h`
- Generated DTSI fragment: `board_inputs/z20_v1_5_axi_lite.dtsi`
- Generator: `scripts/generate_driver_contract.py`
- Verification: `scripts/verify_driver_contract.ps1`
- Source XSA: `board_inputs/system.xsa`
- Source register maps: `ip_repo/axi_stepdir_enc_v2/step_ip_register_map.md`, `board_inputs/pl_estop_register_map.md`, and `docs/io_owner_register_map.md`

C-side driver or HAL glue should include `board_inputs/z20_v1_5_hardware_abi.h` for AXI base addresses, register offsets, and identity constants. Copy `board_inputs/z20_v1_5_axi_lite.dtsi` into the Linux device-tree overlay or `system-user.dtsi`. Do not hand-copy AXI addresses or offsets from this handoff text into driver code.

## Hardware Platform Snapshot

- Timing hard gate: final timing must be `timing_met`; WNS/WHS must be non-negative and TNS/THS must be zero.
- WNS `0.100ns` is an advisory margin target only, not a hard failure gate.
- Current timing snapshot is recorded in `board_inputs/manifest.json`.
- Active constraints are mapped-only. The source truth XDC and old project XDC must not be loaded by Vivado.

## ADC Interface

- ADC owner: dedicated XADC VP/VN analog input, one differential channel.
- `ADC_IN1` maps to `XADC_VP`/`XADC_VN` through the external analog front end.
- `ADC_IN2` is not assigned in this hardware revision.
- XADC dedicated analog package pins are `L11/M12`; they are not normal PL `PACKAGE_PIN` constraints in active XDC.
- ADC SPI through MCP3202 is retired for the ADC function.
- `U10`, `U9`, `AA12`, and `AB12` are restored in the source XDC as spare `FPGA1_IO1/2` nets and remain inactive in active XDC.

Before ADC software work, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify_adc_spi_mapping.ps1
```

Required ADC state:

- `adc_mapping=ok`
- `adc_owner=XADC_VP_VN_ONE_CHANNEL`
- `adc_xadc_pins=L11,M12`
- `adc_spi_mapping=retired`
- `active_adc_spi_assignments=0`

## PL E-Stop Software Interface

- PL E-stop AXI-Lite block: `pl_estop/S_AXI`
- AXI base/range: `0x41260000`, `64K`
- AXI interconnect: `ps7_0_axi_periph/M21_AXI`
- Interrupt: `pl_estop/estop_irq` -> `xlconcat_0/In14`
- Register map: `board_inputs/pl_estop_register_map.md`
- Current board closure state: `local_verified_only`
- Current readiness state: `pl_estop_readiness=not_ready`
- Current timing constants: `CLK_HZ=100000000`, `DEBOUNCE_MS=10`, `BRAKE_LEAD_US=50`
- Current derived counts: `debounce_cycles=1000000`, `brake_cycles=5000`
- Current axis config: `AXIS_COUNT=8`, `Z_AXIS_INDEX=2`

Current Vivado-only axis and IO boundary: `PULS1-8/DIR1-8` are driven from the wrapper 8-axis step/dir outputs through the top E-stop gate, and all eight encoder A/B/Z inputs enter the wrapper 8-axis encoder inputs. `ENA1-8`, `ALM1-8`, `DI1-18`, `FR_DI1-16`, `TS_DI`, MPG, SCALE, `TP_INT`, `TP_RST`, and normal `DO1-14`/`PWM1-2` control are owned by `z20_v15_io_owner_axi_lite` at AXI base `0x41270000`; `DO/PWM` and `ENA` still pass through the top PL E-stop gate before leaving the FPGA. `RS485_FPGA_RX/TX` are exported through PS UART1 EMIO. This is local Vivado code closure only, not board IO validation.

The board software may consume the AXI status/IRQ as an observation and alarm input. The current plan is code-review-only, so it must not treat the local XSA or bitstream as board-verified safety proof.

Before PL E-stop software work, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify_pl_estop_register_map.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify_pl_estop_timing_params.ps1
```

Required register-map state:

- `pl_estop_register_map=ok`
- `registers=9`
- `status_bits=9`
- `control_bits=2`
- `pl_estop_timing_params=ok`
- `bd_clock_net=processing_system7_0_FCLK_CLK0`

## Z20 v1.5 IO Owner Software Interface

- IO owner AXI-Lite block: `z20_v15_io_owner/S_AXI`
- AXI base/range: `0x41270000`, `64K`
- Register map: `docs/io_owner_register_map.md`
- `MAGIC`: `0x494F4F57`
- `VERSION`: `0x00010000`
- `BUILD_ID`: `0x20260701`

The IO owner provides synchronized status for `DI1-18`, `FR_DI1-16`, `TS_DI`, MPG, SCALE, `ALM1-8`, and `TP_INT`, and normal software ownership for `DO1-14`, `PWM1-2`, `ENA1-8`, and `TP_RST`. `DO/PWM` and `ENA` still pass through the top PL E-stop gate before leaving the FPGA. Board software must not bypass `docs/io_owner_register_map.md` or infer board-level polarity/load behavior from this local register map alone.

## PL E-Stop Safety Boundaries

- Physical E-stop input is promoted to the active XDC top input and drives local PL hard gates.
- DO/PWM outputs now have a normal Vivado owner (`z20_v15_io_owner_axi_lite`) before the PL E-stop gate, and are locally hard-gated off by PL E-stop logic; board validation is not run.
- `ENA1-8` now have a Vivado register owner before the top E-stop gate; enable polarity and board behavior are not board-validated.
- `DI/FR_DI/TS_DI/MPG/SCALE/ALM/TP_INT` now enter IO-owner input synchronizers/status registers; board input validation is not run.
- `TP_RST` is driven by the IO owner touch reset register and defaults released high; board touch validation is not run.
- `RS485_FPGA_RX/TX` are exported from PS UART1 EMIO; board serial validation is not run.
- Bus TX is locally gated before `gmii2rgmii` on the PS GEM1/EMIO GMII TX path.
- RGMII Link, RX, MDIO, and clock are preserved by design; the gate blocks TX send enable/data instead of breaking physical Link.
- STO, brake, and axis safety outputs are not connected as real outputs in the current handoff.
- Board deploy and board safety validation are not run.

Physical measurement and board safety claims are outside the current plan. For code review only, keep these gates as reference checks rather than completion blockers:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify_pl_estop_field_intake.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify_pl_estop_readiness.ps1
```

## Field Evidence Inputs

Use only project-relative evidence under `docs/evidence/pl_estop/`.

- `docs/pl_estop_hardware_evidence_request.md`: field checklist.
- `docs/pl_estop_field_packet.md`: exact CSV fields and suggested evidence file names.
- `docs/pl_estop_field_execution_runbook.md`: step-by-step field sequence and stop conditions.
- `docs/pl_estop_evidence_record_templates.md`: templates for real evidence records.
- `docs/evidence/pl_estop/README.md`: allowed evidence root policy.

Do not reference generated templates themselves as measured evidence. If a later scope restores board measurement, real board evidence rows must point to real non-placeholder `.md` evidence records and project-relative attachments under `docs/evidence/pl_estop/`.

## Do Not Do This In Board Software

- Do not use absolute paths to consume artifacts.
- Do not depend on old Vivado project files.
- Do not add normal PL `PACKAGE_PIN` constraints for `XADC_VP` or `XADC_VN`.
- Do not reintroduce `U10`, `U9`, `AA12`, or `AB12` as ADC SPI pins.
- Do not bypass `board_inputs/manifest.json` hash validation.
- Do not report PL E-stop, DO/PWM shutdown, bus TX gate, or Link-preserved recovery as board-verified from this code-review-only handoff.
