find_package(Qt6 REQUIRED COMPONENTS Test)

function(coeus_add_test name source)
    qt_add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE coeus_core coeus_warnings Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    # Widget tests run without a display.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

coeus_add_test(test_vault vault/test_vault.cpp)
coeus_add_test(test_fuzzy_match ui/test_fuzzy_match.cpp)
coeus_add_test(test_main_window ui/test_main_window.cpp)
coeus_add_test(test_open_large_vault perf/test_open_large_vault.cpp)
