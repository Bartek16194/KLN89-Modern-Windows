# KLN 89 Simulator - Modern Windows Compatibility Wrapper

Unofficial community compatibility wrapper for the legacy **Bendix/King / AlliedSignal KLN 89/89B simulator** on modern Windows.

The original simulator is **not included** in this repository or in release binaries. This project contains only the compatibility wrapper and patching code.

## Download

**[Download the latest KLN89Modern.exe](https://github.com/Bartek16194/KLN89-Modern-Windows/releases/latest/download/KLN89Modern.exe)**

All published versions are available on the **[Releases page](https://github.com/Bartek16194/KLN89-Modern-Windows/releases)**.

## Original simulator required

KLN89Modern does **not** include the original KLN 89B simulator. You must already have the original simulator files.

A currently available third-party download source is:

**[KLN 89B Simulator - Software Informer](https://kln-89b-simulator.software.informer.com/)**

This external website is not affiliated with this project. Availability and licensing of files hosted there are outside the control of this repository.

The wrapper expects at least these files from the original simulator:

```text
kln89.exe
c_navdb.dat
user.dat
```

`kln89.hlp` is optional. Modern Windows no longer supports the old WinHelp format natively, so KLN89Modern does not depend on it.

## Usage

1. Download `KLN89Modern.exe` from the Releases page.
2. Run it from anywhere.
3. Select your original `kln89.exe` when asked.
4. `c_navdb.dat` and `user.dat` must be in the same folder as the selected executable.

The original executable is never modified. KLN89Modern creates a patched working copy in a unique directory under `%TEMP%`, runs it there, copies updated `user.dat` back after exit, and removes the temporary directory.

## What it fixes

- Bypasses the legacy WinHelp failure that causes the 1997 simulator to abort on current Windows versions.
- Prevents Windows from repeatedly opening the obsolete WinHelp support page.
- Adds a resizable window with **aspect-ratio-preserving scaling**.
- Maps mouse interaction back to the original simulator controls.
- Uses buffered/cached rendering to greatly reduce flicker from the legacy Win32/GDI interface.

A very occasional single-frame flicker may still occur on some systems because of the way the original 1997 application renders its interface.

## Screenshot

<!-- Upload your screenshot as docs/screenshot.png, then uncomment the line below. -->
<!-- ![KLN89 Modern Windows](docs/screenshot.png) -->

## Supported original executable

The patcher currently targets the known 1997 executable:

- Size: `721,952 bytes`
- SHA-256: `e1ab4dbd76a4816056a7caecbb5e29a93587dab427e2cdcd3b7a6a6558fe3748`

The program verifies the expected machine-code bytes before patching. If the executable differs, it stops instead of modifying an unknown build.

## Building from source

A GitHub Actions workflow automatically builds the Windows executable.

To build locally, open a **Developer Command Prompt for Visual Studio** and run:

```bat
cl /std:c++17 /O2 /EHsc src\KLN89Modern.cpp /link user32.lib gdi32.lib comdlg32.lib /SUBSYSTEM:WINDOWS /OUT:KLN89Modern.exe
```

Normal users do **not** need to build the project themselves; use the ready-made executable from Releases.

## Legal / trademark notice

This project is an **unofficial compatibility modification** and is not affiliated with, sponsored by, or endorsed by Bendix/King, AlliedSignal, Honeywell, Software Informer, or any successor rights holder.

The original KLN 89/89B simulator executable, navigation database, help file, artwork, documentation, trademarks, and other original assets are **not distributed by this project**. Users must provide their own lawfully obtained original simulator files.

All rights in the original simulator and associated marks remain with their respective rights holders.

## License

The compatibility wrapper source code in this repository is licensed under the MIT License. This license applies **only to the code in this repository** and does not grant any rights to the original KLN 89/89B simulator or its assets.
