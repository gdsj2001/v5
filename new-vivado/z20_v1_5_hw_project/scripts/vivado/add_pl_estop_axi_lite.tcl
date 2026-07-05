set script_dir [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join $script_dir ".." ".."]]
set project_file "z20_v1_5_hw_project.xpr"
set bd_file [file join "z20_v1_5_hw_project.srcs" "sources_1" "bd" "system" "system.bd"]
set estop_cell pl_estop
set estop_ref pl_estop_axi_lite_v3
set estop_base 0x41260000
set estop_range 64K

proc fail {message} {
  puts "ERROR: $message"
  exit 2
}

proc require_bd_cell {cell_name} {
  if {[llength [get_bd_cells -quiet $cell_name]] == 0} {
    fail "required BD cell not found: $cell_name"
  }
}

proc connect_pin_if_open {src_pin dst_pin} {
  if {[llength [get_bd_pins -quiet $src_pin]] == 0} {
    fail "source pin not found: $src_pin"
  }
  if {[llength [get_bd_pins -quiet $dst_pin]] == 0} {
    fail "destination pin not found: $dst_pin"
  }
  if {[llength [get_bd_nets -of_objects [get_bd_pins $dst_pin]]] == 0} {
    connect_bd_net [get_bd_pins $src_pin] [get_bd_pins $dst_pin]
  }
}

proc connect_intf_if_open {src_pin dst_pin} {
  if {[llength [get_bd_intf_pins -quiet $src_pin]] == 0} {
    fail "source interface pin not found: $src_pin"
  }
  if {[llength [get_bd_intf_pins -quiet $dst_pin]] == 0} {
    fail "destination interface pin not found: $dst_pin"
  }
  if {[llength [get_bd_intf_nets -of_objects [get_bd_intf_pins $dst_pin]]] == 0} {
    connect_bd_intf_net [get_bd_intf_pins $src_pin] [get_bd_intf_pins $dst_pin]
  }
}

cd $project_dir
open_project $project_file

foreach rtl_file [list \
    [file join $project_dir "rtl" "pl_estop_core.v"] \
    [file join $project_dir "rtl" "pl_estop_axi_lite.v"]] {
  if {![file exists $rtl_file]} {
    fail "RTL file not found: $rtl_file"
  }
  if {[llength [get_files -quiet $rtl_file]] == 0} {
    add_files -fileset sources_1 $rtl_file
  }
}
update_compile_order -fileset sources_1

open_bd_design $bd_file

foreach required_cell [list processing_system7_0 ps7_0_axi_periph rst_ps7_0_100M xlconcat_0 cnc_const_zero] {
  require_bd_cell $required_cell
}

if {[llength [get_bd_cells -quiet $estop_cell]] == 0} {
  create_bd_cell -type module -reference $estop_ref $estop_cell
} else {
  set rebuild_estop 0
  if {[llength [info commands update_module_reference]] > 0} {
    if {[catch {update_module_reference [get_bd_cells $estop_cell]} update_message]} {
      puts "WARN: update_module_reference failed for $estop_cell: $update_message"
    }
  }
  if {[llength [get_bd_pins -quiet $estop_cell/general_output_in]] == 0 ||
      [llength [get_bd_pins -quiet $estop_cell/bus_tx_enable_in]] == 0 ||
      [llength [get_bd_pins -quiet $estop_cell/bus_tx_queue_flushed_in]] == 0} {
    puts "WARN: $estop_cell is missing expected v3 pins after refresh; rebuilding module reference"
    set rebuild_estop 1
  }
  if {$rebuild_estop} {
    delete_bd_objs [get_bd_cells $estop_cell]
    foreach stale_net [get_bd_intf_nets -quiet -of_objects [get_bd_intf_pins -quiet ps7_0_axi_periph/M21_AXI]] {
      delete_bd_objs $stale_net
    }
    foreach stale_net [get_bd_nets -quiet -of_objects [get_bd_pins -quiet xlconcat_0/In14]] {
      delete_bd_objs $stale_net
    }
    foreach ip_root [list \
        [file join $project_dir "z20_v1_5_hw_project.srcs" "sources_1" "bd" "system" "ip"] \
        [file join $project_dir "z20_v1_5_hw_project.gen" "sources_1" "bd" "system" "ip"]] {
      foreach stale_dir [glob -nocomplain [file join $ip_root "system_pl_estop_\[0-9\]*"]] {
        file delete -force $stale_dir
      }
    }
    create_bd_cell -type module -reference $estop_ref $estop_cell
  }
}

set axi_periph [get_bd_cells ps7_0_axi_periph]
set num_mi [get_property CONFIG.NUM_MI $axi_periph]
if {$num_mi < 22} {
  set_property CONFIG.NUM_MI 22 $axi_periph
}

if {[llength [get_bd_intf_pins -quiet ps7_0_axi_periph/M21_AXI]] == 0} {
  fail "ps7_0_axi_periph/M21_AXI was not created after NUM_MI update"
}
if {[llength [get_bd_intf_nets -of_objects [get_bd_intf_pins ps7_0_axi_periph/M21_AXI]]] > 0 &&
    [llength [get_bd_intf_nets -of_objects [get_bd_intf_pins $estop_cell/S_AXI]]] == 0} {
  fail "ps7_0_axi_periph/M21_AXI is already connected to another slave"
}
connect_intf_if_open ps7_0_axi_periph/M21_AXI $estop_cell/S_AXI

connect_pin_if_open processing_system7_0/FCLK_CLK0 $estop_cell/S_AXI_ACLK
connect_pin_if_open rst_ps7_0_100M/peripheral_aresetn $estop_cell/S_AXI_ARESETN
connect_pin_if_open processing_system7_0/FCLK_CLK0 ps7_0_axi_periph/M21_ACLK
connect_pin_if_open rst_ps7_0_100M/peripheral_aresetn ps7_0_axi_periph/M21_ARESETN

connect_pin_if_open cnc_const_zero/dout $estop_cell/estop_nc_in

set axis_zero_cell pl_estop_axis_zero
if {[llength [get_bd_cells -quiet $axis_zero_cell]] == 0} {
  create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 $axis_zero_cell
}
set_property -dict [list CONFIG.CONST_WIDTH {8} CONFIG.CONST_VAL {0}] [get_bd_cells $axis_zero_cell]
connect_pin_if_open $axis_zero_cell/dout $estop_cell/step_in
connect_pin_if_open $axis_zero_cell/dout $estop_cell/enable_in

set general_zero_cell pl_estop_do_zero
set old_general_zero_cell pl_estop_general_output_zero
if {[llength [get_bd_cells -quiet $old_general_zero_cell]] > 0 &&
    [llength [get_bd_cells -quiet $general_zero_cell]] == 0} {
  set_property name $general_zero_cell [get_bd_cells $old_general_zero_cell]
}
if {[llength [get_bd_cells -quiet $general_zero_cell]] == 0} {
  create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 $general_zero_cell
}
set_property -dict [list CONFIG.CONST_WIDTH {16} CONFIG.CONST_VAL {0}] [get_bd_cells $general_zero_cell]
connect_pin_if_open $general_zero_cell/dout $estop_cell/general_output_in

set bus_tx_zero_cell pl_estop_tx_zero
if {[llength [get_bd_cells -quiet $bus_tx_zero_cell]] == 0} {
  create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 $bus_tx_zero_cell
}
set_property -dict [list CONFIG.CONST_WIDTH {1} CONFIG.CONST_VAL {0}] [get_bd_cells $bus_tx_zero_cell]
connect_pin_if_open $bus_tx_zero_cell/dout $estop_cell/bus_tx_enable_in

set bus_tx_flushed_cell pl_estop_tx_flushed
if {[llength [get_bd_cells -quiet $bus_tx_flushed_cell]] == 0} {
  create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 $bus_tx_flushed_cell
}
set_property -dict [list CONFIG.CONST_WIDTH {1} CONFIG.CONST_VAL {1}] [get_bd_cells $bus_tx_flushed_cell]
connect_pin_if_open $bus_tx_flushed_cell/dout $estop_cell/bus_tx_queue_flushed_in

set num_ports [get_property CONFIG.NUM_PORTS [get_bd_cells xlconcat_0]]
if {$num_ports < 15} {
  set_property CONFIG.NUM_PORTS 15 [get_bd_cells xlconcat_0]
}
if {[llength [get_bd_pins -quiet xlconcat_0/In14]] == 0} {
  fail "xlconcat_0/In14 was not created after NUM_PORTS update"
}
if {[llength [get_bd_nets -of_objects [get_bd_pins xlconcat_0/In14]]] > 0 &&
    [llength [get_bd_nets -of_objects [get_bd_pins $estop_cell/estop_irq]]] == 0} {
  fail "xlconcat_0/In14 is already connected to another interrupt source"
}
connect_pin_if_open $estop_cell/estop_irq xlconcat_0/In14

assign_bd_address
set estop_addr_segs {}
foreach seg [get_bd_addr_segs -quiet -of_objects [get_bd_addr_spaces processing_system7_0/Data]] {
  if {[string match "*pl_estop*" $seg]} {
    lappend estop_addr_segs $seg
  }
}
if {[llength $estop_addr_segs] == 0} {
  fail "no address segment found for $estop_cell"
}
foreach seg $estop_addr_segs {
  set_property offset $estop_base $seg
  set_property range $estop_range $seg
}

validate_bd_design
save_bd_design
close_project
exit 0
