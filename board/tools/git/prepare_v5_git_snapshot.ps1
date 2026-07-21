[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Repository,

    [Parameter(Mandatory = $true)]
    [string]$BaselineRef,

    [string[]]$AllowedNewRoot = @()
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

function Convert-ToExtendedPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ($Path.StartsWith('\\?\')) {
        return $Path
    }
    if ($Path.StartsWith('\\')) {
        return '\\?\UNC\' + $Path.Substring(2)
    }
    return '\\?\' + $Path
}

function Invoke-GitLines {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = @(& git -C $script:RepositoryRoot @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorAction
    }
    if ($exitCode -ne 0) {
        $message = ($output | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
        throw "git $($Arguments -join ' ') failed with exit code ${exitCode}:`n$message"
    }
    return @($output | ForEach-Object { $_.ToString() })
}

function Get-WindowsReservedStem {
    param([Parameter(Mandatory = $true)][string]$Name)

    $candidate = $Name -replace '[ .]+$', ''
    $pattern = '^(?i:(CON|PRN|AUX|NUL|CLOCK\$|COM(?:[1-9]|\u00B9|\u00B2|\u00B3)|LPT(?:[1-9]|\u00B9|\u00B2|\u00B3)))(?:\..*)?$'
    if ($candidate -match $pattern) {
        return $Matches[1]
    }
    return $null
}

function Get-UnregisteredRootFiles {
    param(
        [Parameter(Mandatory = $true)][string[]]$BaselineRoot,
        [string[]]$AllowedRoot = @()
    )

    $extendedRoot = Convert-ToExtendedPath -Path $script:RepositoryRoot
    $files = @([IO.Directory]::EnumerateFiles(
        $extendedRoot,
        '*',
        [IO.SearchOption]::TopDirectoryOnly
    ))

    $candidates = foreach ($sourcePath in $files) {
        $name = [IO.Path]::GetFileName($sourcePath)
        if ($BaselineRoot -contains $name -or $AllowedRoot -contains $name) {
            continue
        }

        $reservedStem = Get-WindowsReservedStem -Name $name
        $reason = 'unregistered_root_file'
        if ($null -ne $reservedStem) {
            $reason += ";windows_reserved_name:$reservedStem"
        }

        [pscustomobject]@{
            Name       = $name
            SourcePath = $sourcePath
            Reason     = $reason
        }
    }

    return @($candidates | Sort-Object -Property Name -Unique)
}

function Assert-StableRegularFile {
    param([Parameter(Mandatory = $true)]$Candidate)

    $attributes = [IO.File]::GetAttributes($Candidate.SourcePath)
    if (($attributes -band [IO.FileAttributes]::Directory) -ne 0) {
        throw "Refusing to quarantine a directory: $($Candidate.Name)"
    }
    if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing to quarantine a reparse point: $($Candidate.Name)"
    }

    [IO.FileInfo]$fileInfo = $Candidate.SourcePath
    $fileInfo.Refresh()
    if (-not $fileInfo.Exists) {
        throw "Root file disappeared during preflight: $($Candidate.Name)"
    }
    $lengthBefore = $fileInfo.Length
    $writeBefore = $fileInfo.LastWriteTimeUtc.Ticks

    Start-Sleep -Milliseconds 200

    $fileInfo.Refresh()
    if (-not $fileInfo.Exists) {
        throw "Root file disappeared during stability check: $($Candidate.Name)"
    }
    if ($lengthBefore -ne $fileInfo.Length -or $writeBefore -ne $fileInfo.LastWriteTimeUtc.Ticks) {
        throw "Root file is still being written; retry after its producer exits: $($Candidate.Name)"
    }
}

function Convert-ToManifestField {
    param([Parameter(Mandatory = $true)][string]$Value)

    return $Value.Replace("`t", '\t').Replace("`r", '\r').Replace("`n", '\n')
}

$resolvedRepository = (Resolve-Path -LiteralPath $Repository).ProviderPath
$script:RepositoryRoot = [IO.Path]::GetFullPath($resolvedRepository).TrimEnd([IO.Path]::DirectorySeparatorChar)
if (-not [IO.Directory]::Exists((Join-Path $script:RepositoryRoot '.git'))) {
    throw "Git repository not found: $script:RepositoryRoot"
}
$requiredRuleOwner = Join-Path $script:RepositoryRoot 'AGENTS.md'
if (-not [IO.File]::Exists($requiredRuleOwner)) {
    throw "Required root rule owner is missing: $requiredRuleOwner"
}

$baselineRoot = @(Invoke-GitLines -Arguments @(
    '-c', 'core.quotepath=false',
    'ls-tree', '--name-only', $BaselineRef, '--'
))
$allowedFromEnvironment = @(
    $env:V5_GIT_ALLOWED_NEW_ROOT -split ';' |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
)
$canonicalAllowedRoot = @('AGENTS.md')
$effectiveAllowedRoot = @(
    $canonicalAllowedRoot + $AllowedNewRoot + $allowedFromEnvironment |
        Sort-Object -Unique
)

$runId = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$quarantineRoot = Join-Path $script:RepositoryRoot "repo_ignored\git_push_quarantine\$runId"
$manifestPath = Join-Path $quarantineRoot 'manifest.tsv'
$manifestEncoding = New-Object System.Text.UTF8Encoding($false)
$movedCount = 0
$maxPasses = 3

for ($pass = 1; $pass -le $maxPasses; $pass++) {
    $candidates = @(Get-UnregisteredRootFiles -BaselineRoot $baselineRoot -AllowedRoot $effectiveAllowedRoot)
    if ($candidates.Count -eq 0) {
        break
    }

    if (-not [IO.Directory]::Exists($quarantineRoot)) {
        [IO.Directory]::CreateDirectory($quarantineRoot) | Out-Null
        [IO.File]::WriteAllText(
            $manifestPath,
            "original_path`tquarantine_path`treason`r`n",
            $manifestEncoding
        )
    }

    foreach ($candidate in $candidates) {
        Assert-StableRegularFile -Candidate $candidate

        $destinationName = 'root_' + $candidate.Name
        $destinationPath = Join-Path $quarantineRoot $destinationName
        if ([IO.File]::Exists((Convert-ToExtendedPath -Path $destinationPath))) {
            throw "Quarantine destination already exists: $destinationPath"
        }

        [IO.File]::Move(
            $candidate.SourcePath,
            (Convert-ToExtendedPath -Path $destinationPath)
        )

        $relativeDestination = $destinationPath.Substring($script:RepositoryRoot.Length + 1)
        $manifestLine = "{0}`t{1}`t{2}`r`n" -f
            (Convert-ToManifestField -Value $candidate.Name),
            (Convert-ToManifestField -Value $relativeDestination),
            (Convert-ToManifestField -Value $candidate.Reason)
        [IO.File]::AppendAllText($manifestPath, $manifestLine, $manifestEncoding)

        Write-Host ("  quarantined {0} -> {1} [{2}]" -f
            $candidate.Name,
            $relativeDestination,
            $candidate.Reason)
        $movedCount++
    }
}

$remaining = @(Get-UnregisteredRootFiles -BaselineRoot $baselineRoot -AllowedRoot $effectiveAllowedRoot)
if ($remaining.Count -gt 0) {
    $names = ($remaining | ForEach-Object { $_.Name }) -join ', '
    throw "Unregistered root files remain after $maxPasses preflight passes: $names"
}

if ($movedCount -eq 0) {
    Write-Host 'Pre-stage quarantine: no unregistered root files.'
} else {
    $relativeManifest = $manifestPath.Substring($script:RepositoryRoot.Length + 1)
    Write-Host "Pre-stage quarantine: moved $movedCount file(s)."
    Write-Host "Quarantine manifest: $relativeManifest"
}
