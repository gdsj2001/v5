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

. (Join-Path $PSScriptRoot 'write_text_with_retry.ps1')
. (Join-Path $PSScriptRoot 'vivado_warning_common.ps1')

function Convert-ToProjectRelativePath {
  param(
    [string]$RootDir,
    [string]$Path
  )

  $root = (Resolve-Path -LiteralPath $RootDir).Path.TrimEnd('\', '/')
  $full = (Resolve-Path -LiteralPath $Path).Path
  if (-not $full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Path is outside project: $Path"
  }
  return ($full.Substring($root.Length).TrimStart('\', '/') -replace '\\', '/')
}

function Escape-MarkdownCell {
  param([string]$Text)
  if ($null -eq $Text) {
    return ''
  }
  return (($Text -replace '\|', '\\|') -replace "`r?`n", ' ')
}

$projectRoot = (Resolve-Path -LiteralPath $ProjectDir).Path
$warnings = @(Get-VivadoWarningRows -ProjectDir $projectRoot)
$summaryRows = @(Get-VivadoWarningSummaryRows -Warnings $warnings)
$policy = Test-VivadoWarningPolicy -Warnings $warnings

if ($policy.unexpected_warning_code_count -ne 0) {
  throw "Unexpected Vivado warning codes: $($policy.unexpected_warning_codes)"
}
if ($policy.constraint_truth_warning_lines -ne 0) {
  throw "Vivado warning log still contains source/old constraint warning lines: $($policy.constraint_truth_warning_lines)"
}
if ($policy.retired_hdmi_warning_lines -ne 0) {
  throw "Vivado warning log still contains retired HDMI/DVI/TMDS warning lines: $($policy.retired_hdmi_warning_lines)"
}

$csvText = ($summaryRows | ConvertTo-Csv -NoTypeInformation) -join [Environment]::NewLine
Write-TextFileWithRetry -Path $CsvPath -Text ($csvText + [Environment]::NewLine)

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# Vivado Warning Summary')
$lines.Add('')
$lines.Add('This generated summary classifies the current `synth_1/runme.log` and `impl_1/runme.log` warnings.')
$lines.Add('')
$lines.Add('## Gate Result')
$lines.Add('')
$lines.Add('- `vivado_warning_summary=classified`')
$lines.Add('- `vivado_warning_lines=' + $policy.warning_count + '`')
$lines.Add('- `unexpected_warning_codes=' + $policy.unexpected_warning_code_count + '`')
$lines.Add('- `constraint_truth_warning_lines=' + $policy.constraint_truth_warning_lines + '`')
$lines.Add('- `retired_hdmi_warning_lines=' + $policy.retired_hdmi_warning_lines + '`')
$lines.Add('')
$lines.Add('`constraint_truth_warning_lines=0` is the requirement that proves the v1.5 source XDC and old `system.xdc` warning chain is not present in the active Vivado run. Remaining warning lines are classified below and are not board safety proof.')
$lines.Add('')
$lines.Add('## Summary')
$lines.Add('')
$lines.Add('| Count | Code | Phases | Classification | Owner | Next Action |')
$lines.Add('| ---: | --- | --- | --- | --- | --- |')
foreach ($row in $summaryRows) {
  $code = Escape-MarkdownCell -Text ([string]$row.code)
  $phases = Escape-MarkdownCell -Text ([string]$row.phases)
  $classification = Escape-MarkdownCell -Text ([string]$row.classification)
  $owner = Escape-MarkdownCell -Text ([string]$row.owner)
  $nextAction = Escape-MarkdownCell -Text ([string]$row.next_action)
  $lines.Add('| ' + $row.count + ' | `' + $code + '` | `' + $phases + '` | `' + $classification + '` | `' + $owner + '` | `' + $nextAction + '` |')
}
$lines.Add('')
$lines.Add('## Boundary')
$lines.Add('')
$lines.Add('- This file does not claim all Vivado warnings are removed.')
$lines.Add('- It proves that current warning codes are known and that source/old constraint warning lines are absent.')
$lines.Add('- Retired HDMI/DVI warning residue is 0 after BD-level HDMI removal; MPG remains the owner of the shared pins.')

Write-TextFileWithRetry -Path $MarkdownPath -Text (($lines -join [Environment]::NewLine) + [Environment]::NewLine)

$csvRel = Convert-ToProjectRelativePath -RootDir $projectRoot -Path $CsvPath
$mdRel = Convert-ToProjectRelativePath -RootDir $projectRoot -Path $MarkdownPath

Write-Output 'vivado_warning_summary=classified'
Write-Output "vivado_warning_summary_csv=$csvRel"
Write-Output "vivado_warning_summary_md=$mdRel"
Write-Output "vivado_warning_lines=$($policy.warning_count)"
Write-Output "vivado_warning_codes=$($policy.allowed_code_count)"
Write-Output "unexpected_warning_codes=$($policy.unexpected_warning_code_count)"
Write-Output "constraint_truth_warning_lines=$($policy.constraint_truth_warning_lines)"
Write-Output "retired_hdmi_warning_lines=$($policy.retired_hdmi_warning_lines)"
Write-Output 'warning_summary_boundary=known_noncritical_warning_classes'
