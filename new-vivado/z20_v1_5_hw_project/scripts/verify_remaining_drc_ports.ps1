param(
  [string]$ProjectDir
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

$csvPath = Join-Path $ProjectDir 'docs/remaining_drc_ports.csv'
$exportScript = Join-Path $ProjectDir 'scripts/export_remaining_drc_ports.ps1'
$checkScript = Join-Path $ProjectDir 'scripts/check_active_xdc.ps1'

if (-not (Test-Path -LiteralPath $exportScript)) {
  throw "Missing export script: scripts/export_remaining_drc_ports.ps1"
}
if (-not (Test-Path -LiteralPath $checkScript)) {
  throw "Missing active XDC check script: scripts/check_active_xdc.ps1"
}

& powershell -NoProfile -ExecutionPolicy Bypass -File $exportScript -ProjectDir $ProjectDir -CsvOut $csvPath | Out-Null

if (-not (Test-Path -LiteralPath $csvPath)) {
  throw "Missing CSV after export: docs/remaining_drc_ports.csv"
}

$rows = @(Import-Csv -LiteralPath $csvPath)
$requiredColumns = @(
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

$checkOutput = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $checkScript -ProjectDir $ProjectDir)
if ($LASTEXITCODE -ne 0) {
  throw "check_active_xdc.ps1 failed during CSV verification: $($checkOutput -join '; ')"
}

$checkCountLine = @($checkOutput | Where-Object { $_ -match '^unassigned_top_ports_count=\d+$' } | Select-Object -First 1)
if ($checkCountLine.Count -ne 1 -or $checkCountLine[0] -notmatch '^unassigned_top_ports_count=(\d+)$') {
  throw 'Could not parse unassigned_top_ports_count from check_active_xdc.ps1'
}
$checkUnassignedCount = [int]$Matches[1]

$checkPorts = New-Object System.Collections.Generic.List[string]
$readPorts = $false
foreach ($line in $checkOutput) {
  if ($line -eq 'unassigned_top_ports=') {
    $readPorts = $true
    continue
  }
  if ($readPorts -and $line -match '^\s+(.+?)\s*$') {
    $checkPorts.Add($Matches[1])
  }
}

if ($checkPorts.Count -ne $checkUnassignedCount) {
  throw "Parsed unassigned ports count $($checkPorts.Count) does not match check_active_xdc count $checkUnassignedCount"
}

if ($rows.Count -eq 0) {
  if ($checkUnassignedCount -ne 0) {
    throw "CSV has no rows but check_active_xdc reports $checkUnassignedCount unassigned ports"
  }
  $headerLine = Get-Content -LiteralPath $csvPath -TotalCount 1
  $actualColumns = @($headerLine -split ',' | ForEach-Object { $_.Trim().Trim('"') })
  $missingColumns = @($requiredColumns | Where-Object { $_ -notin $actualColumns })
  if ($missingColumns.Count -gt 0) {
    throw "CSV missing required columns: $($missingColumns -join ',')"
  }
  Write-Output 'csv_rows=0'
  Write-Output "check_active_unassigned_ports=$checkUnassignedCount"
  Write-Output 'csv_matches_check_active=yes'
  Write-Output 'required_columns=present'
  Write-Output 'closure_blockers='
  Write-Output 'active_pin_conflicts=0'
  exit 0
}

$actualColumns = @($rows[0].PSObject.Properties.Name)
$missingColumns = @($requiredColumns | Where-Object { $_ -notin $actualColumns })
if ($missingColumns.Count -gt 0) {
  throw "CSV missing required columns: $($missingColumns -join ',')"
}

$duplicatePorts = @($rows | Group-Object port | Where-Object { $_.Count -gt 1 } | ForEach-Object Name)
if ($duplicatePorts.Count -gt 0) {
  throw "CSV duplicate ports: $($duplicatePorts -join ',')"
}

$missingGroups = @($rows | Where-Object { [string]::IsNullOrWhiteSpace($_.group) } | ForEach-Object port)
if ($missingGroups.Count -gt 0) {
  throw "CSV rows missing group: $($missingGroups -join ',')"
}

$missingConditions = @($rows | Where-Object { [string]::IsNullOrWhiteSpace($_.close_condition) } | ForEach-Object port)
if ($missingConditions.Count -gt 0) {
  throw "CSV rows missing close_condition: $($missingConditions -join ',')"
}

$missingBlockers = @($rows | Where-Object { [string]::IsNullOrWhiteSpace($_.closure_blocker) } | ForEach-Object port)
if ($missingBlockers.Count -gt 0) {
  throw "CSV rows missing closure_blocker: $($missingBlockers -join ',')"
}

$missingNextActions = @($rows | Where-Object { [string]::IsNullOrWhiteSpace($_.next_action) } | ForEach-Object port)
if ($missingNextActions.Count -gt 0) {
  throw "CSV rows missing next_action: $($missingNextActions -join ',')"
}

$invalidConflictValues = @($rows | Where-Object { $_.active_pin_conflict -notin @('yes', 'no') } | ForEach-Object port)
if ($invalidConflictValues.Count -gt 0) {
  throw "CSV rows have invalid active_pin_conflict: $($invalidConflictValues -join ',')"
}

$activeConflictMismatch = @(
  $rows | Where-Object {
    ($_.active_pin_conflict -eq 'yes' -and [string]::IsNullOrWhiteSpace($_.active_port_same_pin)) -or
    ($_.active_pin_conflict -eq 'no' -and -not [string]::IsNullOrWhiteSpace($_.active_port_same_pin))
  } | ForEach-Object port
)
if ($activeConflictMismatch.Count -gt 0) {
  throw "CSV active conflict flag mismatch: $($activeConflictMismatch -join ',')"
}

$csvPorts = @($rows | ForEach-Object port | Sort-Object)
$checkPortsSorted = @($checkPorts | Sort-Object)
$missingFromCsv = @($checkPortsSorted | Where-Object { $_ -notin $csvPorts })
$extraInCsv = @($csvPorts | Where-Object { $_ -notin $checkPortsSorted })
if ($missingFromCsv.Count -gt 0 -or $extraInCsv.Count -gt 0) {
  throw "CSV/check_active_xdc port mismatch missing_from_csv=[$($missingFromCsv -join ',')] extra_in_csv=[$($extraInCsv -join ',')]"
}

$groups = @($rows | Group-Object group | Sort-Object Name)
$blockers = @($rows | Group-Object closure_blocker | Sort-Object Name)
$activeConflicts = @($rows | Where-Object { $_.active_pin_conflict -eq 'yes' } | Sort-Object port)

Write-Output "csv_rows=$($rows.Count)"
Write-Output "check_active_unassigned_ports=$checkUnassignedCount"
Write-Output 'csv_matches_check_active=yes'
Write-Output "required_columns=present"
$groups | ForEach-Object {
  Write-Output "$($_.Name)=$($_.Count)"
}
Write-Output 'closure_blockers='
$blockers | ForEach-Object {
  Write-Output "  $($_.Name)=$($_.Count)"
}
Write-Output "active_pin_conflicts=$($activeConflicts.Count)"
$activeConflicts | ForEach-Object {
  Write-Output "  $($_.port) old_pin=$($_.old_pin) active_port=$($_.active_port_same_pin)"
}
