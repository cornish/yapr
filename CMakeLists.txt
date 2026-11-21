cmake_minimum_required(VERSION 3.21)

project(yapr
    VERSION 0.1.0
    DESCRIPTION "yet another pdf reader - fast, predictable, no bloat"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Pdf PdfWidgets PrintSupport Network)

qt_standard_project_setup()

qt_add_executable(yapr
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/DocumentTab.h
    src/DocumentTab.cpp
    src/PdfView.h
    src/PdfView.cpp
    src/ThumbnailModel.h
    src/ThumbnailModel.cpp
)

# App icon: embedded for the window/taskbar at runtime, plus the .exe icon
# Explorer shows on Windows. Generated from assets/yapr-icon.png.
qt_add_resources(yapr "icons"
    PREFIX "/"
    BASE assets
    FILES
        assets/icons/yapr-16.png
        assets/icons/yapr-24.png
        assets/icons/yapr-32.png
        assets/icons/yapr-48.png
        assets/icons/yapr-64.png
        assets/icons/yapr-128.png
        assets/icons/yapr-256.png
)
if(WIN32)
    target_sources(yapr PRIVATE assets/yapr.rc)
endif()

target_link_libraries(yapr PRIVATE
    Qt6::Widgets
    Qt6::Pdf
    Qt6::PdfWidgets
    Qt6::PrintSupport
    Qt6::Network
)

set_target_properties(yapr PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)
