# Vivado Timing Margin Improvement Plan

Date: 2026-06-11

## Current Baseline

当前可交付基线已经合格，不是阻塞项。

- Official gate script: `scripts/vivado_gate_current.tcl`
- Current route directive: `MoreGlobalIterations`
- Timing status: `timing_met`
- Global timing: `WNS=0.154ns`, `TNS=0.000ns`, `WHS=0.037ns`, `THS=0.000ns`
- `step_ip / clk_fpga_1`: `WNS=0.154ns`, `WHS=0.079ns`
- Final evidence: `../evidence/20260611_vivado_timing_margin_improve/final_moreglobal`
- Bit SHA256: `BE2E40A7D4F3DABC0BCCBF80FC5D33F1C7788AAEE07524E688E5F90EC14DE52C`
- XSA SHA256: `5BAA0AFA41BE8853A2625FDBBC07DFE3C499D90A60E04D9639B209FE495F62D1`

Known remaining warnings:

- DRC warning: `RTSTAT-10 No routable loads`
- BD critical warning: old `/hdmi_out/DVI_Transmitter_0` IP
- BD critical warning: `z20_dna_reader/S_AXI/Reg` unassigned address segment

These warnings existed before this plan. Do not expand the next timing task into IP upgrade or BD address cleanup unless it is explicitly scoped.

## Decision

进一步改进是工程余量优化，不是当前必须修复的 bug。

Recommended approach:

1. Keep the current `MoreGlobalIterations` result as the safe baseline.
2. Only do another improvement pass if the goal is to raise margin toward `WNS >= 0.200ns`.
3. Do not change motion semantics, STEP/DIR behavior, LinuxCNC configuration, UI logic, or board runtime files for this work.
4. Do not use false paths, multicycle paths, or relaxed clocks to hide real synchronous paths.

## Direct Expected Effect

进一步改进只应影响 FPGA 实现余量和重建稳定性。

It should not change:

- Axis speed, G0 speed, homing speed, or acceleration
- LinuxCNC motion behavior
- STEP/DIR pulse algorithm
- EtherCAT or HAL runtime behavior
- UI page behavior

It may improve:

- Rebuild tolerance across Vivado route variation
- Margin on PS reset/AXI interconnect paths
- Margin on `step_ip` internal control paths

## Target

Preferred target for the next pass:

- Global `WNS >= 0.200ns`
- Global `WHS >= 0.030ns`
- `step_ip / clk_fpga_1 WNS >= 0.150ns`
- No new DRC errors
- No new timing exceptions hiding real synchronous logic

If `WNS >= 0.300ns` is reached without new risk, keep it. If the design remains between `0.154ns` and `0.200ns`, the current baseline is still acceptable.

## Candidate Work Plan

### Phase 1: Reproduce Current Baseline

Run the official gate from a clean Vivado generated-output state.

```powershell
$env:VIVADO_JOBS='8'
vivado.bat -mode batch `
  -source .\scripts\vivado_gate_current.tcl `
  -log ..\evidence\<task_id>\vivado_gate_baseline.log `
  -journal ..\evidence\<task_id>\vivado_gate_baseline.jou
```

Acceptance for Phase 1:

- `BUILD_STATUS=bitstream_generated`
- `TIMING_STATUS=timing_met`
- Timing is close to the current baseline: `WNS ~= 0.154ns`, `WHS ~= 0.037ns`

### Phase 2: Analyze Actual Worst Paths

Collect targeted reports before changing source.

Required checks:

- Worst setup path
- Worst hold path
- `step_ip / clk_fpga_1` setup and hold
- High fanout reset nets
- Reset path owner and fanout around `rst_ps7_0_100M`

Useful Vivado report commands:

```tcl
report_timing_summary -file <out>/timing_summary.rpt
report_timing -max_paths 20 -sort_by slack -delay_type max -file <out>/setup_paths.rpt
report_timing -max_paths 20 -sort_by slack -delay_type min -file <out>/hold_paths.rpt
report_high_fanout_nets -file <out>/high_fanout_nets.rpt
```

### Phase 3: Low-Risk Strategy Sweep

Only sweep implementation settings first. Do not modify HDL or XDC in this phase.

Candidate routes to compare:

- Current: `MoreGlobalIterations`
- Previous baseline: `HigherDelayCost`
- Hold check only if setup improves: `AdvancedSkewModeling`

Avoid adopting any strategy that:

- Makes timing fail
- Improves setup but drops global `WHS < 0.030ns`
- Moves the worst path back into `step_ip` with less than current margin
- Creates extra Vivado auto-edited tracked source files that must be kept

### Phase 4: Targeted Structural Fix, Only If Needed

If strategy sweep does not reach the target and more margin is still required, inspect the reset/AXI path. The current global worst setup is around:

```text
rst_ps7_0_100M -> ps7_0_axi_periph auto_pc reset path
```

Allowed investigation:

- Whether reset fanout can be reduced or replicated safely
- Whether register placement or hierarchy preservation is hurting this path
- Whether a local reset register inside the affected AXI island reduces route delay

Not allowed without separate scope:

- Relaxing `clk_fpga_0` or `clk_fpga_1`
- False-pathing synchronous reset release paths just to improve WNS
- Upgrading DVI IP
- Reworking BD address map
- Changing `step_ip` motion behavior

## Stop Conditions

Stop and keep the current baseline if any of these happen:

- New result has `TIMING_STATUS=timing_not_met`
- Global `WHS < 0.030ns`
- Global `WNS` does not beat `0.154ns`
- `step_ip / clk_fpga_1` regresses below `WNS=0.150ns`
- DRC gains a new error
- Fix requires BD topology changes outside the timing-margin scope
- Fix requires motion/STEP/DIR semantic changes

## Final Acceptance

A next-pass change is acceptable only when all are true:

- `vivado_gate_current.tcl` exits `0`
- `vivado_export_xsa_current.tcl` exits `0`
- `TIMING_STATUS=timing_met`
- Global `WNS >= 0.200ns` or clearly documented why the smaller improvement is worth keeping
- Global `WHS >= 0.030ns`
- No new DRC errors
- Known warnings are listed explicitly
- Bit, XSA, timing report, DRC report, pulse-width report, and logs are copied into `evidence/<task_id>/final_*`
- `artifact_manifest.json` records SHA256 for final bit, XSA, and timing report
- Vivado generated directories and auto-edited tracked `.xci/.bd/.bda/.xpr` noise are cleaned or restored before handoff
- focused Vivado/build checks and `git diff --check` pass

## Current Recommendation

Do not start another long Vivado run unless the release goal explicitly needs more margin than `WNS=0.154ns`.

If another pass is approved, start with Phase 1 and Phase 2 above, then do one bounded strategy sweep. Do not proceed to structural reset changes unless the sweep cannot reach the target and the user accepts the extra risk.
