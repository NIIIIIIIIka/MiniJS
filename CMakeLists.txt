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
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Core sources are discovered while the project is still small. main.cpp is
# excluded because it belongs to the command-line executable.
file(GLOB_RECURSE MINIJS_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(REMOVE_ITEM MINIJS_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
)

if(MINIJS_CORE_SOURCES)
    add_library(minijs_core STATIC ${MINIJS_CORE_SOURCES})
    target_include_directories(minijs_core
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_features(minijs_core PUBLIC cxx_std_17)
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
        minijs_enable_warnings(minijs_tests)

        add_test(NAME minijs_tests COMMAND minijs_tests)
    endif()
endif()
