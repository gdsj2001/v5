function Write-TextFileWithRetry {
  param(
    [string]$Path,
    [string]$Text,
    [int]$Attempts = 25,
    [int]$DelayMs = 100
  )

  $dir = Split-Path -Parent $Path
  if (-not (Test-Path -LiteralPath $dir)) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
  }

  $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
  for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
    try {
      [System.IO.File]::WriteAllText($Path, $Text, $utf8NoBom)
      return
    } catch [System.IO.IOException] {
      if ($attempt -eq $Attempts) {
        throw
      }
      Start-Sleep -Milliseconds $DelayMs
    } catch [System.UnauthorizedAccessException] {
      if ($attempt -eq $Attempts) {
        throw
      }
      Start-Sleep -Milliseconds $DelayMs
    }
  }
}
