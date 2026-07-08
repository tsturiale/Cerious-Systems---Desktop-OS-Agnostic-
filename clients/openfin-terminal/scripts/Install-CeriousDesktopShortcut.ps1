param(
  [switch]$StartMenuOnly,
  [switch]$DesktopOnly
)

$ErrorActionPreference = 'Stop'

$clientRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repoRoot = (Resolve-Path (Join-Path $clientRoot '..\..')).Path
$vbsLauncher = Join-Path $clientRoot 'bin\CeriousDesktop.vbs'
$iconPath = Join-Path $repoRoot 'cerious.ico'

if (-not (Test-Path -LiteralPath $vbsLauncher)) {
  throw "Missing launcher: $vbsLauncher"
}
if (-not (Test-Path -LiteralPath $iconPath)) {
  throw "Missing icon: $iconPath"
}

function New-CeriousShortcut {
  param(
    [Parameter(Mandatory=$true)][string]$ShortcutPath
  )

  $shell = New-Object -ComObject WScript.Shell
  $shortcut = $shell.CreateShortcut($ShortcutPath)
  $shortcut.TargetPath = 'wscript.exe'
  $shortcut.Arguments = '"' + $vbsLauncher + '"'
  $shortcut.WorkingDirectory = $repoRoot
  $shortcut.IconLocation = $iconPath
  $shortcut.Description = 'Cerious Desktop'
  $shortcut.Save()
}

if (-not $StartMenuOnly) {
  $desktopPath = [Environment]::GetFolderPath('Desktop')
  New-CeriousShortcut -ShortcutPath (Join-Path $desktopPath 'Cerious Desktop.lnk')
}

if (-not $DesktopOnly) {
  $programsPath = [Environment]::GetFolderPath('Programs')
  $folder = Join-Path $programsPath 'Cerious Systems'
  New-Item -ItemType Directory -Path $folder -Force | Out-Null
  New-CeriousShortcut -ShortcutPath (Join-Path $folder 'Cerious Desktop.lnk')
}

Write-Output 'Cerious Desktop shortcuts installed.'
