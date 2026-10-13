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

function(minijs_copy_mingw_runtime_dlls target)
    if(NOT WIN32 OR NOT MINGW)
        return()
    endif()

    get_filename_component(MINIJS_MINGW_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(MINIJS_MINGW_DLL
            libstdc++-6.dll
            libgcc_s_seh-1.dll
            libwinpthread-1.dll
    )
        if(EXISTS "${MINIJS_MINGW_RUNTIME_DIR}/${MINIJS_MINGW_DLL}")
            add_custom_command(
                TARGET ${target}
                POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${MINIJS_MINGW_RUNTIME_DIR}/${MINIJS_MINGW_DLL}"
                    "$<TARGET_FILE_DIR:${target}>/${MINIJS_MINGW_DLL}"
                VERBATIM
            )
        endif()
    endforeach()
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
    minijs_copy_mingw_runtime_dlls(minijs)
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
            NAME minijs_cli_benchmark_accepts_sampling_options
            COMMAND minijs --benchmark "${CMAKE_CURRENT_SOURCE_DIR}/examples/inline_cache_benchmark.js" --benchmark-samples 2 --benchmark-warmup 1
        )
        set_tests_properties(minijs_cli_benchmark_accepts_sampling_options PROPERTIES
            PASS_REGULAR_EXPRESSION "warmup_samples: 1"
        )

        add_test(
            NAME minijs_cli_benchmark_reports_json_format
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_jit_benchmark.js" --benchmark-samples 1 --benchmark-format json
        )
        set_tests_properties(minijs_cli_benchmark_reports_json_format PROPERTIES
            PASS_REGULAR_EXPRESSION "\"with_jit\""
        )

        add_test(
            NAME minijs_cli_benchmark_compare_reports_ic_on_and_off
            COMMAND minijs --benchmark-compare "${CMAKE_CURRENT_SOURCE_DIR}/examples/inline_cache_benchmark.js"
        )
        set_tests_properties(minijs_cli_benchmark_compare_reports_ic_on_and_off PROPERTIES
            PASS_REGULAR_EXPRESSION "without_ic:"
        )

        add_test(
            NAME minijs_cli_benchmark_compare_reports_jit_on_and_off
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_jit_benchmark.js"
        )
        set_tests_properties(minijs_cli_benchmark_compare_reports_jit_on_and_off PROPERTIES
            PASS_REGULAR_EXPRESSION "baseline_entries=[1-9][0-9]*"
        )
        add_test(
            NAME minijs_cli_numeric_benchmark_enters_baseline
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_numeric_benchmark.js" --benchmark-samples 1 --benchmark-warmup 0
        )
        set_tests_properties(minijs_cli_numeric_benchmark_enters_baseline PROPERTIES
            PASS_REGULAR_EXPRESSION "baseline_entries=[1-9][0-9]*"
        )
        add_test(
            NAME minijs_cli_boolean_benchmark_enters_baseline
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_boolean_benchmark.js" --benchmark-samples 1 --benchmark-warmup 0
        )
        set_tests_properties(minijs_cli_boolean_benchmark_enters_baseline PROPERTIES
            PASS_REGULAR_EXPRESSION "baseline_entries=[1-9][0-9]*"
        )
        add_test(
            NAME minijs_cli_benchmark_helper_profile_json
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_boolean_benchmark.js" --benchmark-samples 1 --benchmark-helper-profile on --benchmark-format json
        )
        set_tests_properties(minijs_cli_benchmark_helper_profile_json PROPERTIES
            PASS_REGULAR_EXPRESSION "helper_profile"
        )
        add_test(
            NAME minijs_cli_local_pair_benchmark_enters_baseline
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_local_pair_benchmark.js" --benchmark-samples 1 --benchmark-warmup 0
        )
        set_tests_properties(minijs_cli_local_pair_benchmark_enters_baseline PROPERTIES
            PASS_REGULAR_EXPRESSION "baseline_entries=[1-9][0-9]*"
        )
        add_test(
            NAME minijs_cli_local_expression_benchmark_enters_baseline
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_local_expression_benchmark.js" --benchmark-samples 1 --benchmark-warmup 0
        )
        set_tests_properties(minijs_cli_local_expression_benchmark_enters_baseline PROPERTIES
            PASS_REGULAR_EXPRESSION "baseline_entries=[1-9][0-9]*"
        )
        add_test(
            NAME minijs_cli_local_return_benchmark_enters_baseline
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_local_return_benchmark.js" --benchmark-samples 1 --benchmark-warmup 0
        )
        set_tests_properties(minijs_cli_local_return_benchmark_enters_baseline PROPERTIES
            PASS_REGULAR_EXPRESSION "baseline_entries=[1-9][0-9]*"
        )
        add_test(
            NAME minijs_cli_number_return_benchmark_enters_baseline
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_number_return_benchmark.js" --benchmark-samples 1 --benchmark-warmup 0
        )
        set_tests_properties(minijs_cli_number_return_benchmark_enters_baseline PROPERTIES
            PASS_REGULAR_EXPRESSION "baseline_entries=[1-9][0-9]*"
        )
        add_test(
            NAME minijs_cli_literal_return_benchmark_enters_baseline
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_literal_return_benchmark.js" --benchmark-samples 1 --benchmark-warmup 0
        )
        set_tests_properties(minijs_cli_literal_return_benchmark_enters_baseline PROPERTIES
            PASS_REGULAR_EXPRESSION "baseline_entries=[1-9][0-9]*"
        )
        add_test(
            NAME minijs_cli_local_copy_benchmark_enters_baseline
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_local_copy_benchmark.js" --benchmark-samples 1 --benchmark-warmup 0
        )
        set_tests_properties(minijs_cli_local_copy_benchmark_enters_baseline PROPERTIES
            PASS_REGULAR_EXPRESSION "baseline_entries=[1-9][0-9]*"
        )
        add_test(
            NAME minijs_cli_constant_assign_benchmark_enters_baseline
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_constant_assign_benchmark.js" --benchmark-samples 1 --benchmark-warmup 0
        )
        set_tests_properties(minijs_cli_constant_assign_benchmark_enters_baseline PROPERTIES
            PASS_REGULAR_EXPRESSION "baseline_entries=[1-9][0-9]*"
        )
        add_test(
            NAME minijs_cli_unary_update_benchmark_enters_baseline
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_unary_update_benchmark.js" --benchmark-samples 1 --benchmark-warmup 0
        )
        set_tests_properties(minijs_cli_unary_update_benchmark_enters_baseline PROPERTIES
            PASS_REGULAR_EXPRESSION "baseline_entries=[1-9][0-9]*"
        )
        add_test(
            NAME minijs_cli_arithmetic_assign_benchmark_enters_baseline
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_arithmetic_assign_benchmark.js" --benchmark-samples 1 --benchmark-warmup 0
        )
        set_tests_properties(minijs_cli_arithmetic_assign_benchmark_enters_baseline PROPERTIES
            PASS_REGULAR_EXPRESSION "baseline_entries=[1-9][0-9]*"
        )
        add_test(
            NAME minijs_cli_comparison_assign_benchmark_enters_baseline
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_comparison_assign_benchmark.js" --benchmark-samples 1 --benchmark-warmup 0
        )
        set_tests_properties(minijs_cli_comparison_assign_benchmark_enters_baseline PROPERTIES
            PASS_REGULAR_EXPRESSION "baseline_entries=[1-9][0-9]*"
        )
        add_test(
            NAME minijs_cli_local_branch_benchmark_enters_baseline
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_local_branch_benchmark.js" --benchmark-samples 1 --benchmark-warmup 0
        )
        set_tests_properties(minijs_cli_local_branch_benchmark_enters_baseline PROPERTIES
            PASS_REGULAR_EXPRESSION "baseline_entries=[1-9][0-9]*"
        )
        add_test(
            NAME minijs_cli_comparison_branch_benchmark_enters_baseline
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_comparison_branch_benchmark.js" --benchmark-samples 1 --benchmark-warmup 0
        )
        set_tests_properties(minijs_cli_comparison_branch_benchmark_enters_baseline PROPERTIES
            PASS_REGULAR_EXPRESSION "baseline_entries=[1-9][0-9]*"
        )
        add_test(
            NAME minijs_cli_constant_branch_benchmark_enters_baseline
            COMMAND minijs --benchmark-compare-jit "${CMAKE_CURRENT_SOURCE_DIR}/examples/baseline_constant_branch_benchmark.js" --benchmark-samples 1 --benchmark-warmup 0
        )
        set_tests_properties(minijs_cli_constant_branch_benchmark_enters_baseline PROPERTIES
            PASS_REGULAR_EXPRESSION "baseline_entries=[1-9][0-9]*"
        )
    endif()
endif()
