# Qt desktop GUI. Included from the root CMakeLists.txt, which already defines
# the shared `filerelic_core` library (the GUI no longer builds its own copy).

# Standard CMake automoc keeps this compatible with Qt 6.2 (Ubuntu 22.04).
set(CMAKE_AUTOMOC ON)

add_executable(filerelic-gui
    main.cpp
    main_window.cpp
    main_window.h
)

target_link_libraries(filerelic-gui PRIVATE filerelic_core Qt6::Widgets Qt6::Concurrent)
