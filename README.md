# Document Viewer

A native C++ / Qt Widgets PDF and EPUB reader with multiple document tabs, continuous
vertical scrolling, and zoom from 10% to 500%.

Only one instance runs per Windows login session (per user on Linux/macOS). A
second launch exits without opening a window or modifying saved state. Instance
locks are released or recovered after exit or a crash.

Each document opens in its own closable, reorderable tab and retains its scroll position
and zoom. Select PDFs and EPUBs in the open dialog, drop multiple files, or pass
multiple file paths at startup. Closing the last tab returns to the welcome view.

The app automatically saves the tab order, active tab, each document's horizontal
and vertical scroll positions, zoom percentage and fit mode, and window geometry
on close and every five seconds. The next launch restores the session before
opening any files supplied on the command line. Checkpoints use atomic file
replacement so an interrupted write preserves the previous saved session.
After a crash, the latest completed checkpoint is restored.

Session data is stored in `%LOCALAPPDATA%\Document Viewer\session.json` on Windows,
`~/.local/share/Document Viewer/session.json` on Linux (or `$XDG_DATA_HOME`), and
`~/Library/Application Support/Document Viewer/session.json` on macOS.
Missing or unreadable documents are skipped with a status message. Passwords are never
saved; protected PDFs request their password again during restoration. Closing all
tabs and exiting saves an empty session.

Features: open dialog, drag and drop, password-protected documents, page navigation,
fit width, fit page, editable zoom percentage, and keyboard shortcuts. Pages are
rendered as visible 512-pixel tiles with a 64 MiB cache, including high-DPI support.
An unsuccessful open keeps the previous document available.

## Build and run on this machine

Uses the installed Qt 6.11.2 MinGW kit, MinGW 13.1, CMake, and Ninja under `C:\Qt`.
No Python is used.

```powershell
.\build.cmd -Test
.\build\DocumentViewer.exe
# Optionally open a document at startup:
.\build\DocumentViewer.exe 'C:\documents\example.pdf'
```

The build script deploys the Qt runtime beside the executable. In Qt Creator,
open `CMakeLists.txt` and select the installed Qt 6.11.2 MinGW 64-bit kit.
`CMakePresets.json` contains the local paths; adjust them for another installation.
The `.cmd` launcher permits the build script for that process only, so it also
works with this machine's default PowerShell script policy.

To compile Qt itself with the installed MSVC x64 toolchain and LTO, then build and
package the viewer against it:

```powershell
.\build-qt-msvc.cmd
.\build-msvc.cmd -Package
```

The Qt build script fetches pinned Qt 6.11.2 Base and SVG sources into `C:\Qt\src`,
builds under `C:\Qt\build`, and installs to `C:\Qt\6.11.2\msvc_lto_64`.
It uses shared release libraries with `-ltcg -optimize-size`; Qt examples and
Qt's own test suite are excluded. Visual Studio's C++ workload, Python 3, Git,
and the local CMake/Ninja tools are required. Both scripts accept `-Jobs` (default 8).
The MSVC viewer uses `build/msvc`, and its portable launcher uses
`build/portable-msvc`, keeping compiler caches separate from MinGW.
`build-msvc.cmd -Test` builds and tests; `-Package` also tests, deploys, and creates
`dist/dv-windows-x64.exe`, then smoke-tests that portable executable.
The package includes the MSVC runtime DLLs beside the application, without
requiring users to run a separate redistributable installer.
The `local-msvc-lto` preset can also be used from an MSVC developer shell.
GitHub Actions uses the same Qt source build and runtime deployment scripts for
both Windows x64 and ARM64. Linux and macOS continue to use prebuilt Qt kits.

## Controls

| Action | Control |
| --- | --- |
| Open PDF or EPUB | Open button, Ctrl+O, or drop a local file |
| Scroll continuously | Mouse wheel, trackpad, scrollbar, Page Up / Page Down |
| Zoom | − / + buttons, percentage field, or Ctrl+wheel |
| Zoom shortcuts | Ctrl+−, Ctrl++, Ctrl+= |
| Fit width | Toolbar button or Ctrl+0 |
| Fit page | Zoom dropdown |
| Navigate | Page number, arrow buttons, Alt+Left / Alt+Right |
| Switch tabs | Click a tab, Ctrl+Tab, or Ctrl+Shift+Tab |
| Close tab | Tab close button or Ctrl+W |
| Exit and save session | Escape |
| Find substring | Find button or Ctrl+F |
| Next / previous match | Enter or F3 / Shift+F3, or search bar buttons |

Search is case-insensitive, includes partial words and overlapping matches, and
wraps at the first/last result.
Navigation is available as soon as matches are found, while the remaining pages
are still being searched. Next/previous wraps among the matches found so far;
new results and search completion preserve the selected match and scroll position.
All results are highlighted in yellow, with the selected result in orange.
Each tab keeps its own query while the app is open.
Clear the query to remove highlights. Search scans one page per event-loop turn;
an unusually complex page can still briefly delay input. Image-only scans need
an existing OCR text layer to be searchable; the app does not perform OCR.

## PDF rendering dependency

The installed Qt kit does not include Qt PDF. The interface uses local Qt Widgets;
rendering uses [PDFium](https://pdfium.googlesource.com/pdfium/) through the
[PDFium binary distribution](https://github.com/bblanchon/pdfium-binaries).
CMake downloads the matching platform/architecture release `chromium/8044` at first configure and verifies
its SHA-256 checksum. Subsequent builds use the cached dependency. PDF files stay
local. PDFium's license and third-party notices are included in the build output.

Build targets include Windows x64/ARM64, Linux x64, and macOS x64/ARM64.
Text selection, editing, and printing are
outside the current scope. Rendering runs on the UI thread, so unusually complex
pages can briefly delay input. The file is kept in memory while open; rendered
tiles have a separate bounded cache.

## EPUB support

EPUB 2 and EPUB 3 books are read directly from their ZIP container without
extracting files. Chapters follow the package spine reading order. Qt lays out
text, images, tables, and supported CSS into A4 pages in memory, displayed through
the existing viewer. Continuous scrolling, zoom, substring search, mixed PDF/EPUB
tabs, and session recovery work for books too. The original EPUB is unchanged.

This is a basic paginated EPUB reader: complex CSS, fixed-layout fidelity, inline
SVG, embedded fonts, scripts, multimedia, and interactive book navigation are not
fully supported. Fonts use installed fallbacks. DRM-encrypted books are rejected.
Opening a large book can take a moment while pages are laid out; zoom scales these
pages rather than reflowing the book. External network/file resources are not loaded.

Archive reading uses the installed Qt 6.11.2 CorePrivate ZIP reader. Rebuild with
matching private headers when upgrading Qt; distribute the matching deployed Qt DLLs.

## Validation

`build.cmd -Test` runs Qt Test offscreen against a generated three-page PDF. It
checks page rendering, continuous scroll range, navigation, zoom limits,
Ctrl+wheel, resize-to-fit, invalid-file recovery, Unicode paths, and reopening.
It also checks independent tab state, toolbar synchronization, keyboard tab
switching and closing, and reopening after the last document is closed.
Session tests cover periodic checkpoints without a close event, immediate saves
on close, restored tab state, missing documents, empty sessions, and corrupt JSON.
EPUB tests cover versions 2 and 3, spine order, relative image/CSS resources,
substring search, zoom/session recovery, and missing-chapter error recovery.

## GitHub Actions

`.github/workflows/build.yml` runs on pushes, pull requests, and manual dispatch:

| Runner | Toolchain / architecture |
| --- | --- |
| `windows-latest` | MSVC, x64 |
| `windows-11-arm` | Native MSVC, ARM64 |
| `ubuntu-latest` | GCC, x64 |
| `macos-latest` | Apple Clang, ARM64 |

Windows jobs compile pinned Qt 6.11.2 Base and SVG sources with MSVC, LTO, and
size optimization. The installed Qt kits are cached by architecture, compiler,
Windows SDK, and build-script hash. The first build after a cache change takes
longer. Windows packages bundle the matching MSVC runtime DLLs directly, omitting
the redistributable installer. Portable smoke tests exclude the Qt installation
from the environment.

Linux and macOS jobs install prebuilt Qt 6.11.2 with matching private headers.
Each job configures CMake/Ninja, builds, runs the offscreen Qt tests, and deploys
the application with its Qt and PDFium dependencies. Platform-specific PDFium archives are pinned by SHA-256.
Failed jobs upload test logs. Successful jobs upload a
platform artifact:

| Artifact | Contents |
| --- | --- |
| `dv-windows-x64.exe` | `dv-windows-x64.exe` |
| `dv-windows-arm64.exe` | `dv-windows-arm64.exe` |
| `dv-linux-x64.AppImage` | `dv-linux-x64.AppImage.tar` containing the executable AppImage |
| `dv-macos-arm64` | `dv-macos-arm64.dmg` |

GitHub wraps workflow artifacts in ZIP files. After unzipping the Linux download,
run `tar -xf dv-linux-x64.AppImage.tar`; the AppImage retains executable permissions
and can be launched directly. The tar wrapper avoids GitHub's artifact permission
loss. Windows files launch directly; open the macOS disk image and launch or copy
`DocumentViewer.app`.
The macOS app is not Developer ID signed or notarized.
Linux artifacts target the runner's distribution/runtime generation.

For a portable local configure with Qt available in `CMAKE_PREFIX_PATH`:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --install build --prefix stage
```

The existing `local-qt` preset and `build.cmd` remain specific to this Windows
machine. Offscreen tests load an installed font; set `DOCUMENT_VIEWER_TEST_FONT`
to a font file to override the platform default. On an already deployed Windows
build, use the test preset or set `QT_QPA_PLATFORM_PLUGIN_PATH` to the Qt kit's
`plugins/platforms` directory so the offscreen plugin can be found.

Link-time optimization (LTO/IPO) is enabled by default for Release, RelWithDebInfo,
and MinSizeRel builds, including the Windows portable launcher. CMake checks
compiler/linker support at configure time; use `-DDOCUMENT_VIEWER_ENABLE_LTO=OFF`
to disable it. Prebuilt Qt and PDFium libraries are not rebuilt with LTO; the
`build-qt-msvc.cmd` path and Windows CI compile Qt itself with LTO.

Local presets, CI, and the portable launcher use `MinSizeRel` to optimize for size
(`-Os` with GCC/Clang, `/O1` with MSVC) while retaining LTO. GNU-linked size builds
strip symbols; Apple and MSVC size builds remove unused code. Windows payloads use
maximum ZIP compression. Most of the portable file still consists of the prebuilt
Qt/PDFium dependencies. Prebuilt dependencies retain their upstream compilation
settings; the optional local MSVC Qt build enables size optimization and LTO.

## Single-file distribution

Run `package.cmd` on this machine to build, test, deploy, and generate
`dist/dv-windows-x64.exe`. This is the file to copy to another Windows x64 machine;
it embeds the application, Qt plugins, PDFium, compiler runtime, and notices.
The portable launcher uses Windows' built-in `tar.exe` (Windows 10 1803+ / Windows
11) to unpack into a unique temporary directory, forwards command-line arguments,
waits for the viewer to exit, and then deletes that directory. A forced termination
of the launcher can leave temporary files behind. Session settings remain in the
normal user data directory, so they persist across launches and updates.

This uses self-extraction rather than static Qt/PDFium linking. Linux AppImages
similarly contain their runtime dependencies. A macOS `.dmg` is a single distribution
file containing the native `.app` bundle. CI smoke-tests the packaged Windows and
Linux launchers and the deployed macOS bundle with `--smoke-test`, which initializes
the UI without loading or changing the reading session.

`packaging/package.py` packages an existing CMake install tree. On Windows it builds
a small native launcher with a statically linked compiler runtime. Linux packaging
uses checksum-verified appimagetool 1.9.1; macOS uses the built-in `hdiutil` tool.
