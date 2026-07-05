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

Run from the repo root or this project directory:

```powershell
$env:VIVADO_JOBS='8'
& C:\Xilinx\Vivado\2020.2\bin\vivado.bat -mode batch `
  -source D:\re\v3\vivado_hw_project\scripts\vivado_gate_current.tcl
& C:\Xilinx\Vivado\2020.2\bin\vivado.bat -mode batch `
  -source D:\re\v3\vivado_hw_project\scripts\vivado_export_xsa_current.tcl
```

The export script recreates `board_inputs/system.xsa` as a generated output.
Generated Vivado run/cache directories such as `.Xil`, `*.runs`, `*.gen`,
`*.cache`, and `board_inputs/*.xsa` are not project source.

## Archived Cleanup

Non-Vivado documents, board runtime inputs, PetaLinux project residue, local
tools, image assets, and old HLS generated work directories were moved out of
this directory during cleanup.

Archive location:

```text
D:\re\v3_archive\20260611_vivado_project_cleanup\repo_ignored\vivado_hw_project_removed
```

The archive contains `_cleanup_manifest.json` listing every moved item.
