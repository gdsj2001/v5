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

proc require_bd_pin_width {pin_name expected_left expected_right} {
  require_bd_pin $pin_name
  set pin_obj [get_bd_pins $pin_name]
  set actual_left [get_property LEFT $pin_obj]
  set actual_right [get_property RIGHT $pin_obj]
  if {$actual_left ne $expected_left || $actual_right ne $expected_right} {
    fail "BD pin $pin_name has unexpected width LEFT=$actual_left RIGHT=$actual_right"
  }
}

proc disconnect_pin_from_nets_if_present {pin_name} {
  set pin_obj [get_bd_pins -quiet $pin_name]
  if {[llength $pin_obj] == 0} {
    return
  }
  foreach net [get_bd_nets -quiet -of_objects $pin_obj] {
    disconnect_bd_net $net $pin_obj
  }
}

proc disconnect_port_from_nets_if_present {port_name} {
  set port_obj [get_bd_ports -quiet $port_name]
  if {[llength $port_obj] == 0} {
    return
  }
  foreach net [get_bd_nets -quiet -of_objects $port_obj] {
    disconnect_bd_net $net $port_obj
  }
}

proc delete_port_and_connected_nets_if_present {port_name} {
  set port_obj [get_bd_ports -quiet $port_name]
  if {[llength $port_obj] == 0} {
    return
  }
  foreach net [get_bd_nets -quiet -of_objects $port_obj] {
    delete_bd_objs $net
  }
  delete_bd_objs $port_obj
}

proc delete_cell_and_connected_nets_if_present {cell_name} {
  set cell_obj [get_bd_cells -quiet $cell_name]
  if {[llength $cell_obj] == 0} {
    return
  }
  foreach pin [get_bd_pins -quiet -of_objects $cell_obj] {
    foreach net [get_bd_nets -quiet -of_objects $pin] {
      delete_bd_objs $net
    }
  }
  delete_bd_objs $cell_obj
}

proc ensure_vector_port {port_name dir left right} {
  set port_obj [get_bd_ports -quiet $port_name]
  if {[llength $port_obj] == 0} {
    return [create_bd_port -dir $dir -from $left -to $right $port_name]
  }
  set actual_dir [get_property DIR $port_obj]
  set actual_left [get_property LEFT $port_obj]
  set actual_right [get_property RIGHT $port_obj]
  if {$actual_dir ne $dir || $actual_left ne $left || $actual_right ne $right} {
    fail "BD port $port_name exists with unexpected shape: DIR=$actual_dir LEFT=$actual_left RIGHT=$actual_right"
  }
  return $port_obj
}

proc ensure_scalar_port {port_name dir} {
  set port_obj [get_bd_ports -quiet $port_name]
  if {[llength $port_obj] == 0} {
    return [create_bd_port -dir $dir $port_name]
  }
  set actual_dir [get_property DIR $port_obj]
  if {$actual_dir ne $dir} {
    fail "BD port $port_name exists with unexpected direction: $actual_dir"
  }
  return $port_obj
}

proc ensure_xlconstant {cell_name width value} {
  set cell [get_bd_cells -quiet $cell_name]
  if {[llength $cell] == 0} {
    create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 $cell_name
  }
  set_property -dict [list CONFIG.CONST_WIDTH $width CONFIG.CONST_VAL $value] [get_bd_cells $cell_name]
}

proc rewire_pin_to_pin {src_pin dst_pin} {
  require_bd_pin $src_pin
  require_bd_pin $dst_pin
  disconnect_pin_from_nets_if_present $src_pin
  disconnect_pin_from_nets_if_present $dst_pin
  connect_bd_net [get_bd_pins $src_pin] [get_bd_pins $dst_pin]
}

proc rewire_pin_to_port {src_pin dst_port} {
  require_bd_pin $src_pin
  if {[llength [get_bd_ports -quiet $dst_port]] == 0} {
    fail "required BD port not found: $dst_port"
  }
  disconnect_pin_from_nets_if_present $src_pin
  disconnect_port_from_nets_if_present $dst_port
  connect_bd_net [get_bd_pins $src_pin] [get_bd_ports $dst_port]
}

proc rewire_port_to_pin {src_port dst_pin} {
  if {[llength [get_bd_ports -quiet $src_port]] == 0} {
    fail "required BD port not found: $src_port"
  }
  require_bd_pin $dst_pin
  disconnect_port_from_nets_if_present $src_port
  disconnect_pin_from_nets_if_present $dst_pin
  connect_bd_net [get_bd_ports $src_port] [get_bd_pins $dst_pin]
}

cd $project_dir
open_project $project_file
open_bd_design $bd_file

require_bd_cell processing_system7_0
require_bd_cell step_ip
require_bd_cell pl_estop
require_bd_cell cnc_const_zero

# Retire the old exported BD boundary. These names must not reappear on the
# generated wrapper because they make the new project look like the old board.
foreach old_port [list step_o dir_o enc_a_i enc_b_i enc_z_i adc_spi_cs_n adc_spi_sclk adc_spi_mosi adc_spi_miso] {
  delete_port_and_connected_nets_if_present $old_port
}
delete_cell_and_connected_nets_if_present axis_const_zero2
foreach retired_axis_adapter [list axis_puls_8_concat axis_dir_8_concat axis_puls_zero2 axis_dir_zero2 axis_enc_a_low6 axis_enc_b_low6 axis_enc_z_low6] {
  delete_cell_and_connected_nets_if_present $retired_axis_adapter
}

# ADC now uses the dedicated XADC VP/VN pins outside normal PL PACKAGE_PIN
# routing. Keep PS SPI0 from driving an exported EMIO ADC boundary.
set_property -dict [list \
  CONFIG.PCW_EN_SPI0 {0} \
  CONFIG.PCW_SPI0_PERIPHERAL_ENABLE {0} \
  CONFIG.PCW_EN_EMIO_SPI0 {0} \
  CONFIG.PCW_SPI0_SPI0_IO {MIO} \
] [get_bd_cells processing_system7_0]
disconnect_pin_from_nets_if_present processing_system7_0/SPI0_SS_O
disconnect_pin_from_nets_if_present processing_system7_0/SPI0_SCLK_O
disconnect_pin_from_nets_if_present processing_system7_0/SPI0_MOSI_O
disconnect_pin_from_nets_if_present processing_system7_0/SPI0_MISO_I
if {[llength [get_bd_pins -quiet processing_system7_0/SPI0_MISO_I]] > 0} {
  connect_bd_net [get_bd_pins cnc_const_zero/dout] [get_bd_pins processing_system7_0/SPI0_MISO_I]
}

# The BD wrapper now exposes only current board-level axis semantics. Configure
# step_ip itself as an 8-axis pulse/direction/encoder owner; do not recreate a
# padded lower-width external boundary.
set_property -dict [list CONFIG.N_AXES {8}] [get_bd_cells step_ip]
if {[catch {update_module_reference system_pl_estop_2} update_module_msg]} {
  fail "failed to refresh pl_estop module reference after AXIS_COUNT update: $update_module_msg"
}
require_bd_pin_width pl_estop/step_in 7 0
require_bd_pin_width pl_estop/enable_in 7 0

ensure_vector_port axis_puls_o O 7 0
ensure_vector_port axis_dir_o O 7 0
ensure_vector_port axis_ena_o O 7 0
ensure_vector_port axis_enc_a_i I 7 0
ensure_vector_port axis_enc_b_i I 7 0
ensure_vector_port axis_enc_z_i I 7 0
ensure_scalar_port estop_nc_in I

ensure_xlconstant axis_const_zero8 8 0
ensure_xlconstant pl_estop_axis_zero 8 0

rewire_pin_to_port step_ip/step_o axis_puls_o
rewire_pin_to_port step_ip/dir_o axis_dir_o
rewire_pin_to_port axis_const_zero8/dout axis_ena_o
rewire_port_to_pin axis_enc_a_i step_ip/enc_a_i
rewire_port_to_pin axis_enc_b_i step_ip/enc_b_i
rewire_port_to_pin axis_enc_z_i step_ip/enc_z_i
rewire_pin_to_pin pl_estop_axis_zero/dout pl_estop/step_in
disconnect_pin_from_nets_if_present pl_estop/enable_in
connect_bd_net [get_bd_pins pl_estop_axis_zero/dout] [get_bd_pins pl_estop/enable_in]

# Make the PL E-stop AXI status/IRQ observe the same top-level input that gates
# DO/PWM and GMII TX; do not leave the saved BD tied to a fail-closed constant.
rewire_port_to_pin estop_nc_in pl_estop/estop_nc_in

validate_bd_design
save_bd_design
generate_target all [get_files $bd_file]
make_wrapper -files [get_files $bd_file] -top -import -force
update_compile_order -fileset sources_1
close_project
exit 0
