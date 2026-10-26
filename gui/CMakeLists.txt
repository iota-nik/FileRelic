cmake_minimum_required(VERSION 3.16)

project(FileRelicGui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Use standard CMake automoc support for compatibility with Qt 6.2, which is
# the version supplied by Ubuntu 22.04. Newer Qt convenience helpers are not
# required for this small Widgets application.
set(CMAKE_AUTOMOC ON)

add_executable(filerelic-gui
    main.cpp
    main_window.cpp
    main_window.h
)

target_link_libraries(filerelic-gui PRIVATE Qt6::Widgets)

# Override this when FileRelic's CLI is installed elsewhere.
set(FILERELIC_CLI_PATH "" CACHE FILEPATH "Path to the filerelic command-line executable")
if(FILERELIC_CLI_PATH)
    target_compile_definitions(filerelic-gui
        PRIVATE FILERELIC_CLI_PATH=\"${FILERELIC_CLI_PATH}\")
endif()

