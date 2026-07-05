# Active Pin Conflicts

This file is generated from `docs/remaining_drc_ports.csv` by `scripts/export_active_pin_conflicts.ps1`.
It lists old wrapper ports whose old physical pins are already claimed by active XDC ports, plus the current BD/net connection evidence for both sides.

Current status: no `active_pin_already_claimed` rows remain in `docs/remaining_drc_ports.csv`.

N3C-2 closure note:

- The board-level top uses the current 8-axis boundary: `PULS1-8` and `DIR1-8` are driven from wrapper `axis_puls_o[7:0]` and `axis_dir_o[7:0]` through the top E-stop gate.
- `rtl/system_top.v` exposes v1.5 encoder receiver-output inputs `A1_IO/B1_IO/Z1_IO` through `A8_IO/B8_IO/Z8_IO`; all eight A/B/Z channels feed wrapper `axis_enc_a_i[7:0]`, `axis_enc_b_i[7:0]`, and `axis_enc_z_i[7:0]`.
- `ENA1-8` are driven by `z20_v15_io_owner_axi_lite` through the top E-stop gate. `ALM1-8`, `DI1-18`, `FR_DI1-16`, `TS_DI`, MPG, scale select, and `TP_INT` feed the IO owner input synchronizers/status registers instead of keep-only placeholders.
- `DO1-14` and `PWM1-2` normal outputs are driven by `z20_v15_io_owner_axi_lite`, then pass through the top-level PL E-stop output gate. The default reset state remains fail-closed/local-safe until software writes the owner registers.
- `RS485_FPGA_RX/TX` are exported at the top boundary and wired through PS UART1 EMIO. `TP_INT` feeds the IO owner status register and `TP_RST` is driven by the IO owner touch reset register.
- The ADC SPI board-level mapping on `U10`, `U9`, `AA12`, and `AB12` is retired; ADC_IN1 uses dedicated XADC VP/VN analog pins `L11/M12`, which are not normal PL active-XDC PACKAGE_PIN rows.
