# Building Coeus

Verified 2026-10-12 on Linux Mint 22.3 (Ubuntu 24.04 base) with GCC 13.3, CMake 3.28, Ninja 1.11 and Qt 6.4.2.

## Dependencies

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools qt6-svg-dev \
    libsqlite3-dev libmd4c-dev libyaml-cpp-dev libtomlplusplus-dev libsodium-dev libgit2-dev
```

All of these are required to build. CMake also downloads llama.cpp's source (release b10926, checked against its SHA-256) on the first configure and builds it with Coeus, which adds about 90 seconds to a clean build; it runs the embedding model (D-008). `qt6-svg-dev` brings Qt's SVG image plugins, which draw the tool strip's own icons (`resources/icons/`) where the desktop's icon theme has none; without them those buttons show their names instead.

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
| llama.cpp | b10926 | Fetched and built by CMake; runs the embedding model (D-008) |

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

## Embedding model

Semantic search needs bge-small-en-v1.5 (67 MB), which is not in the repository. Fetch it once per machine:

```bash
python3 tools/fetch_model.py      # into ~/.local/share/coeus/models/, SHA-256 checked
```

Without it everything else works, and `test_embedder` skips.

## Run

```bash
./build/coeus ~/notes          # open a vault
./build/coeus                  # start empty; File › Open Vault… (Ctrl+O)
./build/coeus --verbose ~/notes
```

`Ctrl+P` opens the command palette. Window geometry and dock layout are saved to `~/.config/coeus/coeus.ini` on exit; delete that file to reset the layout.

## MCP server

`coeus --mcp --vault <folder>` (or `COEUS_VAULT=<folder> coeus --mcp`) serves the vault's index to an AI session over stdio (F-033, SPEC.md §MCP surface). Claude Code is the client it is built for; a local model through LM Studio is optional (D-021). It needs no display and works whether or not the Coeus window is open; it keeps the same `.coeus/index.db` up to date, scanning for changes before answering. Use the absolute path of the built binary.

**Claude Code** (in a terminal, or in an IDE such as Antigravity through its Claude Code extension), once per machine, for every project on it:

```bash
claude mcp add --scope user coeus -- "$HOME/Projects/Coeus/build/coeus" --mcp --vault "$HOME/Projects"
claude mcp list          # coeus should show as connected
claude mcp remove coeus -s user   # to undo
```

The shell expands `$HOME` when the command is run, so the registration holds this machine's absolute paths; run it again on each machine, after building Coeus there. A session started before the registration does not see the tools until it is restarted.

Inside a session, `/mcp` lists the server and its tools.

**Claude Desktop**, in `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "coeus": {
      "command": "/home/you/Projects/Coeus/build/coeus",
      "args": ["--mcp", "--vault", "/home/you/Vault"]
    }
  }
}
```

**LM Studio** (optional, D-021), in `~/.lmstudio/mcp.json` (Program tab › Install › Edit mcp.json), the same shape as Claude Desktop's; use a model marked for tool use, such as Qwen3.5 9B, and raise its context length to 16k–32k tokens, since a register file can be 8k tokens on its own:

```json
{ "mcpServers": { "coeus": { "command": "/home/you/Projects/Coeus/build/coeus", "args": ["--mcp", "--vault", "/home/you/Projects"] } } }
```

Claude in a web browser (claude.ai) cannot start a local program, so it cannot use a stdio server; it would need Coeus to serve MCP over HTTP, which it does not.

**Writing.** The write tools (`next_id`, `append_entry`, `write_note`) refuse until the vault allows them, in `<vault>/.coeus/config.toml`:

```toml
[mcp]
allow_writes = true
```

It is read on every call, so deleting it turns writing off at once. With the Coeus window open on the vault, a note or register open with unsaved changes is never written, and each write shows a toast with Undo for 10 seconds.

To try the protocol by hand:

```bash
printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"project_status","arguments":{}}}' \
  | ./build/coeus --mcp --vault ~/Vault
```

## Large vaults and inotify

Coeus watches directories, not files, but a vault plus linked roots can still exceed the default inotify limit. If the explorer stops noticing new files, raise it:

```bash
cat /proc/sys/fs/inotify/max_user_watches
echo 'fs.inotify.max_user_watches=524288' | sudo tee /etc/sysctl.d/90-coeus-inotify.conf
sudo sysctl --system
```
