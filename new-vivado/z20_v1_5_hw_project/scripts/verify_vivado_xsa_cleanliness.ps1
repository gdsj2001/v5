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

function Read-ProjectText {
  param(
    [string]$RootDir,
    [string]$RelativePath
  )
  $path = Get-ProjectPath -RootDir $RootDir -RelativePath $RelativePath
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing required file: $RelativePath"
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
    throw "Missing Vivado XSA cleanliness evidence: $Label"
  }
}

function Assert-NoMatch {
  param(
    [string]$Text,
    [string]$Pattern,
    [string]$Label
  )
  if ($Text -match $Pattern) {
    throw "Forbidden Vivado XSA cleanliness evidence found: $Label"
  }
}

function Count-Matches {
  param(
    [string]$Text,
    [string]$Pattern
  )
  return ([regex]::Matches($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)).Count
}

function Get-DrcRuleRows {
  param([string]$DrcText)

  $rows = New-Object System.Collections.Generic.List[object]
  foreach ($line in ($DrcText -split '\r?\n')) {
    if ($line -match '^\|\s*(?<rule>[A-Z0-9]+-[0-9]+)\s*\|\s*(?<severity>Error|Warning|Advisory)\s*\|') {
      $rows.Add([pscustomobject]@{
          rule = $Matches.rule
          severity = $Matches.severity
        })
    }
  }
  return $rows.ToArray()
}

$projectRoot = (Resolve-Path -LiteralPath $ProjectDir).Path

$xpr = Read-ProjectText -RootDir $projectRoot -RelativePath 'z20_v1_5_hw_project.xpr'
$synthLog = Read-ProjectText -RootDir $projectRoot -RelativePath 'z20_v1_5_hw_project.runs/synth_1/runme.log'
$implLog = Read-ProjectText -RootDir $projectRoot -RelativePath 'z20_v1_5_hw_project.runs/impl_1/runme.log'
$drcReport = Read-ProjectText -RootDir $projectRoot -RelativePath 'z20_v1_5_hw_project.runs/impl_1/system_top_drc_routed.rpt'
$timingHistoryPath = Get-ProjectPath -RootDir $projectRoot -RelativePath 'artifacts/vivado/timing_history.csv'
$bitPath = Get-ProjectPath -RootDir $projectRoot -RelativePath 'z20_v1_5_hw_project.runs/impl_1/system_top.bit'
$xsaPath = Get-ProjectPath -RootDir $projectRoot -RelativePath 'board_inputs/system.xsa'

foreach ($requiredPath in @($timingHistoryPath, $bitPath, $xsaPath)) {
  if (-not (Test-Path -LiteralPath $requiredPath)) {
    throw "Missing required Vivado/XSA artifact: $requiredPath"
  }
}

Assert-Match -Text $xpr -Pattern 'Path="\$PPRDIR/z20_v1_5_hw_project\.xpr"' -Label 'project path remains $PPRDIR-relative'
Assert-Match -Text $xpr -Pattern '\$PPRDIR/constraints/z20_v1_5_active_mapped\.xdc' -Label 'project active constraints use mapped XDC'
Assert-NoMatch -Text $xpr -Pattern 'z20-v1_5_20260623\.xdc' -Label 'truth/source XDC must not be loaded in project constrs set'
Assert-NoMatch -Text $xpr -Pattern 'constraints_reference/system_old\.xdc|constrs_1/system\.xdc' -Label 'old project XDC must not be loaded'

$combinedRunLogs = $synthLog + "`n" + $implLog
$truthXdcParsedCount = Count-Matches -Text $combinedRunLogs -Pattern 'Parsing XDC File \[[^\]]*z20-v1_5_20260623\.xdc\]'
$oldXdcParsedCount = Count-Matches -Text $combinedRunLogs -Pattern 'Parsing XDC File \[[^\]]*(constraints_reference/)?system(_old)?\.xdc\]'
$criticalWarningCount = Count-Matches -Text $combinedRunLogs -Pattern '^CRITICAL WARNING:'
$errorCount = Count-Matches -Text $combinedRunLogs -Pattern '^ERROR:'
$warningLineCount = Count-Matches -Text $combinedRunLogs -Pattern '^WARNING:'

if ($truthXdcParsedCount -ne 0) {
  throw "Vivado run parsed the v1.5 source/truth XDC instead of only the active mapped XDC: $truthXdcParsedCount"
}
if ($oldXdcParsedCount -ne 0) {
  throw "Vivado run parsed an old system XDC: $oldXdcParsedCount"
}
if ($criticalWarningCount -ne 0) {
  throw "Vivado run has critical warnings: $criticalWarningCount"
}
if ($errorCount -ne 0) {
  throw "Vivado run has errors: $errorCount"
}

Assert-Match -Text $drcReport -Pattern 'Design\s+:\s+system_top' -Label 'routed DRC is for system_top'
Assert-NoMatch -Text $drcReport -Pattern '\bNSTD-1\b|\bUCIO-1\b' -Label 'routed DRC must not contain NSTD-1 or UCIO-1'
Assert-NoMatch -Text $drcReport -Pattern '\|\s*[A-Z0-9]+-[0-9]+\s*\|\s*Error\s*\|' -Label 'routed DRC must not contain error-severity rules'

$drcRows = @(Get-DrcRuleRows -DrcText $drcReport)
$drcErrorRows = @($drcRows | Where-Object { $_.severity -eq 'Error' })
$drcWarningRows = @($drcRows | Where-Object { $_.severity -eq 'Warning' })
$unexpectedDrcWarnings = @($drcWarningRows | Where-Object { $_.rule -ne 'RTSTAT-10' })
if ($drcErrorRows.Count -ne 0) {
  throw "Routed DRC has error-severity rows: $($drcErrorRows.Count)"
}
if ($unexpectedDrcWarnings.Count -ne 0) {
  throw "Routed DRC has unexpected warning rows: $($unexpectedDrcWarnings.rule -join ',')"
}

$timingRows = @(Import-Csv -LiteralPath $timingHistoryPath)
if ($timingRows.Count -eq 0) {
  throw 'Timing history has no rows'
}
$latestTiming = $timingRows[-1]
if ($latestTiming.build_status -ne 'bitstream_generated') {
  throw "Latest timing build status is not bitstream_generated: $($latestTiming.build_status)"
}
if ($latestTiming.timing_status -ne 'timing_met') {
  throw "Latest timing status is not timing_met: $($latestTiming.timing_status)"
}
if ($latestTiming.bit_file -ne 'z20_v1_5_hw_project.runs/impl_1/system_top.bit') {
  throw "Latest timing bit_file is not project-relative system_top.bit: $($latestTiming.bit_file)"
}
$timingWns = [double]::Parse([string]$latestTiming.wns, [System.Globalization.CultureInfo]::InvariantCulture)
$timingWhs = [double]::Parse([string]$latestTiming.whs, [System.Globalization.CultureInfo]::InvariantCulture)
if ($timingWns -lt 0.000) {
  throw "Latest timing WNS is negative: $($latestTiming.wns)"
}
if ($timingWhs -lt 0.000) {
  throw "Latest timing WHS is negative: $($latestTiming.whs)"
}
$timingMarginTargetStatus = if ($timingWns -ge 0.100) { 'advisory_met' } else { 'advisory_below_target' }

$bitItem = Get-Item -LiteralPath $bitPath
$xsaItem = Get-Item -LiteralPath $xsaPath
if ($xsaItem.LastWriteTimeUtc -lt $bitItem.LastWriteTimeUtc) {
  throw 'XSA is older than the current bitstream'
}

Write-Output 'vivado_xsa_cleanliness=ok'
Write-Output 'active_constraints_loaded=mapped_only'
Write-Output 'truth_source_xdc_loaded=no'
Write-Output 'old_project_xdc_loaded=no'
Write-Output "truth_source_xdc_parse_count=$truthXdcParsedCount"
Write-Output "old_project_xdc_parse_count=$oldXdcParsedCount"
Write-Output "vivado_warning_lines=$warningLineCount"
Write-Output "vivado_critical_warnings=$criticalWarningCount"
Write-Output "vivado_errors=$errorCount"
Write-Output 'drc_blocking_rules=0'
Write-Output "drc_warning_rules=$($drcWarningRows.Count)"
Write-Output "drc_allowed_warning_rules=$($drcWarningRows.rule -join ',')"
Write-Output "timing_timestamp=$($latestTiming.timestamp)"
Write-Output "build_status=$($latestTiming.build_status)"
Write-Output "timing_status=$($latestTiming.timing_status)"
Write-Output "timing_wns=$($latestTiming.wns)"
Write-Output "timing_whs=$($latestTiming.whs)"
Write-Output 'timing_margin_policy=timing_met_required_wns_0p100_advisory'
Write-Output 'timing_margin_target_wns=0.100'
Write-Output "timing_margin_target_status=$timingMarginTargetStatus"
Write-Output 'bitstream_artifact=current'
Write-Output 'xsa_artifact=current'
