set script_dir [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join $script_dir ".." ".."]]
set project_file $project_dir/vivado_hw_project.xpr
set rtl_file $project_dir/rtl/z20_dna_reader_axi_lite.v
set bd_file $project_dir/vivado_hw_project.srcs/sources_1/bd/system/system.bd
set dna_cell z20_dna_reader
set dna_base 0x41250000
set dna_range 64K

open_project $project_file

if {[llength [get_files -quiet $rtl_file]] == 0} {
  add_files -fileset sources_1 $rtl_file
}
update_compile_order -fileset sources_1

open_bd_design $bd_file

if {[llength [get_bd_cells -quiet $dna_cell]] == 0} {
  create_bd_cell -type module -reference z20_dna_reader_axi_lite $dna_cell
}

set axi_periph [get_bd_cells ps7_0_axi_periph]
set num_mi [get_property CONFIG.NUM_MI $axi_periph]
if {$num_mi < 21} {
  set_property CONFIG.NUM_MI 21 $axi_periph
}

if {[llength [get_bd_intf_nets -of_objects [get_bd_intf_pins $dna_cell/S_AXI]]] == 0} {
  connect_bd_intf_net [get_bd_intf_pins ps7_0_axi_periph/M20_AXI] [get_bd_intf_pins $dna_cell/S_AXI]
}

if {[llength [get_bd_nets -of_objects [get_bd_pins $dna_cell/S_AXI_ACLK]]] == 0} {
  connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] [get_bd_pins $dna_cell/S_AXI_ACLK]
}

if {[llength [get_bd_nets -of_objects [get_bd_pins $dna_cell/S_AXI_ARESETN]]] == 0} {
  connect_bd_net [get_bd_pins rst_ps7_0_100M/peripheral_aresetn] [get_bd_pins $dna_cell/S_AXI_ARESETN]
}

if {[llength [get_bd_pins -quiet ps7_0_axi_periph/M20_ACLK]] > 0 &&
    [llength [get_bd_nets -of_objects [get_bd_pins ps7_0_axi_periph/M20_ACLK]]] == 0} {
  connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] [get_bd_pins ps7_0_axi_periph/M20_ACLK]
}

if {[llength [get_bd_pins -quiet ps7_0_axi_periph/M20_ARESETN]] > 0 &&
    [llength [get_bd_nets -of_objects [get_bd_pins ps7_0_axi_periph/M20_ARESETN]]] == 0} {
  connect_bd_net [get_bd_pins rst_ps7_0_100M/peripheral_aresetn] [get_bd_pins ps7_0_axi_periph/M20_ARESETN]
}

assign_bd_address
set dna_addr_segs {}
foreach seg [get_bd_addr_segs -quiet -of_objects [get_bd_addr_spaces processing_system7_0/Data]] {
  if {[string match "*z20_dna_reader*" $seg]} {
    lappend dna_addr_segs $seg
  }
}
if {[llength $dna_addr_segs] == 0} {
  puts "ERROR: no address segment found for $dna_cell"
  exit 2
}
foreach seg $dna_addr_segs {
  set_property offset $dna_base $seg
  set_property range $dna_range $seg
}

validate_bd_design
save_bd_design
close_project
exit 0
