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

proc delete_gpio0_external_interface_if_present {} {
  set intf_port [get_bd_intf_ports -quiet gpio0]
  if {[llength $intf_port] == 0} {
    return
  }

  set intf_nets [get_bd_intf_nets -quiet -of_objects $intf_port]
  foreach net $intf_nets {
    delete_bd_objs $net
  }
  delete_bd_objs $intf_port
}

proc ensure_gpio0_zero_const {} {
  set cell [get_bd_cells -quiet gpio0_const_zero]
  if {[llength $cell] == 0} {
    create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 gpio0_const_zero
  }
  set_property -dict [list CONFIG.CONST_WIDTH {7} CONFIG.CONST_VAL {0}] [get_bd_cells gpio0_const_zero]
}

cd $project_dir
open_project $project_file
open_bd_design $bd_file

require_bd_cell processing_system7_0
foreach pin [list processing_system7_0/GPIO_I processing_system7_0/GPIO_O processing_system7_0/GPIO_T] {
  require_bd_pin $pin
}

delete_gpio0_external_interface_if_present

# The old PS EMIO GPIO bundle mixes old-only pins, PL_RST, touch INT/RST,
# MPG_A, and SCALE_SEL1. Until each bit has a confirmed owner and direction,
# remove the external IOBUFs and present the PS GPIO input sample as zero.
ensure_gpio0_zero_const
connect_pin_to_pin_if_open gpio0_const_zero/dout processing_system7_0/GPIO_I

validate_bd_design
save_bd_design
generate_target all [get_files $bd_file]
make_wrapper -files [get_files $bd_file] -top -import -force
update_compile_order -fileset sources_1
close_project
exit 0
