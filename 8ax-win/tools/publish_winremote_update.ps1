param(
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$true)][long]$ReleaseSequence,
    [string]$Channel = "stable",
    [string]$Configuration = "Release",
    [string]$Runtime = "win-x64",
    [string]$Vps = "vps3",
    [string]$RemoteDir = "/var/www/html/updates/8ax-winremote/win-x64",
    [string]$OutDir = "",
    [string]$PrimaryDomain = "https://license.cjwsjzyy.xyz",
    [string]$BackupDomain = "https://license.3dtouch.top",
    [string]$PublishDir = "",
    [string]$SigningPrivateKey = "D:\授权私钥\winremote-update-signing-private.pem",
    [string]$Python = "python",
    [switch]$SkipUpload
)

$ErrorActionPreference = "Stop"
$keyId = "winremote-update-p256-2026-01"
$manifestSchema = "v5.winremote_update_manifest.v2"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($PublishDir)) {
    $PublishDir = Join-Path $repo "8ax-win\publish\$Runtime"
}
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $repo "repo_ignored\release\winremote_update"
}
$officialPublishDir = [System.IO.Path]::GetFullPath((Join-Path $repo "8ax-win\publish\$Runtime")).TrimEnd('\', '/')
$diagnosticRoot = [System.IO.Path]::GetFullPath((Join-Path $repo "repo_ignored")).TrimEnd('\', '/')
$PublishDir = [System.IO.Path]::GetFullPath($PublishDir).TrimEnd('\', '/')
$OutDir = [System.IO.Path]::GetFullPath($OutDir).TrimEnd('\', '/')
if ($SkipUpload -and
    ($PublishDir.Equals($officialPublishDir, [System.StringComparison]::OrdinalIgnoreCase) -or
     -not $PublishDir.StartsWith($diagnosticRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase) -or
     -not $OutDir.StartsWith($diagnosticRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase))) {
    throw "-SkipUpload is limited to isolated repo_ignored publish/output directories and cannot update the formal WinRemote executable."
}
$project = Join-Path $repo "8ax-win\src\8ax.WinRemote\8ax.WinRemote.csproj"
$publicKey = Join-Path $repo "8ax-win\src\8ax.WinRemote\Update\winremote-update-signing-public.pem"
$signTool = Join-Path $repo "8ax-win\tools\sign_winremote_update_manifest.py"
$packageName = "8ax-winremote-win-x64.zip"
$packagePath = Join-Path $OutDir $packageName
$manifestPath = Join-Path $OutDir "manifest.json"
$signaturePath = Join-Path $OutDir "manifest.sig"
$exeName = "8ax.WinRemote.exe"
$requiredFiles = @($exeName)
$forbiddenSidecars = @(
    "8ax.WinRemote.dll",
    "8ax.WinRemote.deps.json",
    "8ax.WinRemote.runtimeconfig.json",
    "8ax.WinRemote.pdb"
)

if ($ReleaseSequence -le 0) {
    throw "ReleaseSequence must be a positive monotonic integer."
}
if ($Channel -ne "stable") {
    throw "This WinRemote client release owner is fixed to channel=stable."
}
foreach ($required in @($SigningPrivateKey, $publicKey, $signTool)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "missing update signing input: $required"
    }
}

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
if ($LASTEXITCODE -ne 0) {
    throw "WinRemote single-file publish failed."
}

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
$archive = [System.IO.Compression.ZipFile]::OpenRead($packagePath)
try {
    $zipFileNames = @($archive.Entries | ForEach-Object { $_.FullName })
} finally {
    $archive.Dispose()
}
if ($zipFileNames.Count -ne 1 -or $zipFileNames[0] -ne $exeName) {
    throw "update package must contain only $exeName"
}

$hash = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash.ToLowerInvariant()
$size = (Get-Item -LiteralPath $packagePath).Length
$manifest = [ordered]@{
    schema = $manifestSchema
    app_id = "8ax.WinRemote"
    channel = $Channel
    version = $Version
    release_sequence = $ReleaseSequence
    key_id = $keyId
    file_name = $packageName
    package_url = $packageName
    size = $size
    sha256 = $hash
    published_at_utc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
}
$manifestJson = $manifest | ConvertTo-Json
$utf8NoBom = New-Object System.Text.UTF8Encoding($false, $true)
[System.IO.File]::WriteAllText($manifestPath, $manifestJson, $utf8NoBom)
Remove-Item -LiteralPath $signaturePath -Force -ErrorAction SilentlyContinue
& $Python $signTool sign --private-key $SigningPrivateKey --public-key $publicKey --manifest $manifestPath --signature $signaturePath
if ($LASTEXITCODE -ne 0) {
    throw "WinRemote manifest signing failed."
}
& $Python $signTool verify --public-key $publicKey --manifest $manifestPath --signature $signaturePath
if ($LASTEXITCODE -ne 0) {
    throw "WinRemote local manifest signature verification failed."
}

$localHashes = @{
    package = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash.ToLowerInvariant()
    manifest = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    signature = (Get-FileHash -LiteralPath $signaturePath -Algorithm SHA256).Hash.ToLowerInvariant()
}
$primaryBaseUrl = "$PrimaryDomain/8ax-winremote/win-x64"
$backupBaseUrl = "$BackupDomain/8ax-winremote/win-x64"

function Assert-ManifestMatchesRelease {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Context
    )
    $raw = [System.IO.File]::ReadAllBytes($Path)
    if ($raw.Length -ge 3 -and $raw[0] -eq 0xEF -and $raw[1] -eq 0xBB -and $raw[2] -eq 0xBF) {
        throw "$Context manifest contains a forbidden UTF-8 BOM"
    }
    $json = $utf8NoBom.GetString($raw)
    $parsed = $json | ConvertFrom-Json
    if ($parsed.schema -ne $manifestSchema -or $parsed.app_id -ne "8ax.WinRemote" -or
        $parsed.channel -ne $Channel -or $parsed.version -ne $Version -or
        [int64]$parsed.release_sequence -ne $ReleaseSequence -or $parsed.key_id -ne $keyId -or
        $parsed.file_name -ne $packageName -or $parsed.package_url -ne $packageName -or
        [int64]$parsed.size -ne [int64]$size -or $parsed.sha256 -ne $hash) {
        throw "$Context signed manifest fields do not match the release"
    }
}

function Assert-SignedReleaseSet {
    param(
        [Parameter(Mandatory=$true)][string]$Package,
        [Parameter(Mandatory=$true)][string]$Manifest,
        [Parameter(Mandatory=$true)][string]$Signature,
        [Parameter(Mandatory=$true)][string]$Context
    )
    Assert-ManifestMatchesRelease -Path $Manifest -Context $Context
    & $Python $signTool verify --public-key $publicKey --manifest $Manifest --signature $Signature
    if ($LASTEXITCODE -ne 0) {
        throw "$Context detached signature verification failed"
    }
    $actualPackage = (Get-FileHash -LiteralPath $Package -Algorithm SHA256).Hash.ToLowerInvariant()
    $actualManifest = (Get-FileHash -LiteralPath $Manifest -Algorithm SHA256).Hash.ToLowerInvariant()
    $actualSignature = (Get-FileHash -LiteralPath $Signature -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualPackage -ne $localHashes.package -or $actualManifest -ne $localHashes.manifest -or
        $actualSignature -ne $localHashes.signature) {
        throw "$Context package/manifest/signature raw bytes differ from the local signed release"
    }
}

function Download-SignedReleaseSet {
    param(
        [Parameter(Mandatory=$true)][string]$BaseUrl,
        [Parameter(Mandatory=$true)][string]$Destination,
        [Parameter(Mandatory=$true)][string]$Context
    )
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    $downloadedPackage = Join-Path $Destination $packageName
    $downloadedManifest = Join-Path $Destination "manifest.json"
    $downloadedSignature = Join-Path $Destination "manifest.sig"
    Invoke-WebRequest -UseBasicParsing -TimeoutSec 120 -Uri "$BaseUrl/$packageName" -OutFile $downloadedPackage
    Invoke-WebRequest -UseBasicParsing -TimeoutSec 30 -Uri "$BaseUrl/manifest.json" -OutFile $downloadedManifest
    Invoke-WebRequest -UseBasicParsing -TimeoutSec 30 -Uri "$BaseUrl/manifest.sig" -OutFile $downloadedSignature
    Assert-SignedReleaseSet -Package $downloadedPackage -Manifest $downloadedManifest -Signature $downloadedSignature -Context $Context
}

$uploadStatus = "pending"
if (-not $SkipUpload) {
    # Formal release path: package generation must be followed by direct VPS upload and verification.
    $remotePackagePath = "$RemoteDir/$packageName"
    $remoteManifestPath = "$RemoteDir/manifest.json"
    $remoteSignaturePath = "$RemoteDir/manifest.sig"
    ssh $Vps "mkdir -p '$RemoteDir'"
    scp $packagePath "${Vps}:$remotePackagePath"
    scp $manifestPath "${Vps}:$remoteManifestPath"
    scp $signaturePath "${Vps}:$remoteSignaturePath"

    foreach ($remotePair in @(
        @($remotePackagePath, $localHashes.package, "package"),
        @($remoteManifestPath, $localHashes.manifest, "manifest"),
        @($remoteSignaturePath, $localHashes.signature, "signature")
    )) {
        $remoteHash = (& ssh $Vps "sha256sum '$($remotePair[0])' | cut -d ' ' -f 1" | Select-Object -First 1).Trim().ToLowerInvariant()
        if ($remoteHash -ne $remotePair[1]) {
            throw "VPS $($remotePair[2]) SHA-256 mismatch: $remoteHash != $($remotePair[1])"
        }
    }

    $verifyRoot = Join-Path $OutDir ("verify-" + [Guid]::NewGuid().ToString("N"))
    try {
        $remoteReadback = Join-Path $verifyRoot "vps"
        New-Item -ItemType Directory -Force -Path $remoteReadback | Out-Null
        scp "${Vps}:$remotePackagePath" (Join-Path $remoteReadback $packageName)
        scp "${Vps}:$remoteManifestPath" (Join-Path $remoteReadback "manifest.json")
        scp "${Vps}:$remoteSignaturePath" (Join-Path $remoteReadback "manifest.sig")
        Assert-SignedReleaseSet `
            -Package (Join-Path $remoteReadback $packageName) `
            -Manifest (Join-Path $remoteReadback "manifest.json") `
            -Signature (Join-Path $remoteReadback "manifest.sig") `
            -Context "VPS readback"

        Download-SignedReleaseSet -BaseUrl $primaryBaseUrl -Destination (Join-Path $verifyRoot "primary") -Context "primary HTTPS"
        Download-SignedReleaseSet -BaseUrl $backupBaseUrl -Destination (Join-Path $verifyRoot "backup") -Context "backup HTTPS"
    } finally {
        Remove-Item -LiteralPath $verifyRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
    $uploadStatus = "uploaded and verified"
} else {
    Write-Warning "-SkipUpload is diagnostic only; generated packages are not a formal release until uploaded to VPS."
    $uploadStatus = "skipped (diagnostic only)"
}

Write-Host "Published WinRemote signed update:"
Write-Host "  primary: $primaryBaseUrl/manifest.json + manifest.sig"
Write-Host "  backup:  $backupBaseUrl/manifest.json + manifest.sig"
Write-Host "  exe:     $exeName"
Write-Host "  version: $Version"
Write-Host "  release_sequence: $ReleaseSequence"
Write-Host "  key_id:  $keyId"
Write-Host "  sha256:  $hash"
Write-Host "  package contains single executable only: $exeName"
Write-Host "  upload:  $uploadStatus"
