param(
  [string]$ProjectDir
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

function Assert-TextMatch {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )

  if ($Text -notmatch $Pattern) {
    throw "PL E-stop timing parameter mismatch: $Label"
  }
}

function Assert-Equal {
  param(
    [string]$Actual,
    [string]$Expected,
    [string]$Label
  )

  if ($Actual -ne $Expected) {
    throw "PL E-stop timing parameter mismatch: $Label expected $Expected, got $Actual"
  }
}

function Get-ParameterValues {
  param(
    [string]$Text,
    [string]$Name
  )

  $matches = [regex]::Matches($Text, "parameter\s+integer\s+$Name\s*=\s*(?<value>\d+)")
  return @($matches | ForEach-Object { $_.Groups['value'].Value } | Sort-Object -Unique)
}

$corePath = Join-Path $ProjectDir 'rtl/pl_estop_core.v'
$axiPath = Join-Path $ProjectDir 'rtl/pl_estop_axi_lite.v'
$bdPath = Join-Path $ProjectDir 'z20_v1_5_hw_project.srcs/sources_1/bd/system/system.bd'

foreach ($path in @($corePath, $axiPath, $bdPath)) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing PL E-stop timing input: $path"
  }
}

$core = Get-Content -LiteralPath $corePath -Raw -Encoding UTF8
$axi = Get-Content -LiteralPath $axiPath -Raw -Encoding UTF8
$bd = Get-Content -LiteralPath $bdPath -Raw -Encoding UTF8 | ConvertFrom-Json

$expectedClockHz = '100000000'
$expectedDebounceMs = '10'
$expectedBrakeLeadUs = '50'
$expectedAxisCount = '8'
$expectedZAxisIndex = '2'
$expectedDebounceCycles = '1000000'
$expectedBrakeCycles = '5000'

$combinedRtl = $core + [Environment]::NewLine + $axi
foreach ($entry in @(
  @{ name = 'CLK_HZ'; expected = $expectedClockHz },
  @{ name = 'DEBOUNCE_MS'; expected = $expectedDebounceMs },
  @{ name = 'BRAKE_LEAD_US'; expected = $expectedBrakeLeadUs },
  @{ name = 'AXIS_COUNT'; expected = $expectedAxisCount },
  @{ name = 'Z_AXIS_INDEX'; expected = $expectedZAxisIndex }
)) {
  $values = @(Get-ParameterValues -Text $combinedRtl -Name $entry.name)
  if ($values.Count -eq 0) {
    throw "Missing PL E-stop parameter: $($entry.name)"
  }
  if ($values.Count -ne 1 -or $values[0] -ne $entry.expected) {
    throw "Unexpected PL E-stop parameter values for $($entry.name): $($values -join ',')"
  }
}

Assert-TextMatch -Text $core -Pattern 'DEBOUNCE_CYCLES_RAW\s*=\s*\(CLK_HZ\s*/\s*1000\)\s*\*\s*DEBOUNCE_MS' -Label 'debounce cycle formula'
Assert-TextMatch -Text $core -Pattern 'BRAKE_CYCLES_RAW\s*=\s*\(CLK_HZ\s*/\s*1000000\)\s*\*\s*BRAKE_LEAD_US' -Label 'brake cycle formula'
Assert-TextMatch -Text $axi -Pattern 'ADDR_TIMING:\s*read_reg\s*=\s*\{DEBOUNCE_MS_VAL,\s*BRAKE_LEAD_US_VAL\}' -Label 'AXI TIMING register'
Assert-TextMatch -Text $axi -Pattern 'ADDR_AXIS_CONFIG:\s*read_reg\s*=\s*\{AXIS_COUNT_VAL,\s*Z_AXIS_INDEX_VAL\}' -Label 'AXI AXIS_CONFIG register'
Assert-TextMatch -Text $axi -Pattern '\.CLK_HZ\(CLK_HZ\)' -Label 'core CLK_HZ forwarding'
Assert-TextMatch -Text $axi -Pattern '\.DEBOUNCE_MS\(DEBOUNCE_MS\)' -Label 'core DEBOUNCE_MS forwarding'
Assert-TextMatch -Text $axi -Pattern '\.BRAKE_LEAD_US\(BRAKE_LEAD_US\)' -Label 'core BRAKE_LEAD_US forwarding'
Assert-TextMatch -Text $axi -Pattern '\.AXIS_COUNT\(AXIS_COUNT\)' -Label 'core AXIS_COUNT forwarding'

$plEstop = $bd.design.components.pl_estop
if ($null -eq $plEstop) {
  throw 'BD is missing pl_estop component'
}
Assert-Equal -Actual $plEstop.reference_info.ref_name -Expected 'pl_estop_axi_lite_v3' -Label 'BD module reference'
Assert-Equal -Actual $plEstop.interface_ports.S_AXI.parameters.FREQ_HZ.value -Expected $expectedClockHz -Label 'BD S_AXI FREQ_HZ'
Assert-Equal -Actual $plEstop.ports.step_in.left -Expected '7' -Label 'BD PL E-stop step_in left'
Assert-Equal -Actual $plEstop.ports.step_in.right -Expected '0' -Label 'BD PL E-stop step_in right'
Assert-Equal -Actual $plEstop.ports.enable_in.left -Expected '7' -Label 'BD PL E-stop enable_in left'
Assert-Equal -Actual $plEstop.ports.enable_in.right -Expected '0' -Label 'BD PL E-stop enable_in right'

$clk0Ports = @($bd.design.nets.processing_system7_0_FCLK_CLK0.ports)
if (-not ($clk0Ports -contains 'pl_estop/S_AXI_ACLK')) {
  throw 'BD processing_system7_0_FCLK_CLK0 does not clock pl_estop/S_AXI_ACLK'
}
if ($clk0Ports -contains 'step_ip/s_axi_aclk') {
  throw 'BD pl_estop timing check expected pl_estop on FCLK_CLK0, not mixed with step_ip FCLK_CLK1 owner'
}

$segments = $bd.design.addressing.'/processing_system7_0'.address_spaces.Data.segments
$plEstopSegment = $segments.SEG_pl_estop_reg0
if ($null -eq $plEstopSegment) {
  throw 'BD addressing is missing SEG_pl_estop_reg0'
}
Assert-Equal -Actual $plEstopSegment.offset -Expected '0x41260000' -Label 'BD PL E-stop base address'
Assert-Equal -Actual $plEstopSegment.range -Expected '64K' -Label 'BD PL E-stop address range'

$debounceCycles = ([int]$expectedClockHz / 1000) * [int]$expectedDebounceMs
$brakeCycles = ([int]$expectedClockHz / 1000000) * [int]$expectedBrakeLeadUs
Assert-Equal -Actual ([string]$debounceCycles) -Expected $expectedDebounceCycles -Label 'derived debounce cycles'
Assert-Equal -Actual ([string]$brakeCycles) -Expected $expectedBrakeCycles -Label 'derived brake cycles'

Write-Output 'pl_estop_timing_params=ok'
Write-Output "clock_hz=$expectedClockHz"
Write-Output "debounce_ms=$expectedDebounceMs"
Write-Output "debounce_cycles=$expectedDebounceCycles"
Write-Output "brake_lead_us=$expectedBrakeLeadUs"
Write-Output "brake_cycles=$expectedBrakeCycles"
Write-Output "axis_count=$expectedAxisCount"
Write-Output "z_axis_index=$expectedZAxisIndex"
Write-Output "bd_axi_freq_hz=$($plEstop.interface_ports.S_AXI.parameters.FREQ_HZ.value)"
Write-Output 'bd_clock_net=processing_system7_0_FCLK_CLK0'
Write-Output 'register_timing_exposed=yes'
Write-Output 'register_axis_config_exposed=yes'
