cmake_minimum_required(VERSION 3.25)

project(Coeus
    VERSION 0.1.0
    DESCRIPTION "Local-first Markdown knowledge base"
    LANGUAGES C CXX)   # C for ggml, under llama.cpp

set(CMAKE_CXX_STANDARD 23)              # D-013
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(COEUS_BUILD_TESTS "Build the Coeus test suite" OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Concurrent Gui Network Widgets)
find_package(md4c REQUIRED)        # D-014
find_package(yaml-cpp REQUIRED)    # D-015
find_package(SQLite3 3.40 REQUIRED) # D-018
find_package(PkgConfig REQUIRED)
pkg_check_modules(sodium REQUIRED IMPORTED_TARGET libsodium>=1.0.18) # D-017
pkg_check_modules(libgit2 REQUIRED IMPORTED_TARGET libgit2>=1.7)     # .gitignore rules; Git features in Phase 3
qt_standard_project_setup()

# Semantic search (D-008): llama.cpp runs the embedding model, bge-small-en-v1.5,
# on the CPU. Built from source at a pinned release, verified by hash, as a
# static library with its tools, examples, server and HTTPS left out; tuned
# for the CPU that builds it, and optimised even in a Debug build, where it
# would otherwise be too slow to embed a vault. The model file itself is not
# in the repository: tools/fetch_model.py downloads it.
include(FetchContent)
set(BUILD_SHARED_LIBS OFF)
foreach(option LLAMA_BUILD_COMMON LLAMA_BUILD_TESTS LLAMA_BUILD_TOOLS LLAMA_BUILD_EXAMPLES LLAMA_BUILD_SERVER
               LLAMA_BUILD_APP LLAMA_OPENSSL LLAMA_ALL_WARNINGS)
    set(${option} OFF CACHE BOOL "Set by Coeus" FORCE)
endforeach()
set(GGML_NATIVE ON CACHE BOOL "Set by Coeus" FORCE)
set(GGML_CCACHE OFF CACHE BOOL "Set by Coeus" FORCE)
FetchContent_Declare(llama
    URL https://github.com/ggml-org/llama.cpp/archive/refs/tags/b10926.tar.gz
    URL_HASH SHA256=7eb66cd46bc6b5a64149588c7d1025000092996df6a73355873fc6eb00863a82
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)                         # its headers are not held to Coeus's -Werror
FetchContent_MakeAvailable(llama)
foreach(target ggml ggml-base ggml-cpu llama)
    if(TARGET ${target})
        target_compile_options(${target} PRIVATE -O2)
    endif()
endforeach()

# Project-wide warning policy. Applied to Coeus targets only, never to Qt.
add_library(coeus_warnings INTERFACE)
target_compile_options(coeus_warnings INTERFACE -Wall -Wextra -Werror)

# Everything except main() lives in a static library so tests can link it.
add_library(coeus_core STATIC
    src/index/Backlinks.cpp
    src/index/ContentHash.cpp
    src/index/Database.cpp
    src/index/Indexer.cpp
    src/index/IndexQueries.cpp
    src/index/IndexRoots.cpp
    src/index/IndexService.cpp
    src/index/IndexStore.cpp
    src/index/LinkResolver.cpp
    src/index/ProjectResolver.cpp
    src/git/GitCommand.cpp
    src/git/GitRepository.cpp
    src/index/LintResolver.cpp
    src/mcp/GuiLink.cpp
    src/mcp/McpServer.cpp
    src/mcp/McpTools.cpp
    src/index/Schema.cpp
    src/index/Search.cpp
    src/index/SearchQuery.cpp
    src/index/Tags.cpp
    src/parser/Frontmatter.cpp
    src/parser/HtmlRenderer.cpp
    src/parser/NoteParser.cpp
    src/parser/RegisterParser.cpp
    src/registers/EntryWriter.cpp
    src/search/Chunker.cpp
    src/search/Embedder.cpp
    src/search/EmbeddingService.cpp
    src/search/VectorStore.cpp
    src/search/Sidecar.cpp
    src/registers/Lint.cpp
    src/registers/LintCommand.cpp
    src/registers/NextId.cpp
    src/registers/ProjectOverview.cpp
    src/registers/ProblemReport.cpp
    src/registers/ProjectScaffold.cpp
    src/registers/ProjectStatus.cpp
    src/registers/RegisterTemplates.cpp
    src/registers/Vocabulary.cpp
    src/vault/AtomicWriter.cpp
    src/vault/DailyNotes.cpp
    src/vault/GitIgnore.cpp
    src/vault/GitIgnoreOffer.cpp
    src/vault/Journal.cpp
    src/vault/Roots.cpp
    src/vault/TempSweeper.cpp
    src/vault/Templates.cpp
    src/vault/NewNote.cpp
    src/vault/Vault.cpp
    src/vault/VaultConfig.cpp
    src/vault/VaultLogging.cpp
    src/vault/VaultWatcher.cpp
    src/ui/BacklinksPanel.cpp
    src/ui/CommandPalette.cpp
    src/ui/CommitDialog.cpp
    src/ui/DiffView.cpp
    src/ui/Editor.cpp
    src/ui/ExplorerPanel.cpp
    src/ui/FuzzyMatch.cpp
    src/ui/MainWindow.cpp
    src/ui/MarkdownHighlighter.cpp
    src/ui/McpLink.cpp
    src/ui/NoteCatalogue.cpp
    src/ui/NoteView.cpp
    src/ui/OutlinePanel.cpp
    src/ui/PropertiesPanel.cpp
    src/ui/HistoryPanel.cpp
    src/ui/Icons.cpp
    src/ui/ProblemsPanel.cpp
    src/ui/PromoteDialog.cpp
    src/ui/NewNoteDialog.cpp
    src/ui/Shortcuts.cpp
    src/ui/ShortcutsDialog.cpp
    src/ui/Theme.cpp
    src/ui/ProjectBoard.cpp
    src/ui/RegisterPanel.cpp
    src/ui/RelatedPanel.cpp
    src/ui/SearchPanel.cpp
    src/ui/SidebarPanel.cpp
    src/ui/TagPanel.cpp
    src/ui/VersionView.cpp
    src/ui/WikilinkCompleter.cpp
    src/ui/WriteToast.cpp
    src/ui/WikilinkCompletion.cpp
)
target_include_directories(coeus_core PUBLIC src)
# The tool strip's own icons, used where the desktop's icon theme has none (ui/Icons).
qt_add_resources(coeus_core "icons" PREFIX "/icons" BASE resources/icons
    FILES resources/icons/explorer.svg resources/icons/search.svg resources/icons/board.svg
          resources/icons/tags.svg resources/icons/palette.svg resources/icons/sidebar.svg)
target_link_libraries(coeus_core
    PUBLIC Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Network Qt6::Widgets
    PRIVATE md4c::md4c yaml-cpp::yaml-cpp SQLite::SQLite3 PkgConfig::sodium PkgConfig::libgit2 llama coeus_warnings)

qt_add_executable(coeus src/app/main.cpp)
target_compile_definitions(coeus PRIVATE COEUS_VERSION="${PROJECT_VERSION}")
target_compile_definitions(coeus_core PRIVATE COEUS_VERSION="${PROJECT_VERSION}") # the MCP server reports it
target_link_libraries(coeus PRIVATE coeus_core coeus_warnings)

if(COEUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
