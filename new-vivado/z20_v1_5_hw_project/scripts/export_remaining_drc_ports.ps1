param(
  [string]$ProjectDir,
  [string]$CsvOut
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($CsvOut)) {
  $CsvOut = Join-Path $ProjectDir 'docs/remaining_drc_ports.csv'
}

. (Join-Path $PSScriptRoot 'write_text_with_retry.ps1')

$projectPath = Join-Path $ProjectDir 'z20_v1_5_hw_project.xpr'
$activeXdcPath = Join-Path $ProjectDir 'constraints/z20_v1_5_active_mapped.xdc'
$projectParentDir = (Resolve-Path -LiteralPath (Join-Path $ProjectDir '..')).Path
$v15XdcPath = Join-Path $projectParentDir 'z20-v1_5_20260623.xdc'

foreach ($path in @($projectPath, $activeXdcPath, $v15XdcPath)) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing required file: $path"
  }
}

function Get-CurrentTopSource {
  param([string]$RootDir)

  $xprPath = Join-Path $RootDir 'z20_v1_5_hw_project.xpr'
  $xprText = Get-Content -LiteralPath $xprPath -Raw
  $topModule = 'system_wrapper'
  if ($xprText -match '<Option\s+Name="TopModule"\s+Val="([^"]+)"') {
    $topModule = $Matches[1]
  }

  $candidates = @(
    (Join-Path $RootDir "rtl/$topModule.v"),
    (Join-Path $RootDir "z20_v1_5_hw_project.srcs/sources_1/imports/hdl/$topModule.v")
  )
  foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate) {
      return [pscustomobject]@{
        Module = $topModule
        Path = (Resolve-Path -LiteralPath $candidate).Path
      }
    }
  }

  $modulePattern = "(?m)^\s*module\s+$([regex]::Escape($topModule))\b"
  $source = Get-ChildItem -LiteralPath $RootDir -File -Recurse -Include '*.v','*.vh' |
    Where-Object { $_.FullName -notmatch '(\\|/)([^\\/]*\.runs|[^\\/]*\.gen|[^\\/]*\.cache|\.Xil)(\\|/)' } |
    Where-Object { (Get-Content -LiteralPath $_.FullName -Raw) -match $modulePattern } |
    Select-Object -First 1

  if (-not $source) {
    throw "Could not find source file for top module: $topModule"
  }
  return [pscustomobject]@{
    Module = $topModule
    Path = $source.FullName
  }
}

function Expand-PortName {
  param(
    [string]$Name,
    [string]$Range
  )
  if ([string]::IsNullOrWhiteSpace($Range)) {
    return @($Name)
  }
  if ($Range -notmatch '\[\s*(\d+)\s*:\s*(\d+)\s*\]') {
    return @($Name)
  }
  $left = [int]$Matches[1]
  $right = [int]$Matches[2]
  $min = [Math]::Min($left, $right)
  $max = [Math]::Max($left, $right)
  $items = @()
  for ($idx = $min; $idx -le $max; $idx++) {
    $items += "$Name[$idx]"
  }
  return $items
}

function Get-PortGroup {
  param([string]$Port)
  if ($Port -match '^step_o\[' -or $Port -match '^dir_o\[') { return 'axis_step_dir' }
  if ($Port -match '^enc_[abz]_i\[') { return 'encoder_di_do_mpg_overlap' }
  if ($Port -match '^gpio0_tri_io\[') { return 'gpio_misc_touch_mpg_scale' }
  if ($Port -match '^i2c4_') { return 'spare_i2c4_fpga1_io5' }
  if ($Port -match '^rs485_') { return 'rs485_pair' }
  if ($Port -match '^pl_led_tri_o\[') { return 'pl_led_scale_beep_overlap' }
  if ($Port -match '^tmds_') { return 'retired_hdmi_regression' }
  return 'unclassified'
}

function Get-CloseCondition {
  param([string]$Group)
  switch ($Group) {
    'axis_step_dir' { return 'Use the current 8-axis DB15 PULS/DIR/ENA/A/B/Z/ALM boundary and close PL E-stop gate points before exposing any new output path.' }
    'encoder_di_do_mpg_overlap' { return 'Split encoder, DI, DO, MPG, and safety-input ownership before any active XDC promotion.' }
    'gpio_misc_touch_mpg_scale' { return 'Decide each GPIO bit owner and direction: reset, touch INT/RST, MPG, scale select, or retire.' }
    'spare_i2c4_fpga1_io5' { return 'Confirm actual external I2C device and pull-ups on FPGA1_IO5_P/N, or retire old I2C4 at BD/custom-wrapper level so generated IOBUFs disappear.' }
    'rs485_pair' { return 'Close RX/TX together with board wiring, duplicate B13_IO_0 review, and I/O bank strategy.' }
    'pl_led_scale_beep_overlap' { return 'Decide LED versus v1.5 SCALE_SEL2/BEEP_EN function and output polarity.' }
    'retired_hdmi_regression' { return 'HDMI/DVI is abandoned; remove the retired TMDS top-level or wrapper port so MPG keeps ownership of the shared pins.' }
    default { return 'Classify function ownership before active XDC promotion.' }
  }
}

function Get-ClosureBlocker {
  param(
    [string]$Group,
    [string]$ActivePortSamePin
  )
  if (-not [string]::IsNullOrWhiteSpace($ActivePortSamePin)) {
    return 'active_pin_already_claimed'
  }
  switch ($Group) {
    'axis_step_dir' { return 'axis_model_unresolved' }
    'encoder_di_do_mpg_overlap' { return 'mixed_io_owner_unresolved' }
    'gpio_misc_touch_mpg_scale' { return 'bit_owner_direction_unresolved' }
    'spare_i2c4_fpga1_io5' { return 'external_i2c_device_unconfirmed' }
    'rs485_pair' { return 'rs485_wiring_bank_unresolved' }
    'pl_led_scale_beep_overlap' { return 'panel_output_function_unresolved' }
    'retired_hdmi_regression' { return 'retired_hdmi_port_reappeared' }
    default { return 'unclassified_owner_unresolved' }
  }
}

function Get-NextAction {
  param(
    [string]$Group,
    [string]$ActivePortSamePin
  )
  if (-not [string]::IsNullOrWhiteSpace($ActivePortSamePin)) {
    return 'Retire or remap this old top-level port before bitstream; the physical pin is already claimed in active XDC.'
  }
  switch ($Group) {
    'axis_step_dir' { return 'Keep the current 8-axis DB15 model; define step/dir/enable/alarm ownership and PL E-stop gate points together.' }
    'encoder_di_do_mpg_overlap' { return 'Split encoder, DI, DO, MPG, scale, and safety input ownership before wrapper or active XDC changes.' }
    'gpio_misc_touch_mpg_scale' { return 'Decide each GPIO bit owner and direction, then promote only bit-level confirmed ports.' }
    'spare_i2c4_fpga1_io5' { return 'Confirm an external I2C device and pull-ups on FPGA1_IO5_P/N, or run scripts/vivado/retire_i2c4_failclosed.tcl to remove the external interface and hold SCL/SDA inputs idle-high.' }
    'rs485_pair' { return 'Close RX/TX wiring, TX pin migration, duplicate B13_IO_0 note, and bank placement strategy together.' }
    'pl_led_scale_beep_overlap' { return 'Choose LED, scale select, or beeper ownership and output polarity before promotion.' }
    'retired_hdmi_regression' { return 'Remove the retired HDMI/DVI/TMDS top-level function; shared pins are already assigned to MPG.' }
    default { return 'Classify function ownership before active XDC promotion.' }
  }
}

function Get-XdcPortName {
  param([string]$Line)
  if ($Line -match '\[get_ports\s+\{([^}]+)\}\]') {
    return $Matches[1].Trim()
  }
  if ($Line -match '\[get_ports\s+([^\]\s]+)\]') {
    return $Matches[1].Trim()
  }
  return $null
}

$topInfo = Get-CurrentTopSource -RootDir $ProjectDir
$topText = Get-Content -LiteralPath $topInfo.Path -Raw
$declared = @{}
$declPattern = '(?m)^\s*(input|output|inout)\s+(?:wire\s+|reg\s+)?(?<range>\[[^\]]+\]\s*)?(?<name>[A-Za-z_][A-Za-z0-9_]*)\s*;'
foreach ($match in [regex]::Matches($topText, $declPattern)) {
  $direction = $match.Groups[1].Value
  $name = $match.Groups['name'].Value
  $range = $match.Groups['range'].Value
  foreach ($expanded in Expand-PortName -Name $name -Range $range) {
    $declared[$expanded] = [pscustomobject]@{
      Port = $expanded
      Direction = $direction
    }
  }
}

$activePorts = @{}
$activeByPin = @{}
foreach ($line in Get-Content -LiteralPath $activeXdcPath) {
  if ($line -match 'PACKAGE_PIN\s+([^\s\}]+)') {
    $pin = $Matches[1]
    $portName = Get-XdcPortName -Line $line
    if (-not [string]::IsNullOrWhiteSpace($portName)) {
      $activePorts[$portName] = $pin
      if ($activeByPin.ContainsKey($pin)) {
        $activeByPin[$pin] = "$($activeByPin[$pin]);$portName"
      } else {
        $activeByPin[$pin] = $portName
      }
    }
  }
}

$oldByPort = @{}

$v15ByPin = @{}
$lastComment = ''
foreach ($line in Get-Content -LiteralPath $v15XdcPath) {
  if ($line -match '^\s*#\s*(.+)$') {
    $lastComment = $Matches[1].Trim()
    continue
  }
  if ($line -match 'set_io\s+\{([^}]+)\}\s+([^\s]+)') {
    $net = $Matches[1]
    $pin = $Matches[2]
    $source = ''
    $core = ''
    if ($lastComment -match 'source\s+([^,]+)') {
      $source = $Matches[1].Trim()
    }
    if ($lastComment -match 'core\s+([^,]+)') {
      $core = $Matches[1].Trim()
    }
    $v15ByPin[$pin] = [pscustomobject]@{
      Net = $net
      Pin = $pin
      Source = $source
      Core = $core
      Comment = $lastComment
    }
    $lastComment = ''
  }
}

$ignorePatterns = @('^DDR_', '^FIXED_IO_')
$rows = @()
foreach ($port in ($declared.Keys | Sort-Object)) {
  if ($activePorts.ContainsKey($port)) {
    continue
  }
  $ignore = $false
  foreach ($pattern in $ignorePatterns) {
    if ($port -match $pattern) {
      $ignore = $true
      break
    }
  }
  if ($ignore) {
    continue
  }
  $old = $oldByPort[$port]
  $v15 = $null
  if ($old -and $v15ByPin.ContainsKey($old.Pin)) {
    $v15 = $v15ByPin[$old.Pin]
  }
  $activePortSamePin = ''
  if ($old -and $activeByPin.ContainsKey($old.Pin)) {
    $activePortSamePin = $activeByPin[$old.Pin]
  }
  $group = Get-PortGroup -Port $port
  $closureBlocker = Get-ClosureBlocker -Group $group -ActivePortSamePin $activePortSamePin
  $rows += [pscustomobject]@{
    port = $port
    direction = $declared[$port].Direction
    group = $group
    closure_blocker = $closureBlocker
    old_pin = ''
    old_iostandard = ''
    v15_net_same_pin = if ($v15) { $v15.Net } else { '' }
    v15_pin = if ($v15) { $v15.Pin } else { '' }
    v15_source = if ($v15) { $v15.Source } else { '' }
    v15_core = if ($v15) { $v15.Core } else { '' }
    active_port_same_pin = $activePortSamePin
    active_pin_conflict = if ([string]::IsNullOrWhiteSpace($activePortSamePin)) { 'no' } else { 'yes' }
    close_condition = Get-CloseCondition -Group $group
    next_action = Get-NextAction -Group $group -ActivePortSamePin $activePortSamePin
  }
}

$outDir = Split-Path -Parent $CsvOut
if (-not (Test-Path -LiteralPath $outDir)) {
  New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

$csvColumns = @(
  'port',
  'direction',
  'group',
  'closure_blocker',
  'old_pin',
  'old_iostandard',
  'v15_net_same_pin',
  'v15_pin',
  'v15_source',
  'v15_core',
  'active_port_same_pin',
  'active_pin_conflict',
  'close_condition',
  'next_action'
)
if ($rows.Count -eq 0) {
  $csvText = (($csvColumns | ForEach-Object { '"' + ($_ -replace '"', '""') + '"' }) -join ',') + [Environment]::NewLine
} else {
  $csvText = (($rows | ConvertTo-Csv -NoTypeInformation) -join [Environment]::NewLine) + [Environment]::NewLine
}
Write-TextFileWithRetry -Path $CsvOut -Text $csvText
$projectFullPath = (Resolve-Path -LiteralPath $ProjectDir).Path
$csvFullPath = (Resolve-Path -LiteralPath $CsvOut).Path
$csvDisplay = $CsvOut
if ($csvFullPath.StartsWith($projectFullPath, [System.StringComparison]::OrdinalIgnoreCase)) {
  $csvDisplay = $csvFullPath.Substring($projectFullPath.Length) -replace '^[\\/]+', ''
}
Write-Output "remaining_ports=$($rows.Count)"
Write-Output "csv=$csvDisplay"
$rows | Group-Object group | Sort-Object Name | ForEach-Object {
  Write-Output "$($_.Name)=$($_.Count)"
}
