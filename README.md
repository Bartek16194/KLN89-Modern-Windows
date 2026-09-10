# KLN 89 Simulator - Modern Windows Compatibility Wrapper

Unofficial community compatibility wrapper for the legacy **Bendix/King / AlliedSignal KLN 89 simulator** on modern Windows.

The original simulator is **not included** in this repository or in release binaries. This project only contains original compatibility code written for this project.

## What it fixes

- Bypasses the legacy WinHelp failure that causes the 1997 simulator to abort on current Windows versions.
- Prevents Windows from repeatedly opening the obsolete WinHelp support page.
- Adds a resizable window with **aspect-ratio-preserving scaling**.
- Keeps mouse interaction mapped to the original simulator coordinates.
- Uses double-buffered rendering to reduce flicker.

## Requirements

You must already have a copy of the original KLN 89 simulator with at least:

```text
kln89.exe
c_navdb.dat
user.dat
```

`kln89.hlp` is optional. Modern Windows no longer supports the old WinHelp format natively, so the compatibility wrapper does not depend on it.

Run `KLN89Modern.exe` from anywhere. The program will ask you to select your original `kln89.exe`; `c_navdb.dat` and `user.dat` must be next to that selected executable.

The original executable is never modified. A patched working copy is created in a unique temporary directory under `%TEMP%` and removed after the simulator exits normally.

## Supported original executable

The patcher currently targets the known 1997 executable:

- Size: `721,952 bytes`
- SHA-256: `e1ab4dbd76a4816056a7caecbb5e29a93587dab427e2cdcd3b7a6a6558fe3748`

The program verifies the expected machine-code bytes before patching. If the executable differs, it stops instead of modifying an unknown build.

## Build

Open a **Developer Command Prompt for Visual Studio** and run:

```bat
cl /std:c++17 /O2 /EHsc /DUNICODE /D_UNICODE src\KLN89Modern.cpp /link user32.lib gdi32.lib shell32.lib comdlg32.lib /SUBSYSTEM:WINDOWS /OUT:KLN89Modern.exe
```

A GitHub Actions workflow is included and builds the executable automatically on Windows.

## Legal / trademark notice

This project is an **unofficial compatibility modification** and is not affiliated with, sponsored by, or endorsed by Bendix/King, AlliedSignal, Honeywell, or any successor rights holder.

The original KLN 89 simulator executable, navigation database, help file, artwork, documentation, trademarks, and other original assets are **not distributed by this project**. Users must provide their own lawfully obtained original simulator files.

All rights in the original simulator and associated marks remain with their respective rights holders.

## License

The compatibility wrapper source code in this repository is licensed under the MIT License. This license applies **only to the code in this repository** and does not grant any rights to the original KLN 89 simulator or its assets.

## Temporary working directory

KLN89Modern never patches the user's original `kln89.exe`. On each launch it creates a unique working directory under `%TEMP%\KLN89Modern_*`, copies the required runtime data there, patches only the temporary executable, and launches that copy. When the simulator exits normally, `user.dat` is copied back to the original simulator folder to preserve user data and the complete temporary directory is deleted. Leftover `KLN89Modern_*` folders from interrupted/crashed previous runs are cleaned on the next launch.
