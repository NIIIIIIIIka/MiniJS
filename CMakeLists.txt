cmake_minimum_required(VERSION 3.20)

project(
    MiniJSVM
    VERSION 0.1.0
    DESCRIPTION "A tiny JavaScript-like bytecode virtual machine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MINIJS_BUILD_TESTS "Build MiniJSVM tests" ON)
option(MINIJS_ENABLE_WARNINGS "Enable additional compiler warnings" ON)

function(minijs_enable_warnings target)
    if(NOT MINIJS_ENABLE_WARNINGS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Core sources are discovered while the project is still small. Headers are
# listed as target sources as well so IDE project trees display them.
file(GLOB_RECURSE MINIJS_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
file(GLOB_RECURSE MINIJS_CORE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)
list(REMOVE_ITEM MINIJS_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
)

if(MINIJS_CORE_SOURCES)
    add_library(minijs_core STATIC
        ${MINIJS_CORE_SOURCES}
        ${MINIJS_CORE_HEADERS}
    )
    target_include_directories(minijs_core
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_features(minijs_core PUBLIC cxx_std_17)
    if(MINIJS_BUILD_TESTS)
        target_compile_definitions(minijs_core PRIVATE MINIJS_TESTING)
    endif()
    minijs_enable_warnings(minijs_core)
else()
    # This keeps the initial empty project configurable. It becomes a static
    # library automatically after the first core .cpp file is added.
    add_library(minijs_core INTERFACE)
    target_include_directories(minijs_core
        INTERFACE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_features(minijs_core INTERFACE cxx_std_17)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
    add_executable(minijs src/main.cpp)
    target_link_libraries(minijs PRIVATE minijs_core)
    target_compile_definitions(minijs
        PRIVATE
            MINIJS_VERSION="${PROJECT_VERSION}"
    )
    minijs_enable_warnings(minijs)
endif()

if(MINIJS_BUILD_TESTS)
    include(CTest)
    enable_testing()

    file(GLOB_RECURSE MINIJS_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    )

    if(MINIJS_TEST_SOURCES)
        add_executable(minijs_tests ${MINIJS_TEST_SOURCES})
        target_link_libraries(minijs_tests PRIVATE minijs_core)
        target_compile_definitions(minijs_tests PRIVATE MINIJS_TESTING)
        minijs_enable_warnings(minijs_tests)

        add_test(NAME minijs_tests COMMAND minijs_tests)
    endif()

    if(TARGET minijs)
        add_test(
            NAME minijs_cli_default_uses_vm
            COMMAND minijs "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cli_vm_only.js"
        )
        set_tests_properties(minijs_cli_default_uses_vm PROPERTIES
            PASS_REGULAR_EXPRESSION "^0\n$"
        )

        add_test(
            NAME minijs_cli_run_uses_vm
            COMMAND minijs --run "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cli_vm_only.js"
        )
        set_tests_properties(minijs_cli_run_uses_vm PROPERTIES
            PASS_REGULAR_EXPRESSION "^0\n$"
        )

        add_test(
            NAME minijs_cli_interp_runs_ast_interpreter
            COMMAND minijs --interp "${CMAKE_CURRENT_SOURCE_DIR}/examples/basics.js"
        )
        set_tests_properties(minijs_cli_interp_runs_ast_interpreter PROPERTIES
            PASS_REGULAR_EXPRESSION "^30\n$"
        )

        add_test(
            NAME minijs_cli_bytecode_disassembles_program
            COMMAND minijs --bytecode "${CMAKE_CURRENT_SOURCE_DIR}/examples/objects_arrays.js"
        )
        set_tests_properties(minijs_cli_bytecode_disassembles_program PROPERTIES
            PASS_REGULAR_EXPRESSION "OP_SET_PROPERTY"
        )

        add_test(
            NAME minijs_cli_ic_stats_reports_method_cache_hits
            COMMAND minijs --ic-stats "${CMAKE_CURRENT_SOURCE_DIR}/examples/method_calls.js"
        )
        set_tests_properties(minijs_cli_ic_stats_reports_method_cache_hits PROPERTIES
            PASS_REGULAR_EXPRESSION "method call: hits=2 misses=1 updates=1 bypasses=0"
        )

        add_test(
            NAME minijs_cli_ic_stats_reports_polymorphic_method_cache_hits
            COMMAND minijs --ic-stats "${CMAKE_CURRENT_SOURCE_DIR}/examples/polymorphic_method_calls.js"
        )
        set_tests_properties(minijs_cli_ic_stats_reports_polymorphic_method_cache_hits PROPERTIES
            PASS_REGULAR_EXPRESSION "method call: hits=2 misses=2 updates=2 bypasses=0"
        )

        add_test(
            NAME minijs_cli_ic_stats_reports_polymorphic_static_method_cache_hits
            COMMAND minijs --ic-stats "${CMAKE_CURRENT_SOURCE_DIR}/examples/polymorphic_static_calls.js"
        )
        set_tests_properties(minijs_cli_ic_stats_reports_polymorphic_static_method_cache_hits PROPERTIES
            PASS_REGULAR_EXPRESSION "method call: hits=2 misses=2 updates=2 bypasses=0"
        )

        add_test(
            NAME minijs_cli_super_call_preserves_method_cache_boundary
            COMMAND minijs --ic-stats "${CMAKE_CURRENT_SOURCE_DIR}/examples/super_calls.js"
        )
        set_tests_properties(minijs_cli_super_call_preserves_method_cache_boundary PROPERTIES
            PASS_REGULAR_EXPRESSION "method call: hits=0 misses=1 updates=1 bypasses=0"
        )

        add_test(
            NAME minijs_cli_ic_stats_reports_polymorphic_property_reads
            COMMAND minijs --ic-stats "${CMAKE_CURRENT_SOURCE_DIR}/examples/polymorphic_property_reads.js"
        )
        set_tests_properties(minijs_cli_ic_stats_reports_polymorphic_property_reads PROPERTIES
            PASS_REGULAR_EXPRESSION "get property: hits=2 misses=2 updates=2 bypasses=0"
        )

        add_test(
            NAME minijs_cli_ic_stats_reports_polymorphic_property_writes
            COMMAND minijs --ic-stats "${CMAKE_CURRENT_SOURCE_DIR}/examples/polymorphic_property_writes.js"
        )
        set_tests_properties(minijs_cli_ic_stats_reports_polymorphic_property_writes PROPERTIES
            PASS_REGULAR_EXPRESSION "set property: hits=2 misses=2 updates=2 bypasses=0"
        )

        add_test(
            NAME minijs_cli_benchmark_reports_timing_and_ic_stats
            COMMAND minijs --benchmark "${CMAKE_CURRENT_SOURCE_DIR}/examples/inline_cache_benchmark.js"
        )
        set_tests_properties(minijs_cli_benchmark_reports_timing_and_ic_stats PROPERTIES
            PASS_REGULAR_EXPRESSION "run_us: [0-9]+"
        )

        add_test(
            NAME minijs_cli_benchmark_compare_reports_ic_on_and_off
            COMMAND minijs --benchmark-compare "${CMAKE_CURRENT_SOURCE_DIR}/examples/inline_cache_benchmark.js"
        )
        set_tests_properties(minijs_cli_benchmark_compare_reports_ic_on_and_off PROPERTIES
            PASS_REGULAR_EXPRESSION "without_ic:"
        )
    endif()
endif()
