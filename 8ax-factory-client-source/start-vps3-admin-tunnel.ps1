$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$keyPath = Join-Path $projectRoot "vpn\dmit--it.cjwsjzyy.xyz-id_rsa\id_rsa.pem"
$sshHost = "root@154.21.81.167"
$localPort = 18081
$remoteTarget = "127.0.0.1:18081"

$existing = Get-NetTCPConnection -LocalAddress 127.0.0.1 -LocalPort $localPort -State Listen -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host "factory admin SSH tunnel is already listening on http://127.0.0.1:$localPort"
    exit 0
}

if (-not (Test-Path -LiteralPath $keyPath)) {
    throw "SSH private key not found: $keyPath"
}

$args = @(
    "-i", $keyPath,
    "-N",
    "-L", "${localPort}:$remoteTarget",
    "-o", "ExitOnForwardFailure=yes",
    "-o", "ServerAliveInterval=30",
    "-o", "ServerAliveCountMax=3",
    $sshHost
)

Start-Process -FilePath "ssh.exe" -ArgumentList $args -WindowStyle Hidden
Start-Sleep -Seconds 2

$started = Get-NetTCPConnection -LocalAddress 127.0.0.1 -LocalPort $localPort -State Listen -ErrorAction SilentlyContinue
if ($started) {
    Write-Host "factory admin SSH tunnel started: http://127.0.0.1:$localPort"
} else {
    throw "SSH tunnel failed. Run manually: ssh -i `"$keyPath`" -N -L ${localPort}:$remoteTarget $sshHost"
}
