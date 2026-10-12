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
qt_standard_project_setup()

# Project-wide warning policy. Applied to Coeus targets only, never to Qt.
add_library(coeus_warnings INTERFACE)
target_compile_options(coeus_warnings INTERFACE -Wall -Wextra -Werror)

# Everything except main() lives in a static library so tests can link it.
add_library(coeus_core STATIC
    src/vault/AtomicWriter.cpp
    src/vault/TempSweeper.cpp
    src/vault/Vault.cpp
    src/vault/VaultLogging.cpp
    src/ui/CommandPalette.cpp
    src/ui/ExplorerPanel.cpp
    src/ui/FuzzyMatch.cpp
    src/ui/MainWindow.cpp
    src/ui/SidebarPanel.cpp
)
target_include_directories(coeus_core PUBLIC src)
target_link_libraries(coeus_core
    PUBLIC Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Widgets
    PRIVATE coeus_warnings)

qt_add_executable(coeus src/app/main.cpp)
target_compile_definitions(coeus PRIVATE COEUS_VERSION="${PROJECT_VERSION}")
target_link_libraries(coeus PRIVATE coeus_core coeus_warnings)

if(COEUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
