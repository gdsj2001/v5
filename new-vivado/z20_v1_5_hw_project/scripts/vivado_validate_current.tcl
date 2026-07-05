set script_dir [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join $script_dir ".."]]
set project_file "z20_v1_5_hw_project.xpr"
set bd_file [file join "z20_v1_5_hw_project.srcs" "sources_1" "bd" "system" "system.bd"]

cd $project_dir
open_project $project_file
update_compile_order -fileset sources_1
open_bd_design [get_files $bd_file]
validate_bd_design
close_project
exit 0
