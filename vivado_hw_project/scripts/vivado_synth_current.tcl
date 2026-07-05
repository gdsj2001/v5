set script_dir [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join $script_dir ".."]]
set project_file [file join $project_dir "vivado_hw_project.xpr"]

open_project $project_file
proc persist_project_state {project_file stage} {
  puts "INFO: project state checkpoint reached after $stage"
}

update_compile_order -fileset sources_1
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
