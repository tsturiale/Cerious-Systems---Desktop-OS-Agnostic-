Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
root = fso.GetParentFolderName(WScript.ScriptFullName)
script = root & "\Start-CeriousStartupService.ps1"
command = "powershell.exe -STA -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File " & Chr(34) & script & Chr(34)
shell.Run command, 0, False
