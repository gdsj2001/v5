# PL Ethernet DMA H0 XDC Review

Scope: review notes for the H0 prototype in this worktree only. This file is
not a replacement for timing closure.

## Current Constraint Status

The H0 worktree originally inherited the legacy constraint:

```tcl
create_clock -period 8.000 -name rgmii_rxc [get_ports rgmii_rxc]
create_generated_clock -name rgmii_txc_fwd \
  -source [get_ports rgmii_rxc] \
  -divide_by 1 \
  [get_ports rgmii_txc]
```

That was written for the previous PS GEM1 -> EMIO -> `gmii2rgmii_0` route. The
H0 prototype removes `gmii2rgmii_0` and drives AXI Ethernet `gtx_clk` from
`clk_wiz_pl_eth_125/pl_eth_125m`. Therefore `rgmii_txc` must no longer be
modeled as a clock generated from `rgmii_rxc`.

Current H0 status:

- `system.xdc` has been updated to use the AXI Ethernet RGMII generated TX
  clock `system_i/axi_ethernet_0/inst/mac/inst_rgmii_tx_clk`.
- TX output delays now match the generated AXI Ethernet model:
  `-max 0.75`, `-min -0.7`, on both TXC edges.
- The blocked experimental implementation report
  `vivado_hw_project/artifacts/pl_eth_h0/blocked_experimental_bit/timing_summary_postroute_physopted.rpt`
  shows those `0.750ns` output delays and reports all user timing constraints
  met.
- Keep this file as a guardrail: do not reintroduce a top-level generated clock
  on `rgmii_0_txc`.

## Required Next Work Before Release

Before release or board deployment:

1. Preserve `create_clock -period 8.000 -name rgmii_0_rxc [get_ports rgmii_0_rxc]`
   unless the selected AXI Ethernet example design documents a different RXC
   constraint.
2. Keep input/output delay constraints for RGMII RX/TX data/control aligned to
   the board PHY timing mode (`rgmii` vs `rgmii-id`/`txid`/`rxid`). The H0
   AXI Ethernet candidate already forwards delayed TXC and uses PL-side RX
   IDELAY, so the device-tree prototype must default to plain `rgmii` unless a
   PHY register audit proves the internal PHY delays are intentionally enabled
   and the Vivado timing model is updated to match.
   The H0 top-level TX output delay constraints must mirror the generated
   AXI Ethernet RGMII clock constraint model: `-max 0.75` and `-min -0.7`
   on both TXC edges. Do not substitute `2.0/0.5`; that double-counts the
   90-degree forwarded-clock skew as external setup/hold budget.
3. Confirm FCLK2 200 MHz remains the IDELAY/reference source required by the
   chosen AXI Ethernet configuration.
4. Triage methodology warnings before release. Current blocked timing passes,
   but the methodology report still contains generated-IP clocking and CDC
   warnings. Treat those as release blockers until reviewed.
5. Run implementation timing and archive same-run:
   - `report_clocks`
   - `report_clock_interaction`
   - `report_timing_summary`
   - RGMII input/output path timing details

## TIMING-54 Triage Snapshot

Current retained H0 artifact:

```text
vivado_hw_project/artifacts/pl_eth_h0/blocked_experimental_bit/methodology_drc_routed.rpt
```

The remaining release blocker is not negative timing slack. It is a methodology
blocker:

- `TIMING-54#1..#4` report scoped false-path constraints between
  `system_i/axi_ethernet_0/inst/mac/inst_rgmii_tx_clk` and `clkout0`
  at Vivado timing constraint positions `103..106`.
- The generated constraint source search shows the matching Xilinx AXI
  Ethernet IP constraint file:
  `vivado_hw_project.gen/sources_1/bd/system/ip/system_axi_ethernet_0_0/bd_0/ip/ip_1/synth/bd_4bad_mac_0_clocks.xdc`.
  Its `set_false_path` lines between the IP GTX clock and RGMII TX clock are
  lines `56..59` in the generated H0 tree.
- The methodology report also lists RX false-path warnings between
  `system_i/axi_ethernet_0/inst/mac/inst_rgmii_rx_clk` and `rgmii_0_rxc`
  at positions `96..99`. Treat those as part of the same AXI Ethernet RGMII
  generated-constraint review, even though the report summary currently counts
  the TX side as the four critical warnings.
- `system.xdc` lines `227..230` only add top-level TX output delay constraints
  referenced to the AXI Ethernet generated TX clock. They are not a sufficient
  fix for `TIMING-54`; do not mask the warning by adding broad clock groups.

Release-safe resolution options:

1. Preferred: find the Xilinx-recommended AXI Ethernet RGMII constraint model
   for Vivado/PetaLinux 2020.2 and replace the generated broad scoped
   false-path behavior with narrower, point-to-point exceptions or generated
   constraints that pass methodology.
2. If the generated IP constraints are vendor-intended and unavoidable, create
   an explicit waiver file that names the generated XDC file, line numbers,
   clock pair, report positions, and same-run timing evidence. The waiver must
   be reviewed before board release; H0 local evidence alone is not enough.
3. Do not fix this by suppressing `report_methodology`, deleting the generated
   IP XDC blindly, or adding broad asynchronous clock groups. Those can hide
   real synchronous RGMII paths and invalidate the timing report.

## Stop Condition

If RGMII TXC/TXD or RXC/RXD paths are unconstrained, falsely constrained, show
timing violations, or depend on unverified PHY internal-delay settings, stop
before exporting a bootable bitstream.
