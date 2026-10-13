find_package(Qt6 REQUIRED COMPONENTS Test)

function(coeus_add_test name source)
    qt_add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE coeus_core coeus_warnings Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    # Widget tests run without a display.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

coeus_add_test(test_vault vault/test_vault.cpp)

qt_add_executable(atomic_write_helper vault/atomic_write_helper.cpp)
target_link_libraries(atomic_write_helper PRIVATE coeus_core coeus_warnings)
coeus_add_test(test_atomic_writer vault/test_atomic_writer.cpp)
target_compile_definitions(test_atomic_writer PRIVATE
    ATOMIC_WRITE_HELPER="$<TARGET_FILE:atomic_write_helper>")
add_dependencies(test_atomic_writer atomic_write_helper)
coeus_add_test(test_temp_sweeper vault/test_temp_sweeper.cpp)
coeus_add_test(test_roots vault/test_roots.cpp)
coeus_add_test(test_git_ignore vault/test_git_ignore.cpp)
target_link_libraries(test_git_ignore PRIVATE PkgConfig::libgit2)
coeus_add_test(test_vault_watcher vault/test_vault_watcher.cpp)
coeus_add_test(test_gitignore_offer vault/test_gitignore_offer.cpp)
coeus_add_test(test_journal vault/test_journal.cpp)
target_link_libraries(test_gitignore_offer PRIVATE PkgConfig::libgit2)
coeus_add_test(test_note_parser parser/test_note_parser.cpp)
coeus_add_test(test_html_renderer parser/test_html_renderer.cpp)
coeus_add_test(test_database index/test_database.cpp)
coeus_add_test(test_indexer index/test_indexer.cpp)
coeus_add_test(test_rebuild_equivalence index/test_rebuild_equivalence.cpp)
coeus_add_test(test_index_service index/test_index_service.cpp)
coeus_add_test(test_fuzzy_match ui/test_fuzzy_match.cpp)
coeus_add_test(test_editor ui/test_editor.cpp)
coeus_add_test(test_note_view ui/test_note_view.cpp)
coeus_add_test(test_conflict_prompt ui/test_conflict_prompt.cpp)
coeus_add_test(test_main_window ui/test_main_window.cpp)
target_link_libraries(test_main_window PRIVATE PkgConfig::libgit2)
coeus_add_test(test_open_large_vault perf/test_open_large_vault.cpp)

# Documentation cross-references (IMP-001): broken or duplicate IDs fail.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME check_xrefs
             COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tools/check_xrefs.py --quiet ${PROJECT_SOURCE_DIR})
    add_test(NAME test_check_xrefs
             COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tools/test_check_xrefs.py)
endif()
