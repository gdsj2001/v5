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

proc delete_addr_segs_if_present {addr_seg_names} {
  foreach addr_seg_name $addr_seg_names {
    set addr_segs [get_bd_addr_segs -quiet $addr_seg_name]
    if {[llength $addr_segs] > 0} {
      delete_bd_objs $addr_segs
    }
  }
}

proc delete_external_interface_if_present {intf_name} {
  set intf_port [get_bd_intf_ports -quiet $intf_name]
  if {[llength $intf_port] == 0} {
    return
  }

  set intf_nets [get_bd_intf_nets -quiet -of_objects $intf_port]
  foreach net $intf_nets {
    delete_bd_objs $net
  }
  delete_bd_objs $intf_port
}

proc disconnect_pin_from_nets_if_present {pin_name} {
  set pin_obj [get_bd_pins -quiet $pin_name]
  if {[llength $pin_obj] == 0} {
    return
  }

  set nets [get_bd_nets -quiet -of_objects $pin_obj]
  foreach net $nets {
    disconnect_bd_net $net $pin_obj
  }
}

proc connect_pin_to_pin_allow_rewire {src_pin dst_pin} {
  require_bd_pin $src_pin
  require_bd_pin $dst_pin

  set src_obj [get_bd_pins $src_pin]
  set dst_obj [get_bd_pins $dst_pin]
  set src_nets [get_bd_nets -quiet -of_objects $src_obj]
  set dst_nets [get_bd_nets -quiet -of_objects $dst_obj]

  if {[llength $src_nets] > 0 && [llength $dst_nets] > 0 && [lindex $src_nets 0] eq [lindex $dst_nets 0]} {
    return
  }

  disconnect_pin_from_nets_if_present $dst_pin
  connect_bd_net $src_obj $dst_obj
}

proc delete_hdmi_hierarchy_if_present {} {
  set hdmi_cell [get_bd_cells -quiet hdmi_out]
  if {[llength $hdmi_cell] == 0} {
    return
  }

  delete_bd_objs $hdmi_cell
}

cd $project_dir
open_project $project_file
open_bd_design $bd_file

require_bd_cell processing_system7_0
require_bd_cell ps7_0_axi_periph
require_bd_cell xlconcat_0
require_bd_cell cnc_const_zero

# HDMI/DVI is retired. MPG owns the shared physical pins, so the BD must not
# export TMDS wrapper ports or keep the retired encoder hierarchy alive.
delete_addr_segs_if_present [list \
  "/processing_system7_0/Data/SEG_clk_wiz_dyn_Reg" \
  "/processing_system7_0/Data/SEG_hdmi_vtc_Reg" \
  "/processing_system7_0/Data/SEG_v_frmbuf_rd_0_Reg_1" \
  "/hdmi_out/v_frmbuf_rd_0/Data_m_axi_mm_video/SEG_processing_system7_0_HP1_DDR_LOWOCM" \
]
delete_external_interface_if_present tmds
delete_hdmi_hierarchy_if_present

# The retired HDMI IRQ lines used xlconcat In8/In9. Keep the interrupt vector
# width stable for software and fail closed by tying both inputs low.
foreach pin [list xlconcat_0/In8 xlconcat_0/In9] {
  connect_pin_to_pin_allow_rewire cnc_const_zero/dout $pin
}

# M14 was clocked/reset through the HDMI dynamic clock path. After HDMI removal
# keep the configured but unused AXI port on the same safe 100 MHz fabric domain.
connect_pin_to_pin_allow_rewire processing_system7_0/FCLK_CLK0 ps7_0_axi_periph/M14_ACLK
connect_pin_to_pin_allow_rewire rst_ps7_0_100M/peripheral_aresetn ps7_0_axi_periph/M14_ARESETN

# HP1 is no longer used by HDMI frame buffer traffic. Tie its clock to the
# normal fabric clock so the PS input is not left floating if HP1 stays enabled.
connect_pin_to_pin_allow_rewire processing_system7_0/FCLK_CLK0 processing_system7_0/S_AXI_HP1_ACLK

validate_bd_design
save_bd_design
generate_target all [get_files $bd_file]
make_wrapper -files [get_files $bd_file] -top -import -force
update_compile_order -fileset sources_1
close_project
exit 0
