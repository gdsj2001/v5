param(
  [string]$ProjectDir
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

function Read-TextFile {
  param(
    [string]$Path,
    [string]$Label
  )
  if (-not (Test-Path -LiteralPath $Path)) {
    throw "Missing required file for $Label`: $Path"
  }
  return Get-Content -LiteralPath $Path -Raw -Encoding UTF8
}

function Assert-Match {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )
  if ($Text -notmatch $Pattern) {
    throw "Missing PL E-stop bus-gate owner evidence: $Label"
  }
}

$projectRoot = (Resolve-Path -LiteralPath $ProjectDir).Path
$repoRoot = (Resolve-Path -LiteralPath (Join-Path (Join-Path $projectRoot '..') '..')).Path

$hardwareProfilePath = Join-Path $repoRoot 'config/hardware_profile.json'
$bdPath = Get-ProjectPath -RootDir $projectRoot -RelativePath 'z20_v1_5_hw_project.srcs/sources_1/bd/system/system.bd'
$synthPath = Get-ProjectPath -RootDir $projectRoot -RelativePath 'z20_v1_5_hw_project.gen/sources_1/bd/system/synth/system.v'
$wiringPath = Get-ProjectPath -RootDir $projectRoot -RelativePath 'docs/pl_estop_wiring_evidence.csv'
$boardPath = Get-ProjectPath -RootDir $projectRoot -RelativePath 'docs/pl_estop_board_validation_evidence.csv'

$hardwareProfile = (Read-TextFile -Path $hardwareProfilePath -Label 'hardware profile') | ConvertFrom-Json
$bd = Read-TextFile -Path $bdPath -Label 'system.bd'
$synth = Read-TextFile -Path $synthPath -Label 'generated system synth'

if ($hardwareProfile.current_production_profile -ne 'ethercat') {
  throw "Current production profile is not ethercat: $($hardwareProfile.current_production_profile)"
}
$productionProfile = $hardwareProfile.supported_profiles.ethercat
if ($productionProfile.transport -ne 'EtherCAT over PS GEM1/EMIO') {
  throw "Unexpected ethercat transport: $($productionProfile.transport)"
}

Assert-Match -Text $bd -Pattern '"PCW_EN_EMIO_ENET1"\s*:\s*\{\s*"value"\s*:\s*"1"\s*\}' -Label 'PS ENET1 EMIO enabled'
Assert-Match -Text $bd -Pattern '"processing_system7_0_GMII_ETHERNET_1"\s*:\s*\{(?s:.*?)"gmii2rgmii_0/GMII"(?s:.*?)"processing_system7_0/GMII_ETHERNET_1"' -Label 'PS GEM1 GMII connected to gmii2rgmii'
Assert-Match -Text $bd -Pattern '"gmii2rgmii_0_RGMII"\s*:\s*\{(?s:.*?)"rgmii"(?s:.*?)"gmii2rgmii_0/RGMII"' -Label 'gmii2rgmii connected to external RGMII'

Assert-Match -Text $synth -Pattern 'pl_estop_bus_tx_gate\s+#\((?s:.*?)\)\s+system_rgmii_tx_ctl_estop_gate' -Label 'bus TX gate instance exists'
Assert-Match -Text $synth -Pattern '\.estop_latched\(estop_hw_active\)' -Label 'bus TX gate uses physical E-stop hard path'
Assert-Match -Text $synth -Pattern '\.tx_enable_in\(processing_system7_0_GMII_ETHERNET_1_TX_EN\)' -Label 'bus TX gate input is PS GEM1 TX_EN'
Assert-Match -Text $synth -Pattern '\.tx_enable_out\(gmii_tx_enable_gated\)' -Label 'bus TX gate output drives gated GMII TX enable'
Assert-Match -Text $synth -Pattern "assign\s+gmii_txd_gated\s*=\s*gmii_tx_gate_active\s*\?\s*8'h00\s*:\s*processing_system7_0_GMII_ETHERNET_1_TXD;" -Label 'bus TX gate forces TXD idle zero'
Assert-Match -Text $synth -Pattern "assign\s+gmii_tx_er_gated\s*=\s*gmii_tx_gate_active\s*\?\s*1'b0\s*:\s*processing_system7_0_GMII_ETHERNET_1_TX_ER;" -Label 'bus TX gate clears TX_ER'
Assert-Match -Text $synth -Pattern '\.gmii_tx_en\(gmii_tx_enable_gated\[0\]\)' -Label 'gmii2rgmii consumes gated TX_EN'
Assert-Match -Text $synth -Pattern '\.gmii_tx_er\(gmii_tx_er_gated\)' -Label 'gmii2rgmii consumes gated TX_ER'
Assert-Match -Text $synth -Pattern '\.gmii_txd\(gmii_txd_gated\)' -Label 'gmii2rgmii consumes gated TXD'
Assert-Match -Text $synth -Pattern '\.gmii_rx_clk\(processing_system7_0_GMII_ETHERNET_1_RX_CLK\)' -Label 'RX clock remains connected to PS GEM1'
Assert-Match -Text $synth -Pattern '\.gmii_rx_dv\(processing_system7_0_GMII_ETHERNET_1_RX_DV\)' -Label 'RX data-valid remains connected to PS GEM1'
Assert-Match -Text $synth -Pattern '\.gmii_rxd\(processing_system7_0_GMII_ETHERNET_1_RXD\)' -Label 'RX data remains connected to PS GEM1'
Assert-Match -Text $synth -Pattern 'assign\s+mdio_mdc\s*=\s*processing_system7_0_MDIO_ETHERNET_1_MDC;' -Label 'MDIO clock remains PS-owned'
Assert-Match -Text $synth -Pattern 'assign\s+processing_system7_0_MDIO_ETHERNET_1_MDIO_I\s*=\s*mdio_mdio_i;' -Label 'MDIO input remains PS-owned'

if (-not (Test-Path -LiteralPath $wiringPath)) {
  throw 'Missing docs/pl_estop_wiring_evidence.csv'
}
if (-not (Test-Path -LiteralPath $boardPath)) {
  throw 'Missing docs/pl_estop_board_validation_evidence.csv'
}
$wiringRows = @(Import-Csv -LiteralPath $wiringPath)
$boardRows = @(Import-Csv -LiteralPath $boardPath)

$driverRows = @($wiringRows | Where-Object { $_.signal_group -eq 'bus_tx_gate' -and $_.signal_name -eq 'bus_tx_driver_enable' })
$queueRows = @($wiringRows | Where-Object { $_.signal_group -eq 'bus_tx_gate' -and $_.signal_name -eq 'bus_tx_queue_flush' })
if ($driverRows.Count -ne 1 -or $queueRows.Count -ne 1) {
  throw "Expected exactly one bus TX driver row and one queue row, got driver=$($driverRows.Count) queue=$($queueRows.Count)"
}
if ($driverRows[0].evidence_state -ne 'ready_for_rtl_xdc' -or $queueRows[0].evidence_state -ne 'ready_for_rtl_xdc') {
  throw 'Bus TX wiring rows are not locally ready for RTL/XDC'
}

foreach ($testId in @('BV10', 'BV11', 'BV12')) {
  $matches = @($boardRows | Where-Object { $_.test_id -eq $testId })
  if ($matches.Count -ne 1) {
    throw "Expected exactly one board-validation row for $testId, found $($matches.Count)"
  }
  if ($matches[0].evidence_state -ne 'pending') {
    throw "$testId must remain pending until real board evidence exists"
  }
}

Write-Output 'pl_estop_bus_gate_owner=ps_gem1_emio_rgmii_local_verified'
Write-Output "production_profile=$($hardwareProfile.current_production_profile)"
Write-Output "production_transport=$($productionProfile.transport)"
Write-Output 'bd_enet1_emio=enabled'
Write-Output 'bd_ps_gem1_to_gmii2rgmii=ok'
Write-Output 'gate_inserted_before_gmii2rgmii=ok'
Write-Output 'tx_en_gated=yes'
Write-Output 'txd_forced_idle_zero=yes'
Write-Output 'tx_er_forced_idle_zero=yes'
Write-Output 'rx_path_preserved_by_design=yes'
Write-Output 'mdio_path_preserved_by_design=yes'
Write-Output 'link_reset_power_gate=not_used'
Write-Output 'queue_flush_owner=pl_no_fifo_ps_queue_board_evidence_required'
Write-Output 'board_tests_required=BV10,BV11,BV12'
Write-Output 'board_evidence_state=pending'
