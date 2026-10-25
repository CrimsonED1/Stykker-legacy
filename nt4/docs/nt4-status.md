# NT4 SP6 VM – Dokumentation (korrigiert, Stand 2026-10-25, Netzwerk läuft)

Dieser Stand ersetzt die Fassung von 2026-10-24. Die Punkte wurden am 25.10. im Live-Test geprüft.

---

## 1. Die VM

| Parameter | Wert |
|---|---|
| Name | `NT4_SP6-New` |
| UUID | `4e2a0086-1bdd-4872-a4c0-fc649f7f64ee` |
| Gast-OS | Windows NT 4.0 Workstation, Build 1381, Service Pack 6 (also **kein** SP1 nötig) |
| RAM / CPU | 256 MB / 1 |
| Festplatte | `%USERPROFILE%\vms\winnt_sp6\Windows NT 4.0\Windows NT 4.0.vmdk` (0,75 GB) |
| Netzwerk | NIC 1: NAT, Typ `Am79C973` (AMD PCnet-FAST III). Im Gast: `AMDPCN1`, IP `10.0.2.15`, Gateway `10.0.2.2` (per DHCP) |
| DVD (IDE-1-0) | `S:\workspace\nt4\iso\en_winnt4_wks_sp1.iso` (NT4 Workstation Install-CD, im Gast als `D:`) |
| Login | Benutzer `Administrator`, leeres Passwort (laut `HANDOFF_NT4.md`) |

**Backup:** `%USERPROFILE%\vms\winnt_sp6\backup_vor_netzwerk\Windows NT 4.0.vmdk` (Stand vor der Netzwerk-Konfiguration am 25.10.).

**Achtung Snapshots:** Die alte VM `NT4_SP6` (ausgeschaltet) ist auf dieselbe Platte registriert. Deshalb schlägt `snapshot take` fehl. Vor riskanten Schritten die Platte kopieren, solange die VM aus ist.

**Achtung Hostname:** Nach DHCP heißt der Gast `winworld-nt4ws.tail85052b.ts.net`. Der Suffix kommt von der Host-Umgebung.

---

## 2. Korrekturen zur alten Doku

| Alt (falsch) | Korrigiert |
|---|---|
| Enter geht nicht (`keyboardputscancode 1c` oder `ret`) | Enter geht mit Drücken **und** Loslassen: `1c 9c` |
| Zeichen erscheinen als `^R ^U ^E` | Strg-Taste hängt. Lösung: `9d` (Strg loslassen) |
| „Netzwerk: Kein Treiber vorhanden“ | Der Treiber ist da. Er startete nicht, weil ein Registry-Parameter fehlte (Ereignis 5011). Nach Neuinstallation läuft er, siehe Abschnitt 5 |
| `keyboardputcad` für Strg+Alt+Entf | Gibt es in VBoxManage 7.2 nicht. Stattdessen Scancodes `1d 38 53 d3 b8 9d` |
| Herunterfahren / Neustart hängt | Ursache sind VMware-Tools-Reste (`VMwareTray.exe`, `VMwareService.e.exe`). Sie crashen unter VirtualBox und blockieren das Beenden (siehe Abschnitt 6) |
| `ncpa.cpl` öffnet Netzwerk | Gibt es unter NT4 nicht. Netzwerk geht über Systemsteuerung → `Network` |

Nicht geprüft und deshalb entfernt: Python auf dem Host, Shared Folders, FTP.

---

## 3. Bedienung der VM (getestet)

Alias für die Beispiele: `VBM` = `"C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"`. Name der VM: `NT4_SP6-New`.

### Status und Start/Stopp
- Laufende VMs: `VBM list runningvms`
- Starten (mit Fenster): `VBM startvm "NT4_SP6-New" --type gui`
- Hart ausschalten: `VBM controlvm "NT4_SP6-New" poweroff`
- Konfiguration anzeigen: `VBM showvminfo "NT4_SP6-New" --machinereadable`

### Bildschirm sehen
- Screenshot: `VBM controlvm "NT4_SP6-New" screenshotpng S:/workspace/name.png`
- Danach die PNG mit dem Read-Tool öffnen und ansehen.
- Der Gast reagiert langsam. Nach Tasteneingaben 2–10 s warten, bevor der Screenshot kommt.

### Tastatur (Scancodes, Drücken und Loslassen)
| Aktion | Befehl (Scancodes) |
|---|---|
| Text tippen | `keyboardputstring "text"` |
| Enter | `keyboardputscancode 1c 9c` |
| Esc | `keyboardputscancode 01 81` |
| Tab / Shift+Tab | `0f 8f` / `2a 0f 8f aa` |
| Strg+Tab | `1d 0f 8f 9d` |
| Strg+Shift+Tab | `1d 2a 0f 8f aa 9d` |
| Pfeil runter / hoch | `e0 50 e0 d0` / `e0 48 e0 c8` |
| Strg+Esc (Startmenü) | `1d 01 81 9d` |
| Alt+A (Add) / Alt+R (Remove) / Alt+P (Properties) | `38 1e 9e b8` / `38 13 93 b8` / `38 19 99 b8` |
| Alt+O (DHCP-Radio) | `38 18 98 b8` |
| Alt+F4 (Fenster schließen) | `38 3e be b8` |
| Taste `r` (Run-Dialog aus Startmenü) | `13 93` |
| Strg+Alt+Entf | `1d 38 53 d3 b8 9d` |
| Strg loslassen (wenn Zeichen als ^X erscheinen) | `9d` |

### Navigation in der Oberfläche
- Startmenü: Strg+Esc. Der Eintrag „Shut Down...“ ist der letzte, also Pfeil hoch aus dem leeren Menü, dann Enter.
- Herunterfahren-Dialog: Pfeil runter wählt „Restart the computer?“, dann Enter.
- Run-Dialog: Strg+Esc, dann `r`, dann Befehl tippen und Enter. Nützliche Befehle: `cmd`, `control`, `eventvwr`.
- `ncpa.cpl` als Run-Befehl liefert eine Fehlermeldung. Netzwerk nur über `control` → `n` (springt zu `Network`).
- Control Panel: Buchstabe tippen springt zum Eintrag, z.B. `n` für `Network`.
- Dialoge: Tab / Shift+Tab bewegt den Fokus, Leertaste drückt den fokussierten Button, Esc bricht ab. Enter wirkt nur gut, wenn der Fokus auf einem Button liegt. Auf Listen oder Auswahlfeldern macht Enter etwas anderes.
- Alt+Buchstabe funktioniert nur bei Buttons mit Unterstrich-Buchstabe. Getestet: Alt+T öffnet „Display Type...“ im Settings-Tab. Alt+Y und Alt+G hatten keine Wirkung.
- Alt allein (Alt-Tipper) aktiviert den Menümodus. Danach öffnet Enter das Systemmenü des Fensters (Move, Close). Mit Esc schließen.
- Es gibt keine Mausbefehle. Alles läuft über die Tastatur.

### Laufwerk wechseln (DVD)
- IDE unterstützt kein Hot-Plug. Die VM muss also aus sein:
  `VBM storageattach "NT4_SP6-New" --storagectl IDE --port 1 --device 0 --type dvddrive --medium <ISO-Pfad oder emptydrive>`
- Der Befehl funktioniert nur bei ausgeschalteter VM.
- Erst `emptydrive` anhängen, dann das ISO setzen (ohne das leere Laufwerk schlägt das Setzen des ISO fehl).

---

## 4. Gast-Zugang
1. Strg+Alt+Entf senden (Scancodes, siehe oben).
2. Benutzername `Administrator` steht vor, Passwortfeld leer. Enter.
3. Dr.-Watson-Dialoge zu `VMwareService.e.exe` erscheinen nach dem Start. Mit Tab + Leertaste oder Enter schließen.
4. Dialog „At least one service or driver failed during startup“ erscheint nach jedem Start. Mit Enter schließen.

---

## 5. Netzwerk (funktioniert seit 25.10.)

### Ausgangslage
- Adapter war eingetragen, startete aber nicht.
- Ereignisanzeige (System-Log), Quelle `AMDPCN`, Ereignis-ID 5011: `AMDPCN1: A required parameter is missing from the Registry.`
- Dienststeuerung: Fehler 7000 für AMDPCN.

### Was geholfen hat
1. Netzwerk öffnen: Control Panel (`control` im Run-Dialog) → Taste `n` → Enter.
2. Tab zu Adapters: Ctrl+Tab dreimal.
3. Adapter `[1] AMD PCNET PCI Ethernet Adapter` auswählen, Alt+R (Remove). Warnung mit Enter bestätigen.
4. Adapter-Liste leer. Alt+A (Add). Type-ahead: `AMD` tippen, Enter. Eintrag `AMD PCNET Family Ethernet Adapter` ist ausgewählt.
5. Setup fragt nach der Quelle. `D:\I386` eintragen, Tab zu Continue, Enter.
6. Treiberdialog `AMD PCNET PCI Ethernet Adapter v3.11`: Standardwerte belassen, Tab zu OK, Enter.
7. Adapter ist wieder eingetragen. Netzwerk-Dialog mit Alt+C schließen, bzw. Tab zu Close und Enter. Beim Schließen erscheint der Treiberdialog nochmal, dann wieder bestätigen.
8. TCP/IP-Dialog: Alt+O für „Obtain an IP address from a DHCP server“, bestätigen (Enter), Frage „Enable DHCP?“ mit Enter.
9. „Network Settings Change“: Neustart mit Enter. Danach dauert der Boot etwa 2 Minuten.

### Ergebnis
- `ipconfig`: `AMDPCN1`, IP `10.0.2.15`, Maske `255.255.255.0`, Gateway `10.0.2.2`.
- `ping 10.0.2.2` antwortet mit Reply.
- Nicht getestet: Zugriff auf den Stykker-Proxy (Port 17500) von NT4 aus.

---

## 6. Bekannte Störungen
- **VMware-Tools-Reste:** `VMwareTray.exe` und `VMwareService.e.exe` sind im Gast installiert. Sie stürzen unter VirtualBox ab und blockieren das Herunterfahren. Der Dialog „VMwareTray.exe cannot respond“ muss mit `End Task` (Tab, dann Enter) beendet werden. Das ist noch nicht dauerhaft gelöst.
- **Startdialog „service or driver failed“:** Erscheint nach jedem Start. Wahrscheinlich ein VMware-Dienst. Noch nicht untersucht (Ereignisanzeige, Quelle `Service Control Manager`).
- **Dr. Watson:** Dialoge bei jedem Absturz des VMware-Dienstes. Log-Verzeichnis fehlt (Fehler 80). Stört, blockiert aber nicht dauerhaft.
- **Langsame Eingabe:** Der Gast braucht nach Eingaben Zeit. Wartezeiten einplanen.

---

## 7. Dateien aus diesem Testlauf (`S:\workspace\`)
- `nt4_*.png` sind Screenshots. Können nach Durchsicht gelöscht werden.
- Backup-VMDK: siehe Abschnitt 1.

---

## 8. Dateitransfer Host → NT4 (getestet 25.10.)

Weg: Ordner auf dem Host → ISO bauen → ISO als DVD in die laufende VM einhängen. Es braucht keine Netzfreigabe, keinen FTP-Server und keine Gast-Erweiterungen.

1. Dateien in `S:\workspace\nt4\apps\` legen. Namen im 8.3-Format (z. B. `TEST.EXE`), damit NT4 sie sicher liest.
2. ISO bauen:
   ```
   & 'S:\workspace\nt4\tools\make-iso.ps1' -SourceDir 'S:\workspace\nt4\apps' -IsoPath 'S:\workspace\nt4\apps.iso'
   ```
3. ISO in die laufende VM einhängen (kein Neustart nötig, der bestehende DVD-Eintrag wird getauscht):
   ```
   & 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe' storageattach "NT4_SP6-New" --storagectl IDE --port 1 --device 0 --type dvddrive --medium 'S:\workspace\nt4\apps.iso'
   ```
4. Im Gast im cmd: `dir D:\`. Die Dateien liegen im Wurzelverzeichnis von `D:`.

**Hinweis:** Die SP1-Installations-CD ist seit 25.10. nicht mehr eingehängt. Sie liegt weiter unter `S:\workspace\nt4\iso\en_winnt4_wks_sp1.iso`. Für weitere Treiberinstallationen wieder mit dem Befehl aus Schritt 3 einhängen.

**Gotcha:** VirtualBox sperrt eine eingehängte ISO. Deshalb `make-iso.ps1` nicht auf dieselbe Datei laufen lassen, sondern vorher den DVD-Eintrag auf `emptydrive` setzen. Alternativ jedes Mal einen neuen Dateinamen verwenden (`apps2.iso`, `apps3.iso`, …).

---

## 9. C++-Build-Stand (Stand 25.10. abends)

**Toolchain**
- VS 2022 Build Tools mit der Komponente „C++ Clang tools for Windows“ (clang-cl 19, Toolset `ClangCL`). Die Komponente wurde im zweiten Anlauf per UAC installiert.
- MSVC 14.44, Windows SDK 10.0.26100.

**Projekt** `S:\workspace\nt4\src\nt4-exe-test` (Beispiel aus dem Plan)
- Submodul `EnlyzeWinCompatLib` steht nicht auf dem Superprojekt-Pin `f13afca`, sondern auf Branch `clang-vs2022-fls-initcriticalsectionex` (Commit `994a24a`). Grund: Der Pin hat keinen Shim für `FlsAlloc`, `InitializeCriticalSectionEx` fehlt ebenfalls. Der Branch bringt `fls.cpp` und `winnt_52.cpp` mit.
- Submodul `libcxx` auf `5553077` (libc++ 17.0.6), `winpthreads` auf `65a44cf`.
- **Lokale Änderungen, nicht committet:**
  - `EnlyzeWinCompatLib\src\EnlyzeWinCompatLib.vcxproj`: `-Wno-cast-function-type-mismatch` in beiden `AdditionalOptions` (Clang 19 meldet die Casts in `winnt_40.cpp` als Fehler).
  - `nt4-exe-test.vcxproj`: `-march=pentium-mmx` in beiden `ClCompile`-Blöcken.

**Build**
- `MSBuild nt4-exe-test.sln /p:Configuration=Release /p:Platform=x86 /p:VCToolsVersion=14.38.33130` (MSBuild aus den Build Tools). Die `.sln` kennt nur `x86`, nicht `Win32`. Die Version 14.38 ist nötig, siehe Test unten.
- Ergebnis: `S:\workspace\nt4\src\nt4-exe-test\Release\nt4-exe-test.exe`.
- Bibliotheken: `S:\workspace\nt4\src\build\Release\` (laut `.vcxproj` über `$(SolutionDir)..\build`).

**Prüfung mit dumpbin**
- OS-Version 4.00, Subsystem 4.00.
- Importe nur `KERNEL32.dll` und `USER32.dll`. Keine `api-ms-win`-DLLs, keine UCRT.
- `FlsAlloc` und `InitializeCriticalSectionEx` werden nicht mehr direkt importiert.

**Test in der VM (`NT4_SP6-New`) mit MSVC 14.44 (vorher, fehlgeschlagen)**
- Start ohne Importfehler.
- Absturz: `illegal instruction (0xc000001d)` bei `00408EBD: xorps xmm0,xmm0`.
- Die Stelle liegt in `__scrt_common_main_seh`, also im Start-Code der statisch gelinkten MSVC-CRT 14.44. Diese CRT nutzt SSE. NT4 aktiviert SSE nicht, deshalb schlägt die Anweisung fehl.
- `-march=pentium-mmx` in den eigenen Projekten hilft gegen diesen Fehler nicht, weil die CRT-Objekte vorkompiliert sind.

**Offen**
**Lösung und Erfolg (25.10. nachts)**
- MSVC 14.38.33130 als Komponente `Microsoft.VisualStudio.Component.VC.14.38.17.8.x86.x64` in die Build Tools installiert (elevated, UAC).
- Neu gebaut mit `/p:VCToolsVersion=14.38.33130`, alle Bibliotheken neu.
- Der Start-Code `__scrt_common_main_seh` enthält keine SSE-Befehle mehr. Der Absturz ist weg.
- **Test in NT4 erfolgreich:** `D:\NT4TEST.EXE` startet, zeigt das Meldungsfenster „Hello from 2024!“, lässt sich schließen und kehrt zum Prompt zurück.
- Die Funktionen für Mathematik (`log10`, `ceil`) enthalten weiterhin SSE. Sie sind nur in der Laufzeit, aber nicht im Start. Beim Aufruf könnte ein Absturz kommen. Das ist noch nicht getestet.
- ISO-Stand: `S:\workspace\nt4\apps6.iso` (eingehängt, enthält `NT4WIN.EXE`, siehe Abschnitt 10).
- Das Patch-Skript `patch_exe_os_version.ps1` aus dem ENLYZE-Branch setzt nur die Versionsfelder, die bei uns schon stimmen. Es ist für den SSE-Fehler nicht relevant.
- `Dependencies` (lucasg.Dependencies) ist installiert und freigegeben, aber noch nicht gegen die EXE gelaufen. Bisher wurde `dumpbin` genutzt.

---

## 10. Fenster-App (Stykker-Stil) und Anzeige (Stand 25.10. nachts)

**Fenster-App**
- Quelle: `S:\workspace\nt4\src\nt4-window\main.cpp`, Projekt `nt4-window.vcxproj` (nur Release|Win32, Toolset ClangCL, gleiche ENLYZE-Bibliotheken wie Abschnitt 9).
- Ergebnis: `S:\workspace\nt4\src\nt4-window\Release\nt4-window.exe`. Kopie für NT4: `S:\workspace\nt4\apps\NT4WIN.EXE`, im ISO `apps6.iso`.
- Im Gast im cmd starten: `D:\NT4WIN.EXE`.
- Bedienung: `R` oder Enter aktualisiert (Zeile „Zuletzt aktualisiert: hh:mm:ss“). Esc beendet die App, Alt+F4 geht auch.
- Tab und Leertaste lösen die gezeichneten Buttons nicht aus. Deshalb nur R, Enter und Esc.
- Schrift Arial (Segoe UI gibt es auf NT4 nicht). Farben aus den Stykker-Tokens (`tokens.css`, dunkel: Hintergrund `#060916`, Akzent `#4fe3ff`).

**Farbtiefe und Grafik**
- Aktuell: 16 Farben, 640 × 480. Treiber: Standard-VGA (`vga.sys`, `vga.dll`, Microsoft). VM-Grafikcontroller `vboxvga`, 32 MB VRAM.
- Die Punktierung kommt von den 16 Farben. GDI ersetzt Farben, die nicht in der Palette sind, durch Muster (Dithering). Am Code liegt es nicht.
- Versuch am 25.10. (Tastenweg: Strg+Tab viermal zum Reiter „Settings“, Alt+T für „Display Type...“, Shift+Tab bis „Change...“, Leertaste):
  - Die Herstellerliste hat unter „(Standard display types)“ nur „VGA compatible display adapter“.
  - Zwischen „Micronics“ und „Miro“ gibt es keinen Microsoft-Eintrag. Ein Super-VGA-Treiber war dort nicht zu finden. Der Rest der Liste nach „Miro“ ist nicht geprüft.
  - Die Auswahl mit Leertaste hat erneut den VGA-Treiber installiert. Keine Änderung. NT4 verlangte danach einen Neustart. Ich habe „No“ gewählt. Die VM läuft unverändert weiter.
- Nicht gemacht: Cirrus, S3 oder andere Chip-Treiber. VirtualBox bildet diese Chips nicht nach. Ein falscher Treiber kann den Start verhindern. Das Backup `backup_vor_netzwerk` ist älter als das Netzwerk und würde es zurücksetzen.
- Versuch 2 (25.10., Treiber „Cirrus Logic 544x Compatible Graphics Adapter“ von der CD): Der Treiber wurde installiert, NT4 hat neu gestartet und bootet bis zum Anmeldebildschirm. Danach meldet NT4 „Invalid Display Settings“. Die Anzeige zeigt weiterhin 16 Farben und 640 × 480. Vermutlich greift der Cirrus-Treiber auf `vboxvga` nicht. Der Treiber bleibt eingetragen, die Rückstellung auf VGA steht noch aus.
- Recherche (25.10.): Die Quellen empfehlen einen VBE-Treiber und keinen Cirrus- oder S3-Treiber. VBEMP ist ein VBE-Treiber für NT 3.x bis NT 4, der VirtualBox als Plattform nennt. Er ist ein Drittanbieter-Treiber von Fremdseiten und nicht heruntergeladen (Quellen: https://www.vogons.org/viewtopic.php?p=120421, https://huggingface.co/spaces/peterpeter8585/v86/blob/main/docs/windows-nt.md). Im WinWorld-Forum scheiterte VBE9X mit NT 4 SP6 in VirtualBox, dort war 128 MB VRAM als Problem genannt (https://forum.winworldpc.com/discussion/comment/146342). Die Miniport-Version aus dem OS/2 Museum nennt NT 3.1 bis Windows 7, ob NT 4 dabei ist, ist unklar (https://www.os2museum.com/wp/?p=869).
- Offen: Ob die VBox-Grafik (`vboxvga`) mehr Modi anbietet, wenn man sie anders konfiguriert. Bis dahin gilt: 16 Farben, 640 × 480.
