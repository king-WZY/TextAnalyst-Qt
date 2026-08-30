# TextAnalyst-Qt

**High-performance text log filtering and marking tool for Linux desktops.**

A modern Qt 6 re-implementation of [TextAnalysisTool.NET](https://dlaa.me/)
(TAT) — opens multi-GiB log files instantly, filters and highlights them with
substring/regex rules, and stays drop-in compatible with the original
`.tat` filter format.

> Inspired by [TextAnalysisTool.NET](https://dlaa.me/) by David Anson —
> this project reimplements its filtering workflow and `.tat` format
> on Qt 6 / Ubuntu.

[![CI](https://github.com/king-WZY/TextAnalyst-Qt/actions/workflows/ci.yml/badge.svg)](https://github.com/king-WZY/TextAnalyst-Qt/actions/workflows/ci.yml)
![Platform](https://img.shields.io/badge/platform-Ubuntu%2022.04%20%7C%2024.04-E95420)
![Language](https://img.shields.io/badge/language-C%2B%2B17-00599C)
![UI](https://img.shields.io/badge/UI-Qt6%20Widgets-41CD52)
![License](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)
![Version](https://img.shields.io/badge/version-1.1.0-informational)

---

## Table of Contents

1. [Features](#features)
2. [Installation (deb)](#installation-deb)
3. [Build from Source](#build-from-source)
4. [Packaging (deb)](#packaging-deb)
5. [Usage](#usage)
   - [GUI](#gui)
   - [Command Line](#command-line)
   - [Keyboard](#keyboard)
6. [Architecture](#architecture)
7. [Performance](#performance)
8. [Logging](#logging)
9. [Testing](#testing)
10. [Known Limitations & Roadmap](#known-limitations--roadmap)
11. [License](#license)

---

## Features

- **Instant open** — `mmap` snapshot + lazy byte-offset line index: a 1 GiB
  file opens in **~140 ms** (measured), with memory bounded by file size +
  index (≈8 B/line).
- **Encoding autodetect** — BOM → strict UTF-8 validation → iconv (GB18030/
  GBK/…) → locale fallback; the index always stays UTF-8 byte-based.
- **Filter engine** — include/exclude rules, substring or regex
  (PCRE2-backed `QRegularExpression`), case sensitivity, whole-word match,
  per-rule colors, live match counts, `.tat` import/export.
- **8 line markers** — `Ctrl+1..8` toggle, `Alt+1..8` circular jump,
  persisted across sessions.
- **Smooth UI** — virtualized list view (`QListView` uniform-item-size fast
  path), lock-free double-buffered filter results, background indexing and
  matching with generation-based cancellation. The GUI thread never does
  I/O or regex.
- **CLI automation** — `--file --grep --line --load --export` for
  scriptable log extraction.
- **TextAnalysisTool.NET compatibility** — reads and writes `.tat` filter
  files (superset: optional `word`/`rank`/`match`/`desc` attributes only).

## Installation (deb)

Download `textanalyst-qt_<version>_amd64.deb` from
[Releases](https://github.com/king-WZY/TextAnalyst-Qt/releases), then:

```bash
sudo apt install ./textanalyst-qt_1.1.0_amd64.deb
# or:  sudo dpkg -i textanalyst-qt_1.1.0_amd64.deb
textanalyst-qt            # launch
textanalyst-qt app.log    # open a file directly
```

Package name is multi-arch aware across Ubuntu 22.04/24.04
(`libqt6core6 | libqt6core6t64` dependencies).

Uninstall: `sudo apt remove textanalyst-qt`

## Build from Source

### Dependencies (Ubuntu 22.04 / 24.04)

```bash
sudo apt install -y build-essential cmake \
  qt6-base-dev qt6-base-dev-tools \
  fonts-noto-cjk          # CJK glyphs (suggests)
```

`core` (BLL+DAL) is Qt-free by design (C++17 + POSIX only) and builds with
just `g++` — see `tools/check-core.sh`.

### Build

```bash
./build.sh Release          # CMake (Ninja or Make) + full test suite
build/Release/src/textanalyst-qt --version
```

### Test

```bash
ctest --test-dir build/Release --output-on-failure   # 8 test binaries
tools/check-core.sh                                    # core-only (no Qt)
tools/check-core.sh --asan                             # ASan/UBSan core
tools/check-core.sh --bench 256                        # perf smoke
build/Release/tests/bench_1gb --size 1024              # full 1 GiB run
```

CI matrix (GitHub Actions): `ubuntu-22.04` + `ubuntu-24.04`, fail on
perf regressions > 2× baseline.

## Packaging (deb)

```bash
./packaging/deb/build-deb.sh Release
```

Produces `build/Release/textanalyst-qt_1.1.0_amd64.deb` via CPack DEB —
**no root required**. The package ships:

| Path | Content |
| :--- | :--- |
| `/usr/bin/textanalyst-qt` | application binary |
| `/usr/share/applications/*.desktop` | launcher (EN/zh_CN, URL handling) |
| `/usr/share/icons/hicolor/.../*.svg` | application icon |
| `/usr/share/mime/packages/*.xml` | `text/x-log` MIME registration |
| `/usr/share/man/man1/textanalyst-qt.1` | man page |
| `/usr/share/doc/textanalyst-qt/copyright` | GPL-3.0-or-later |

## Usage

### GUI

- **Open** — toolbar, `Ctrl+O`, or drag & drop a `.log`/`.txt` file.
- **Filter rules live in the `Filters` menu** (TAT original):
  `F8`/`Shift+F8` jump between matching lines, `Ctrl+N` adds a rule,
  Edit/Remove act on the selected rule, plus Enable/Disable/Remove-All.
  Editing happens in a dialog (Filter / Text Color / Background
  dropdowns + Text + Description + Excluding/Case/Regex checkboxes) —
  double-click a log line to pre-fill it; the dialog is destroyed on
  confirmation. The bottom panel is a plain rule list (no buttons).
  Rules are evaluated **in list order**: the first matching rule wins
  (Exclude → the row is folded away entirely, color rule → highlight).
  New rules default to color (Include); tick *Excluding [!]* to hide.
  `Ctrl+H` toggles "show only filtered lines" (View menu). Original
  line numbers are always displayed for locating raw file positions.
- **Double-click a line** — turns that line into a filter rule.
- **Find** (`Ctrl+F`) — substring or regex, `F3`-style navigation.
- **Export** — writes visible lines (hidden/dimmed rows are skipped).
- **Save/Load rules** — `.tat` format (TextAnalysisTool.NET compatible),
  atomic replace with `.bak` backup.

### Command Line

```
textanalyst-qt [options] [file]

  -h, --help            Show help
  -V, --version         Show version
  -f, --file <path>     Open file (overrides positional)
  --line <n>            Jump to line n
  --grep <pattern>      Filter to matching lines
  --load <file.tat>     Apply rules from a .tat file
  --export <file>       Export visible lines
  --no-restore          Skip last-session restore
  --verbose             Verbose stderr logging
  --compact-memory      Enable MADV_DONTNEED unloading (experimental)
```

Example:

```bash
textanalyst-qt --file app.log --grep "ERROR|FATAL" --export errors.txt
```

### Keyboard

| Shortcut | Action |
| :--- | :--- |
| `Ctrl+O` / `Ctrl+S` | open file / save rules |
| `Ctrl+F` | find (`F3` / `Shift+F3` cycle through hits) |
| `F8` / `Shift+F8` | next / previous filter match |
| `Ctrl+N` | add new filter rule |
| `Ctrl+1..8` | toggle marker 1–8 on current line |
| `Alt+1..8` | jump to next marker of that color (circular) |
| `Ctrl+Home` / `Ctrl+End` | first / last line |
| `Esc` | cancel running filter/search |

## Architecture

Four layers, dependencies strictly top-down (UI → Controller → BLL → DAL):

```
src/ui/          Qt Widgets view layer
src/controller/  MainController mediator, CLI
src/io/          .tat / settings / inotify / log rotation
src/core/engine  BLL: FilterEngine, Searcher, MarkerManager  (Qt-free)
src/core/buffer  DAL: mmap, encoding detect, line index      (POSIX-only)
```

Key decisions (full rationale in the design docs):

- **`SharedSnapshot<T>`** lock-free publish for the line buffer and filter
  results (C++17-compatible `shared_ptr` atomic load/store; readers never
  see freed memory).
- **Double-buffer + generation tokens** — rule edits cancel in-flight
  matches; workers check every 8 KB.
- **Sorted-vector markers** — `std::unordered_set` iteration order can't
  express "next greater line no.", so markers live in ordered vectors
  (O(log n), <1 ms at 100M rows).
- **White-list-first state machine** — include hit ⇒ highlight, include
  miss ⇒ dim, exclude hit ⇒ hide (only when no include rules exist).

Docs (design/architecture set, grouped under `docs/`):

| Document | Scope |
| :--- | :--- |
| [01-blueprint.md](docs/architecture/01-blueprint.md) | concept blueprint (legacy) |
| [02-system-architecture.md](docs/architecture/02-system-architecture.md) | platform/threading/packaging constraints |
| [03-detailed-design.md](docs/architecture/03-detailed-design.md) | interface-level design + review rulings |
| [BUILD.md](docs/BUILD.md) | build & testing manual |

## Project Layout

```
src/core/engine   FilterEngine / Searcher / MarkerManager   (Qt-free BLL)
src/core/buffer   mmap, encoding detect, line index         (POSIX-only DAL)
src/io            .tat serializer, settings, inotify, log rotation
src/controller    MainController mediator, CLI
src/ui            Qt Widgets views, delegate, dialogs
packaging/deb     desktop entry, icon, man page, CPack config
tests/            Qt-free unit tests + QtTest integration + bench
docs/architecture blueprint / system architecture / detailed design
```

## Performance

Measured on Ubuntu 24.04, 8 vCPU, NVMe (see `bench_1gb`):

| Metric | Measured | Target |
| :--- | ---: | ---: |
| Open 1 GiB (mmap + index, 9.4M rows) | **139 ms** | < 500 ms |
| Full-file filter (substring, 8 threads) | **239 ms** | < 3 s @ 100M rows |
| RSS after 1 GiB run | **1.1 GiB** | ≤ 1.4 GiB |
| Marker jump @ 100k markers | < 0.1 ms | < 1 ms |

## Logging

Runtime events (file opens, filter/export results, warnings) are appended
to `~/.cache/textanalyst-qt/textanalyst-qt.log` with automatic rotation:
**4 MiB per file × 4 backups = 20 MiB maximum** retained. Debug messages
go to stderr only. See the man page or the detailed design (§11.2) for the
log format.

## Known Limitations & Roadmap

v1.0/1.1 boundaries:

- Regex "precompilation" is currently a cache key (Qt 6 has no public
  regex serialization) — **v1.1** switches to the PCRE2 C API.
- UTF-16 files are indexed byte-wise (`0D 00 0A 00` splits lines) —
  **v1.1** code-unit indexing.
- Files > 4 GiB trigger a segmented fallback (interface reserved) —
  **v1.1** full implementation.
- `MADV_DONTNEED` unloading is opt-in only (conflicts with lock-free
  rendering invariants).

Full list: [03-detailed-design.md §13](docs/architecture/03-detailed-design.md).

## License

GPL-3.0-or-later — see [LICENSE](LICENSE). The Debian copyright notice is
shipped in the package at `/usr/share/doc/textanalyst-qt/copyright`.
