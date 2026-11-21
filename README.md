<p align="center">
  <img src="assets/yapr-logo-900.png" alt="yapr — Yet Another PDF Reader" width="450">
</p>

<p align="center"><b>A fast, light, cross-platform PDF reader that never surprises you.</b></p>

<p align="center">
  <a href="https://github.com/cornish/yapr/actions/workflows/build.yml"><img src="https://github.com/cornish/yapr/actions/workflows/build.yml/badge.svg" alt="Build status"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue" alt="License: GPL-3.0"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white" alt="C++17">
  <img src="https://img.shields.io/badge/Qt-6-41CD52?logo=qt&logoColor=white" alt="Qt 6">
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-555" alt="Platforms: Windows, Linux, macOS">
</p>

Built because Adobe is bloated and the free alternatives are janky. yapr's
contract with you:

- **Deterministic opening.** Every document opens in continuous scroll,
  fit-to-width. No per-document mystery state, no mode guessing.
- **Closed means closed.** Session state is written the moment a tab is
  opened or closed — never reconstructed from a snapshot at exit. Closed
  tabs stay closed.
- **One file, one tab.** Opening an already-open PDF switches to its tab.
- **Single instance.** Double-clicking a PDF opens a tab in the existing
  window, not a new window.
- **Instant start, no bloat.** A small native binary that opens before you
  finish letting go of the mouse button.
- **Explicit memory, never spooky.** yapr remembers your last page per
  document (just the page — mode and zoom stay at their defaults), and
  ☰ → Forget Reading Positions wipes it all, no questions asked.

## Download

Get the latest build from [Releases](https://github.com/cornish/yapr/releases).

| Platform | File | Notes |
|---|---|---|
| Windows | `yapr-X.Y.Z-windows-x64-setup.exe` | Installs for your user (no admin needed), adds Start menu entry and "Open with" for PDFs |
| Windows | `yapr-X.Y.Z-windows-x64-portable.zip` | Unzip anywhere and run `yapr.exe`; settings stay in a `data` folder beside it |
| Linux | `yapr-X.Y.Z-linux-x86_64.AppImage` | `chmod +x` it and run; works on any distro from Ubuntu 22.04-era onward |
| macOS | `yapr-X.Y.Z-macos.dmg` | Universal (Apple Silicon + Intel), macOS 13+; drag yapr to Applications |

The builds aren't code-signed yet, so the first launch needs one extra click:

- **Windows:** SmartScreen says "Windows protected your PC" → **More info** → **Run anyway**.
- **macOS:** right-click (or Control-click) yapr in Applications → **Open** → **Open**.
  Only needed once.

## Features

- Compact layout: tab row with a ☰ menu, one toolbar (page box, previous/next page,
  fit width/page, zoom, inline Find), no menu bar or status bar
- Tabbed reading, drag-and-drop to open
- Text selection: drag to select (across pages), double-click for a word,
  Ctrl+A for everything, Ctrl+C to copy
- Ctrl+drag a rectangle to copy that region as a 300 dpi image
- Printing (Ctrl+P) with page ranges; landscape pages auto-rotate to fit
- Text search (Ctrl+F) with match count and F3/Shift+F3 navigation
- Sidebar (F9) with document outline and async-rendered page thumbnails
- Dark mode (toolbar toggle or Ctrl+Shift+D; System/Light/Dark under
  ☰ → View → Theme) and inverted page colors (Ctrl+I) for night reading —
  both persisted
- Session restore that only ever reflects reality
- Recent files (☰ → Open Recent) and Reopen Closed Tab (Ctrl+Shift+T) —
  explicit, never automatic
- Back/forward through link, outline, and page jumps (Alt+Left/Right or
  mouse side buttons)
- Auto-reload when the file changes on disk, keeping your scroll position
  (toggle in ☰ → Preferences). Files are read into memory, so yapr never
  locks a PDF that another program wants to rewrite.
- Full screen (F11), go to page (Ctrl+G), tab context menu (copy path, open
  containing folder, close others / to the right)

## Stack

- C++17 / Qt 6 Widgets
- [Qt PDF](https://doc.qt.io/qt-6/qtpdf-index.html) (`QPdfView`,
  `QPdfDocument`, `QPdfSearchModel`, `QPdfBookmarkModel`) — PDFium under
  the hood
- CMake + Ninja

## Building

Requires Qt 6.8 or newer with the Qt PDF module (releases are built with
Qt 6.11) and a C++17 compiler.

### Windows (MSYS2 / UCRT64)

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base \
          mingw-w64-ucrt-x86_64-qt6-pdf mingw-w64-ucrt-x86_64-qt6-tools
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`build\yapr.exe` needs `C:\msys64\ucrt64\bin` on PATH to find its DLLs;
the packaged `dist\yapr\yapr.exe` (below) is self-contained.

### Linux

Most distros' packaged Qt is older than 6.8, so install Qt with
[aqtinstall](https://github.com/miurahr/aqtinstall), as CI does:

```sh
pip install aqtinstall
aqt install-qt linux desktop 6.11.3 linux_gcc_64 -m qtpdf --outputdir ~/Qt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=~/Qt/6.11.3/gcc_64
cmake --build build
```

### macOS

```sh
brew install qt ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Keyboard shortcuts

| Key | Action |
|---|---|
| Ctrl+O | Open file(s) |
| Ctrl+P | Print |
| Ctrl+W | Close tab |
| Ctrl+Shift+T | Reopen closed tab |
| Ctrl+G | Go to page |
| Alt+Left / Alt+Right | Back / forward |
| F5 | Reload |
| F11 / Esc | Full screen / exit full screen |
| Ctrl+, | Preferences |
| Ctrl+C / Ctrl+A | Copy selection / select all text |
| Ctrl+drag | Copy region as image |
| Ctrl+F | Find in document |
| F3 / Shift+F3 (Enter / Shift+Enter in the box) | Next / previous match |
| Ctrl+2 / Ctrl+0 / Ctrl+1 | Fit width / fit page / actual size |
| Ctrl+= / Ctrl+- | Zoom in / out |
| F9 | Toggle sidebar (outline / thumbnails) |
| Ctrl+Shift+D | Toggle light / dark theme |
| Ctrl+I | Invert page colors |
| Ctrl+Q | Quit |

## Packaging

Each script packages an existing Release build from `build/` into `dist/`:

```sh
./scripts/package-windows.ps1   # portable zip, plus setup.exe if Inno Setup 6 is installed
./scripts/package-linux.sh      # AppImage (set QMAKE to your Qt's qmake)
./scripts/package-macos.sh      # dmg (set MACDEPLOYQT to your Qt's macdeployqt)
```

To register a local build in Windows' "Open with" list without the
installer: `.\scripts\register-open-with.ps1` (`-Unregister` to undo).

## Releasing

CI ([build.yml](.github/workflows/build.yml)) builds and packages all three
platforms on every push and pull request. To cut a release:

1. Bump `VERSION` in `CMakeLists.txt` and commit.
2. Tag and push: `git tag vX.Y.Z && git push origin vX.Y.Z`. The tag must
   match the CMake version, or CI fails before building anything.
3. CI creates a **draft** release with the installer, portable zip,
   AppImage, and dmg attached. Review the notes, then click **Publish**.

## License

GPL-3.0 (required by the Qt PDF module's licensing). See [LICENSE](LICENSE).
