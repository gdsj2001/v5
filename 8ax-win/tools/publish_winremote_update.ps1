param(
    [Parameter(Mandatory=$true)][string]$Version,
    [string]$Configuration = "Release",
    [string]$Runtime = "win-x64",
    [string]$Vps = "vps3",
    [string]$RemoteDir = "/var/www/html/updates/8ax-winremote/win-x64",
    [string]$OutDir = "",
    [string]$PrimaryDomain = "https://license.cjwsjzyy.xyz",
    [string]$BackupDomain = "https://license.3dtouch.top",
    [string]$PublishDir = "",
    [switch]$SkipUpload
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($PublishDir)) {
    $PublishDir = Join-Path $repo "8ax-win\publish\$Runtime"
}
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $repo "repo_ignored\release\winremote_update"
}
$project = Join-Path $repo "8ax-win\src\8ax.WinRemote\8ax.WinRemote.csproj"
$packageName = "8ax-winremote-win-x64.zip"
$packagePath = Join-Path $OutDir $packageName
$manifestPath = Join-Path $OutDir "manifest.json"
$exeName = "8ax.WinRemote.exe"
$requiredFiles = @($exeName)
$forbiddenSidecars = @(
    "8ax.WinRemote.dll",
    "8ax.WinRemote.deps.json",
    "8ax.WinRemote.runtimeconfig.json",
    "8ax.WinRemote.pdb"
)

$versionParts = @([regex]::Matches($Version, '\d+') | ForEach-Object { [int]$_.Value })
if ($versionParts.Count -lt 1) {
    throw "Version must contain numeric parts, got: $Version"
}
while ($versionParts.Count -lt 4) {
    $versionParts += 0
}
$assemblyVersion = ($versionParts[0..3] -join ".")

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Remove-Item -LiteralPath $PublishDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $PublishDir | Out-Null

dotnet publish $project `
    -c $Configuration `
    -r $Runtime `
    --self-contained true `
    -o $PublishDir `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:EnableCompressionInSingleFile=true `
    -p:PublishTrimmed=false `
    -p:DebugType=embedded `
    -p:DebugSymbols=false `
    -p:Version=$Version `
    -p:InformationalVersion=$Version `
    -p:AssemblyVersion=$assemblyVersion `
    -p:FileVersion=$assemblyVersion

foreach ($file in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $PublishDir $file))) {
        throw "publish output missing required file: $file"
    }
}

$publishedFiles = @(Get-ChildItem -LiteralPath $PublishDir -File)
if ($publishedFiles.Count -ne 1 -or $publishedFiles[0].Name -ne $exeName) {
    $names = ($publishedFiles | ForEach-Object { $_.Name }) -join ", "
    throw "single-file publish produced unexpected files: $names"
}
foreach ($file in $forbiddenSidecars) {
    if (Test-Path -LiteralPath (Join-Path $PublishDir $file)) {
        throw "multi-file sidecar must not be published: $file"
    }
}

Remove-Item -LiteralPath $packagePath -Force -ErrorAction SilentlyContinue
$lastZipError = $null
for ($attempt = 1; $attempt -le 8; $attempt++) {
    try {
        Compress-Archive -LiteralPath (Join-Path $PublishDir $exeName) -DestinationPath $packagePath -Force -ErrorAction Stop
        $lastZipError = $null
        break
    } catch {
        $lastZipError = $_
        Start-Sleep -Milliseconds (500 * $attempt)
    }
}
if ($lastZipError -ne $null) {
    throw $lastZipError
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipFileNames = [System.IO.Compression.ZipFile]::OpenRead($packagePath).Entries | ForEach-Object { $_.FullName }
foreach ($file in $requiredFiles) {
    if ($zipFileNames -notcontains $file) {
        throw "update package missing required file: $file"
    }
}
if ($zipFileNames.Count -ne 1) {
    throw "update package must contain only $exeName"
}

$hash = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash
$size = (Get-Item -LiteralPath $packagePath).Length
$manifest = [ordered]@{
    app_id = "8ax.WinRemote"
    version = $Version
    file_name = $packageName
    package_url = $packageName
    sha256 = $hash
    size = $size
    published_at_utc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
}
$manifestJson = $manifest | ConvertTo-Json
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($manifestPath, $manifestJson, $utf8NoBom)

$primaryManifestUrl = "$PrimaryDomain/8ax-winremote/win-x64/manifest.json"
$backupManifestUrl = "$BackupDomain/8ax-winremote/win-x64/manifest.json"
$uploadStatus = "pending"

function Assert-ManifestMatchesRelease {
    param(
        [Parameter(Mandatory=$true)][string]$Json,
        [Parameter(Mandatory=$true)][string]$Context
    )

    $Json = $Json.TrimStart([char]0xFEFF)
    if ($Json.Length -ge 3 -and $Json[0] -eq [char]0x00EF -and $Json[1] -eq [char]0x00BB -and $Json[2] -eq [char]0x00BF) {
        $Json = $Json.Substring(3)
    }
    $parsed = $Json | ConvertFrom-Json
    if ($parsed.app_id -ne "8ax.WinRemote") {
        throw "$Context manifest app_id mismatch: $($parsed.app_id)"
    }
    if ($parsed.version -ne $Version) {
        throw "$Context manifest version mismatch: $($parsed.version) != $Version"
    }
    if ($parsed.file_name -ne $packageName) {
        throw "$Context manifest file_name mismatch: $($parsed.file_name) != $packageName"
    }
    if ($parsed.package_url -ne $packageName) {
        throw "$Context manifest package_url mismatch: $($parsed.package_url) != $packageName"
    }
    if ($parsed.sha256 -ne $hash) {
        throw "$Context manifest sha256 mismatch: $($parsed.sha256) != $hash"
    }
    if ([int64]$parsed.size -ne [int64]$size) {
        throw "$Context manifest size mismatch: $($parsed.size) != $size"
    }
}

function Test-ManifestEndpoint {
    param([Parameter(Mandatory=$true)][string]$Uri)

    try {
        $response = Invoke-WebRequest -UseBasicParsing -TimeoutSec 15 -Uri $Uri
        Assert-ManifestMatchesRelease -Json $response.Content -Context $Uri
        return $true
    } catch {
        Write-Warning "manifest verification failed for ${Uri}: $($_.Exception.Message)"
        return $false
    }
}

if (-not $SkipUpload) {
    # Formal release path: package generation must be followed by direct VPS upload and verification.
    $remotePackagePath = "$RemoteDir/$packageName"
    $remoteManifestPath = "$RemoteDir/manifest.json"
    ssh $Vps "mkdir -p '$RemoteDir'"
    scp $packagePath "${Vps}:$remotePackagePath"
    scp $manifestPath "${Vps}:$remoteManifestPath"

    $remoteHash = (& ssh $Vps "sha256sum '$remotePackagePath' | cut -d ' ' -f 1")
    $remoteHash = ($remoteHash | Select-Object -First 1).Trim()
    if ($remoteHash.ToUpperInvariant() -ne $hash.ToUpperInvariant()) {
        throw "VPS package sha256 mismatch: $remoteHash != $hash"
    }

    $remoteManifestJson = ((& ssh $Vps "cat '$remoteManifestPath'") -join "`n")
    Assert-ManifestMatchesRelease -Json $remoteManifestJson -Context "VPS remote file"

    $httpsVerified = $false
    for ($attempt = 1; $attempt -le 6; $attempt++) {
        $primaryOk = Test-ManifestEndpoint -Uri $primaryManifestUrl
        $backupOk = Test-ManifestEndpoint -Uri $backupManifestUrl
        if ($primaryOk -and $backupOk) {
            $httpsVerified = $true
            break
        }
        Start-Sleep -Seconds 2
    }
    if (-not $httpsVerified) {
        throw "VPS HTTPS manifest verification failed for primary or backup domain"
    }
    $uploadStatus = "uploaded and verified"
} else {
    Write-Warning "-SkipUpload is diagnostic only; generated packages are not a formal release until uploaded to VPS."
    $uploadStatus = "skipped (diagnostic only)"
}

Write-Host "Published WinRemote update:"
Write-Host "  primary: $primaryManifestUrl"
Write-Host "  backup:  $backupManifestUrl"
Write-Host "  exe:     $exeName"
Write-Host "  version: $Version"
Write-Host "  filever: $assemblyVersion"
Write-Host "  sha256:  $hash"
Write-Host "  source:  $PublishDir"
Write-Host "  package contains single executable only: $exeName"
Write-Host "  upload:  $uploadStatus"
