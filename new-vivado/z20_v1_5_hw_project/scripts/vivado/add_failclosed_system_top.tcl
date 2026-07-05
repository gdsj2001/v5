set script_dir [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join $script_dir ".." ".."]]
set project_file "z20_v1_5_hw_project.xpr"
set top_file [file join $project_dir "rtl" "system_top.v"]

proc fail {message} {
  puts "ERROR: $message"
  exit 2
}

cd $project_dir
if {![file exists $project_file]} {
  fail "missing project file: $project_file"
}
if {![file exists $top_file]} {
  fail "missing top file: $top_file"
}

open_project $project_file

set existing [get_files -quiet $top_file]
if {[llength $existing] == 0} {
  add_files -fileset sources_1 $top_file
}

set_property top system_top [get_filesets sources_1]
update_compile_order -fileset sources_1
puts "TOP_MODULE=[get_property top [get_filesets sources_1]]"

close_project
exit 0
