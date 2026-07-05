set script_dir [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join $script_dir ".."]]
set project_file "z20_v1_5_hw_project.xpr"

proc apply_gmii_pre_oddr_estop_patch {script_dir project_dir} {
  set patch_script [file join $script_dir "patch_gmii_pre_oddr_estop_gate.ps1"]
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

proc persist_project_state {project_file stage} {
  puts "INFO: project state checkpoint reached after $stage"
}

cd $project_dir
apply_gmii_pre_oddr_estop_patch $script_dir $project_dir
open_project $project_file
update_compile_order -fileset sources_1
apply_gmii_pre_oddr_estop_patch $script_dir $project_dir
persist_project_state $project_file "compile order refresh"
set jobs 8
if {[info exists ::env(VIVADO_JOBS)]} {
  set jobs $::env(VIVADO_JOBS)
}
reset_run synth_1
launch_runs synth_1 -jobs $jobs
wait_on_run synth_1
if {[string match "*Complete*" [get_property STATUS [get_runs synth_1]]]} {
  catch {open_run synth_1}
}
persist_project_state $project_file "synth_1 completion"
puts "SYNTH_STATUS:[get_property STATUS [get_runs synth_1]]"
close_project
exit
