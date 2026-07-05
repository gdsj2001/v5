param(
  [string]$ProjectDir,
  [string]$CsvPath,
  [string]$MarkdownPath
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($CsvPath)) {
  $CsvPath = Join-Path $ProjectDir 'docs/vivado_warning_summary.csv'
}
if ([string]::IsNullOrWhiteSpace($MarkdownPath)) {
  $MarkdownPath = Join-Path $ProjectDir 'docs/vivado_warning_summary.md'
}

. (Join-Path $PSScriptRoot 'vivado_warning_common.ps1')

$projectRoot = (Resolve-Path -LiteralPath $ProjectDir).Path
foreach ($path in @($CsvPath, $MarkdownPath)) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing Vivado warning summary artifact: $path"
  }
}

$warnings = @(Get-VivadoWarningRows -ProjectDir $projectRoot)
$expectedRows = @(Get-VivadoWarningSummaryRows -Warnings $warnings)
$policy = Test-VivadoWarningPolicy -Warnings $warnings

if ($policy.unexpected_warning_code_count -ne 0) {
  throw "Unexpected Vivado warning codes: $($policy.unexpected_warning_codes)"
}
if ($policy.constraint_truth_warning_lines -ne 0) {
  throw "Vivado warning log still contains source/old constraint warning lines: $($policy.constraint_truth_warning_lines)"
}

$actualRows = @(Import-Csv -LiteralPath $CsvPath)
if ($actualRows.Count -ne $expectedRows.Count) {
  throw "Vivado warning summary row count mismatch: expected $($expectedRows.Count), got $($actualRows.Count)"
}

foreach ($expected in $expectedRows) {
  $actual = @($actualRows | Where-Object { $_.code -eq $expected.code })
  if ($actual.Count -ne 1) {
    throw "Vivado warning summary missing or duplicate code row: $($expected.code)"
  }
  $row = $actual[0]
  foreach ($field in @('count', 'phases', 'classification', 'owner', 'next_action', 'allowed')) {
    if ([string]$row.$field -ne [string]$expected.$field) {
      throw "Vivado warning summary mismatch for $($expected.code) field $field`: expected '$($expected.$field)', got '$($row.$field)'"
    }
  }
}

$markdown = Get-Content -LiteralPath $MarkdownPath -Raw -Encoding UTF8
if ($markdown -match '\$\(Escape-MarkdownCell') {
  throw 'Vivado warning summary markdown contains an unevaluated Escape-MarkdownCell expression'
}
if ($policy.retired_hdmi_warning_lines -ne 0) {
  throw "Vivado warning summary still contains retired HDMI/DVI/TMDS warning lines: $($policy.retired_hdmi_warning_lines)"
}
foreach ($required in @(
    'vivado_warning_summary=classified',
    "vivado_warning_lines=$($policy.warning_count)",
    'constraint_truth_warning_lines=0',
    'retired_hdmi_warning_lines=0',
    'This file does not claim all Vivado warnings are removed.'
  )) {
  if ($markdown -notmatch [regex]::Escape($required)) {
    throw "Vivado warning summary markdown missing: $required"
  }
}

Write-Output 'vivado_warning_summary_verify=ok'
Write-Output "vivado_warning_lines=$($policy.warning_count)"
Write-Output "vivado_warning_codes=$($policy.allowed_code_count)"
Write-Output "unexpected_warning_codes=$($policy.unexpected_warning_code_count)"
Write-Output "constraint_truth_warning_lines=$($policy.constraint_truth_warning_lines)"
Write-Output "retired_hdmi_warning_lines=$($policy.retired_hdmi_warning_lines)"
Write-Output 'warning_summary_boundary=known_noncritical_warning_classes'
