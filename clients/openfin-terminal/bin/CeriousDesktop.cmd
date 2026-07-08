@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
set "CLIENT_ROOT=%SCRIPT_DIR%.."
powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%CLIENT_ROOT%\scripts\Launch-CeriousOpenFin.ps1" -StartBackend
endlocal
