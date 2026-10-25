# NT 4.0 test setup

Notes and sources from bringing up a Windows NT 4.0 SP6 test machine in VirtualBox, and from building a small Win32 app for it in the Stykker look.

## Contents

| Path | What it is |
|---|---|
| `docs/nt4-status.md` | Status log: VM facts, keyboard control through VBoxManage scancodes, network, file transfer, build state, display research. Start here. |
| `tools/make-iso.ps1` | Builds an ISO from a folder (IMAPI2FS). The ISO is mounted as a DVD in the running VM, which is how files get in. |
| `window/main.cpp`, `window/nt4-window.vcxproj` | Win32/GDI window app with the Stykker design tokens. Built with clang-cl against ENLYZE. |
| `build-notes/*.patch` | Local changes to third-party sources, as patches. The sources themselves are not in this repo. |

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

## Not included

- Binaries, ISOs, screenshots, the VM disk.
- Third-party sources (solemnwarning/nt4-exe-test, EnlyzeWinCompatLib). Their local changes are in `build-notes/`.
- NT 4 install media.
