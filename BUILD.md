# Building Coeus

Verified 2026-10-12 on Linux Mint 22.3 (Ubuntu 24.04 base) with GCC 13.3, CMake 3.28, Ninja 1.11 and Qt 6.4.2.

## Dependencies

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools \
    libsqlite3-dev libmd4c-dev libmd4c-html0-dev libyaml-cpp-dev libtomlplusplus-dev libsodium-dev
```

The Phase 1 shell needs only the first line; the libraries on the second are Phase 2 dependencies (D-014 to D-018) and become required as each module lands.

| Requirement | Minimum | Why |
|-------------|---------|-----|
| GCC or Clang | GCC 13 / Clang 17 | C++23 with `std::expected` (D-013) |
| CMake | 3.25 | |
| Qt | 6.4 | Core, Concurrent, Gui, Widgets; Test for the test suite. Linked dynamically (D-009) |
| SQLite | 3.40 | FTS5 and extension loading enabled; C API, not Qt SQL (D-018) |
| md4c, md4c-html | 0.4.8 | Markdown parsing and reading mode (D-014) |
| yaml-cpp | 0.8 | Frontmatter (D-015) |
| toml++ | 3.4 | `.coeus/config.toml` (D-016) |
| libsodium | 1.0.18 | BLAKE2b-128 hashes; Phase 5 encryption (D-017) |

Phase 3 adds libgit2 and a vendored `sqlite-vec` amalgamation.

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

Documentation cross-references are checked by two ctest entries, `check_xrefs` and `test_check_xrefs` (Python 3, standard library only). To run the checker on its own, or to make it block commits with broken IDs:

```bash
python3 tools/check_xrefs.py            # errors and one-way references
python3 tools/check_xrefs.py --strict   # one-way references fail too
git config core.hooksPath tools/hooks   # enable the pre-commit hook in this clone
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
