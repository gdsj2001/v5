# PL E-Stop Field Execution Runbook

This runbook is the next-action guide for collecting real PL E-stop wiring and board evidence. It is not measurement evidence and must not be referenced from CSV evidence fields.

## Entry State

- Current board closure state: `local_verified_only`.
- Current PL E-stop readiness: `not_ready`.
- Current real safety wiring state: `EGS_DI`, `DO1` - `DO14`, and `PWM1` - `PWM2` are connected to active external pins through local top-level hard gates; RGMII TX send is locally gated in `system_top`. STO, brake, axis gate, DO/PWM load/off-polarity proof, RGMII TX Link/RX/release proof, and board safety validation are still open.
- Current board artifacts: bitstream and XSA are local Vivado products only, not board safety proof.

## Inputs To Use

- Evidence request checklist: `docs/pl_estop_hardware_evidence_request.md`
- Field intake packet: `docs/pl_estop_field_packet.md`
- Wiring CSV to fill: `docs/pl_estop_wiring_evidence.csv`
- Board validation CSV to fill: `docs/pl_estop_board_validation_evidence.csv`
- Evidence record templates: `docs/pl_estop_evidence_record_templates.md`
- Evidence file root: `docs/evidence/pl_estop/`

## Field Sequence

1. Run the local handoff gates before touching real hardware:
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify_board_input_manifest.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify_new_vivado_local_closure.ps1
   ```
2. Pick one wiring row from `docs/pl_estop_field_packet.md`; do not fill multiple unrelated rows from memory.
3. Confirm the actual signal owner, connector, board node, healthy/trip level, disconnect behavior, safe/off polarity, and PL gate point.
4. Create a real `.md` evidence record under `docs/evidence/pl_estop/` using the contract in `docs/pl_estop_evidence_record_templates.md`.
5. Store raw captures, photos, scope exports, logic-analyzer exports, and logs under `docs/evidence/pl_estop/`; reference them from the `.md` evidence record using project-relative paths.
6. Update `docs/pl_estop_wiring_evidence.csv` with project-relative evidence paths only.
7. Run the intake gate:
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify_pl_estop_field_intake.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify_pl_estop_readiness.ps1
   ```
8. Only if the relevant gate changes from `not_ready` to the required ready state may the corresponding real RTL/XDC connection be implemented.

## DO/PWM Rule

- `DO1` - `DO14` and `PWM1` - `PWM2` must be gated through `pl_estop_general_output_gate` after the normal output owner.
- A channel cannot be promoted as an ordinary output first and retrofitted with E-stop later.
- Each channel is currently held by a zero normal owner; before normal output use, it needs confirmed load wiring, off polarity, normal owner before the gate, PL gate point, simulation coverage, and bench or board forced-off evidence.

## Bus TX Rule

- The bus gate must use `pl_estop_bus_tx_gate` or an equivalent PL gate on TX send-enable, driver-enable, TX idle, or TX_CTL.
- Do not use PHY reset, transceiver reset, link clock removal, link power removal, or RX/status disable as the E-stop bus gate.
- Keep the physical link alive so release can recover without rebuilding the link.
- Before TX re-enable, queued TX FIFO entries, buffered commands, and control frames accumulated while E-stop was latched must be flushed or invalidated.
- Current production drive transport is EtherCAT over PS GEM1/EMIO. `scripts/verify_pl_estop_bus_gate_owner.ps1` proves the local GMII pre-ODDR TX gate is on that PS GEM1/EMIO path before `gmii2rgmii`, but it does not by itself prove production drive-bus functional safety until BV10-BV12 measure the real board path, Link/RX/MDIO continuity, release recovery, and stale TX/control-frame behavior.

## Promotion Gates

- Real RTL/XDC promotion requires `scripts/verify_pl_estop_field_intake.ps1` to show the relevant wiring evidence is ready, and `scripts/verify_pl_estop_readiness.ps1` to report `e11_rtl_xdc_ready=yes`.
- Board validation requires all applicable rows in `docs/pl_estop_board_validation_evidence.csv` to reach `board_verified` with non-placeholder `.md` evidence records and real attachments.
- A final safety claim requires `a11_board_validation_ready=yes`; local simulation, routed implementation, bitstream generation, XSA export, and manifest hashes are not substitutes.

## Stop Conditions

- Stop if off polarity is unknown, load wiring is unknown, the evidence path is absolute, or the evidence record has no real attachment.
- Stop if a proposed bus gate breaks physical Link, disables RX/status observation, or has no queue-flush owner.
- Stop if the proposed path would add normal PL `PACKAGE_PIN` constraints for XADC dedicated analog pins `L11/M12`, or would reintroduce `U10`, `U9`, `AA12`, or `AB12` as ADC SPI pins.
- Stop if a proposed change modifies the old Vivado project or introduces a source dependency on it.
