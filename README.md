# yapr — yet another pdf reader

A fast, light, cross-platform PDF reader that never surprises you.

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
| Ctrl+W | Close tab |
| Ctrl+F | Find in document |
| F3 / Shift+F3 | Next / previous match |
| Ctrl+= / Ctrl+- | Zoom in / out |
| Ctrl+Q | Quit |

## License

GPL-3.0 (required by the Qt PDF module's licensing). See [LICENSE](LICENSE).
