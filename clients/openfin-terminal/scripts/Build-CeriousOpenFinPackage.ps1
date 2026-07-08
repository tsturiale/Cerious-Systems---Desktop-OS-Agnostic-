param(
  [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'

$clientRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repoRoot = (Resolve-Path (Join-Path $clientRoot '..\..')).Path

if (-not $OutputDirectory) {
  $OutputDirectory = Join-Path $repoRoot 'release'
}

$packageRoot = Join-Path $OutputDirectory 'CeriousOpenFinDesktop'
$zipPath = Join-Path $OutputDirectory 'CeriousOpenFinDesktop-local.zip'

if (Test-Path -LiteralPath $packageRoot) {
  Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null

$items = @(
  'README.md',
  'package.json',
  'package-lock.json',
  'manifests',
  'scripts',
  'bin',
  'installer'
)

foreach ($item in $items) {
  $source = Join-Path $clientRoot $item
  if (Test-Path -LiteralPath $source) {
    Copy-Item -LiteralPath $source -Destination $packageRoot -Recurse -Force
  }
}

Copy-Item -LiteralPath (Join-Path $repoRoot 'cerious.ico') -Destination (Join-Path $packageRoot 'cerious.ico') -Force

@'
# Cerious Desktop Local Package

This package launches the Cerious terminal in OpenFin/HERE against a local
Cerious gateway.

1. Make sure the Cerious backend repository is present on this machine.
2. Run:

   npm.cmd install
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\Install-CeriousDesktopShortcut.ps1

3. Launch "Cerious Desktop" from the desktop or Start Menu.

Trading authority remains in the C++ backend services.
'@ | Set-Content -LiteralPath (Join-Path $packageRoot 'INSTALL.md') -Encoding UTF8

if (Test-Path -LiteralPath $zipPath) {
  Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath $zipPath -Force

if (-not (Test-Path -LiteralPath $zipPath)) {
  throw "Package zip was not created: $zipPath"
}

Write-Output "Built $zipPath"
