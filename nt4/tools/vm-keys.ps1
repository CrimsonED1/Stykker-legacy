# Sends keyboard input to the NT 4 VM and optionally takes a screenshot.
# Steps: "k:<scancodes>" (e.g. "k:1d 01 81 9d" for Ctrl+Esc), "t:<text>" typed string, "w:<ms>" wait.
# Example: .\vm-keys.ps1 -Steps "k:1d 01 81 9d","w:600","k:13 93","t:cmd","k:1c 9c" -Shot run1
param(
    [string[]]$Steps,
    [string]$Shot = "",
    [string]$Vm = "NT4_SP6-New",
    [string]$OutDir = "S:\workspace\nt4\shots"
)

$vbm = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
foreach ($s in $Steps) {
    $kind = $s.Substring(0, 2); $val = $s.Substring(2)
    switch ($kind) {
        "k:" { & $vbm controlvm $Vm keyboardputscancode ($val -split ' ') | Out-Null; Start-Sleep -Milliseconds 400 }
        "t:" { & $vbm controlvm $Vm keyboardputstring $val | Out-Null; Start-Sleep -Milliseconds 400 }
        "w:" { Start-Sleep -Milliseconds ([int]$val) }
    }
}
if ($Shot) {
    New-Item -ItemType Directory -Force $OutDir | Out-Null
    Start-Sleep -Milliseconds 800
    & $vbm controlvm $Vm screenshotpng (Join-Path $OutDir "$Shot.png") | Out-Null
    Join-Path $OutDir "$Shot.png"
}
