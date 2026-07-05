set script_dir [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join $script_dir ".." ".."]]
set project_file "z20_v1_5_hw_project.xpr"
set bd_file [file join "z20_v1_5_hw_project.srcs" "sources_1" "bd" "system" "system.bd"]
set owner_cell z20_v15_io_owner
set owner_ref z20_v15_io_owner_axi_lite
set owner_base 0x41270000
set owner_range 64K
set owner_mi_index 22
set owner_mi_name [format "M%02d_AXI" $owner_mi_index]

proc fail {message} {
  puts "ERROR: $message"
  exit 2
}

proc require_bd_cell {cell_name} {
  if {[llength [get_bd_cells -quiet $cell_name]] == 0} {
    fail "required BD cell not found: $cell_name"
  }
}

proc bd_endpoint {name} {
  set pins [get_bd_pins -quiet $name]
  if {[llength $pins] > 0} {
    return $pins
  }
  set ports [get_bd_ports -quiet $name]
  if {[llength $ports] > 0} {
    return $ports
  }
  return ""
}

proc disconnect_endpoint {name} {
  set obj [bd_endpoint $name]
  if {$obj eq ""} {
    fail "endpoint not found for disconnect: $name"
  }
  foreach net [get_bd_nets -quiet -of_objects $obj] {
    disconnect_bd_net $net $obj
  }
}

proc connect_endpoint_if_open {src_name dst_name} {
  set src [bd_endpoint $src_name]
  set dst [bd_endpoint $dst_name]
  if {$src eq ""} {
    fail "source endpoint not found: $src_name"
  }
  if {$dst eq ""} {
    fail "destination endpoint not found: $dst_name"
  }
  set src_nets [get_bd_nets -quiet -of_objects $src]
  set dst_nets [get_bd_nets -quiet -of_objects $dst]
  if {[llength $dst_nets] == 0} {
    connect_bd_net $src $dst
    return
  }
  if {[llength $src_nets] == 0} {
    fail "$dst_name is already connected and $src_name is open"
  }
  if {[lindex $src_nets 0] ne [lindex $dst_nets 0]} {
    fail "$dst_name is already connected to a different net"
  }
}

proc connect_intf_if_open {src_pin dst_pin} {
  if {[llength [get_bd_intf_pins -quiet $src_pin]] == 0} {
    fail "source interface pin not found: $src_pin"
  }
  if {[llength [get_bd_intf_pins -quiet $dst_pin]] == 0} {
    fail "destination interface pin not found: $dst_pin"
  }
  set src_nets [get_bd_intf_nets -quiet -of_objects [get_bd_intf_pins $src_pin]]
  set dst_nets [get_bd_intf_nets -quiet -of_objects [get_bd_intf_pins $dst_pin]]
  if {[llength $dst_nets] == 0} {
    connect_bd_intf_net [get_bd_intf_pins $src_pin] [get_bd_intf_pins $dst_pin]
    return
  }
  if {[llength $src_nets] == 0} {
    fail "$dst_pin is already connected and $src_pin is open"
  }
  if {[lindex $src_nets 0] ne [lindex $dst_nets 0]} {
    fail "$dst_pin is already connected to a different interface net"
  }
}

proc ensure_port {name dir width} {
  set existing [get_bd_ports -quiet $name]
  if {[llength $existing] == 0} {
    if {$width == 1} {
      create_bd_port -dir $dir $name
    } else {
      create_bd_port -dir $dir -from [expr {$width - 1}] -to 0 $name
    }
    return
  }
  set port [get_bd_ports $name]
  set current_dir [get_property DIR $port]
  if {$current_dir ne $dir} {
    fail "BD port $name direction is $current_dir, expected $dir"
  }
  if {$width > 1} {
    set left [get_property LEFT $port]
    set right [get_property RIGHT $port]
    if {$left ne [expr {$width - 1}] || $right ne 0} {
      fail "BD port $name width mismatch"
    }
  }
}

cd $project_dir
open_project $project_file

set owner_rtl [file join $project_dir "rtl" "z20_v15_io_owner_axi_lite.v"]
if {![file exists $owner_rtl]} {
  fail "RTL file not found: $owner_rtl"
}
if {[llength [get_files -quiet $owner_rtl]] == 0} {
  add_files -fileset sources_1 $owner_rtl
}
update_compile_order -fileset sources_1

open_bd_design $bd_file

foreach required_cell [list processing_system7_0 ps7_0_axi_periph rst_ps7_0_100M step_ip] {
  require_bd_cell $required_cell
}

if {[llength [get_bd_cells -quiet $owner_cell]] == 0} {
  create_bd_cell -type module -reference $owner_ref $owner_cell
} else {
  if {[llength [info commands update_module_reference]] > 0} {
    if {[catch {update_module_reference [get_bd_cells $owner_cell]} update_message]} {
      puts "WARN: update_module_reference failed for $owner_cell: $update_message"
    }
  }
  if {[llength [get_bd_pins -quiet $owner_cell/do_o]] == 0 ||
      [llength [get_bd_pins -quiet $owner_cell/pwm_o]] == 0 ||
      [llength [get_bd_pins -quiet $owner_cell/axis_ena_o]] == 0 ||
      [llength [get_bd_pins -quiet $owner_cell/tp_rst_n_o]] == 0} {
    fail "$owner_cell is missing expected IO owner pins after refresh"
  }
}

set axi_periph [get_bd_cells ps7_0_axi_periph]
set required_mi_count [expr {$owner_mi_index + 1}]
set num_mi [get_property CONFIG.NUM_MI $axi_periph]
if {$num_mi < $required_mi_count} {
  set_property CONFIG.NUM_MI $required_mi_count $axi_periph
}

if {[llength [get_bd_intf_pins -quiet ps7_0_axi_periph/$owner_mi_name]] == 0} {
  fail "ps7_0_axi_periph/$owner_mi_name was not created after NUM_MI update"
}
connect_intf_if_open ps7_0_axi_periph/$owner_mi_name $owner_cell/S_AXI

connect_endpoint_if_open processing_system7_0/FCLK_CLK0 $owner_cell/S_AXI_ACLK
connect_endpoint_if_open rst_ps7_0_100M/peripheral_aresetn $owner_cell/S_AXI_ARESETN
connect_endpoint_if_open processing_system7_0/FCLK_CLK0 ps7_0_axi_periph/M22_ACLK
connect_endpoint_if_open rst_ps7_0_100M/peripheral_aresetn ps7_0_axi_periph/M22_ARESETN

ensure_port io_owner_di_i I 18
ensure_port io_owner_fr_di_i I 16
ensure_port io_owner_ts_di_i I 1
ensure_port io_owner_mpg_axis_sel_i I 8
ensure_port io_owner_mpg_a_i I 1
ensure_port io_owner_mpg_b_i I 1
ensure_port io_owner_scale_sel_i I 3
ensure_port io_owner_alarm_i I 8
ensure_port io_owner_tp_int_i I 1
ensure_port io_owner_do_o O 14
ensure_port io_owner_pwm_o O 2
ensure_port io_owner_tp_rst_n_o O 1

connect_endpoint_if_open io_owner_di_i $owner_cell/di_i
connect_endpoint_if_open io_owner_fr_di_i $owner_cell/fr_di_i
connect_endpoint_if_open io_owner_ts_di_i $owner_cell/ts_di_i
connect_endpoint_if_open io_owner_mpg_axis_sel_i $owner_cell/mpg_axis_sel_i
connect_endpoint_if_open io_owner_mpg_a_i $owner_cell/mpg_a_i
connect_endpoint_if_open io_owner_mpg_b_i $owner_cell/mpg_b_i
connect_endpoint_if_open io_owner_scale_sel_i $owner_cell/scale_sel_i
connect_endpoint_if_open io_owner_alarm_i $owner_cell/alarm_i
connect_endpoint_if_open io_owner_tp_int_i $owner_cell/tp_int_i
connect_endpoint_if_open $owner_cell/do_o io_owner_do_o
connect_endpoint_if_open $owner_cell/pwm_o io_owner_pwm_o
connect_endpoint_if_open $owner_cell/tp_rst_n_o io_owner_tp_rst_n_o

set axis_ena_port axis_ena_o
set axis_ena_owner_nets [get_bd_nets -quiet -of_objects [get_bd_pins -quiet $owner_cell/axis_ena_o]]
set axis_ena_port_nets [get_bd_nets -quiet -of_objects [get_bd_ports -quiet $axis_ena_port]]
if {[llength $axis_ena_port_nets] > 0 &&
    ([llength $axis_ena_owner_nets] == 0 || [lindex $axis_ena_port_nets 0] ne [lindex $axis_ena_owner_nets 0])} {
  delete_bd_objs $axis_ena_port_nets
}
connect_endpoint_if_open $owner_cell/axis_ena_o $axis_ena_port

assign_bd_address
set owner_addr_segs {}
foreach seg [get_bd_addr_segs -quiet -of_objects [get_bd_addr_spaces processing_system7_0/Data]] {
  if {[string match "*$owner_cell*" $seg]} {
    lappend owner_addr_segs $seg
  }
}
if {[llength $owner_addr_segs] == 0} {
  fail "no address segment found for $owner_cell"
}
foreach seg $owner_addr_segs {
  set_property offset $owner_base $seg
  set_property range $owner_range $seg
}

validate_bd_design
save_bd_design
generate_target all [get_files $bd_file]
make_wrapper -files [get_files $bd_file] -top -import -force
update_compile_order -fileset sources_1
close_project
exit 0
