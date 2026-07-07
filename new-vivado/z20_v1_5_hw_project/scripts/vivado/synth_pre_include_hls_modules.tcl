namespace eval ::z20 {}
set script_dir [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join $script_dir ".." ".."]]

# Keep this pre-hook lightweight and deterministic:
# - Do not inject hls_manual_src files.
# - Inject frozen csynth RTL under ip_repo/hls_frozen_src.
# - Bypass compile_c for the current system BD after frozen RTL is injected.
#   In this project compile_c is only needed for the active LCD v_frmbuf_rd HLS block;
#   bypassing it avoids obsolete Vivado 2020.2 HLS IP packager revision overflow.
# - Restore generated GMII pre-ODDR E-stop gating after BD generation and before
#   synth_design reads the generated system netlist.

proc ::z20::apply_gmii_pre_oddr_estop_patch {project_dir} {
  set patch_script [file normalize [file join $project_dir "scripts/patch_gmii_pre_oddr_estop_gate.ps1"]]
  if {![file exists $patch_script]} {
    error "Missing GMII pre-ODDR E-stop patch script: $patch_script"
  }
  set powershell_cmd [auto_execok powershell]
  if {$powershell_cmd eq ""} {
    set powershell_cmd [auto_execok pwsh]
  }
  if {$powershell_cmd eq ""} {
    error "Cannot find powershell or pwsh for GMII pre-ODDR E-stop patch"
  }
  set patch_output [exec {*}$powershell_cmd -NoProfile -ExecutionPolicy Bypass -File [file nativename $patch_script] -ProjectDir [file nativename $project_dir]]
  puts $patch_output
}

::z20::apply_gmii_pre_oddr_estop_patch $project_dir

if {[llength [info commands compile_c]] > 0 && [llength [info commands __z20_orig_compile_c]] == 0} {
  rename compile_c __z20_orig_compile_c
  proc compile_c {args} {
    set cmd_line [join $args " "]
    if {[string match *v_frmbuf_rd* $cmd_line] || [string match *system.bd* $cmd_line]} {
      puts "INFO: synth pre-hook: bypass compile_c for system BD v_frmbuf_rd blocks (use frozen RTL)"
      return 0
    }
    return [uplevel 1 [linsert $args 0 __z20_orig_compile_c]]
  }
}

set __z20_frozen_roots [list \
  [file normalize [file join $project_dir "ip_repo/hls_frozen_src/system_v_frmbuf_rd_0_1"]] \
]

set __z20_vfiles {}
foreach __z20_root $__z20_frozen_roots {
  foreach __z20_v [glob -nocomplain -directory $__z20_root *.v] {
    lappend __z20_vfiles $__z20_v
  }
}

if {[llength $__z20_vfiles] > 0} {
  puts "INFO: synth pre-hook: injecting [llength $__z20_vfiles] frozen HLS Verilog files"
  read_verilog -library xil_defaultlib $__z20_vfiles
} else {
  puts "WARNING: synth pre-hook: no frozen HLS Verilog files found under ip_repo/hls_frozen_src"
}

puts "INFO: synth pre-hook: manual HLS RTL disabled; v_frmbuf_rd compile_c bypass enabled"
