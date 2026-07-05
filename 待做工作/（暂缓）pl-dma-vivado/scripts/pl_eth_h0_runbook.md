# PL Ethernet DMA H0 Runbook

Scope: build a rollback-safe Vivado 2020.2 prototype for PL AXI Ethernet +
AXI DMA. This is hardware-only work in the A2 worktree. It must not touch the
running board, LinuxCNC, EtherCAT runtime config, or any `lvgl_app` software
files.

## Files

- `pl_eth_h0_create_prototype.tcl`: inventory and optional prototype BD update.
- `pl_eth_h0_review_export.tcl`: review/export script. It now fails closed if
  H0 blockers remain, unless `PL_ETH_H0_ALLOW_BLOCKED_XSA=1` is explicitly set.
- `pl_eth_h0_system-user.dtsi`: review-only device-tree fragment with concrete
  address and interrupt values.
- `pl_eth_h0_xdc_review.md`: constraint stop-gate notes for the RGMII path.
- `pl_eth_h0_check_artifact.ps1`: local artifact verifier for the generated
  H0 bitstream and same-run timing/route/DRC reports.

## Inventory Check

Run from `D:\re\v3_worktrees\pl-eth-h0-prototype`:

```powershell
& C:\Xilinx\Vivado\2020.2\bin\vivado.bat -mode batch `
  -source vivado_hw_project\scripts\pl_eth_h0_create_prototype.tcl
```

Expected result:

- PS GEM1/EMIO baseline checks pass.
- `gmii2rgmii_0` is present.
- FCLK2 is confirmed at 200 MHz.
- AXI DMA and AXI Ethernet IP definitions exist in the local Vivado catalog.
- No project files are saved.

## Apply Prototype In This Worktree Only

```powershell
$env:PL_ETH_H0_APPLY='1'
& C:\Xilinx\Vivado\2020.2\bin\vivado.bat -mode batch `
  -source vivado_hw_project\scripts\pl_eth_h0_create_prototype.tcl
Remove-Item Env:\PL_ETH_H0_APPLY
```

The script saves rollback artifacts under:

```text
vivado_hw_project/artifacts/pl_eth_h0/
```

Expected prototype changes:

- Disable PS GEM1/EMIO for eth1 in the prototype BD.
- Remove the old `gmii2rgmii_0` path in the prototype BD.
- Add AXI DMA in SG mode, SG length width 12.
- Add AXI Ethernet for RGMII.
- Add `clk_wiz_pl_eth_125` to derive AXI Ethernet `gtx_clk` from the existing
  200 MHz FCLK2 source.
- Map AXI Ethernet at `0x40000000/0x40000`.
- Map AXI DMA at `0x40400000/0x10000`.
- Route AXI Ethernet core `interrupt` through concat input 13. H3.1 v6 proved
  the previous unconnected MAC core IRQ leaves Linux with only
  `Ethernet core IRQ not defined` and no independent PL netdev. H3.3 proved
  default FDT already assigns Linux offset 31 to `v_frmbuf_wr@43c40000`; using
  In2 corrupts the default display DMA IRQ. `In13` maps to Linux DTS offset 57,
  which the default FDT does not use.
- Route DMA TX/RX interrupts through the existing concat inputs 14/15. This
  preserves lower existing interrupt inputs, but it means the review-only DTS
  should use offsets 58/59 because Zynq-7000 maps `IRQ_F2P[14]/[15]` to
  GIC SPI 90/91. Final interrupt numbers must be verified from exported XSA and
  `/proc/interrupts`.
- Keep AXI Ethernet MDIO fail-closed by default. H0 must not bind `mdio_0_mdc`
  or `mdio_0_mdio_io` to E19/E20 unless schematic review proves a dedicated
  MDIO bus and `PL_ETH_H0_ENABLE_AXI_MDIO=1` is used intentionally.

Current H0 evidence:

- `apply_v7.log` passed `validate_bd_design` and saved the initial prototype in this
  worktree only.
- `repair_current_bd_v3_mdio_failclosed.log` passed and saved the current BD
  after removing the external AXI Ethernet MDIO port. It also confirms the PS
  GEM1 release and 16-bit IRQ concat gates.
- `review_export_v6_mdio_failclosed.log` passed. The current
  `h0_review_summary.txt` reports `blocker_count=0` and
  `intf.axi_ethernet_0/mdio=unconnected`.
- `pl_eth_h0_generate_blocked_bit.tcl` completed fresh implementation and
  bitstream generation in this worktree only on 2026-06-14. Current H0 bit
  artifact:
  `vivado_hw_project/artifacts/pl_eth_h0/blocked_experimental_bit/system_wrapper.bit`.
  SHA256:
  `D6296EF30760C33454BDCBCA9B9E663A90005C18AA04853D104AE711A53B36ED`.
- Current routed and post-route phys-opt timing summaries both show
  `WNS=0.182ns`, `TNS=0.000ns`, `TNS failing endpoints=0`,
  `WHS=0.023ns`, `THS=0.000ns`, and "All user specified timing constraints
  are met."
- `pl_eth_h0_check_artifact.ps1` has verified the retained bit artifact with
  the expected SHA256. It writes `h0_artifact_summary.json` and
  `h0_artifact_summary.txt` into the artifact directory. Current result:
  `hard_pass=True`, `release_blocked=True`,
  `methodology_TIMING_54_count=4`.
- Same-run bus skew reports are archived as `system_wrapper_bus_skew_routed.rpt`
  and `system_wrapper_bus_skew_postroute_physopted.rpt`; sampled constraints
  report positive slack, including `8.663ns` and `5.7ns` class margins.
- Timing strategy attempts were archived under
  `D:\re\v3_archive\20260613_a2_pl_eth_bit_timing\repo_ignored`. The current retained
  strategy is `OPT=Explore`, `PLACE=ExtraTimingOpt`,
  `PHYS_OPT=AggressiveExplore`, `ROUTE=NoTimingRelaxation`,
  `POST_ROUTE_PHYS_OPT=AggressiveExplore`, with synth retiming disabled. Same-
  constraint strategy comparison:
  - `strategy_best_before_retiming`: `WNS=0.128ns`, but this older result is
    not release-safe because the RGMII RX input constraints were not applied
    correctly before the XDC Tcl repair.
  - `strategy_xdc_port_clock_stable_noretime_candidate`: corrected XDC,
    `ROUTE=Explore`, `WNS=0.015ns`.
  - `strategy_xdc_port_clock_moreglobal_noretime_selected`: corrected XDC,
    `ROUTE=MoreGlobalIterations`, `WNS=0.140ns`.
  - `strategy_xdc_port_clock_notiming_noretime_selected`: corrected XDC,
    `ROUTE=NoTimingRelaxation`, `WNS=0.182ns`, selected.
- Current normal routed DRC has no Critical Warnings, but methodology DRC still
  has 4 AXI Ethernet generated-constraint `TIMING-54` Critical Warnings. Treat
  those as release blockers until eliminated or waived with exact evidence.
- H3.1 v5/v6 board evidence changed the next H0 revision requirement:
  v5 proved disabling the fallback MAC was not the cause of no shell, and v6
  removed the DMA MMIO resource conflict. The remaining netdev blocker is the
  real AXI Ethernet core IRQ/MDIO/PHY route. The next H0 repair must connect
  `axi_ethernet_0/interrupt` to an unused concat input and keep MDIO single-owner;
  `mac_irq` remains review-only unless a driver/DTS consumer is proven.
- `vivado_h0_irq_repair_20260619.log` passed `validate_bd_design` and saved the
  BD with `axi_ethernet_0/interrupt -> xlconcat_0/In2`, but v8 no-PL-nodes
  proved that mapping is not boot-safe because default FDT uses offset31 for
  `v_frmbuf_wr@43c40000`.
- `vivado_h0_irq_review_20260619.log` passed and exported
  `system_pl_eth_h0_no_bitstream.xsa` SHA256
  `2757CA358B6632E2C7A401B9CC2AFADC0C968A7DCD1BCE26AF30DE52A4A4879E`. The
  summary records `blocker_count=0`,
  `net.axi_ethernet_0/interrupt=/axi_ethernet_0_interrupt`, and
  `net.xlconcat_0/In2=/axi_ethernet_0_interrupt`; this is historical evidence
  only and must not be reused for new bit generation.
- `vivado_h0_irq_bit_20260619.log` completed synth, implementation, and
  write_bitstream. New experimental bit SHA256:
  `533E4CD3FB91AC1D472984D1F616F23ACAADC85502B8263E8EE5FD624923D16F`.
  Artifact checker reports `hard_pass=True`, routed/post-route phys-opt
  `WNS=0.09ns`, `WHS=0.036ns`, route errors `0`, but
  `release_blocked=True` because `methodology_TIMING_54_count=4`.
- Resolved gates from earlier failed apply runs:
  - AXI Ethernet `gtx_clk` is driven by `clk_wiz_pl_eth_125/pl_eth_125m`,
    not by the 100 MHz FCLK0.
  - DMA data uses HP2 via SmartConnect; HP0/HP1 remain for display paths.
  - DMA SG uses ACP via SmartConnect.
  - DMA Lite/ethernet Lite clock/reset are corrected to the FCLK0 reset domain.
- H3.2 v7 strict one-shot loaded the In2 bit successfully but kernel stopped
  before peripheral probe. H3.3 v8 no-PL-nodes used the same bit with the
  default stable FDT and still stopped at the same point. The stable FDT already
  uses offset31 for `v_frmbuf_wr@43c40000`; therefore the next repair moves MAC
  IRQ to In13/offset57, keeps DMA on In14/In15 offsets 58/59, and then first
  proves a no-PL-nodes FIT boots before enabling PL Ethernet/DMA nodes.
- Remaining warnings to review before implementation:
  - Existing `/hdmi_out/DVI_Transmitter_0` is old IP; unrelated to the H0
    Ethernet prototype.
  - ACP AWUSER/ARUSER width mismatch warnings remain between PS7 ACP and the
    SG SmartConnect path. Treat this as a review gate before synthesis.
  - `step_ip` and `lcd_out/rgb2lcd` pin property propagation warnings are
    inherited/generated by Vivado; confirm they are not timing regressions.

## Stop Points

Stop after the prototype BD and review artifacts are generated if the next step
would do any of these:

- Run implementation or produce a board bitstream.
- Modify PetaLinux, kernel config, device tree in the boot image, or rootfs.
- Change `/etc/sysconfig/ethercat`, backend scripts, or board runtime services.
- Deploy to `/opt/8ax` or replace the known bootable FPGA image.

## Local Artifact Check

Run this after each local H0 implementation run and before handing off the bit
artifact:

```powershell
powershell -ExecutionPolicy Bypass -File `
  D:\re\v3_worktrees\pl-eth-h0-prototype\vivado_hw_project\scripts\pl_eth_h0_check_artifact.ps1 `
  -ArtifactDir D:\re\v3_worktrees\pl-eth-h0-prototype\vivado_hw_project\artifacts\pl_eth_h0\blocked_experimental_bit `
  -ExpectedSha256 D6296EF30760C33454BDCBCA9B9E663A90005C18AA04853D104AE711A53B36ED
```

Expected H0-only result for the current artifact:

- Exit code `0`.
- `hard_pass=True`.
- `release_blocked=True` because `TIMING-54` methodology critical warnings
  remain.
- `h0_artifact_summary.json` and `h0_artifact_summary.txt` are generated next
  to the bitstream and should be archived with the selected strategy evidence.

## Review Gates Before Phase B

- `h0_review_summary.txt` must report `blocker_count=0`.
- PS ENET1 release must be proven by all of these values:
  `PCW_ENET1_PERIPHERAL_ENABLE=0`, `PCW_EN_ENET1=0`,
  `PCW_EN_EMIO_ENET1=0`, and `PCW_ENET1_GRP_MDIO_ENABLE=0`.
- `xlconcat_0.NUM_PORTS=16` and `xlconcat_0.dout_width=16` must both be true
  before using DMA IRQ offsets 58/59.
- `axi_ethernet_0/interrupt` must be connected to `xlconcat_0/In13`; the review
  summary must record `irq_map.axi_ethernet_0/interrupt=xlconcat_0/In13 ->
  Linux_DTS_offset_57`. Do not write an Ethernet
  `interrupts` property in DTS unless this real hardware net is present.
- If `dout_width` remains read-only at 14, choose a different interrupt routing
  strategy: regenerate `xlconcat_0` from a clean 16-input instance, add a second
  concat feeding unused lower bits, or move DMA interrupts to verified free
  existing lower inputs. Record the exact bit-to-GIC mapping before DTS work.
- Vivado address map shows AXI Ethernet and AXI DMA at the planned addresses.
- Interrupt wiring is documented and matches the exported XSA and booted
  `/proc/interrupts`. For current H0 In14/In15 wiring, the review-only DTS uses
  offsets 58/59, not 43/44 or 29/30.
- RGMII XDC no longer models `rgmii_txc` as generated from `rgmii_rxc`; TXC is
  sourced from the AXI Ethernet/ODDR path with timing constraints reviewed.
- FCLK2/IDELAYCTRL 200 MHz reference is present in reports.
- IDELAY/RGMII reset release is proven safe before Phase B. An earlier failed H0
  generated `system.v` tied AXI Ethernet `axi_*_arstn` pins to `1'b1` and used
  the 100 MHz AXI-lite reset for stream reset; that condition is a blocker if it
  returns.
- The repaired H0 source must contain `rst_pl_eth_axis` or an equivalent reset
  controller. Review output must show the AXI Ethernet `axis_clk` domain
  (`FCLK_CLK1` in H0) driving that controller, the Ethernet clock wizard
  `locked` signal gating release when available, `ref_clk` still on the 200 MHz
  FCLK2 path, and `axi_ethernet_0/axi_rxd_arstn`, `axi_rxs_arstn`,
  `axi_txc_arstn`, and `axi_txd_arstn` driven from that controller instead of
  `1'b1`.
- AXI DMA descriptor memory is planned in audited OCM, not DDR.
- The generated full DTS, `/proc/iomem`, and kernel `.config` prove the chosen
  OCM subrange is not also claimed by Zynq suspend/cpuidle/PM SRAM wake code or
  a generic SRAM node.
- Because H0 uses `xlnx,sg-length-width = <0x0c>` for 4096 byte DMA transfers,
  Phase B must force the prototype netdev MTU to 1500 before traffic, run
  `ethtool -K "$PL_ETH_IFACE" tso off gso off gro off lro off 2>/dev/null ||
  true`, and reject or immediately revert jumbo MTU settings. The driver should
  set `max_mtu = 1500` or an equivalent `ndo_change_mtu` guard; script locking
  is only a fallback.
- Use `scripts/pl_eth_h0_phase_b_guard.sh "$PL_ETH_IFACE"` as the Phase B
  starting guard. Archive its stdout/stderr with the link-up evidence. The guard
  is intentionally fail-closed on unproven OCM/PM ownership unless the operator
  sets a temporary `PL_ETH_H0_ALLOW_*` evidence override.
- Shared MDIO topology is confirmed before enabling the PL Ethernet DTS node.
- H0 MDIO fail-closed state is confirmed:
  - `h0_review_summary.txt` contains `intf.axi_ethernet_0/mdio=unconnected`;
  - `system.xdc` has no active `PACKAGE_PIN E19/E20` assignment for
    `mdio_0_*`;
  - `system_wrapper.v` has no `mdio_0_*` top-level ports.
- Before Phase B enables the DTS node, the generated device tree must define
  exactly one MDIO owner for the physical bus. If the bus is shared, AXI
  Ethernet remains a `phy-handle` consumer and must not instantiate another
  MDIO controller for E19/E20.
- Before Phase B enables the DTS node, run at least 20 full power-cycle
  cold-boot Link Up tests. Any boot that requires cable replug, driver reload,
  or manual reset blocks the netdev bring-up until the IDELAY/RGMII reset chain
  is repaired. After Link Up, run a fixed ping or packet test and verify
  `rx_crc_errors`, `rx_dropped`, `tx_errors`, and driver DMA error counters do
  not increase during the test window.
