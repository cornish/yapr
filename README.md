<p align="center">
  <img src="assets/yapr-logo-900.png" alt="yapr — Yet Another PDF Reader" width="450">
</p>

<p align="center"><b>A fast, light, cross-platform PDF reader that never surprises you.</b></p>

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
- Recent files (File → Open Recent) and Reopen Closed Tab (Ctrl+Shift+T) —
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

Requires Qt 6.4+ with the Qt PDF module.

### Windows (MSYS2 / UCRT64)

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base \
          mingw-w64-ucrt-x86_64-qt6-pdf
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Linux

Install Qt 6 development packages including QtPdf (e.g. `qt6-base-dev` and
the distro's qt6 pdf dev package), then the same cmake invocation.

### macOS

```sh
brew install qt
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

## Packaging (Windows)

```powershell
.\scripts\package-windows.ps1       # -> dist\yapr-win64-portable.zip
.\scripts\register-open-with.ps1    # add yapr to "Open with" for .pdf (HKCU, no admin)
.\scripts\register-open-with.ps1 -Unregister
```

The portable folder is fully self-contained — run it from anywhere.

## License

GPL-3.0 (required by the Qt PDF module's licensing). See [LICENSE](LICENSE).
