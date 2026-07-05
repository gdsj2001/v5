param(
  [string]$ProjectDir
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
  $ProjectDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

$projectRoot = (Resolve-Path -LiteralPath $ProjectDir).Path
$repoRoot = (Resolve-Path -LiteralPath (Join-Path (Join-Path $projectRoot '..') '..')).Path
$newVivadoRoot = (Resolve-Path -LiteralPath (Join-Path $projectRoot '..')).Path

function Get-PortableTextFiles {
  param([string]$RootDir)

  $textExtensions = @(
    '.bd', '.csv', '.gitattributes', '.editorconfig', '.jou', '.js', '.json',
    '.log', '.md', '.ps1', '.rpt', '.sh', '.tcl', '.txt', '.v', '.vdi',
    '.vds', '.vh', '.vhd', '.vhdl', '.vvp', '.xdc', '.xci', '.xml', '.xpr'
  )

  return @(
    Get-ChildItem -LiteralPath $RootDir -File -Recurse -Force |
      Where-Object { $_.Extension -in $textExtensions -or $textExtensions -contains $_.Name } |
      Sort-Object FullName -Unique
  )
}

function Convert-ToSlashPath {
  param([string]$Path)
  return ($Path -replace '\\', '/')
}

function Convert-ToBackslashPath {
  param([string]$Path)
  return ($Path -replace '/', '\')
}

function Add-Replacement {
  param(
    [System.Collections.Generic.List[object]]$List,
    [string]$From,
    [string]$To
  )

  if (-not [string]::IsNullOrWhiteSpace($From)) {
    $List.Add([pscustomobject]@{ From = $From; To = $To })
  }
}

function Replace-ToolchainAbsolutePaths {
  param([string]$Text)

  $result = $Text
  $result = [regex]::Replace(
    $result,
    '(?i)(?<![A-Za-z])[A-Za-z]:[\\/]Xilinx[\\/]Vivado[\\/][^\\/\s\]\)'']+',
    { '$VIVADO_ROOT' })
  $result = [regex]::Replace(
    $result,
    '(?i)(?<![A-Za-z])[A-Za-z]:[\\/]Xilinx[\\/]Vitis[\\/][^\\/\s\]\)'']+',
    { '$VITIS_ROOT' })
  $result = [regex]::Replace(
    $result,
    '(?i)(?<![A-Za-z])[A-Za-z]:[\\/]Xilinx',
    { '$XILINX_ROOT' })
  return $result
}

$replacements = New-Object System.Collections.Generic.List[object]

foreach ($pathShape in @(
    (Convert-ToSlashPath $projectRoot),
    (Convert-ToBackslashPath $projectRoot),
    (Convert-ToSlashPath $projectRoot).ToLowerInvariant(),
    (Convert-ToBackslashPath $projectRoot).ToLowerInvariant())) {
  Add-Replacement -List $replacements -From $pathShape -To '$PPRDIR'
}
foreach ($pathShape in @(
    (Convert-ToSlashPath $newVivadoRoot),
    (Convert-ToBackslashPath $newVivadoRoot),
    (Convert-ToSlashPath $newVivadoRoot).ToLowerInvariant(),
    (Convert-ToBackslashPath $newVivadoRoot).ToLowerInvariant())) {
  Add-Replacement -List $replacements -From $pathShape -To '$PROJECT_PARENT'
}
foreach ($pathShape in @(
    (Convert-ToSlashPath $repoRoot),
    (Convert-ToBackslashPath $repoRoot),
    (Convert-ToSlashPath $repoRoot).ToLowerInvariant(),
    (Convert-ToBackslashPath $repoRoot).ToLowerInvariant())) {
  Add-Replacement -List $replacements -From $pathShape -To '$REPO_ROOT'
}

$changed = 0
foreach ($file in Get-PortableTextFiles -RootDir $projectRoot) {
  $text = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
  $newText = $text
  foreach ($replacement in $replacements) {
    $newText = $newText.Replace($replacement.From, $replacement.To)
  }
  $newText = [regex]::Replace($newText, '(\$PPRDIR[\\/])+', '$PPRDIR/')
  $newText = Replace-ToolchainAbsolutePaths -Text $newText
  if ($newText -ne $text) {
    Set-Content -LiteralPath $file.FullName -Value $newText -Encoding UTF8 -NoNewline
    $changed += 1
  }
}

$projectFile = Join-Path $projectRoot 'z20_v1_5_hw_project.xpr'
if (Test-Path -LiteralPath $projectFile) {
  $xprText = Get-Content -LiteralPath $projectFile -Raw -Encoding UTF8
  $fixed = $xprText -replace '<Project Version="7" Minor="54" Path="[^"]+">', '<Project Version="7" Minor="54" Path="$PPRDIR/z20_v1_5_hw_project.xpr">'
  if ($fixed -ne $xprText) {
    Set-Content -LiteralPath $projectFile -Value $fixed -Encoding UTF8 -NoNewline
    $changed += 1
  }
}

Write-Output "vivado_absolute_path_scrub=ok"
Write-Output "text_files_changed=$changed"
