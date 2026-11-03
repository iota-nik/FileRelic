cmake_minimum_required(VERSION 3.16)

project(FileRelic VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Core library: everything that inspects an image. CLI and GUI both link it.
add_library(filerelic_core STATIC
    src/image_source.cpp
    src/sha256.cpp
    src/ext_reader.cpp
    src/filesystem_probe.cpp
    src/partition_scanner.cpp
    src/report_format.cpp
    src/report_sections.cpp
    src/report_text.cpp
    src/case_manifest.cpp
    src/ntfs_adapter.cpp
    src/image_inspector.cpp
    src/volume_access.cpp
)
target_include_directories(filerelic_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- Command-line program.
add_executable(filerelic src/cli_main.cpp)
target_link_libraries(filerelic PRIVATE filerelic_core)
target_compile_definitions(filerelic PRIVATE FILERELIC_VERSION="${PROJECT_VERSION}")

enable_testing()
add_test(NAME cli-report-smoke
    COMMAND filerelic report ${CMAKE_CURRENT_SOURCE_DIR}/testdata/practice-fs-ext4.raw
            ${CMAKE_CURRENT_BINARY_DIR}/cli-report-smoke.json)
add_executable(case-manifest-test tests/case_manifest_test.cpp)
target_link_libraries(case-manifest-test PRIVATE filerelic_core)
add_test(NAME case-manifest-actions COMMAND case-manifest-test)

target_compile_definitions(filerelic_core PUBLIC FILERELIC_VERSION="${PROJECT_VERSION}")

# --- Optional Qt desktop GUI. Built automatically when Qt 6 is installed;
# otherwise the CLI still builds. Turn it off with -DFILERELIC_BUILD_GUI=OFF.
option(FILERELIC_BUILD_GUI "Build the Qt desktop GUI when Qt 6 is available" ON)
if(FILERELIC_BUILD_GUI)
    find_package(Qt6 QUIET COMPONENTS Widgets Concurrent)
    if(Qt6_FOUND)
        add_subdirectory(gui)
    else()
        message(STATUS "Qt 6 (Widgets, Concurrent) not found - building the CLI only")
    endif()
endif()
