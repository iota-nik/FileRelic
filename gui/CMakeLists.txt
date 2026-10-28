cmake_minimum_required(VERSION 3.16)

project(FileRelicGui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

# Build the same inspection engine used by the CLI. The GUI no longer shells
# out to a second executable or parses terminal output as its API.
add_library(filerelic_core STATIC ../src/image_inspector.cpp)
target_include_directories(filerelic_core PUBLIC ../src)

# Use standard CMake automoc support for compatibility with Qt 6.2, which is
# the version supplied by Ubuntu 22.04. Newer Qt convenience helpers are not
# required for this small Widgets application.
set(CMAKE_AUTOMOC ON)

add_executable(filerelic-gui
    main.cpp
    main_window.cpp
    main_window.h
)

target_link_libraries(filerelic-gui PRIVATE filerelic_core Qt6::Widgets Qt6::Concurrent)

