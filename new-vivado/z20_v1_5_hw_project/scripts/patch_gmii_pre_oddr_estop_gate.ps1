param(
  [string]$ProjectDir,
  [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

function Get-ProjectPath {
  param(
    [string]$RootDir,
    [string]$RelativePath
  )
  return Join-Path $RootDir ($RelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
}

function Read-NormalizedText {
  param([string]$Path)

  if (-not (Test-Path -LiteralPath $Path)) {
    throw "Missing required generated file: $Path"
  }
  $text = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
  return (($text -replace "`r`n", "`n") -replace "`r", "`n")
}

function Write-Utf8NoBom {
  param(
    [string]$Path,
    [string]$Text
  )

  $encoding = New-Object System.Text.UTF8Encoding($false)
  [System.IO.File]::WriteAllText($Path, $Text, $encoding)
}

function Replace-FirstExact {
  param(
    [string]$Text,
    [string]$Needle,
    [string]$Replacement,
    [string]$Label
  )

  $index = $Text.IndexOf($Needle, [System.StringComparison]::Ordinal)
  if ($index -lt 0) {
    throw "Cannot apply GMII pre-ODDR E-stop patch; missing anchor: $Label"
  }
  return $Text.Substring(0, $index) + $Replacement + $Text.Substring($index + $Needle.Length)
}

function Replace-FirstExisting {
  param(
    [string]$Text,
    [string[]]$Needles,
    [string]$Replacement,
    [string]$Label
  )

  foreach ($needle in $Needles) {
    $index = $Text.IndexOf($needle, [System.StringComparison]::Ordinal)
    if ($index -ge 0) {
      return $Text.Substring(0, $index) + $Replacement + $Text.Substring($index + $needle.Length)
    }
  }
  throw "Cannot apply GMII pre-ODDR E-stop patch; missing anchor: $Label"
}

function Assert-Match {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )

  if ($Text -notmatch $Pattern) {
    throw "Missing GMII pre-ODDR E-stop patch evidence: $Label"
  }
}

function Assert-NoMatch {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )

  if ($Text -match $Pattern) {
    throw "Forbidden generated E-stop boundary patch evidence found: $Label"
  }
}

function Patch-SystemWrapper {
  param([string]$Text)

  $changed = $false

  if ($Text -notmatch '(?m)^\s*estop_nc_in,\s*$') {
    $Text = Replace-FirstExact -Text $Text `
      -Needle "    axis_enc_z_i,`n    i2c0_scl_io," `
      -Replacement "    axis_enc_z_i,`n    estop_nc_in,`n    i2c0_scl_io," `
      -Label 'system_wrapper module port list'
    $changed = $true
  }

  if ($Text -notmatch '(?m)^\s*input\s+estop_nc_in;\s*$') {
    $Text = Replace-FirstExact -Text $Text `
      -Needle "  input [7:0]axis_enc_z_i;`n  inout i2c0_scl_io;" `
      -Replacement "  input [7:0]axis_enc_z_i;`n  input estop_nc_in;`n  inout i2c0_scl_io;" `
      -Label 'system_wrapper input declaration'
    $changed = $true
  }

  if ($Text -notmatch '(?m)^\s*wire\s+estop_nc_in;\s*$') {
    $Text = Replace-FirstExact -Text $Text `
      -Needle "  wire [7:0]axis_enc_z_i;`n  wire i2c0_scl_i;" `
      -Replacement "  wire [7:0]axis_enc_z_i;`n  wire estop_nc_in;`n  wire i2c0_scl_i;" `
      -Label 'system_wrapper internal wire'
    $changed = $true
  }

  if ($Text -notmatch '\.estop_nc_in\(estop_nc_in\),') {
    $Text = Replace-FirstExact -Text $Text `
      -Needle "        .axis_enc_z_i(axis_enc_z_i),`n        .i2c0_scl_i(i2c0_scl_i)," `
      -Replacement "        .axis_enc_z_i(axis_enc_z_i),`n        .estop_nc_in(estop_nc_in),`n        .i2c0_scl_i(i2c0_scl_i)," `
      -Label 'system_wrapper system_i estop connection'
    $changed = $true
  }

  return [pscustomobject]@{
    text = $Text
    changed = $changed
  }
}

function Patch-SystemSynth {
  param([string]$Text)

  $changed = $false

  if ($Text -notmatch '(?m)^\s*estop_nc_in,\s*$') {
    $Text = Replace-FirstExact -Text $Text `
      -Needle "    axis_enc_z_i,`n    i2c0_scl_i," `
      -Replacement "    axis_enc_z_i,`n    estop_nc_in,`n    i2c0_scl_i," `
      -Label 'system synth module port list'
    $changed = $true
  }

  if ($Text -notmatch '(?m)^\s*input\s+estop_nc_in;\s*$') {
    $Text = Replace-FirstExact -Text $Text `
      -Needle "  input [7:0]axis_enc_z_i;`n  (* X_INTERFACE_INFO = `"xilinx.com:interface:iic:1.0 i2c0 SCL_I`" *) input i2c0_scl_i;" `
      -Replacement "  input [7:0]axis_enc_z_i;`n  input estop_nc_in;`n  (* X_INTERFACE_INFO = `"xilinx.com:interface:iic:1.0 i2c0 SCL_I`" *) input i2c0_scl_i;" `
      -Label 'system synth input declaration'
    $changed = $true
  }

  if ($Text -notmatch '(?m)^\s*wire\s+estop_hw_active;\s*$') {
    $Text = Replace-FirstExisting -Text $Text `
      -Needles @("  wire estop_nc_in_2;`n  wire [3:0]gmii2rgmii_0_RGMII_RD;") `
      -Replacement "  wire estop_nc_in_2;`n  wire estop_hw_active;`n  wire [0:0]gmii_tx_enable_gated;`n  wire gmii_tx_gate_active;`n  wire gmii_tx_queue_flush_req_unused;`n  wire [7:0]gmii_txd_gated;`n  wire gmii_tx_er_gated;`n  wire [3:0]gmii2rgmii_0_RGMII_RD;" `
      -Label 'system synth GMII gate wires'
    $changed = $true
  }

  if ($Text -notmatch 'assign\s+estop_hw_active\s*=\s*~estop_nc_in;') {
    $Text = Replace-FirstExisting -Text $Text `
      -Needles @("  assign estop_nc_in_2 = estop_nc_in;`n  assign gmii2rgmii_0_RGMII_RD = rgmii_rd[3:0];") `
      -Replacement "  assign estop_nc_in_2 = estop_nc_in;`n  assign estop_hw_active = ~estop_nc_in;`n  assign gmii_txd_gated = gmii_tx_gate_active ? 8'h00 : processing_system7_0_GMII_ETHERNET_1_TXD;`n  assign gmii_tx_er_gated = gmii_tx_gate_active ? 1'b0 : processing_system7_0_GMII_ETHERNET_1_TX_ER;`n  assign gmii2rgmii_0_RGMII_RD = rgmii_rd[3:0];" `
      -Label 'system synth GMII gate assignments'
    $changed = $true
  }

  if ($Text -notmatch 'system_rgmii_tx_ctl_estop_gate') {
    $Text = Replace-FirstExact -Text $Text `
      -Needle "  system_gmii2rgmii_0_0 gmii2rgmii_0`n" `
      -Replacement "  pl_estop_bus_tx_gate #(`n        .GATE_COUNT(1),`n        .IDLE_LEVELS(1'b0)`n    ) system_rgmii_tx_ctl_estop_gate (`n        .estop_latched(estop_hw_active),`n        .tx_enable_in(processing_system7_0_GMII_ETHERNET_1_TX_EN),`n        .tx_enable_out(gmii_tx_enable_gated),`n        .queue_flush_req(gmii_tx_queue_flush_req_unused),`n        .gate_active(gmii_tx_gate_active));`n  system_gmii2rgmii_0_0 gmii2rgmii_0`n" `
      -Label 'system synth GMII gate instance'
    $changed = $true
  }

  if ($Text -notmatch '\.gmii_tx_en\(gmii_tx_enable_gated\[0\]\)') {
    $Text = Replace-FirstExact -Text $Text `
      -Needle "        .gmii_tx_en(processing_system7_0_GMII_ETHERNET_1_TX_EN)," `
      -Replacement "        .gmii_tx_en(gmii_tx_enable_gated[0])," `
      -Label 'gmii2rgmii gated TX_EN connection'
    $changed = $true
  }

  if ($Text -notmatch '\.gmii_tx_er\(gmii_tx_er_gated\)') {
    $Text = Replace-FirstExact -Text $Text `
      -Needle "        .gmii_tx_er(processing_system7_0_GMII_ETHERNET_1_TX_ER)," `
      -Replacement "        .gmii_tx_er(gmii_tx_er_gated)," `
      -Label 'gmii2rgmii gated TX_ER connection'
    $changed = $true
  }

  if ($Text -notmatch '\.gmii_txd\(gmii_txd_gated\)') {
    $Text = Replace-FirstExact -Text $Text `
      -Needle "        .gmii_txd(processing_system7_0_GMII_ETHERNET_1_TXD)," `
      -Replacement "        .gmii_txd(gmii_txd_gated)," `
      -Label 'gmii2rgmii gated TXD connection'
    $changed = $true
  }

  if ($Text -match '\.estop_nc_in\(cnc_const_zero_dout\),') {
    $Text = Replace-FirstExact -Text $Text `
      -Needle "        .estop_nc_in(cnc_const_zero_dout),`n" `
      -Replacement "        .estop_nc_in(estop_nc_in),`n" `
      -Label 'pl_estop AXI status observation input'
    $changed = $true
  }

  return [pscustomobject]@{
    text = $Text
    changed = $changed
  }
}

function Verify-PatchedFiles {
  param(
    [string]$WrapperText,
    [string]$SystemText
  )

  Assert-Match -Text $WrapperText -Pattern '(?m)^\s*input\s+estop_nc_in;\s*$' -Label 'wrapper estop input'
  Assert-Match -Text $WrapperText -Pattern '\.estop_nc_in\(estop_nc_in\),' -Label 'wrapper passes estop to system'
  Assert-Match -Text $SystemText -Pattern '(?m)^\s*input\s+estop_nc_in;\s*$' -Label 'system synth estop input'
  Assert-Match -Text $SystemText -Pattern 'assign\s+estop_hw_active\s*=\s*~estop_nc_in;' -Label 'system synth estop active level'
  Assert-Match -Text $SystemText -Pattern 'pl_estop_bus_tx_gate\s*#\(' -Label 'system synth bus TX gate module'
  Assert-Match -Text $SystemText -Pattern 'system_rgmii_tx_ctl_estop_gate' -Label 'system synth gate instance name'
  Assert-Match -Text $SystemText -Pattern '\.tx_enable_in\(processing_system7_0_GMII_ETHERNET_1_TX_EN\)' -Label 'gate source is GMII TX_EN'
  Assert-Match -Text $SystemText -Pattern '\.tx_enable_out\(gmii_tx_enable_gated\)' -Label 'gate output is GMII TX_EN'
  Assert-Match -Text $SystemText -Pattern "assign\s+gmii_txd_gated\s*=\s*gmii_tx_gate_active\s*\?\s*8'h00\s*:\s*processing_system7_0_GMII_ETHERNET_1_TXD;" -Label 'GMII TXD zeroed while gated'
  Assert-Match -Text $SystemText -Pattern "assign\s+gmii_tx_er_gated\s*=\s*gmii_tx_gate_active\s*\?\s*1'b0\s*:\s*processing_system7_0_GMII_ETHERNET_1_TX_ER;" -Label 'GMII TX_ER cleared while gated'
  Assert-Match -Text $SystemText -Pattern '\.gmii_tx_en\(gmii_tx_enable_gated\[0\]\)' -Label 'gmii2rgmii consumes gated TX_EN'
  Assert-Match -Text $SystemText -Pattern '\.gmii_tx_er\(gmii_tx_er_gated\)' -Label 'gmii2rgmii consumes gated TX_ER'
  Assert-Match -Text $SystemText -Pattern '\.gmii_txd\(gmii_txd_gated\)' -Label 'gmii2rgmii consumes gated TXD'
  Assert-Match -Text $SystemText -Pattern 'assign\s+rgmii_tx_ctl\s*=\s*gmii2rgmii_0_RGMII_TX_CTL;' -Label 'RGMII TX_CTL remains post-gmii2rgmii'
  Assert-Match -Text $SystemText -Pattern 'assign\s+rgmii_td\[3:0\]\s*=\s*gmii2rgmii_0_RGMII_TD;' -Label 'RGMII TD remains post-gmii2rgmii'
  Assert-Match -Text $SystemText -Pattern 'assign\s+rgmii_txc\s*=\s*gmii2rgmii_0_RGMII_TXC;' -Label 'RGMII TXC remains post-gmii2rgmii'
  Assert-Match -Text $SystemText -Pattern '\.estop_nc_in\(estop_nc_in(_2)?\),' -Label 'pl_estop AXI status observes physical E-stop input'
  Assert-NoMatch -Text $SystemText -Pattern '\.estop_nc_in\(cnc_const_zero_dout\),' -Label 'pl_estop AXI status must not remain tied to fail-closed constant in generated synth netlist'
}

$projectRoot = (Resolve-Path -LiteralPath $ProjectDir).Path
$wrapperPath = Get-ProjectPath -RootDir $projectRoot -RelativePath 'z20_v1_5_hw_project.srcs/sources_1/imports/hdl/system_wrapper.v'
$systemSynthPath = Get-ProjectPath -RootDir $projectRoot -RelativePath 'z20_v1_5_hw_project.gen/sources_1/bd/system/synth/system.v'

$wrapperOriginal = Read-NormalizedText -Path $wrapperPath
$systemOriginal = Read-NormalizedText -Path $systemSynthPath

$wrapperPatch = Patch-SystemWrapper -Text $wrapperOriginal
$systemPatch = Patch-SystemSynth -Text $systemOriginal

Verify-PatchedFiles -WrapperText $wrapperPatch.text -SystemText $systemPatch.text

if ($CheckOnly) {
  if ($wrapperPatch.changed -or $systemPatch.changed) {
    throw 'GMII pre-ODDR E-stop patch is required but CheckOnly was requested'
  }
} else {
  if ($wrapperPatch.changed) {
    Write-Utf8NoBom -Path $wrapperPath -Text $wrapperPatch.text
  }
  if ($systemPatch.changed) {
    Write-Utf8NoBom -Path $systemSynthPath -Text $systemPatch.text
  }
}

if ($wrapperPatch.changed -or $systemPatch.changed) {
  Write-Output 'gmii_pre_oddr_estop_patch=applied'
} else {
  Write-Output 'gmii_pre_oddr_estop_patch=already_applied'
}
Write-Output "system_wrapper_patch_changed=$($wrapperPatch.changed)"
Write-Output "system_synth_patch_changed=$($systemPatch.changed)"
Write-Output 'pl_estop_axi_observation_patch=top_estop_input'
Write-Output 'gmii_pre_oddr_estop_patch_verify=ok'
