set script_dir [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join $script_dir ".." ".."]]
set project_file "z20_v1_5_hw_project.xpr"
set bd_file [file join "z20_v1_5_hw_project.srcs" "sources_1" "bd" "system" "system.bd"]

proc fail {message} {
  puts "ERROR: $message"
  exit 2
}

proc require_bd_cell {cell_name} {
  if {[llength [get_bd_cells -quiet $cell_name]] == 0} {
    fail "required BD cell not found: $cell_name"
  }
}

proc require_bd_pin {pin_name} {
  if {[llength [get_bd_pins -quiet $pin_name]] == 0} {
    fail "required BD pin not found: $pin_name"
  }
}

proc connect_pin_to_pin_if_open {src_pin dst_pin} {
  require_bd_pin $src_pin
  require_bd_pin $dst_pin

  set src_obj [get_bd_pins $src_pin]
  set dst_obj [get_bd_pins $dst_pin]
  set src_nets [get_bd_nets -quiet -of_objects $src_obj]
  set dst_nets [get_bd_nets -quiet -of_objects $dst_obj]

  if {[llength $src_nets] > 0 && [llength $dst_nets] > 0 && [lindex $src_nets 0] eq [lindex $dst_nets 0]} {
    return
  }
  if {[llength $src_nets] > 0 && [llength $dst_nets] > 0} {
    fail "pins $src_pin and $dst_pin are already connected to different nets"
  }
  connect_bd_net $src_obj $dst_obj
}

proc delete_i2c4_external_interface_if_present {} {
  set intf_port [get_bd_intf_ports -quiet i2c4]
  if {[llength $intf_port] == 0} {
    return
  }

  set intf_nets [get_bd_intf_nets -quiet -of_objects $intf_port]
  foreach net $intf_nets {
    delete_bd_objs $net
  }
  delete_bd_objs $intf_port
}

cd $project_dir
open_project $project_file
open_bd_design $bd_file

require_bd_cell i2c4
require_bd_cell cnc_const_one
foreach pin [list i2c4/scl_i i2c4/sda_i i2c4/scl_o i2c4/scl_t i2c4/sda_o i2c4/sda_t] {
  require_bd_pin $pin
}

delete_i2c4_external_interface_if_present

# With no confirmed external device on FPGA1_IO5_P/N, keep the AXI IIC
# core internally idle-high and remove the generated top-level IOBUFs.
connect_pin_to_pin_if_open cnc_const_one/dout i2c4/scl_i
connect_pin_to_pin_if_open cnc_const_one/dout i2c4/sda_i

validate_bd_design
save_bd_design
generate_target all [get_files $bd_file]
make_wrapper -files [get_files $bd_file] -top -import -force
update_compile_order -fileset sources_1
close_project
exit 0
