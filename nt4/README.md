# NT 4.0 test setup

Notes and sources from bringing up a Windows NT 4.0 SP6 test machine in VirtualBox, and from building a small Win32 app for it in the Stykker look.

## Contents

| Path | What it is |
|---|---|
| `docs/nt4-status.md` | Status log: VM facts, keyboard control through VBoxManage scancodes, network, file transfer, build state, display research. Start here. |
| `tools/make-iso.ps1` | Builds an ISO from a folder (IMAPI2FS). The ISO is mounted as a DVD in the running VM, which is how files get in. |
| `window/main.cpp`, `window/nt4-window.vcxproj` | Win32/GDI window app with the Stykker design tokens. Built with clang-cl against ENLYZE. |
| `build-notes/*.patch` | Local changes to third-party sources, as patches. The sources themselves are not in this repo. |
| `lite/` | `NT4LITE.EXE`: Win32 app with litehtml as an embedded HTML view (GDI drawing, HTTP over Winsock, meta refresh, accesskey links). `nt4_crt.cpp` replaces UCRT float/rounding functions that use SSE. `litehtml-nt4.vcxproj` builds litehtml for NT 4. |
| `blazor/` | .NET 10 Blazor test app that runs on the host and is opened from NT 4 in Opera 10.63. Shows what works there: static SSR, form posts, ES3 JavaScript with XHR. Interactive Blazor Server does not. |

## Build

- Toolchain: Visual Studio 2022 Build Tools with the "C++ Clang tools for Windows" component (clang-cl), toolset `ClangCL`, Windows SDK 10.0.26100.
- Use the MSVC 14.38 toolset (`VCToolsVersion` 14.38.33130). With 14.44 the startup code uses SSE instructions and fails on NT 4 with `illegal instruction`.
- Libraries: EnlyzeWinCompatLib (branch `clang-vs2022-fls-initcriticalsectionex`, commit `994a24a`), libc++ 17.0.6, winpthreads. The projects expect them under `..\build\Release\` relative to the project folder.
- Command: `MSBuild nt4-window.vcxproj /p:Configuration=Release /p:Platform=Win32`

## Status (25.10.)

- Network in the VM works (DHCP through NAT, gateway ping).
- Files reach the VM through an ISO that is mounted in the running VM.
- The test program and the window app start on NT 4.
- Screen: 16 colors at 640x480 (standard VGA) before VBEMP. The Stykker gradients dither in that mode.
- Tried: the Cirrus Logic 544x and the generic S3 driver from the NT 4 CD. Both install and NT 4 boots, but List All Modes shows only 640x480 and 800x600 at 16 colors.
- VBE driver VBEMP (third party, archive copy of vbempg.zip, STABLE 2007): installed. The screen switches to 256 colors at 640x480. 800x600 at 256 colors is set and was confirmed with the test dialog. A reboot with this setting is not checked yet.

## Status (27.10.)

- Screen: VBEMP also offers 65536 colors (16 bit). Set to 800x600 at 65536 colors, confirmed with the test dialog, no reboot needed.
- Browser: Opera 10.63 (Presto 2.6.30) is already installed and is now the default browser. HTTP only, no current TLS.
- Host access: the VirtualBox 7 NAT does not forward `10.0.2.2` to the host's loopback. The app listens on the host's LAN IP instead, and NT 4 opens `http://<host LAN IP>:5080/`.
- Blazor in Opera 10.63: static SSR pages, form posts, own ES3 JavaScript and XHR work. Interactive Blazor Server (SignalR, `blazor.web.js`) does not connect.
- Run the app: `dotnet run -c Release --no-launch-profile --urls http://<host LAN IP>:5080` in `blazor/`.

## litehtml client (27.10.)

- `lite/NT4LITE.EXE` renders `GET /lite` from the Blazor app with litehtml. The page is static SSR without script. A server-side timer drives a progress bar, and the page sets its own refresh rate with `<meta http-equiv="refresh">`. Tested on NT 4: start from the app (key S), pause from the host, the NT 4 view follows.
- The static UCRT from Windows SDK 10.0.26100 uses SSE in printf/strtod float code, rounding, `pow` and fenv without a CPU check. NT 4 never enables SSE, so those calls crash with "illegal instruction". `lite/nt4_crt.cpp` provides x87 versions that the linker picks before the UCRT objects. Check a build with `llvm-objdump -d` for `xmm` and map hits with `llvm-symbolizer`.
- Build layout: `lite/` expects `../litehtml` (litehtml commit `7ea2956`), `../nt4-exe-test/EnlyzeWinCompatLib` and `../build/Release/...` next to it, as in the window app. Build `litehtml-nt4.vcxproj` first, then `nt4-lite.vcxproj`.
- Limits: no JavaScript, no images, no form controls. Each refresh reloads and re-renders the whole page.

## Not included

- Binaries, ISOs, screenshots, the VM disk.
- Third-party sources (solemnwarning/nt4-exe-test, EnlyzeWinCompatLib). Their local changes are in `build-notes/`.
- NT 4 install media.
