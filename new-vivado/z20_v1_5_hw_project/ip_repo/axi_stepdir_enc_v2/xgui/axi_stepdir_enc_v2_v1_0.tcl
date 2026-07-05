# Definitional proc to organize widgets for parameters.
proc init_gui { IPINST } {
  ipgui::add_param $IPINST -name "Component_Name"
  #Adding Page
  set Page_0 [ipgui::add_page $IPINST -name "Page 0"]
  ipgui::add_param $IPINST -name "CLK_FREQ_HZ" -parent ${Page_0}
  ipgui::add_param $IPINST -name "C_S_AXI_ADDR_WIDTH" -parent ${Page_0}
  ipgui::add_param $IPINST -name "C_S_AXI_DATA_WIDTH" -parent ${Page_0}
  ipgui::add_param $IPINST -name "N_AXES" -parent ${Page_0}


}

proc update_PARAM_VALUE.CLK_FREQ_HZ { PARAM_VALUE.CLK_FREQ_HZ } {
	# Procedure called to update CLK_FREQ_HZ when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.CLK_FREQ_HZ { PARAM_VALUE.CLK_FREQ_HZ } {
	# Procedure called to validate CLK_FREQ_HZ
	set v [get_property value ${PARAM_VALUE.CLK_FREQ_HZ}]
	if {![string is integer -strict $v]} {
		set_property errmsg "CLK_FREQ_HZ must be an integer > 0." ${PARAM_VALUE.CLK_FREQ_HZ}
		return false
	}
	if { $v <= 0 } {
		set_property errmsg "CLK_FREQ_HZ must be > 0." ${PARAM_VALUE.CLK_FREQ_HZ}
		return false
	}
	return true
}

proc update_PARAM_VALUE.C_S_AXI_ADDR_WIDTH { PARAM_VALUE.C_S_AXI_ADDR_WIDTH } {
	# Procedure called to update C_S_AXI_ADDR_WIDTH when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.C_S_AXI_ADDR_WIDTH { PARAM_VALUE.C_S_AXI_ADDR_WIDTH } {
	# Procedure called to validate C_S_AXI_ADDR_WIDTH
	set v [get_property value ${PARAM_VALUE.C_S_AXI_ADDR_WIDTH}]
	if {![string is integer -strict $v]} {
		set_property errmsg "C_S_AXI_ADDR_WIDTH must be integer 12." ${PARAM_VALUE.C_S_AXI_ADDR_WIDTH}
		return false
	}
	if { $v != 12 } {
		set_property errmsg "C_S_AXI_ADDR_WIDTH must be 12 for this register map." ${PARAM_VALUE.C_S_AXI_ADDR_WIDTH}
		return false
	}
	return true
}

proc update_PARAM_VALUE.C_S_AXI_DATA_WIDTH { PARAM_VALUE.C_S_AXI_DATA_WIDTH } {
	# Procedure called to update C_S_AXI_DATA_WIDTH when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.C_S_AXI_DATA_WIDTH { PARAM_VALUE.C_S_AXI_DATA_WIDTH } {
	# Procedure called to validate C_S_AXI_DATA_WIDTH
	set v [get_property value ${PARAM_VALUE.C_S_AXI_DATA_WIDTH}]
	if {![string is integer -strict $v]} {
		set_property errmsg "C_S_AXI_DATA_WIDTH must be integer 32." ${PARAM_VALUE.C_S_AXI_DATA_WIDTH}
		return false
	}
	if { $v != 32 } {
		set_property errmsg "C_S_AXI_DATA_WIDTH must be 32." ${PARAM_VALUE.C_S_AXI_DATA_WIDTH}
		return false
	}
	return true
}

proc update_PARAM_VALUE.N_AXES { PARAM_VALUE.N_AXES } {
	# Procedure called to update N_AXES when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.N_AXES { PARAM_VALUE.N_AXES } {
	# Procedure called to validate N_AXES
	set v [get_property value ${PARAM_VALUE.N_AXES}]
	if {![string is integer -strict $v]} {
		set_property errmsg "N_AXES must be an integer in 1..16." ${PARAM_VALUE.N_AXES}
		return false
	}
	if { $v < 1 || $v > 16 } {
		set_property errmsg "N_AXES valid range is 1..16." ${PARAM_VALUE.N_AXES}
		return false
	}
	return true
}


proc update_MODELPARAM_VALUE.C_S_AXI_ADDR_WIDTH { MODELPARAM_VALUE.C_S_AXI_ADDR_WIDTH PARAM_VALUE.C_S_AXI_ADDR_WIDTH } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.C_S_AXI_ADDR_WIDTH}] ${MODELPARAM_VALUE.C_S_AXI_ADDR_WIDTH}
}

proc update_MODELPARAM_VALUE.C_S_AXI_DATA_WIDTH { MODELPARAM_VALUE.C_S_AXI_DATA_WIDTH PARAM_VALUE.C_S_AXI_DATA_WIDTH } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.C_S_AXI_DATA_WIDTH}] ${MODELPARAM_VALUE.C_S_AXI_DATA_WIDTH}
}

proc update_MODELPARAM_VALUE.N_AXES { MODELPARAM_VALUE.N_AXES PARAM_VALUE.N_AXES } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.N_AXES}] ${MODELPARAM_VALUE.N_AXES}
}

proc update_MODELPARAM_VALUE.CLK_FREQ_HZ { MODELPARAM_VALUE.CLK_FREQ_HZ PARAM_VALUE.CLK_FREQ_HZ } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.CLK_FREQ_HZ}] ${MODELPARAM_VALUE.CLK_FREQ_HZ}
}

