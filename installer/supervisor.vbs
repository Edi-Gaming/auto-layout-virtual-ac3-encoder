' Virtual AC3 Encoder autostart launcher.
' One-shot hidden launch. engine.exe itself is persistent and owns runtime SURROUND/GUITAR switching.
Set fso = CreateObject("Scripting.FileSystemObject")
base = fso.GetParentFolderName(WScript.ScriptFullName)
appPath = base & "\engine.exe"
logFile = base & "\engine.log"
Set sh = CreateObject("WScript.Shell")
q = Chr(34)
sh.Run q & appPath & q & " --hidden --log " & q & logFile & q, 0, False
