param(
  [string]$ProjectDir
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

function Convert-ToRelativePath {
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

function Add-ExistingFile {
  param(
    [System.Collections.Generic.List[object]]$List,
    [string]$Path
  )
  if (Test-Path -LiteralPath $Path) {
    $List.Add((Get-Item -LiteralPath $Path))
  }
}

function Get-ProjectFiles {
  param([string]$RootDir)

  $files = New-Object System.Collections.Generic.List[object]
  foreach ($relative in @(
      'z20_v1_5_hw_project.xpr',
      'z20_v1_5_hw_project.srcs/sources_1/bd/system/system.bd',
      'board_inputs/manifest.json',
      '.editorconfig',
      '.gitattributes')) {
    Add-ExistingFile -List $files -Path (Join-Path $RootDir ($relative -replace '/', [System.IO.Path]::DirectorySeparatorChar))
  }

  foreach ($relativeDir in @(
      'constraints',
      'scripts',
      'docs',
      'rtl',
      'sim',
      'z20_v1_5_hw_project.srcs/sources_1/bd/system/ip')) {
    $dir = Join-Path $RootDir ($relativeDir -replace '/', [System.IO.Path]::DirectorySeparatorChar)
    if (Test-Path -LiteralPath $dir) {
      Get-ChildItem -LiteralPath $dir -File -Recurse |
        Where-Object { $_.Extension -in @('.xdc', '.tcl', '.ps1', '.md', '.csv', '.json', '.v', '.vh', '.xci', '.xml') } |
        ForEach-Object { $files.Add($_) }
    }
  }

  return @($files | Sort-Object FullName -Unique)
}

function Assert-NoMatches {
  param(
    [object[]]$Files,
    [string]$RootDir,
    [string]$Pattern,
    [string]$Label
  )

  $violations = New-Object System.Collections.Generic.List[string]
  foreach ($file in $Files) {
    $matches = @(Select-String -LiteralPath $file.FullName -Pattern $Pattern -AllMatches)
    foreach ($match in $matches) {
      $relative = Convert-ToRelativePath -RootDir $RootDir -Path $file.FullName
      $violations.Add("${relative}:$($match.LineNumber):$($match.Line.Trim())")
    }
  }

  if ($violations.Count -gt 0) {
    Write-Output "$Label violations:"
    foreach ($violation in $violations) {
      Write-Output "  $violation"
    }
    throw "$Label violations found: $($violations.Count)"
  }
}

$projectFile = Join-Path $ProjectDir 'z20_v1_5_hw_project.xpr'
$bdFile = Join-Path $ProjectDir 'z20_v1_5_hw_project.srcs/sources_1/bd/system/system.bd'
$activeXdc = Join-Path $ProjectDir 'constraints/z20_v1_5_active_mapped.xdc'
$v15Xdc = Join-Path (Split-Path -Parent $ProjectDir) 'z20-v1_5_20260623.xdc'

foreach ($required in @($projectFile, $bdFile, $activeXdc, $v15Xdc)) {
  if (-not (Test-Path -LiteralPath $required)) {
    throw "Missing required project file: $required"
  }
}

$xprText = Get-Content -LiteralPath $projectFile -Raw -Encoding UTF8
if ($xprText -notmatch '<Project[^>]+Path="\$PPRDIR/z20_v1_5_hw_project\.xpr"') {
  throw 'Project file Path is not relative to $PPRDIR'
}
if ($xprText -notmatch '\$PPRDIR/constraints/z20_v1_5_active_mapped\.xdc') {
  throw 'Project file does not reference the active mapped XDC'
}
if ($xprText -match 'constraints_reference/system_old\.xdc' -or $xprText -match 'constrs_1/system\.xdc') {
  throw 'Project file references an old constraint source'
}

$allScanFiles = Get-ProjectFiles -RootDir $ProjectDir
Assert-NoMatches -Files $allScanFiles -RootDir $ProjectDir -Pattern '(?<![A-Za-z])[A-Za-z]:[\\/]' -Label 'absolute_path'

$selfPath = (Resolve-Path -LiteralPath $PSCommandPath).Path
$sourceDependencyFiles = @($allScanFiles | Where-Object {
    $_.FullName -ne $selfPath -and
    $_.FullName -notmatch '(\\|/)docs(\\|/)' -and
    $_.Name -ne 'README.md'
  })
Assert-NoMatches -Files $sourceDependencyFiles -RootDir $ProjectDir -Pattern '([A-Za-z]:[\\/][^\r\n]*[\\/]vivado_hw_project[\\/])|((^|[\\/])\.\.[\\/]vivado_hw_project[\\/])|(vivado_hw_project[\\/])' -Label 'old_project_dependency'

Write-Output 'project_independence=ok'
Write-Output 'project_path_relative=ok'
Write-Output 'active_xdc_source=constraints/z20_v1_5_active_mapped.xdc'
Write-Output "source_files_scanned=$($allScanFiles.Count)"
Write-Output "dependency_files_scanned=$($sourceDependencyFiles.Count)"
