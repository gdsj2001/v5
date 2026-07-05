param(
  [string]$ProjectDir,
  [string]$OutPath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($OutPath)) {
  $OutPath = Join-Path $ProjectDir 'docs/active_pin_conflicts.md'
}

. (Join-Path $PSScriptRoot 'write_text_with_retry.ps1')

$verifyScript = Join-Path $ProjectDir 'scripts/verify_remaining_drc_ports.ps1'
$csvPath = Join-Path $ProjectDir 'docs/remaining_drc_ports.csv'
$bdPath = Join-Path $ProjectDir 'z20_v1_5_hw_project.srcs/sources_1/bd/system/system.bd'

if (-not (Test-Path -LiteralPath $verifyScript)) {
  throw 'Missing scripts/verify_remaining_drc_ports.ps1'
}
if (-not (Test-Path -LiteralPath $bdPath)) {
  throw 'Missing z20_v1_5_hw_project.srcs/sources_1/bd/system/system.bd'
}

function Get-PortBase {
  param([string]$PortName)
  if ($PortName -match '^(?<base>[^\[]+)\[\d+\]$') {
    return $Matches.base
  }
  return $PortName
}

function Read-BdNets {
  param([string]$Path)

  $bd = Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
  $nets = New-Object System.Collections.Generic.List[object]
  foreach ($net in $bd.design.nets.PSObject.Properties) {
    $ports = @($net.Value.ports | ForEach-Object { [string]$_ })
    $nets.Add([pscustomobject]@{
      Name = [string]$net.Name
      Ports = $ports
    })
  }
  return $nets
}

function Find-BdConnection {
  param(
    [object[]]$Nets,
    [string]$PortName
  )

  $baseName = Get-PortBase -PortName $PortName
  $matches = @($Nets | Where-Object { $_.Ports -contains $PortName -or $_.Ports -contains $baseName })
  if ($matches.Count -eq 0) {
    return $null
  }
  return (($matches | ForEach-Object {
    '{0}: {1}' -f $_.Name, ([string]::Join(' <-> ', $_.Ports))
  }) -join '; ')
}

& powershell -NoProfile -ExecutionPolicy Bypass -File $verifyScript -ProjectDir $ProjectDir | Out-Null

$rows = @(Import-Csv -LiteralPath $csvPath | Where-Object { $_.closure_blocker -eq 'active_pin_already_claimed' } | Sort-Object group, port)
$bdNets = @(Read-BdNets -Path $bdPath)

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# Active Pin Conflicts')
$lines.Add('')
$lines.Add('This file is generated from `docs/remaining_drc_ports.csv` by `scripts/export_active_pin_conflicts.ps1`.')
$lines.Add('It lists old wrapper ports whose old physical pins are already claimed by active XDC ports, plus the current BD/net connection evidence for both sides.')
$lines.Add('')
if ($rows.Count -eq 0) {
  $lines.Add('Current status: no `active_pin_already_claimed` rows remain in `docs/remaining_drc_ports.csv`.')
  $lines.Add('')
  $lines.Add('N3C-2 closure note:')
  $lines.Add('')
  $lines.Add('- The board-level top uses the current 8-axis boundary: `PULS1-8` and `DIR1-8` are driven from wrapper `axis_puls_o[7:0]` and `axis_dir_o[7:0]` through the top E-stop gate.')
  $lines.Add('- `rtl/system_top.v` exposes v1.5 encoder receiver-output inputs `A1_IO/B1_IO/Z1_IO` through `A8_IO/B8_IO/Z8_IO`; all eight A/B/Z channels feed wrapper `axis_enc_a_i[7:0]`, `axis_enc_b_i[7:0]`, and `axis_enc_z_i[7:0]`.')
  $lines.Add('- `ENA1-8` are driven by `z20_v15_io_owner_axi_lite` through the top E-stop gate. `ALM1-8`, `DI1-18`, `FR_DI1-16`, `TS_DI`, MPG, scale select, and `TP_INT` feed the IO owner input synchronizers/status registers instead of keep-only placeholders.')
  $lines.Add('- `DO1-14` and `PWM1-2` normal outputs are driven by `z20_v15_io_owner_axi_lite`, then pass through the top-level PL E-stop output gate. The default reset state remains fail-closed/local-safe until software writes the owner registers.')
  $lines.Add('- `RS485_FPGA_RX/TX` are exported at the top boundary and wired through PS UART1 EMIO. `TP_INT` feeds the IO owner status register and `TP_RST` is driven by the IO owner touch reset register.')
  $lines.Add('- The ADC SPI board-level mapping on `U10`, `U9`, `AA12`, and `AB12` is retired; ADC_IN1 uses dedicated XADC VP/VN analog pins `L11/M12`, which are not normal PL active-XDC PACKAGE_PIN rows.')
} else {
  $lines.Add('These rows must be retired or remapped before bitstream. Do not add PACKAGE_PIN constraints for them.')
  $lines.Add('')
  $lines.Add('| Old wrapper port | Direction | Old pin | v1.5 net on same pin | Active XDC port on same pin | Old BD connection | Active BD connection | Group | Required action |')
  $lines.Add('| --- | --- | --- | --- | --- | --- | --- | --- | --- |')
  foreach ($row in $rows) {
    $oldBdConnection = Find-BdConnection -Nets $bdNets -PortName $row.port
    if ([string]::IsNullOrWhiteSpace($oldBdConnection)) {
      throw "Missing BD connection for old wrapper port $($row.port)"
    }

    $activeBdConnection = Find-BdConnection -Nets $bdNets -PortName $row.active_port_same_pin
    if ([string]::IsNullOrWhiteSpace($activeBdConnection)) {
      throw "Missing BD connection for active XDC port $($row.active_port_same_pin)"
    }

    $lines.Add(('| `{0}` | `{1}` | `{2}` | `{3}` | `{4}` | `{5}` | `{6}` | `{7}` | {8} |' -f $row.port, $row.direction, $row.old_pin, $row.v15_net_same_pin, $row.active_port_same_pin, $oldBdConnection, $activeBdConnection, $row.group, $row.next_action))
  }
  $lines.Add('')
  $lines.Add('N3C-1 evidence condition:')
  $lines.Add('')
  $lines.Add('- Each active-pin conflict row must include both the old wrapper-side BD connection and the current active-XDC same-pin owner.')
  $lines.Add('- `step_o[0]` must trace to `step_ip/step_o`; active ADC SPI ports must not reappear as board-level ports.')
  $lines.Add('')
  $lines.Add('N3C-2 close condition:')
  $lines.Add('')
  $lines.Add('- The old wrapper port is removed, internally tied off, or remapped by a BD/top-interface decision.')
  $lines.Add('- The ADC SPI board-level mapping on `U10`, `U9`, `AA12`, and `AB12` remains retired.')
  $lines.Add('- `scripts/check_active_xdc.ps1` and `scripts/verify_remaining_drc_ports.ps1` both pass after the change.')
  $lines.Add('- A Vivado validate/synthesis gate is rerun if RTL, wrapper, or BD connectivity changes.')
}

$outDir = Split-Path -Parent $OutPath
if (-not (Test-Path -LiteralPath $outDir)) {
  New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}
Write-TextFileWithRetry -Path $OutPath -Text (($lines -join [Environment]::NewLine) + [Environment]::NewLine)

$projectFullPath = (Resolve-Path -LiteralPath $ProjectDir).Path
$outFullPath = (Resolve-Path -LiteralPath $OutPath).Path
$outDisplay = $OutPath
if ($outFullPath.StartsWith($projectFullPath, [System.StringComparison]::OrdinalIgnoreCase)) {
  $outDisplay = $outFullPath.Substring($projectFullPath.Length) -replace '^[\\/]+', ''
}

Write-Output "active_pin_conflicts=$($rows.Count)"
Write-Output "report=$outDisplay"
foreach ($row in $rows) {
  $oldBdConnection = Find-BdConnection -Nets $bdNets -PortName $row.port
  $activeBdConnection = Find-BdConnection -Nets $bdNets -PortName $row.active_port_same_pin
  Write-Output "  $($row.port) old_pin=$($row.old_pin) active_port=$($row.active_port_same_pin) old_bd=$oldBdConnection active_bd=$activeBdConnection"
}
