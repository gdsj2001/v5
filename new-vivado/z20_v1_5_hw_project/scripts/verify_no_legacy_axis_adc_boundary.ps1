$ErrorActionPreference = 'Stop'

$ProjectDir = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')

function Read-Text {
  param([string]$RelativePath)
  $path = Join-Path $ProjectDir ($RelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing file: $RelativePath"
  }
  return Get-Content -LiteralPath $path -Raw -Encoding UTF8
}

function Assert-Match {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )
  if ($Text -notmatch $Pattern) {
    throw "Missing expected current boundary evidence: $Label"
  }
}

function Assert-NoMatch {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )
  if ($Text -match $Pattern) {
    throw "Forbidden legacy boundary evidence found: $Label"
  }
}

$top = Read-Text 'rtl/system_top.v'
$wrapper = Read-Text 'z20_v1_5_hw_project.srcs/sources_1/imports/hdl/system_wrapper.v'
$bd = Read-Text 'z20_v1_5_hw_project.srcs/sources_1/bd/system/system.bd'
$bdSynth = Read-Text 'z20_v1_5_hw_project.gen/sources_1/bd/system/synth/system.v'
$stepIpXci = Read-Text 'z20_v1_5_hw_project.srcs/sources_1/bd/system/ip/system_axi_stepdir_enc_v2_0_0/system_axi_stepdir_enc_v2_0_0.xci'
$plEstopXci = Read-Text 'z20_v1_5_hw_project.srcs/sources_1/bd/system/ip/system_pl_estop_2/system_pl_estop_2.xci'
$activeXdc = Read-Text 'constraints/z20_v1_5_active_mapped.xdc'
$ioOwnerRtl = Read-Text 'rtl/z20_v15_io_owner_axi_lite.v'

foreach ($textAndLabel in @(
    @($top, 'system_top'),
    @($wrapper, 'system_wrapper'),
    @($activeXdc, 'active XDC')
  )) {
  $text = [string]$textAndLabel[0]
  $label = [string]$textAndLabel[1]
  Assert-NoMatch -Text $text -Pattern '(?m)^\s*(input|output|inout)\s+(?:\[[^\]]+\]\s*)?(step_o|dir_o|enc_a_i|enc_b_i|enc_z_i|adc_spi_(cs_n|sclk|mosi|miso))\s*;' -Label "$label old top-level port declaration"
  Assert-NoMatch -Text $text -Pattern 'get_ports\s+\{(?:step_o|dir_o|enc_a_i|enc_b_i|enc_z_i|adc_spi_(cs_n|sclk|mosi|miso))' -Label "$label old active port constraint"
}

Assert-NoMatch -Text $top -Pattern '\blegacy_(step|dir|enc)' -Label 'system_top legacy axis adapter names'
Assert-NoMatch -Text $top -Pattern 'axis_ena_normal\s*=\s*8''h00' -Label 'system_top local ENA all-zero owner'
Assert-NoMatch -Text $top -Pattern 'do_pwm_normal\s*=\s*16''h0000' -Label 'system_top local DO/PWM all-zero owner'
Assert-NoMatch -Text $top -Pattern '(axis_alarm_inputs_keep|digital_inputs_keep|mpg_inputs_keep)' -Label 'system_top keep-only input placeholders'
Assert-NoMatch -Text $top -Pattern 'legacy_rs485_(rxd_tieoff|txd_unused)' -Label 'system_top RS485 tie-off'
Assert-NoMatch -Text $top -Pattern 'adc_spi_' -Label 'system_top ADC SPI tie-off'
Assert-Match -Text $top -Pattern 'do_pwm_normal\s*=\s*\{wrapper_io_owner_pwm_o,\s*wrapper_io_owner_do_o\}' -Label 'system_top DO/PWM normal owner from IO owner'
Assert-Match -Text $top -Pattern '\.io_owner_di_i\(wrapper_io_owner_di_i\)' -Label 'system_top DI reaches IO owner'
Assert-Match -Text $top -Pattern '\.io_owner_fr_di_i\(wrapper_io_owner_fr_di_i\)' -Label 'system_top FR_DI reaches IO owner'
Assert-Match -Text $top -Pattern '\.io_owner_mpg_axis_sel_i\(wrapper_io_owner_mpg_axis_sel_i\)' -Label 'system_top MPG axis select reaches IO owner'
Assert-Match -Text $top -Pattern '\.io_owner_alarm_i\(wrapper_io_owner_alarm_i\)' -Label 'system_top ALM reaches IO owner'
Assert-Match -Text $top -Pattern 'TP_RST\s*=\s*wrapper_io_owner_tp_rst_n_o' -Label 'system_top touch reset from IO owner'
Assert-Match -Text $top -Pattern '\.rs485_rxd\(RS485_FPGA_RX\)' -Label 'system_top RS485 RX exported'
Assert-Match -Text $top -Pattern '\.rs485_txd\(RS485_FPGA_TX\)' -Label 'system_top RS485 TX exported'

Assert-Match -Text $wrapper -Pattern '(?m)^\s*output\s+\[7:0\]axis_puls_o;\s*$' -Label 'wrapper 8-bit pulse boundary'
Assert-Match -Text $wrapper -Pattern '(?m)^\s*output\s+\[7:0\]axis_dir_o;\s*$' -Label 'wrapper 8-bit direction boundary'
Assert-Match -Text $wrapper -Pattern '(?m)^\s*output\s+\[7:0\]axis_ena_o;\s*$' -Label 'wrapper 8-bit enable boundary'
Assert-Match -Text $wrapper -Pattern '(?m)^\s*input\s+\[7:0\]axis_enc_a_i;\s*$' -Label 'wrapper 8-bit encoder A boundary'
Assert-Match -Text $wrapper -Pattern '(?m)^\s*input\s+\[7:0\]axis_enc_b_i;\s*$' -Label 'wrapper 8-bit encoder B boundary'
Assert-Match -Text $wrapper -Pattern '(?m)^\s*input\s+\[7:0\]axis_enc_z_i;\s*$' -Label 'wrapper 8-bit encoder Z boundary'
Assert-Match -Text $wrapper -Pattern '(?m)^\s*input\s+\[17:0\]io_owner_di_i;\s*$' -Label 'wrapper DI input owner boundary'
Assert-Match -Text $wrapper -Pattern '(?m)^\s*input\s+\[15:0\]io_owner_fr_di_i;\s*$' -Label 'wrapper FR_DI input owner boundary'
Assert-Match -Text $wrapper -Pattern '(?m)^\s*output\s+\[13:0\]io_owner_do_o;\s*$' -Label 'wrapper DO owner boundary'
Assert-Match -Text $wrapper -Pattern '(?m)^\s*output\s+\[1:0\]io_owner_pwm_o;\s*$' -Label 'wrapper PWM owner boundary'
Assert-Match -Text $wrapper -Pattern '(?m)^\s*output\s+io_owner_tp_rst_n_o;\s*$' -Label 'wrapper touch reset owner boundary'

Assert-NoMatch -Text $bd -Pattern '"step_o"\s*:' -Label 'BD old step_o external port'
Assert-NoMatch -Text $bd -Pattern '"dir_o"\s*:' -Label 'BD old dir_o external port'
Assert-NoMatch -Text $bd -Pattern '"enc_[abz]_i"\s*:' -Label 'BD old encoder external ports'
Assert-NoMatch -Text $bd -Pattern '"adc_spi_(cs_n|sclk|mosi|miso)"\s*:' -Label 'BD old ADC SPI external ports'
Assert-Match -Text $bd -Pattern '"axis_puls_o"\s*:' -Label 'BD current pulse external port'
Assert-Match -Text $bd -Pattern '"axis_dir_o"\s*:' -Label 'BD current direction external port'
Assert-Match -Text $bd -Pattern '"axis_ena_o"\s*:' -Label 'BD current enable external port'
Assert-NoMatch -Text $bd -Pattern '"axis_puls_zero2"|"axis_dir_zero2"|"axis_enc_[abz]_low6"|"axis_puls_8_concat"|"axis_dir_8_concat"' -Label 'BD padded or sliced axis adapter cells'
Assert-Match -Text $bd -Pattern '"step_ip/step_o"[\s\S]*"axis_puls_o"' -Label 'BD direct 8-bit pulse owner'
Assert-Match -Text $bd -Pattern '"step_ip/dir_o"[\s\S]*"axis_dir_o"' -Label 'BD direct 8-bit direction owner'
Assert-Match -Text $bd -Pattern '"axis_enc_a_i"[\s\S]*"step_ip/enc_a_i"' -Label 'BD direct encoder A owner'
Assert-Match -Text $bd -Pattern '"axis_enc_b_i"[\s\S]*"step_ip/enc_b_i"' -Label 'BD direct encoder B owner'
Assert-Match -Text $bd -Pattern '"axis_enc_z_i"[\s\S]*"step_ip/enc_z_i"' -Label 'BD direct encoder Z owner'
Assert-Match -Text $bd -Pattern '"z20_v15_io_owner"\s*:' -Label 'BD IO owner cell'
Assert-Match -Text $bd -Pattern '"ps7_0_axi_periph/M22_AXI"[\s\S]*"z20_v15_io_owner/S_AXI"' -Label 'BD IO owner AXI M22 connection'
Assert-Match -Text $bd -Pattern '"SEG_z20_v15_io_owner_reg0"[\s\S]*"offset"\s*:\s*"0x41270000"[\s\S]*"range"\s*:\s*"64K"' -Label 'BD IO owner address'
Assert-Match -Text $bd -Pattern '"z20_v15_io_owner/axis_ena_o"[\s\S]*"axis_ena_o"' -Label 'BD axis ENA from IO owner'
Assert-Match -Text $bd -Pattern '"z20_v15_io_owner/do_o"[\s\S]*"io_owner_do_o"' -Label 'BD DO from IO owner'
Assert-Match -Text $bd -Pattern '"z20_v15_io_owner/pwm_o"[\s\S]*"io_owner_pwm_o"' -Label 'BD PWM from IO owner'
Assert-Match -Text $bd -Pattern '"io_owner_di_i"[\s\S]*"z20_v15_io_owner/di_i"' -Label 'BD DI reaches IO owner'
Assert-Match -Text $bd -Pattern '"io_owner_fr_di_i"[\s\S]*"z20_v15_io_owner/fr_di_i"' -Label 'BD FR_DI reaches IO owner'
Assert-NoMatch -Text $bd -Pattern '"axis_const_zero8/dout"[\s\S]*"axis_ena_o"' -Label 'BD axis ENA all-zero placeholder'
Assert-Match -Text $bd -Pattern '"N_AXES"[\s\S]*"value"\s*:\s*"8"' -Label 'BD step_ip N_AXES=8'
Assert-Match -Text $stepIpXci -Pattern 'MODELPARAM_VALUE\.N_AXES">8<' -Label 'step_ip XCI N_AXES=8'
Assert-Match -Text $stepIpXci -Pattern 'PARAM_VALUE\.N_AXES">8<' -Label 'step_ip parameter N_AXES=8'
Assert-Match -Text $plEstopXci -Pattern 'MODELPARAM_VALUE\.AXIS_COUNT">8<' -Label 'PL E-stop XCI AXIS_COUNT=8'
Assert-Match -Text $plEstopXci -Pattern '&quot;step_in&quot;:\{&quot;direction&quot;:&quot;in&quot;,&quot;physical_left&quot;:&quot;7&quot;' -Label 'PL E-stop step_in 8-bit generated boundary'
Assert-Match -Text $plEstopXci -Pattern '&quot;enable_in&quot;:\{&quot;direction&quot;:&quot;in&quot;,&quot;physical_left&quot;:&quot;7&quot;' -Label 'PL E-stop enable_in 8-bit generated boundary'
Assert-NoMatch -Text $bdSynth -Pattern 'axis_puls_zero2|axis_dir_zero2|axis_enc_[abz]_low6|axis_puls_8_concat|axis_dir_8_concat' -Label 'generated padded or sliced axis adapter cells'
Assert-Match -Text $bdSynth -Pattern '(?m)^\s*wire\s+\[7:0\]step_ip_step_o\d*;' -Label 'generated 8-bit step_ip step wire'
Assert-Match -Text $bdSynth -Pattern '(?m)^\s*wire\s+\[7:0\]step_ip_dir_o\d*;' -Label 'generated 8-bit step_ip dir wire'
Assert-Match -Text $bdSynth -Pattern 'assign\s+axis_puls_o\[7:0\]\s*=\s*step_ip_step_o\d*;' -Label 'generated direct 8-bit pulse output'
Assert-Match -Text $bdSynth -Pattern 'assign\s+axis_dir_o\[7:0\]\s*=\s*step_ip_dir_o\d*;' -Label 'generated direct 8-bit direction output'
Assert-Match -Text $bdSynth -Pattern 'assign\s+axis_ena_o\[7:0\]\s*=\s*z20_v15_io_owner_axis_ena_o;' -Label 'generated axis enable from IO owner'
Assert-Match -Text $bdSynth -Pattern 'assign\s+io_owner_do_o\[13:0\]\s*=\s*z20_v15_io_owner_do_o;' -Label 'generated DO from IO owner'
Assert-Match -Text $bdSynth -Pattern 'assign\s+io_owner_pwm_o\[1:0\]\s*=\s*z20_v15_io_owner_pwm_o;' -Label 'generated PWM from IO owner'
Assert-Match -Text $bdSynth -Pattern 'system_z20_v15_io_owner_0\s+z20_v15_io_owner' -Label 'generated IO owner instance'
Assert-Match -Text $bdSynth -Pattern 'assign\s+axis_enc_a_i_\d+\s*=\s*axis_enc_a_i\[7:0\];' -Label 'generated direct 8-bit encoder A input'
Assert-Match -Text $bdSynth -Pattern '\.enc_a_i\(axis_enc_a_i_\d+\)' -Label 'generated encoder A reaches step_ip'
Assert-Match -Text $bdSynth -Pattern '(?m)^\s*wire\s+\[7:0\]pl_estop_axis_zero_dout\d*;' -Label 'generated 8-bit PL E-stop axis zero'
Assert-Match -Text $bdSynth -Pattern '\.step_in\(pl_estop_axis_zero_dout\d*\)' -Label 'generated PL E-stop step gate width source'
Assert-Match -Text $bdSynth -Pattern '\.enable_in\(pl_estop_axis_zero_dout\d*\)' -Label 'generated PL E-stop enable gate width source'
Assert-Match -Text $bdSynth -Pattern 'assign\s+estop_nc_in_\d+\s*=\s*estop_nc_in;' -Label 'generated physical E-stop assignment'
Assert-Match -Text $bdSynth -Pattern '\.estop_nc_in\(estop_nc_in_\d+\)' -Label 'generated PL E-stop observes physical input'
Assert-Match -Text $activeXdc -Pattern 'get_ports\s+\{RS485_FPGA_RX\}' -Label 'active XDC RS485 RX pin'
Assert-Match -Text $activeXdc -Pattern 'get_ports\s+\{RS485_FPGA_TX\}' -Label 'active XDC RS485 TX pin'
Assert-Match -Text $activeXdc -Pattern 'get_ports\s+\{TP_INT\}' -Label 'active XDC touch interrupt pin'
Assert-Match -Text $activeXdc -Pattern 'get_ports\s+\{TP_RST\}' -Label 'active XDC touch reset pin'
Assert-Match -Text $ioOwnerRtl -Pattern 'REG_MAGIC\s*=\s*32''h494f4f57' -Label 'IO owner register magic'
Assert-Match -Text $ioOwnerRtl -Pattern 'ADDR_PWM_PERIOD' -Label 'IO owner PWM period register'
Assert-Match -Text $ioOwnerRtl -Pattern 'tp_rst_n_reg\s*<=\s*1''b1' -Label 'IO owner touch reset default released'

Write-Output 'legacy_axis_adc_boundary=retired'
Write-Output 'wrapper_axis_boundary=current_8bit'
Write-Output 'bd_adc_spi_external_boundary=retired'
Write-Output 'axis_functional_completion=vivado_io_owner_connected'
Write-Output 'axis_motion_owner=step_ip_8axis_stepdir_encoder_direct'
Write-Output 'axis_ena_owner=z20_v15_io_owner_axi_lite'
Write-Output 'axis_78_encoder_processing=connected_to_step_ip'
Write-Output 'di_mpg_alarm_processing=z20_v15_io_owner_input_registers'
Write-Output 'do_pwm_normal_owner=z20_v15_io_owner_do_pwm'
Write-Output 'rs485_boundary=exported_ps_uart1_emio'
Write-Output 'touch_int_rst_boundary=z20_v15_io_owner_tp_int_rst'
