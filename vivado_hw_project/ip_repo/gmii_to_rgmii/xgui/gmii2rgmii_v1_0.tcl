# Definitional proc to organize widgets for parameters.
proc init_gui { IPINST } {
  ipgui::add_param $IPINST -name "Component_Name"
  #Adding Page
  set Page_0 [ipgui::add_page $IPINST -name "Page 0"]
  set IDELAY_VALUE [ipgui::add_param $IPINST -name "IDELAY_VALUE" -parent ${Page_0}]
  set_property tooltip {RX input delay tap count (0..31).} ${IDELAY_VALUE}
  set IDELAY_TYPE [ipgui::add_param $IPINST -name "IDELAY_TYPE" -parent ${Page_0} -widget comboBox]
  set_property tooltip {IDELAY mode is fixed to FIXED for this core implementation.} ${IDELAY_TYPE}
  set_property enabled false ${IDELAY_TYPE}
  set TX_CLK_FROM_RX [ipgui::add_param $IPINST -name "TX_CLK_FROM_RX" -parent ${Page_0}]
  set_property tooltip {1 uses recovered RX clock for GMII TX clock; 0 uses raw RGMII_RXC.} ${TX_CLK_FROM_RX}


}

proc update_PARAM_VALUE.IDELAY_VALUE { PARAM_VALUE.IDELAY_VALUE } {
	# Procedure called to update IDELAY_VALUE when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.IDELAY_VALUE { PARAM_VALUE.IDELAY_VALUE } {
	# Procedure called to validate IDELAY_VALUE
	set v [get_property value ${PARAM_VALUE.IDELAY_VALUE}]
	if {![string is integer -strict $v]} {
		set_property errmsg "IDELAY_VALUE must be an integer in 0..31." ${PARAM_VALUE.IDELAY_VALUE}
		return false
	}
	if { $v < 0 || $v > 31 } {
		set_property errmsg "IDELAY_VALUE valid range is 0..31." ${PARAM_VALUE.IDELAY_VALUE}
		return false
	}
	return true
}

proc update_PARAM_VALUE.IDELAY_TYPE { PARAM_VALUE.IDELAY_TYPE } {
	# Procedure called to update IDELAY_TYPE when any of the dependent parameters in the arguments change
	set_property value "FIXED" ${PARAM_VALUE.IDELAY_TYPE}
}

proc validate_PARAM_VALUE.IDELAY_TYPE { PARAM_VALUE.IDELAY_TYPE } {
	# Procedure called to validate IDELAY_TYPE
	set t [string toupper [get_property value ${PARAM_VALUE.IDELAY_TYPE}]]
	if { $t ne "FIXED" } {
		set_property errmsg "IDELAY_TYPE must be FIXED for this core implementation." ${PARAM_VALUE.IDELAY_TYPE}
		return false
	}
	return true
}

proc update_PARAM_VALUE.TX_CLK_FROM_RX { PARAM_VALUE.TX_CLK_FROM_RX } {
	# Procedure called to update TX_CLK_FROM_RX when any dependent parameters change.
}

proc validate_PARAM_VALUE.TX_CLK_FROM_RX { PARAM_VALUE.TX_CLK_FROM_RX } {
	set v [get_property value ${PARAM_VALUE.TX_CLK_FROM_RX}]
	if {![string is integer -strict $v]} {
		set_property errmsg "TX_CLK_FROM_RX must be 0 or 1." ${PARAM_VALUE.TX_CLK_FROM_RX}
		return false
	}
	if { $v < 0 || $v > 1 } {
		set_property errmsg "TX_CLK_FROM_RX valid range is 0..1." ${PARAM_VALUE.TX_CLK_FROM_RX}
		return false
	}
	return true
}


proc update_MODELPARAM_VALUE.IDELAY_VALUE { MODELPARAM_VALUE.IDELAY_VALUE PARAM_VALUE.IDELAY_VALUE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.IDELAY_VALUE}] ${MODELPARAM_VALUE.IDELAY_VALUE}
}

proc update_MODELPARAM_VALUE.IDELAY_TYPE { MODELPARAM_VALUE.IDELAY_TYPE PARAM_VALUE.IDELAY_TYPE } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value "FIXED" ${MODELPARAM_VALUE.IDELAY_TYPE}
}

proc update_MODELPARAM_VALUE.TX_CLK_FROM_RX { MODELPARAM_VALUE.TX_CLK_FROM_RX PARAM_VALUE.TX_CLK_FROM_RX } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.TX_CLK_FROM_RX}] ${MODELPARAM_VALUE.TX_CLK_FROM_RX}
}
