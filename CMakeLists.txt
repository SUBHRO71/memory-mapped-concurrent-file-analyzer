cmake_minimum_required(VERSION 3.20)
project(FileAnalyzer VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This initial version uses Windows file-mapping and process APIs.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build with a 64-bit C++ toolchain.")
endif()

find_package(Threads REQUIRED)
add_library(analyzer_core
    src/scanner.cpp
    src/mapped_file_windows.cpp
    src/analyzer.cpp
    src/metrics_windows.cpp
    src/dataset_generator.cpp
)
target_include_directories(analyzer_core PUBLIC include)
target_compile_features(analyzer_core PUBLIC cxx_std_20)
target_compile_definitions(analyzer_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(analyzer_core PUBLIC Threads::Threads psapi)

add_executable(file_analyzer src/main.cpp src/benchmark.cpp src/report.cpp)
target_link_libraries(file_analyzer PRIVATE analyzer_core shell32 advapi32)
if(MINGW)
    target_link_options(file_analyzer PRIVATE -static)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(analyzer_tests tests/correctness_tests.cpp)
    target_link_libraries(analyzer_tests PRIVATE analyzer_core)
    if(MINGW)
        target_link_options(analyzer_tests PRIVATE -static)
    endif()
    add_test(NAME correctness COMMAND analyzer_tests)
    add_test(NAME cli_help COMMAND file_analyzer --help)
    find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell)
    if(POWERSHELL_EXECUTABLE)
        add_test(NAME cli_subprocess
            COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/smoke-tests.ps1"
                    -Executable "$<TARGET_FILE:file_analyzer>")
    endif()
endif()

foreach(target analyzer_core file_analyzer)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
