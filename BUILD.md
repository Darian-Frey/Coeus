# Building Coeus

Verified 2026-10-12 on Linux Mint 22.3 (Ubuntu 24.04 base) with GCC 13.3, CMake 3.28, Ninja 1.11 and Qt 6.4.2.

## Dependencies

Phase 1 needs only a compiler, CMake and Qt:

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools
```

| Requirement | Minimum | Why |
|-------------|---------|-----|
| GCC or Clang | GCC 13 / Clang 17 | C++23 with `std::expected` (D-013) |
| CMake | 3.25 | |
| Qt | 6.4 | Core, Gui, Widgets; Test for the test suite. Linked dynamically (D-009) |

Later phases add SQLite with FTS5 and `sqlite-vec` (Phase 2–3) and libgit2 (Phase 3). This file will list them when they are first used.

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCOEUS_BUILD_TESTS=ON
cmake --build build -j
```

Omit `-G Ninja` to use Makefiles. Every Coeus target compiles with `-Wall -Wextra -Werror`; a warning is a build failure. `compile_commands.json` is written to `build/` for clangd.

## Test

```bash
ctest --test-dir build --output-on-failure
```

Tests set `QT_QPA_PLATFORM=offscreen` themselves, so they run without a display. `test_open_large_vault` generates a 1,000-file vault and checks the F-002 tree budget of 500 ms; it prints the measured time when run directly:

```bash
./build/tests/test_open_large_vault
```

## Run

```bash
./build/coeus ~/notes          # open a vault
./build/coeus                  # start empty; File › Open Vault… (Ctrl+O)
./build/coeus --verbose ~/notes
```

`Ctrl+P` opens the command palette. Window geometry and dock layout are saved to `~/.config/coeus/coeus.ini` on exit; delete that file to reset the layout.

## Large vaults and inotify

Coeus watches directories, not files, but a vault plus linked roots can still exceed the default inotify limit. If the explorer stops noticing new files, raise it:

```bash
cat /proc/sys/fs/inotify/max_user_watches
echo 'fs.inotify.max_user_watches=524288' | sudo tee /etc/sysctl.d/90-coeus-inotify.conf
sudo sysctl --system
```
