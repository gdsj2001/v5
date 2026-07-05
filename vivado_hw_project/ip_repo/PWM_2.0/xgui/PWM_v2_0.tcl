# Definitional proc to organize widgets for parameters.
proc init_gui { IPINST } {
  ipgui::add_param $IPINST -name "Component_Name"
  #Adding Page
  set Page_0 [ipgui::add_page $IPINST -name "Page 0"]
  set NUM_PWM [ipgui::add_param $IPINST -name "NUM_PWM" -parent ${Page_0}]
  set_property tooltip {Number of PWM signals to output} ${NUM_PWM}
  set POLARITY [ipgui::add_param $IPINST -name "POLARITY" -parent ${Page_0} -widget comboBox]
  set_property tooltip {The polarity of the output pulse. Setting this to Low will cause larger duty cycle values to result in smaller pulses} ${POLARITY}


}

proc update_PARAM_VALUE.NUM_PWM { PARAM_VALUE.NUM_PWM } {
	# Procedure called to update NUM_PWM when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.NUM_PWM { PARAM_VALUE.NUM_PWM } {
	# Procedure called to validate NUM_PWM
	set v [get_property value ${PARAM_VALUE.NUM_PWM}]
	if {![string is integer -strict $v]} {
		set_property errmsg "NUM_PWM must be an integer in 1..16." ${PARAM_VALUE.NUM_PWM}
		return false
	}
	if { $v < 1 || $v > 16 } {
		set_property errmsg "NUM_PWM valid range is 1..16." ${PARAM_VALUE.NUM_PWM}
		return false
	}
	return true
}

proc update_PARAM_VALUE.POLARITY { PARAM_VALUE.POLARITY } {
	# Procedure called to update POLARITY when any of the dependent parameters in the arguments change
	set p [get_property value ${PARAM_VALUE.POLARITY}]
	if { $p == "\"1\"" } {
		set_property value 1 ${PARAM_VALUE.POLARITY}
	} elseif { $p == "\"0\"" } {
		set_property value 0 ${PARAM_VALUE.POLARITY}
	}
}

proc validate_PARAM_VALUE.POLARITY { PARAM_VALUE.POLARITY } {
	# Procedure called to validate POLARITY
	set p [get_property value ${PARAM_VALUE.POLARITY}]
	if { !($p == 0 || $p == 1 || $p == "\"0\"" || $p == "\"1\"") } {
		set_property errmsg "POLARITY must be 0 or 1." ${PARAM_VALUE.POLARITY}
		return false
	}
	return true
}

proc update_PARAM_VALUE.C_PWM_AXI_DATA_WIDTH { PARAM_VALUE.C_PWM_AXI_DATA_WIDTH } {
	# Procedure called to update C_PWM_AXI_DATA_WIDTH when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.C_PWM_AXI_DATA_WIDTH { PARAM_VALUE.C_PWM_AXI_DATA_WIDTH } {
	# Procedure called to validate C_PWM_AXI_DATA_WIDTH
	set v [get_property value ${PARAM_VALUE.C_PWM_AXI_DATA_WIDTH}]
	if {![string is integer -strict $v]} {
		set_property errmsg "C_PWM_AXI_DATA_WIDTH must be integer 32." ${PARAM_VALUE.C_PWM_AXI_DATA_WIDTH}
		return false
	}
	if { $v != 32 } {
		set_property errmsg "C_PWM_AXI_DATA_WIDTH must be 32." ${PARAM_VALUE.C_PWM_AXI_DATA_WIDTH}
		return false
	}
	return true
}

proc update_PARAM_VALUE.C_PWM_AXI_ADDR_WIDTH { PARAM_VALUE.C_PWM_AXI_ADDR_WIDTH } {
	# Procedure called to update C_PWM_AXI_ADDR_WIDTH when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.C_PWM_AXI_ADDR_WIDTH { PARAM_VALUE.C_PWM_AXI_ADDR_WIDTH } {
	# Procedure called to validate C_PWM_AXI_ADDR_WIDTH
	set v [get_property value ${PARAM_VALUE.C_PWM_AXI_ADDR_WIDTH}]
	if {![string is integer -strict $v]} {
		set_property errmsg "C_PWM_AXI_ADDR_WIDTH must be an integer >= 7." ${PARAM_VALUE.C_PWM_AXI_ADDR_WIDTH}
		return false
	}
	if { $v < 7 } {
		set_property errmsg "C_PWM_AXI_ADDR_WIDTH must be >= 7." ${PARAM_VALUE.C_PWM_AXI_ADDR_WIDTH}
		return false
	}
	return true
}

proc update_PARAM_VALUE.C_PWM_AXI_BASEADDR { PARAM_VALUE.C_PWM_AXI_BASEADDR } {
	# Procedure called to update C_PWM_AXI_BASEADDR when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.C_PWM_AXI_BASEADDR { PARAM_VALUE.C_PWM_AXI_BASEADDR } {
	# Procedure called to validate C_PWM_AXI_BASEADDR
	return true
}

proc update_PARAM_VALUE.C_PWM_AXI_HIGHADDR { PARAM_VALUE.C_PWM_AXI_HIGHADDR } {
	# Procedure called to update C_PWM_AXI_HIGHADDR when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.C_PWM_AXI_HIGHADDR { PARAM_VALUE.C_PWM_AXI_HIGHADDR } {
	# Procedure called to validate C_PWM_AXI_HIGHADDR
	return true
}


proc update_MODELPARAM_VALUE.C_PWM_AXI_DATA_WIDTH { MODELPARAM_VALUE.C_PWM_AXI_DATA_WIDTH PARAM_VALUE.C_PWM_AXI_DATA_WIDTH } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.C_PWM_AXI_DATA_WIDTH}] ${MODELPARAM_VALUE.C_PWM_AXI_DATA_WIDTH}
}

proc update_MODELPARAM_VALUE.C_PWM_AXI_ADDR_WIDTH { MODELPARAM_VALUE.C_PWM_AXI_ADDR_WIDTH PARAM_VALUE.C_PWM_AXI_ADDR_WIDTH } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.C_PWM_AXI_ADDR_WIDTH}] ${MODELPARAM_VALUE.C_PWM_AXI_ADDR_WIDTH}
}

proc update_MODELPARAM_VALUE.NUM_PWM { MODELPARAM_VALUE.NUM_PWM PARAM_VALUE.NUM_PWM } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.NUM_PWM}] ${MODELPARAM_VALUE.NUM_PWM}
}

proc update_MODELPARAM_VALUE.POLARITY { MODELPARAM_VALUE.POLARITY PARAM_VALUE.POLARITY } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set p [get_property value ${PARAM_VALUE.POLARITY}]
	if { $p == "\"1\"" } {
		set p 1
	} elseif { $p == "\"0\"" } {
		set p 0
	}
	set_property value $p ${MODELPARAM_VALUE.POLARITY}
}

