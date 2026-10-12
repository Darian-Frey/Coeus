cmake_minimum_required(VERSION 3.25)

project(Coeus
    VERSION 0.1.0
    DESCRIPTION "Local-first Markdown knowledge base"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)              # D-013
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(COEUS_BUILD_TESTS "Build the Coeus test suite" OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Concurrent Gui Widgets)
find_package(md4c REQUIRED)        # D-014
find_package(yaml-cpp REQUIRED)    # D-015
find_package(SQLite3 3.40 REQUIRED) # D-018
find_package(PkgConfig REQUIRED)
pkg_check_modules(sodium REQUIRED IMPORTED_TARGET libsodium>=1.0.18) # D-017
pkg_check_modules(libgit2 REQUIRED IMPORTED_TARGET libgit2>=1.7)     # .gitignore rules; Git features in Phase 3
qt_standard_project_setup()

# Project-wide warning policy. Applied to Coeus targets only, never to Qt.
add_library(coeus_warnings INTERFACE)
target_compile_options(coeus_warnings INTERFACE -Wall -Wextra -Werror)

# Everything except main() lives in a static library so tests can link it.
add_library(coeus_core STATIC
    src/index/ContentHash.cpp
    src/index/Database.cpp
    src/index/Indexer.cpp
    src/index/IndexService.cpp
    src/index/IndexStore.cpp
    src/index/LinkResolver.cpp
    src/index/Schema.cpp
    src/parser/Frontmatter.cpp
    src/parser/NoteParser.cpp
    src/vault/AtomicWriter.cpp
    src/vault/GitIgnore.cpp
    src/vault/Roots.cpp
    src/vault/TempSweeper.cpp
    src/vault/Vault.cpp
    src/vault/VaultLogging.cpp
    src/vault/VaultWatcher.cpp
    src/ui/CommandPalette.cpp
    src/ui/ExplorerPanel.cpp
    src/ui/FuzzyMatch.cpp
    src/ui/MainWindow.cpp
    src/ui/SidebarPanel.cpp
)
target_include_directories(coeus_core PUBLIC src)
target_link_libraries(coeus_core
    PUBLIC Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Widgets
    PRIVATE md4c::md4c yaml-cpp::yaml-cpp SQLite::SQLite3 PkgConfig::sodium PkgConfig::libgit2 coeus_warnings)

qt_add_executable(coeus src/app/main.cpp)
target_compile_definitions(coeus PRIVATE COEUS_VERSION="${PROJECT_VERSION}")
target_link_libraries(coeus PRIVATE coeus_core coeus_warnings)

if(COEUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
