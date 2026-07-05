param(
  [string]$ProjectDir
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

function Assert-Match {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )

  if ($Text -notmatch $Pattern) {
    throw "Missing ADC XADC mapping evidence: $Label"
  }
}

function Assert-NoMatch {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )

  if ($Text -match $Pattern) {
    throw "Forbidden retired ADC SPI/XADC mapping evidence found: $Label"
  }
}

function Get-Text {
  param([string]$Path)

  if (-not (Test-Path -LiteralPath $Path)) {
    throw "Missing required file: $Path"
  }
  return Get-Content -LiteralPath $Path -Raw -Encoding UTF8
}

$sourceXdcPath = Join-Path (Split-Path -Parent $ProjectDir) 'z20-v1_5_20260623.xdc'
$activeXdcPath = Join-Path $ProjectDir 'constraints/z20_v1_5_active_mapped.xdc'
$topPath = Join-Path $ProjectDir 'rtl/system_top.v'
$wrapperPath = Join-Path $ProjectDir 'z20_v1_5_hw_project.srcs/sources_1/imports/hdl/system_wrapper.v'
$bdPath = Join-Path $ProjectDir 'z20_v1_5_hw_project.srcs/sources_1/bd/system/system.bd'
$portMappingPath = Join-Path $ProjectDir 'docs/port_mapping.md'

$sourceXdc = Get-Text -Path $sourceXdcPath
$activeXdc = Get-Text -Path $activeXdcPath
$top = Get-Text -Path $topPath
$wrapper = Get-Text -Path $wrapperPath
$bd = Get-Text -Path $bdPath
$portMapping = Get-Text -Path $portMappingPath

Assert-Match -Text $sourceXdc -Pattern '## ADC one-channel XADC dedicated analog input' -Label 'v1.5 one-channel XADC ADC section'
Assert-Match -Text $sourceXdc -Pattern 'ADC_IN1 uses the dedicated XADC VP/VN analog pair as one differential ADC channel' -Label 'ADC_IN1 XADC VP/VN owner'
Assert-Match -Text $sourceXdc -Pattern 'Do not use MCP3202 SPI for the ADC in this revision' -Label 'MCP3202 ADC retired note'
Assert-Match -Text $sourceXdc -Pattern 'XADC_VP / ADC_P:.*package L11' -Label 'XADC VP package note'
Assert-Match -Text $sourceXdc -Pattern 'XADC_VN / ADC_N:.*package M12' -Label 'XADC VN package note'

Assert-NoMatch -Text $sourceXdc -Pattern '(?m)^\s*set_io\s+\{ADC_SPI_' -Label 'ADC_SPI source set_io'
Assert-Match -Text $sourceXdc -Pattern '(?m)^\s*set_io\s+\{FPGA1_IO1_P\}\s+U10\s*$' -Label 'FPGA1_IO1_P spare restored'
Assert-Match -Text $sourceXdc -Pattern '(?m)^\s*set_io\s+\{FPGA1_IO1_N\}\s+U9\s*$' -Label 'FPGA1_IO1_N spare restored'
Assert-Match -Text $sourceXdc -Pattern '(?m)^\s*set_io\s+\{FPGA1_IO2_P\}\s+AA12\s*$' -Label 'FPGA1_IO2_P spare restored'
Assert-Match -Text $sourceXdc -Pattern '(?m)^\s*set_io\s+\{FPGA1_IO2_N\}\s+AB12\s*$' -Label 'FPGA1_IO2_N spare restored'

Assert-NoMatch -Text $activeXdc -Pattern 'get_ports\s+\{adc_spi_(cs_n|sclk|mosi|miso)\}' -Label 'active ADC SPI top ports'
Assert-NoMatch -Text $activeXdc -Pattern 'PACKAGE_PIN\s+(U10|U9|AA12|AB12)\s+\[get_ports' -Label 'retired ADC SPI physical pins in active XDC'
Assert-NoMatch -Text $activeXdc -Pattern 'get_ports\s+\{[^}]*(XADC|ADC_P|ADC_N|XADC_VP|XADC_VN)[^}]*\}' -Label 'active XADC/ADC_P/ADC_N ports'
Assert-NoMatch -Text $activeXdc -Pattern 'PACKAGE_PIN\s+(L11|M12)\s+\[get_ports' -Label 'active XADC L11/M12 package pins'
Assert-Match -Text $activeXdc -Pattern 'ADC_IN1 uses XADC_VP/XADC_VN dedicated analog pins L11/M12' -Label 'active XDC XADC note'

Assert-NoMatch -Text $top -Pattern '(?m)^\s*(input|output|inout)\s+adc_spi_' -Label 'system_top ADC SPI board-level ports'
Assert-NoMatch -Text $top -Pattern 'adc_spi_' -Label 'system_top ADC SPI tie-off residue'
Assert-NoMatch -Text $wrapper -Pattern '(?m)^\s*(input|output|inout)\s+(adc_spi_(cs_n|sclk|mosi|miso))\s*;' -Label 'system_wrapper ADC SPI external ports'
Assert-NoMatch -Text $bd -Pattern '"adc_spi_(cs_n|sclk|mosi|miso)"\s*:' -Label 'BD ADC SPI external ports'

Assert-Match -Text $portMapping -Pattern '## ADC One-Channel XADC Mapping' -Label 'port mapping XADC section'
Assert-Match -Text $portMapping -Pattern 'ADC SPI through MCP3202 is retired for the ADC function' -Label 'port mapping retired ADC SPI note'
Assert-Match -Text $portMapping -Pattern 'Do not add normal PL `PACKAGE_PIN` constraints for `XADC_VP`/`XADC_VN`' -Label 'port mapping XADC PL-package prohibition'

Write-Output 'adc_mapping=ok'
Write-Output 'adc_owner=XADC_VP_VN_ONE_CHANNEL'
Write-Output 'adc_channel_count=1'
Write-Output 'adc_xadc_pins=L11,M12'
Write-Output 'adc_spi_mapping=retired'
Write-Output 'adc_spi_pins=none'
Write-Output 'adc_spi_top_ports=retired'
Write-Output 'xadc_one_channel_adc=enabled'
Write-Output 'xadc_two_channel_adc=not_used'
Write-Output 'legacy_fpga1_io12_spare_constraints=source_restored_active_unassigned'
Write-Output 'active_adc_spi_assignments=0'
Write-Output 'active_xadc_assignments=0'
