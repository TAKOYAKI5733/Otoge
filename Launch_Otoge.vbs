' Launch_Otoge.vbs
Dim fso, shell, scriptDir, gamePath, exePath

Set fso = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")

' このvbsファイル自身が置かれているフォルダを自動取得
scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)

' 実行場所を "bin" フォルダに固定
gamePath = scriptDir & "\bin"
exePath = gamePath & "\otoge.exe"

If fso.FileExists(exePath) Then
    shell.CurrentDirectory = gamePath
    shell.Run """" & exePath & """", 1, False
Else
    MsgBox "otoge.exe が見つかりません: " & exePath, 16, "起動エラー"
End If