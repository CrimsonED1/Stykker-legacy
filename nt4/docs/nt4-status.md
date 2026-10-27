# NT4 SP6 VM – Dokumentation (korrigiert, Stand 2026-10-27, Netzwerk, 16 Bit, Opera, litehtml)

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
- `S:\workspace\nt4\apps*.iso` (`apps.iso` … `apps9.iso`): Transfer-ISOs, die Ordner `apps*` sind ihre Quellen. `apps7.iso` = VBEMP-Treiber, `apps8.iso` = erste `NT4LITE.EXE` (mit SSE, stürzt ab), `apps9.iso` = funktionierende `NT4LITE.EXE` (derzeit eingehängt). Die älteren können weg.
- `S:\workspace\nt4\src\nt4-blazor`: Blazor-Testapp (Abschnitt 11 und 12). `S:\workspace\nt4\src\nt4-lite`: litehtml-App. `S:\workspace\nt4\src\litehtml`: litehtml-Quelltext (Abschnitt 12).
- `S:\workspace\nt4\tools\vm-keys.ps1`: schickt Tasten (Scancodes, Text, Wartezeit) an die VM und macht auf Wunsch einen Screenshot nach `S:\workspace\nt4\shots`. Beispiel: `.\vm-keys.ps1 -Steps "k:1d 01 81 9d","w:600","k:13 93","t:cmd","k:1c 9c" -Shot run1` (Ctrl+Esc, R, `cmd`, Enter).

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
5. Kopieren mit `copy D:\DATEI.EXE C:\`. Das NT4-`copy` kennt kein `/y`. Existiert die Zieldatei, fragt es nach, deshalb besser einen neuen Zielnamen nehmen.

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
- Die Funktionen für Mathematik (`log10`, `ceil`) enthalten weiterhin SSE. Sie sind nur in der Laufzeit, aber nicht im Start. **Update 27.10.:** Bestätigt für die UCRT-Gleitkommafunktionen (printf `%f`, strtod, Runden, `pow`, fenv). Gelöst mit `nt4_crt.cpp`, siehe Abschnitt 12. `log10` selbst wählt seinen Weg per CPU-Prüfung und ist nicht einzeln getestet.
- ISO-Stand: `S:\workspace\nt4\apps6.iso` enthält `NT4WIN.EXE` (Abschnitt 10). Seit 27.10. ist `apps9.iso` eingehängt (`NT4LITE.EXE`, Abschnitt 12).
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
- Modus-Check nach Versuch 2 (Cirrus 544x, nach Neustart aktiv): „List All Modes“ zeigt nur 640 × 480 und 800 × 600, jeweils mit 16 Farben. Die Palette bietet nur „16 Colors“.
- Versuch 3 (25.10., generischer Treiber „S3 Compatible Graphics Adapter“ von der CD): Die Installation meldet Erfolg, nach dem Neustart erscheint „A new graphics driver has been installed“. „List All Modes“ zeigt wieder nur 640 × 480 und 800 × 600 mit 16 Farben.
- Stand nach Versuch 3: Beide Chip-Treiber (Cirrus, S3) sind eingetragen. VGA ist noch nicht wiederhergestellt. Beim Start erscheint „Invalid Display Settings“, dazu Dr.-Watson- und SCM-Meldungen (VMware-Reste).
- Ergebnis: Die Chip-Treiber von der NT4-CD bringen auf `vboxvga` keine höhere Farbtiefe. Nächster Kandidat ist ein VBE-Treiber (VBEMP, Drittanbieter). Er ist nicht heruntergeladen und braucht eine Zustimmung zur Quelle.
- **Versuch 4 (25.10., VBE-Treiber, Erfolg):** „AnaPa Corp VBE Miniport“ aus `vbempg.zip` (VBEMP, STABLE, Stand 19.10.2007).
  - Quelle: Archiv-Kopie `https://web.archive.org/web/20170922115520/http://bearwindows.boot-land.net/vbempg.zip`. Die Originaldomain `bearwindows.boot-land.net` löst nicht mehr auf.
  - Größe 126.243 Byte, SHA-256 `675a47c1f1eefb1c1d760e037844cd0d9e052d4afa5085403c32e302849328a0`.
  - Verwendet: `VBE30\NT4\vbemp.sys` und `vbemp4.inf`. Im Display Type mit „Change… → Have Disk…“ von `D:\VBE30\NT4` (ISO `S:\workspace\nt4\apps7.iso`, gebaut aus `S:\workspace\nt4\vbemp-iso`). Die NT-Warnung „third-party driver“ mit Yes bestätigt, danach Neustart.
  - Ergebnis: Die Farbpalette zeigt „256 Colors“, die Farbleiste zeigt einen echten Verlauf. Das Startlogo ist ohne Muster.
  - „List All Modes“ meldet 640 × 480, 800 × 600, 1024 × 768 und 1152 × 864, jeweils mit 256 Farben. Die letzten Zeilen habe ich nicht vollständig gelesen.
  - Beim Start weiterhin SCM-Meldung „service or driver failed“ und Dr.-Watson-Fehler (VMware-Reste).
  - Gesetzt: 800 × 600 bei 256 Farben. Zuerst über „Test“ bestätigt („Did you see the test bitmap properly?“ mit Yes), danach mit OK gespeichert. Danach meldet VBox den Modus `800,600,8`. Ein Neustart mit dieser Einstellung ist noch nicht geprüft.
- **16 Bit (27.10.):** „List All Modes“ zeigt bei VBEMP auch 65536 Farben (640 × 480 bis 1152 × 864). Gesetzt: 800 × 600 bei 65536 Farben. Der Test zeigt saubere Verläufe ohne Muster. Mit Yes bestätigt, mit OK gespeichert. Ein Neustart ist nicht nötig. VBox meldet `800,600,16`.

---

## 11. Browser und Blazor (27.10.)

**Browser:** Auf dem NT4 ist bereits Opera 10.63 installiert (Build 3516, Presto 2.6.30, `C:\Program Files\Opera\opera.exe`). Er ist jetzt Standardbrowser. Daneben gibt es nur den IE aus `Plus!\Microsoft Internet` (sehr alt). Opera 10.63 ist ungefähr das Neueste, was auf NT4 läuft: JavaScript ES3/ES5 teilweise, XMLHttpRequest, CSS 2.1. Es gibt kein WebSocket, kein Promise, keine Arrow-Functions und kein aktuelles TLS.

**Netzwerk zum Host:** crimsrv ist der Host der VM.
- `10.0.2.2` (NAT-Gateway) antwortet auf Ping, HTTP darauf geht aber nicht. Seit VirtualBox 7 ist der Loopback des Hosts für NAT-Gäste gesperrt (`localhostreachable` ist aus). Ein Server auf `127.0.0.1` ist deshalb aus der VM nicht erreichbar.
- Was geht: Der Server lauscht auf der LAN-IP von crimsrv, `192.168.178.146`. Opera öffnet `http://192.168.178.146:5080/`. Die Windows-Firewall musste dafür nicht geändert werden.
- Nur HTTP, kein HTTPS. Opera 10.63 spricht kein aktuelles TLS.
- In Opera hat F8 (Adresszeile) nach einer Fehlerseite nicht mehr reagiert. Sicherer Weg: Run-Dialog mit `c:\progra~1\opera\opera.exe <URL>`.

**Testapp:** `S:\workspace\nt4\src\nt4-blazor` (.NET 10, `dotnet new blazor --interactivity Server --empty`). Start:
`dotnet run -c Release --no-launch-profile --urls http://192.168.178.146:5080`

| Test | Ergebnis in Opera 10.63 |
|---|---|
| Seite rendern (Static SSR, Stykker-Farben, CSS 2.1) | geht |
| Formular-POST ohne JS (`@formname`, `AntiforgeryToken`) | geht, Zähler zählt hoch |
| Eigenes JS (ES3): Uhr mit `setInterval` | geht |
| Eigenes JS: `XMLHttpRequest` auf `/api/zeit` | geht |
| Interaktives Blazor (`@rendermode InteractiveServer`, SignalR) | geht nicht. `blazor.web.js` braucht modernes JS. Status bleibt „nicht verbunden“, der Reconnect-Dialog erscheint als Text auf der Seite (kein `<dialog>`) |

**Folgerung:** Blazor für NT4 so bauen: Seiten als Static SSR rendern, Eingaben über Formular-POST, kleine Interaktion mit eigenem ES3-JS und XHR auf Minimal-API-Endpunkte. `blazor.web.js` und `ReconnectModal` sollten auf diesen Seiten wegbleiben. Interaktives Blazor Server ginge höchstens mit einem nach ES5 übersetzten Client samt Polyfills und Long Polling. Das ist ungetestet und aufwendig.

---

## 12. litehtml als eingebauter Browser (27.10.)

**Ziel:** HTML vom Blazor-Server in einer eigenen C++-App auf NT4 anzeigen, ohne JavaScript und ohne WebAssembly. Der Server steuert Fortschrittsbalken und Timer.

**Bausteine:**
- litehtml (BSD-Lizenz, `github.com/litehtml/litehtml`, Commit `7ea2956`, geklont nach `S:\workspace\nt4\src\litehtml`). Das ist ein HTML/CSS-Renderer in C++17 mit eigenem HTML-Parser (gumbo). Er hat kein JavaScript.
- `S:\workspace\nt4\src\nt4-lite\litehtml-nt4.vcxproj` baut litehtml als statische Bibliothek mit der NT4-Toolchain (ClangCL, MSVC 14.38, libc++ aus EnlyzeWinCompatLib, `-march=pentium-mmx`). Ausgabe: `build\Release\litehtml\bin\litehtml.lib`. Der Build lief ohne Änderungen am litehtml-Code durch.
- `S:\workspace\nt4\src\nt4-lite\main.cpp`: Fenster-App `NT4LITE.EXE`. Sie enthält eine eigene GDI-Zeichenschicht für litehtml (Schrift, Text, Flächen, Rahmen, einfache lineare Verläufe in 2-px-Streifen; ohne Bilder und ohne runde Ecken). HTTP/1.0 läuft über Winsock (`ws2_32`), mit Weiterleitungen und nur `http://`.
- Bedienung: Links per Maus. Per Tastatur löst ein Buchstabe den Link mit passendem `accesskey` aus. F5 lädt neu, Pfeiltasten und Bild auf/ab scrollen, Esc beendet. Start-URL ist `http://192.168.178.146:5080/lite` oder das erste Kommandozeilenargument.
- Aktualisierung: Die App wertet `<meta http-equiv="refresh" content="N;url=...">` und den HTTP-Header `Refresh` aus. Damit bestimmt der Server, wie oft der Client nachfragt.

**Serverseite (Blazor-App `nt4-blazor`):**
- `LiteTimer` (Singleton, 20 s): Start, Pause, Reset, Prozent.
- `GET /lite` rendert die Komponente `Components/Lite/LitePage.razor` als ganze Seite (`RazorComponentResult`, Static SSR, ohne `blazor.web.js`). `GET /lite?cmd=start|stop|reset` führt den Befehl aus und leitet auf `/lite` zurück.
- Die Seite setzt den Refresh: jede Sekunde, solange der Timer läuft, sonst alle 3 s.

**Problem: SSE in der UCRT.** Der erste Start brach mit „illegal instruction“ in `__acrt_fenv_get_control` ab. Die statische UCRT aus Windows SDK 10.0.26100 ist für SSE2 gebaut. printf mit Kommazahlen, strtod/atof, Runden (roundf, nearbyint, floor/ceil), `pow` und die fenv-Funktionen nutzen SSE ohne CPU-Prüfung. NT4 schaltet SSE nie frei (CR4.OSFXSR), deshalb ist jeder SSE-Befehl dort ungültig. litehtml braucht diese Funktionen (CSS-Zahlen, `std::to_string(float)` für Schrift-Schlüssel).
- Lösung: `S:\workspace\nt4\src\nt4-lite\nt4_crt.cpp` ersetzt diese Einstiegspunkte durch eigene x87-Versionen: `__stdio_common_vsprintf` (+ `_s`, `vsnprintf_s`, `vswprintf`, `vfprintf`, `vfwprintf`), `strtod`/`strtof`/`atof`/`wcstod` und Verwandte, `floor`, `ceil`, `round`, `roundf`, `trunc`, `rint`, `nearbyint`, `lround`, `lrint`, `pow`, `fegetround`, `fesetround`. Die Datei wird mit `-fno-builtin` gebaut. Der Linker nimmt diese Definitionen vor den UCRT-Objekten, Konflikte gab es keine.
- Prüfung nach dem Build: Disassemblieren mit `llvm-objdump` und Zuordnen der Funktionen mit `llvm-symbolizer` (beide in VS Build Tools `VC\Tools\Llvm\bin`). Übrig bleiben SSE-Befehle in `memmove`, `memset`, `wcslen`, `strnlen` und `tan`. Diese wählen ihren SSE-Pfad nur nach einer CPU-Prüfung. Dazu kommen Fehlerpfade der x87-Mathematik (`_handle_exc`, `_87except`, `_decomp`), die nur bei Mathefehlern laufen.
- Das gilt auch für die bisherige Fenster-App: Sobald dort printf mit `%f`, `atof` oder Rundungsfunktionen genutzt werden, braucht sie `nt4_crt.cpp` ebenfalls (siehe den offenen Punkt „Mathe (log10)“).

**Ergebnis im NT4 (16 Bit, 800 × 600):**
- `NT4LITE.EXE` lädt `/lite` von crimsrv und rendert die Seite im Stykker-Look: Karten, Rahmen, Farbverlauf im Balken, Buttons.
- Taste S startet den Timer auf dem Server. Der Balken wächst jede Sekunde (7 s → 36 %, 13 s → 65 %).
- Pause vom Host aus (`/lite?cmd=stop` per PowerShell): Die NT4-App zeigt danach „angehalten“ bei 25 % und fragt nur noch alle 3 s nach. Der Server steuert also Zustand und Takt.
- Übertragung: ISO `S:\workspace\nt4\apps9.iso` (Volume LITE2), in der VM als `C:\NT4LITE2.EXE`. `copy /y` gibt es in NT4 nicht. Die erste Version liegt noch als `C:\NT4LITE.EXE` (stürzt ab, SSE).
- Größe: `NT4LITE.EXE` etwa 1,6 MB (statisch gelinkt, litehtml + libc++).

**Grenzen:** Kein JavaScript, keine Bilder, keine Formulare (litehtml zeichnet Eingabefelder nicht selbst). Jede Aktualisierung lädt die ganze Seite neu und rendert sie komplett. Bei dieser Seitengröße ist das unauffällig.
