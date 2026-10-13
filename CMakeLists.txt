cmake_minimum_required(VERSION 3.21)

project(yapr
    VERSION 0.1.0
    DESCRIPTION "yet another pdf reader - fast, predictable, no bloat"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Pdf PdfWidgets Network)

qt_standard_project_setup()

qt_add_executable(yapr
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/DocumentTab.h
    src/DocumentTab.cpp
)

target_link_libraries(yapr PRIVATE
    Qt6::Widgets
    Qt6::Pdf
    Qt6::PdfWidgets
    Qt6::Network
)

set_target_properties(yapr PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)
