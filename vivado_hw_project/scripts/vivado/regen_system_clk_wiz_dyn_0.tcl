set proj "vivado_hw_project.xpr"
set ip_name "system_clk_wiz_dyn_0"
set bd_name "system.bd"

open_project $proj

set ip_obj [get_ips -quiet $ip_name]
if {[llength $ip_obj] == 0} {
  puts "ERROR: IP '$ip_name' not found in project."
  close_project
  exit 2
}

set bd_obj [get_files -quiet $bd_name]
if {[llength $bd_obj] == 0} {
  puts "ERROR: BD '$bd_name' not found in project."
  close_project
  exit 3
}

puts "INFO: Regenerating parent BD targets to refresh nested IP metadata ($ip_name)."
reset_target all $bd_obj
generate_target all $bd_obj
export_ip_user_files -of_objects $bd_obj -no_script -sync -force -quiet

set xci_path [get_property IP_FILE $ip_obj]
puts "INFO: Regenerated IP_FILE=$xci_path"
puts "INFO: Next step: verify CLKOUT1/CLKOUT2 requested/actual frequencies in XCI are consistent."

close_project
puts "DONE"
