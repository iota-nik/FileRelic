cmake_minimum_required(VERSION 3.16)

project(FileRelic VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(filerelic_core
    src/image_inspector.cpp
)
target_include_directories(filerelic_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(filerelic
    src/cli_main.cpp
)
target_link_libraries(filerelic PRIVATE filerelic_core)

target_compile_definitions(filerelic PRIVATE FILERELIC_VERSION="${PROJECT_VERSION}")
