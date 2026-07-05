param(
  [string]$ProjectDir,
  [string]$RegisterMapPath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($RegisterMapPath)) {
  $RegisterMapPath = Join-Path $ProjectDir 'board_inputs/pl_estop_register_map.md'
}

function Assert-Contains {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )

  if ($Text -notmatch $Pattern) {
    throw "Missing PL E-stop register-map content: $Label"
  }
}

function Assert-RtlContains {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )

  if ($Text -notmatch $Pattern) {
    throw "RTL mismatch for PL E-stop register map: $Label"
  }
}

$rtlPath = Join-Path $ProjectDir 'rtl/pl_estop_axi_lite.v'
if (-not (Test-Path -LiteralPath $rtlPath)) {
  throw 'Missing rtl/pl_estop_axi_lite.v'
}
if (-not (Test-Path -LiteralPath $RegisterMapPath)) {
  throw 'Missing board_inputs/pl_estop_register_map.md'
}

$rtl = Get-Content -LiteralPath $rtlPath -Raw -Encoding UTF8
$doc = Get-Content -LiteralPath $RegisterMapPath -Raw -Encoding UTF8

if ($doc -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
  throw 'PL E-stop register map contains an absolute path'
}
if ($doc -match 'vivado_hw_project') {
  throw 'PL E-stop register map contains an old-project reference'
}

Assert-RtlContains -Text $rtl -Pattern "REG_MAGIC\s+=\s+32'h45535450" -Label 'REG_MAGIC'
Assert-RtlContains -Text $rtl -Pattern "REG_VERSION\s+=\s+32'h00010001" -Label 'REG_VERSION'
Assert-RtlContains -Text $rtl -Pattern "parameter\s+\[31:0\]\s+BUILD_ID\s+=\s+32'h20260629" -Label 'BUILD_ID'
Assert-RtlContains -Text $rtl -Pattern 'if \(S_AXI_AWADDR\[5:2\] == ADDR_CONTROL && S_AXI_WSTRB\[0\]\)' -Label 'CONTROL WSTRB[0] gate'
Assert-RtlContains -Text $rtl -Pattern 'sw_reset_req\s+<=\s+S_AXI_WDATA\[0\]' -Label 'CONTROL bit 0'
Assert-RtlContains -Text $rtl -Pattern 'irq_clear\s+<=\s+S_AXI_WDATA\[1\]' -Label 'CONTROL bit 1'
Assert-RtlContains -Text $rtl -Pattern 'ADDR_STATUS:\s+begin[\s\S]*bus_tx_queue_flushed[\s\S]*bus_tx_gate_active[\s\S]*general_output_forced_off[\s\S]*estop_irq[\s\S]*brake_delay_active[\s\S]*reset_allowed[\s\S]*estop_input_filtered[\s\S]*estop_input_raw[\s\S]*estop_latched' -Label 'STATUS bit order'

$registers = [ordered]@{
  MAGIC = @{ addr = '0'; offset = '0x00' }
  VERSION = @{ addr = '1'; offset = '0x04' }
  STATUS = @{ addr = '2'; offset = '0x08' }
  CONTROL = @{ addr = '3'; offset = '0x0C' }
  TIMING = @{ addr = '4'; offset = '0x10' }
  AXIS_CONFIG = @{ addr = '5'; offset = '0x14' }
  BUILD_ID = @{ addr = '6'; offset = '0x18' }
  GENERAL_CONFIG = @{ addr = '7'; offset = '0x1C' }
  BUS_TX_CONFIG = @{ addr = '8'; offset = '0x20' }
}

foreach ($name in $registers.Keys) {
  $addr = $registers[$name].addr
  $offset = $registers[$name].offset
  Assert-RtlContains -Text $rtl -Pattern ("ADDR_$name\s+=\s+4'h$addr") -Label "ADDR_$name"
  Assert-Contains -Text $doc -Pattern ('\|\s*`' + [regex]::Escape($offset) + '`\s*\|\s*`' + [regex]::Escape($name) + '`\s*\|') -Label "$name register row"
}

$statusBits = [ordered]@{
  0 = 'estop_latched'
  1 = 'estop_input_raw'
  2 = 'estop_input_filtered'
  3 = 'reset_allowed'
  4 = 'brake_delay_active'
  5 = 'estop_irq'
  6 = 'general_output_forced_off'
  7 = 'bus_tx_gate_active'
  8 = 'bus_tx_queue_flushed'
}

foreach ($bit in $statusBits.Keys) {
  Assert-Contains -Text $doc -Pattern ('\|\s*' + $bit + '\s*\|\s*`' + [regex]::Escape($statusBits[$bit]) + '`\s*\|') -Label "STATUS bit $bit"
}

$controlBits = [ordered]@{
  0 = 'sw_reset_req'
  1 = 'irq_clear'
}

foreach ($bit in $controlBits.Keys) {
  Assert-Contains -Text $doc -Pattern ('\|\s*' + $bit + '\s*\|\s*`' + [regex]::Escape($controlBits[$bit]) + '`\s*\|\s*W1P\s*\|') -Label "CONTROL bit $bit"
}

Assert-Contains -Text $doc -Pattern 'Base address:\s*`0x41260000`' -Label 'base address'
Assert-Contains -Text $doc -Pattern 'Range:\s*`64K`' -Label 'range'
Assert-Contains -Text $doc -Pattern 'AXI address width:\s*`C_S_AXI_ADDR_WIDTH=6`' -Label 'address width'
Assert-Contains -Text $doc -Pattern 'IRQ route:\s*`pl_estop/estop_irq`\s*->\s*`xlconcat_0/In14`' -Label 'IRQ route'
Assert-Contains -Text $doc -Pattern 'Current closure state:\s*`local_verified_only`' -Label 'local-only closure state'
Assert-Contains -Text $doc -Pattern 'Current readiness state:\s*`pl_estop_readiness=not_ready`' -Label 'readiness state'
Assert-Contains -Text $doc -Pattern 'Timing parameter check:\s*`pl_estop_timing_params=ok`' -Label 'timing parameter check'
Assert-Contains -Text $doc -Pattern '`CLK_HZ=100000000`' -Label 'clock parameter'
Assert-Contains -Text $doc -Pattern '`DEBOUNCE_MS=10`.*`debounce_cycles=1000000`' -Label 'debounce parameter'
Assert-Contains -Text $doc -Pattern '`BRAKE_LEAD_US=50`.*`brake_cycles=5000`' -Label 'brake parameter'
Assert-Contains -Text $doc -Pattern '`AXIS_COUNT=8`' -Label 'axis count parameter'
Assert-Contains -Text $doc -Pattern '`Z_AXIS_INDEX=2`' -Label 'Z-axis index parameter'
Assert-Contains -Text $doc -Pattern 'BD clock net:\s*`processing_system7_0/FCLK_CLK0`' -Label 'BD clock net'
Assert-Contains -Text $doc -Pattern 'not board safety proof' -Label 'not safety proof boundary'
Assert-Contains -Text $doc -Pattern 'STATUS\[6\].*STATUS\[7\].*local PL gate state only' -Label 'local gate state boundary'
Assert-Contains -Text $doc -Pattern 'board_verified' -Label 'board verified boundary'

Write-Output 'pl_estop_register_map=ok'
Write-Output 'register_map=board_inputs/pl_estop_register_map.md'
Write-Output 'rtl_source=rtl/pl_estop_axi_lite.v'
Write-Output 'registers=9'
Write-Output 'status_bits=9'
Write-Output 'control_bits=2'
Write-Output 'base_address=0x41260000'
Write-Output 'irq_route=xlconcat_0/In14'
Write-Output 'abi_version=0x00010001'
