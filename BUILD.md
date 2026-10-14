# Building Coeus

Verified 2026-10-12 on Linux Mint 22.3 (Ubuntu 24.04 base) with GCC 13.3, CMake 3.28, Ninja 1.11 and Qt 6.4.2.

## Dependencies

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools \
    libsqlite3-dev libmd4c-dev libyaml-cpp-dev libtomlplusplus-dev libsodium-dev libgit2-dev
```

All of these are required to build.

| Requirement | Minimum | Why |
|-------------|---------|-----|
| GCC or Clang | GCC 13 / Clang 17 | C++23 with `std::expected` (D-013) |
| CMake | 3.25 | |
| Qt | 6.4 | Core, Concurrent, Gui, Widgets; Test for the test suite. Linked dynamically (D-009) |
| SQLite | 3.40 | FTS5 and extension loading enabled; C API, not Qt SQL (D-018) |
| md4c | 0.4.8 | Markdown parsing for the index and reading mode (D-014) |
| yaml-cpp | 0.8 | Frontmatter (D-015) |
| toml++ | 3.4 | `.coeus/config.toml` (D-016) |
| libsodium | 1.0.18 | BLAKE2b-128 hashes; Phase 5 encryption (D-017) |
| libgit2 | 1.7 | `.gitignore` rules per root; Git history, diff, blame, status and commits |
| git (run time, optional) | any | Only for push after a commit (D-020); everything else works without it |

Phase 3 adds a vendored `sqlite-vec` amalgamation.

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

Coeus's own registers and documents are linted by the app itself: the `lint_coeus_docs` ctest entry runs `coeus --lint --mentions --quiet` on the repository and fails on any error (SPEC.md §Lint rules). To run it on its own, or to make it block commits with broken IDs:

```bash
./build/coeus --lint --mentions .            # errors, warnings (one-way references) and info
./build/coeus --lint --mentions --strict .   # warnings fail too
git config core.hooksPath tools/hooks        # enable the pre-commit hook in this clone
```

`--lint` opens no window and needs no display. The hook runs `build/coeus` (or `$COEUS_BIN`), so build before the first commit in a fresh clone; `git commit --no-verify` skips it.

## Sanitizers

A separate tree builds everything with AddressSanitizer and UndefinedBehaviorSanitizer (D-002 relies on them for memory safety). Run it after changes to object lifetimes, threads or widgets:

```bash
cmake -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCOEUS_BUILD_TESTS=ON \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan -j
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure
```

Leak detection is off because Qt and the GPU stack hold allocations until exit. A test that hangs instead of failing in the normal build may be heap corruption whose abort deadlocked; run it here.

## Demo vault

`tools/make_demo_vault.py` builds a vault that exercises every feature so far, with a linked root, a missing root, attachments, every link form, tags, properties with vocabulary errors and broken YAML, and a "Start here" note listing what to try:

```bash
python3 tools/make_demo_vault.py              # ~/CoeusDemo/vault and ~/CoeusDemo/pontus
./build/coeus ~/CoeusDemo/vault
python3 tools/make_demo_vault.py --force      # reset it after experimenting
python3 tools/make_demo_vault.py --bulk 2000  # add 2,000 synthetic notes
```

`--force` deletes the target only if this script made it (it leaves a `.coeus-demo` marker). The script uses the Python standard library and `git init`, if Git is installed, so the `.gitignore` offer and ignore rules can be tried.

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
