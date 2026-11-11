# NT4DNC: CNC-Programme per HTTP auf NT-4-Maschinen

`NT4DNC.EXE` holt CNC-Programme vom Blazor-Server (`../blazor`) und schreibt sie auf der Maschine in einen Ordner.
Gedacht für NT-4-Rechner an Maschinen (zum Beispiel Siemens 840D), die keine aktuellen Windows-Freigaben
(nur SMB1) und kein aktuelles TLS können. Übertragen wird über reines HTTP.

## Ablauf beim Schreiben

1. Das Programm wird in eine Zwischendatei `~NT4DNC.TMP` im Zielordner geladen, dabei wird die CRC32 gerechnet.
2. Die Länge und die CRC32 werden mit den Angaben des Servers verglichen, danach wird die Zwischendatei noch einmal
   von der Platte gelesen und geprüft.
3. Ist das Programm auf der Maschine schon gleich, wird nichts geändert. Ist es anders, kommt die alte Fassung
   zuerst in den Sicherungsordner (`JJJJMMTT-hhmmss_NAME`).
4. Die Zwischendatei wird auf den echten Namen umbenannt. Unter dem echten Namen liegt nie eine halbe Datei.
5. Das Ergebnis steht im lokalen Protokoll und im Protokoll auf dem Server.

Schreibgeschützte oder gerade geöffnete Programme werden nicht angefasst. Das Programm meldet dann „in Benutzung“
bzw. „schreibgeschützt“.

## Bedienung

- Fenster: `NT4DNC.EXE` zeigt die Programme, die der Server für diese Maschine bereithält, mit Größe, Datum und
  Abgleich zur Maschine („fehlt“, „gleich“, „ANDERS“). F5 lädt die Liste neu. Enter oder Doppelklick schreibt das
  gewählte Programm, nach einer Rückfrage. Esc beendet.
- Kommandozeile (Test, Skripte): `NT4DNC.EXE -list` und `NT4DNC.EXE -get NAME`. Die Ausgabe geht auf stdout,
  Exitcode 0 bedeutet ok.

## Einrichtung

**Maschine:** `NT4DNC.example.ini` als `NT4DNC.INI` neben die EXE legen und Server, Maschinenname, Schlüssel und
Ordner eintragen.

**Server:** Die Programme liegen unter `<ProgramRoot>/<MASCHINE>/` (Vorgabe `dnc/programme/`, relativ zum
App-Ordner). Jede Maschine braucht einen Schlüssel in `Dnc:Machines`. Echte Werte gehören nicht ins Repo, sondern
auf den Server, zum Beispiel als Umgebungsvariable:

```
Dnc__Machines__M12=<schluessel>
```

Ohne Eintrag gibt es keinen Zugang. In `appsettings.Development.json` steht nur die Testmaschine `TEST`.

## Schnittstelle (`/nt4/api/dnc`)

Jede Anfrage trägt die Header `X-Nt4-Machine` und `X-Nt4-Token`.

| Anfrage | Antwort |
|---|---|
| `GET /nt4/api/dnc/list` | eine Zeile pro Programm: `NAME;GROESSE;JJJJMMTThhmmss;CRC32` (hex) |
| `GET /nt4/api/dnc/file/NAME` | das Programm unverändert (Bytes), Header `X-Crc32` |
| `POST /nt4/api/dnc/log` | eine Textzeile, landet im Übertragungsprotokoll des Servers |

Programmnamen: Buchstaben, Ziffern, `_ . -`, höchstens 64 Zeichen, keine Pfadteile. Client und Server prüfen das beide.

## Bauen

Wie die anderen NT-4-Projekte (siehe `../README.md`): ClangCL mit MSVC 14.38, `-march=pentium-mmx`,
EnlyzeWinCompatLib. `NT4DNC` braucht kein libc++ und keine Gleitkommazahlen.

```
MSBuild nt4-dnc.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:LibBuildRoot=<Ordner mit Release\EnlyzeWinCompatLib\bin>
```

Prüfung nach dem Bauen: `llvm-objdump -d NT4DNC.exe` zeigt dieselben 506 SSE-Stellen wie `nt4-window.exe`
(nur CRT-Teile mit CPU-Prüfung, die das Programm nicht aufruft).

## Stand (11.11.2026)

Auf dem Windows-11-Host getestet, gegen den echten Server und einen absichtlich fehlerhaften Testserver:
Liste, neues Programm, unverändertes Programm, geändertes Programm mit Sicherung, Binärdaten und 5 MB byte-gleich,
falscher Schlüssel, unbekanntes Programm, ungültiger Name, schreibgeschützte und gesperrte Zieldatei,
Server nicht erreichbar, falsche Prüfsumme, abgebrochene Übertragung.

**Noch nicht geprüft:**
- Lauf auf NT 4 selbst (VM oder Maschine).
- Wohin die Programme auf einer 840D genau müssen, damit die Bediensoftware sie übernimmt. Der Zielordner ist
  deshalb einstellbar. Ob ein direkt in einen Ordner geschriebenes Programm in der Programmverwaltung der Steuerung
  erscheint, muss an einer echten Maschine geprüft werden. Bis dahin nicht in Ordner der Bediensoftware schreiben,
  sondern in einen eigenen Übergabeordner.
- Ohne TLS geht alles im Klartext übers Netz. Nur in einem abgetrennten Maschinennetz einsetzen, in dem die
  Firewall nur den HTTP-Port zum Server durchlässt.
