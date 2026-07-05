param(
  [string]$ProjectDir
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

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

function Test-RelativePathText {
  param([string]$Path)

  if ([string]::IsNullOrWhiteSpace($Path)) {
    return $false
  }
  if ([System.IO.Path]::IsPathRooted($Path)) {
    return $false
  }
  if ($Path -match '(?<![A-Za-z])[A-Za-z]:[\\/]') {
    return $false
  }
  if (($Path -split '[\\/]') -contains '..') {
    return $false
  }
  return $true
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

function Get-PortableTextFiles {
  param([string]$RootDir)

  $textExtensions = @(
    '.bd', '.csv', '.gitattributes', '.editorconfig', '.jou', '.js', '.json',
    '.log', '.md', '.ps1', '.rpt', '.sh', '.tcl', '.txt', '.v', '.vdi',
    '.vds', '.vh', '.vhd', '.vhdl', '.vvp', '.xdc', '.xci', '.xml', '.xpr'
  )
  $generatedDirPattern = '(\\|/)(\.Xil|z20_v1_5_hw_project\.(cache|gen|hw|ip_user_files|runs))(\\|/)'
  $ignoredDirPattern = '(\\|/)repo_ignored(\\|/)'
  $rootPath = (Resolve-Path -LiteralPath $RootDir).Path.TrimEnd('\', '/')

  return @(
    Get-ChildItem -LiteralPath $RootDir -File -Recurse -Force |
      Where-Object {
        if ($_.FullName -match $ignoredDirPattern) {
          return $false
        }
        if ($_.FullName -match $generatedDirPattern) {
          return $false
        }
        if ($_.DirectoryName.TrimEnd('\', '/') -ieq $rootPath -and $_.Name -match '^vivado(_[0-9]+\.backup)?\.(log|jou)$') {
          return $false
        }
        return ($_.Extension -in $textExtensions -or $textExtensions -contains $_.Name)
      } |
      Sort-Object FullName -Unique
  )
}

function Assert-NoPattern {
  param(
    [object[]]$Files,
    [string]$RootDir,
    [string]$Pattern,
    [string]$Label
  )

  $violations = New-Object System.Collections.Generic.List[string]
  $paths = @($Files | ForEach-Object { $_.FullName })
  if ($paths.Count -eq 0) {
    return
  }

  $matches = @(Select-String -LiteralPath $paths -Pattern $Pattern -AllMatches)
  foreach ($match in $matches) {
    $relative = Convert-ToProjectRelativePath -RootDir $RootDir -Path $match.Path
    $violations.Add("${relative}:$($match.LineNumber):$($match.Line.Trim())")
  }

  if ($violations.Count -gt 0) {
    Write-Output "$Label violations:"
    foreach ($violation in $violations) {
      Write-Output "  $violation"
    }
    throw "$Label violations found: $($violations.Count)"
  }
}

$projectRoot = (Resolve-Path -LiteralPath $ProjectDir).Path
$files = Get-PortableTextFiles -RootDir $projectRoot

Assert-NoPattern -Files $files -RootDir $projectRoot -Pattern '(?<![A-Za-z])[A-Za-z]:[\\/]' -Label 'absolute_path'

$selfPath = (Resolve-Path -LiteralPath $PSCommandPath).Path
$dependencyFiles = @($files | Where-Object {
    $_.FullName -ne $selfPath -and
    $_.FullName -notmatch '(\\|/)(docs|board_inputs|reports)(\\|/)' -and
    $_.Name -ne 'README.md'
  })
Assert-NoPattern -Files $dependencyFiles -RootDir $projectRoot -Pattern '([A-Za-z]:[\\/][^\r\n]*[\\/]vivado_hw_project[\\/])|((^|[\\/])\.\.[\\/]vivado_hw_project[\\/])|(vivado_hw_project[\\/])' -Label 'old_project_dependency'

$tmpFiles = @(Get-ChildItem -LiteralPath $projectRoot -Recurse -Force -File -Filter '.*.tmp')
$tmpFiles = @($tmpFiles | Where-Object { $_.FullName -notmatch '(\\|/)repo_ignored(\\|/)' })
if ($tmpFiles.Count -gt 0) {
  foreach ($tmpFile in $tmpFiles) {
    Write-Output "tmp_file=$(Convert-ToProjectRelativePath -RootDir $projectRoot -Path $tmpFile.FullName)"
  }
  throw "Temporary generated files remain in project: $($tmpFiles.Count)"
}

$manifestPath = Join-Path $projectRoot 'board_inputs/manifest.json'
$manifestRelativePathCount = 0
if (Test-Path -LiteralPath $manifestPath) {
  $manifest = (Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8) | ConvertFrom-Json
  if ($manifest.artifact_paths_are_relative_to -ne 'new-vivado/z20_v1_5_hw_project') {
    throw "Unexpected manifest artifact root: $($manifest.artifact_paths_are_relative_to)"
  }
  foreach ($artifact in @($manifest.artifacts)) {
    $manifestRelativePathCount += 1
    if (-not (Test-RelativePathText -Path $artifact.path)) {
      throw "Manifest artifact path is not project-relative: $($artifact.path)"
    }
  }
}

Write-Output 'project_portability=ok'
Write-Output 'absolute_path_scan=ok'
Write-Output 'old_project_dependency=ok'
Write-Output 'manifest_relative_paths=ok'
Write-Output "manifest_artifact_paths=$manifestRelativePathCount"
Write-Output "tmp_files=$($tmpFiles.Count)"
Write-Output "text_files_scanned=$($files.Count)"
Write-Output "dependency_files_scanned=$($dependencyFiles.Count)"
