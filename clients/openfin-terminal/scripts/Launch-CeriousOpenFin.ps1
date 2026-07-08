param(
  [switch]$StartBackend,
  [int]$HealthWaitSeconds = 45
)

$ErrorActionPreference = 'Stop'

$clientRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repoRoot = (Resolve-Path (Join-Path $clientRoot '..\..')).Path
$gatewayHealth = 'http://127.0.0.1:8000/api/health'

if ($StartBackend) {
  $launcher = Join-Path $repoRoot 'Start-CeriousApp.ps1'
  Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -ArgumentList @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', $launcher,
    '-HostOnly'
  )
}

$deadline = (Get-Date).AddSeconds($HealthWaitSeconds)
do {
  try {
    $health = Invoke-RestMethod -Uri $gatewayHealth -TimeoutSec 2
    if ($health.ok) {
      Push-Location $clientRoot
      try {
        npm.cmd run launch:local
      } finally {
        Pop-Location
      }
      exit 0
    }
  } catch {
    Start-Sleep -Milliseconds 500
  }
} while ((Get-Date) -lt $deadline)

throw "Cerious gateway was not healthy at $gatewayHealth within $HealthWaitSeconds seconds."
