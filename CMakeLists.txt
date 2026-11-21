cmake_minimum_required(VERSION 3.21)

# The version here is the single source of truth: the app's About box,
# the Windows file properties, the macOS bundle, and release tags (vX.Y.Z,
# checked by CI) all derive from it.
project(yapr
    VERSION 0.2.0
    DESCRIPTION "yet another pdf reader - fast, predictable, no bloat"
    HOMEPAGE_URL "https://github.com/cornish/yapr"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 6.8: QStyleHints::setColorScheme. Developed and released with 6.11.
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Pdf PdfWidgets PrintSupport Network)

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

target_compile_definitions(yapr PRIVATE YAPR_VERSION="${PROJECT_VERSION}")

# App icon, embedded for the window/taskbar at runtime. The PNGs are
# generated from assets/yapr-icon.png.
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
    # .exe icon (Explorer) and version info (file Properties).
    configure_file(packaging/windows/yapr.rc.in
                   ${CMAKE_CURRENT_BINARY_DIR}/yapr.rc @ONLY)
    target_sources(yapr PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/yapr.rc)
endif()

if(APPLE)
    # Build the .icns with Apple's iconutil from the master PNG.
    set(YAPR_ICNS ${CMAKE_CURRENT_BINARY_DIR}/yapr.icns)
    add_custom_command(
        OUTPUT ${YAPR_ICNS}
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/make-icns.sh
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/yapr-icon.png ${YAPR_ICNS}
        DEPENDS assets/yapr-icon.png packaging/macos/make-icns.sh
        COMMENT "Generating yapr.icns"
    )
    target_sources(yapr PRIVATE ${YAPR_ICNS})
    set_source_files_properties(${YAPR_ICNS} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(yapr PROPERTIES
        OUTPUT_NAME yapr
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.cornish.yapr
        MACOSX_BUNDLE_BUNDLE_NAME yapr
        MACOSX_BUNDLE_ICON_FILE yapr.icns
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
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

# Linux install layout (used to build the AppImage's AppDir).
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS yapr RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/linux/io.github.cornish.yapr.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    foreach(size 16 24 32 48 64 128 256)
        install(FILES assets/icons/yapr-${size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
                RENAME io.github.cornish.yapr.png)
    endforeach()
endif()
