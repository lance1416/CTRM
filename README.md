# CTRM: Cheesy Toast Rename Manager


## Development

### Prerequisites

- CMake 3.24 or later, Ninja, a C++20 compiler, Qt 6.8 or later (Core, Gui, Widgets, Svg).
- clang-format 23 (CI uses this version; other versions can format differently).
- editorconfig-checker 4 (the hooks and CI use it).

macOS (Homebrew, with Xcode or the Command Line Tools):

```bash
brew install qtbase qtsvg cmake ninja clang-format editorconfig-checker shellcheck
```

Linux (Debian or Ubuntu): install `cmake ninja-build g++ libgl-dev libxkbcommon-dev`.
Get Qt 6.8 or later from your distribution (`qt6-base-dev qt6-svg-dev`) if it is new
enough, or from the Qt online installer or [aqtinstall](https://github.com/miurahr/aqtinstall).
Get clang-format with `pipx install clang-format==23.*` and editorconfig-checker
from its [releases](https://github.com/editorconfig-checker/editorconfig-checker/releases).

Windows: install Visual Studio 2022 (Desktop development with C++), Git for Windows,
CMake, Ninja and Qt 6.8 or later (MSVC 2022 64-bit). Get clang-format with
`pip install clang-format==23.*`, and editorconfig-checker from its releases. Use a "Developer PowerShell for VS 2022" and add
the Qt `bin` folder to `PATH` or set `CMAKE_PREFIX_PATH` to the Qt folder.

### Set up the clone

Run this one time after you clone. It enables the git hooks in `.githooks/` and sets
local git options for linear history (rebase on pull, fast-forward merges only).

```bash
scripts/setup-dev.sh
```

On Windows, run `scripts/setup-dev.ps1` in PowerShell.

### Build and test

```bash
cmake --preset dev
```

```bash
cmake --build --preset dev
```

```bash
ctest --preset dev
```

Presets: `dev` (Debug, writes `compile_commands.json` for clangd), `release`, `ci`
(warnings are errors), `perf` (Release; `ctest --preset perf -L perf` runs the timing
tests, which skip in Debug builds). Build output goes to `build/<preset>/`.

### Commit rules

- Conventional Commits, subject line only, 72 characters or less, no body or trailers.
- New source files start with `SPDX-License-Identifier: GPL-3.0-or-later`.
- Linear history: rebase, no merge commits.

The `commit-msg` and `pre-commit` hooks check these rules, EditorConfig and clang-format.
To run the CI lint checks on all files:

```bash
scripts/lint.sh
```
