# vivado_hw_project

Vivado hardware project source for the v3 board build.

This directory is intentionally kept narrow. It should contain only files needed
to open, validate, synthesize, implement, and export the current Vivado hardware
platform, plus a small amount of project-local documentation.

## Kept In This Directory

- `vivado_hw_project.xpr`: Vivado project entry.
- `vivado_hw_project.srcs/`: block design, IP instance metadata, constraints, and generated-source references tracked as source.
- `ip_repo/`: local IP repository and frozen HLS RTL used by the current build.
- `rtl/`: project RTL directly referenced by the Vivado project.
- `scripts/`: Vivado batch scripts for gate, implementation, checks, XSA export, and project maintenance.
- `hls_manual_src/`: retained HLS source reference. The current gate does not inject this directly; it uses frozen RTL under `ip_repo/hls_frozen_src`.
- `TIMING_MARGIN_IMPROVEMENT_PLAN.md`: current timing-margin baseline and next-pass plan.

## Current Build Entry Points

Run from this project directory with `vivado.bat` available on `PATH`:

```powershell
$env:VIVADO_JOBS='8'
vivado.bat -mode batch `
  -source .\scripts\vivado_gate_current.tcl
vivado.bat -mode batch `
  -source .\scripts\vivado_export_xsa_current.tcl
```

The export script recreates `board_inputs/system.xsa` as a generated output.
Generated Vivado run/cache directories such as `.Xil`, `*.runs`, `*.gen`,
`*.cache`, and `board_inputs/*.xsa` are not project source.

## Archived Cleanup

Non-Vivado documents, board runtime inputs, PetaLinux project residue, local
tools, image assets, and old HLS generated work directories were moved out of
this directory during cleanup.

The archived cleanup manifest is not required to open or build this portable
project directory.
