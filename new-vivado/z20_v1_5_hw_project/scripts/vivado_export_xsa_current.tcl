set script_dir [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join $script_dir ".."]]
set project_file "z20_v1_5_hw_project.xpr"
set board_inputs_dir $project_dir/board_inputs
set export_xsa $board_inputs_dir/system.xsa

cd $project_dir
open_project $project_file
open_run impl_1
file mkdir $board_inputs_dir
write_hw_platform -fixed -include_bit -force -file $export_xsa
close_project
exit
