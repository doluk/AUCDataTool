# SPDX-FileCopyrightText: 2026 Lukas Dobler
# SPDX-License-Identifier: LGPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Gui Quick QuickControls2 Concurrent Network)

# Optional: Qt Graphs for the 3D surface view, Qt Print Support (with Widgets, for the
# print dialog) for printing. Without them the app builds without these features.
option(AUC_WITH_GRAPHS "3D surface view (needs Qt Graphs)" ON)
option(AUC_WITH_PRINT "Printing (needs Qt Print Support)" ON)
if(AUC_WITH_GRAPHS)
    find_package(Qt6 QUIET COMPONENTS Graphs)
endif()
if(AUC_WITH_PRINT AND NOT ANDROID AND NOT IOS)
    find_package(Qt6 QUIET COMPONENTS Widgets PrintSupport)
endif()
set(AUC_HAVE_GRAPHS ${Qt6Graphs_FOUND})
if(Qt6PrintSupport_FOUND AND Qt6Widgets_FOUND)
    set(AUC_HAVE_PRINT TRUE)
else()
    set(AUC_HAVE_PRINT FALSE)
endif()
message(STATUS "AUCDataTool: 3D surface ${AUC_HAVE_GRAPHS}, printing ${AUC_HAVE_PRINT}")

qt_add_executable(aucdatatool WIN32 MACOSX_BUNDLE
    main.cpp
)
if(WIN32)
    # Exe and window icon (app/windows/AUCDataTool.ico, from the Linux SVG icon).
    target_sources(aucdatatool PRIVATE windows/AUCDataTool.rc)
endif()

set(auc_qml_files
    qml/Main.qml
    qml/PlotView.qml
    qml/FileList.qml
    qml/OptionsPanel.qml
    qml/ColorBar.qml
    qml/ExportDialog.qml
    qml/RunConditionsView.qml
    qml/IconButton.qml
    qml/AxisSettings.qml
    qml/PlotStylePanel.qml
    qml/ScanTimingView.qml
    qml/OptionSection.qml
    qml/PinButton.qml
    qml/UpdateDialog.qml
)
set(auc_sources
    AppController.h AppController.cpp
    ScanPlot.h ScanPlot.cpp
    PlotAxis.h
    PlotSeries.h
    SurfaceGrid.h
    CurveStyle.h
    Colormap.h Colormap.cpp
    UpdateInfo.h
    UpdateChecker.h UpdateChecker.cpp
    FileOpenRouter.h FileOpenRouter.cpp
)
set(auc_qml_dependencies QtQuick)
if(AUC_HAVE_GRAPHS)
    list(APPEND auc_qml_files qml/SurfaceView.qml)
    list(APPEND auc_sources SurfaceFeeder.h SurfaceFeeder.cpp)
    list(APPEND auc_qml_dependencies QtGraphs)
endif()

qt_add_qml_module(aucdatatool
    URI Auc.DataTool
    VERSION 1.0
    QML_FILES ${auc_qml_files}
    SOURCES ${auc_sources}
    DEPENDENCIES ${auc_qml_dependencies}
)

target_compile_definitions(aucdatatool PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(aucdatatool PRIVATE
    Auc::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Concurrent
    Qt6::Network
)
if(ANDROID)
    # FileOpenRouter: QtAndroidPrivate::registerNewIntentListener (files opened while running).
    if(NOT TARGET Qt6::CorePrivate)
        find_package(Qt6 REQUIRED COMPONENTS CorePrivate)
    endif()
    target_link_libraries(aucdatatool PRIVATE Qt6::CorePrivate)
endif()
if(AUC_HAVE_GRAPHS)
    target_compile_definitions(aucdatatool PRIVATE AUC_HAVE_GRAPHS)
    target_link_libraries(aucdatatool PRIVATE Qt6::Graphs)
endif()
if(AUC_HAVE_PRINT)
    target_compile_definitions(aucdatatool PRIVATE AUC_HAVE_PRINT)
    target_link_libraries(aucdatatool PRIVATE Qt6::Widgets Qt6::PrintSupport)
endif()

set_target_properties(aucdatatool PROPERTIES
    OUTPUT_NAME "AUCDataTool"
    MACOSX_BUNDLE_GUI_IDENTIFIER de.uni-konstanz.coelfen.aucdatatool
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in"
)

# Android: an APK via `cmake --build build --target apk` with Qt for Android (qt-cmake).
# QT_ANDROID_VERSION_CODE must increase with every published release.
set_target_properties(aucdatatool PROPERTIES
    QT_ANDROID_PACKAGE_NAME de.unikonstanz.coelfen.aucdatatool
    QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    QT_ANDROID_VERSION_NAME ${PROJECT_VERSION}
    QT_ANDROID_VERSION_CODE 1
    QT_ANDROID_MIN_SDK_VERSION 28
    QT_ANDROID_TARGET_SDK_VERSION 34
)
if(ANDROID)
    return()  # no install rules or Qt deployment, androiddeployqt bundles Qt into the APK
endif()

# vcpkg's applocal step copies only the DLLs the executable links directly, not
# the Qt plugins, QML modules or their dependencies, and vcpkg's Qt looks for
# those under Qt6/ rather than next to the executable. Deploy them into the build
# directory and add a qt.conf so it runs as-is.
if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR)
    set(_auc_qt_tools "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/Qt6/bin")
    add_custom_command(TARGET aucdatatool POST_BUILD
        COMMAND "$<IF:$<CONFIG:Debug>,${_auc_qt_tools}/windeployqt.debug.bat,${_auc_qt_tools}/windeployqt6.exe>"
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                --no-translations --no-compiler-runtime
                "$<TARGET_FILE:aucdatatool>"
        VERBATIM)
    file(GENERATE OUTPUT "$<TARGET_FILE_DIR:aucdatatool>/qt.conf" CONTENT
"[Paths]
Prefix=.
Plugins=.
QmlImports=qml
")
endif()

install(TARGETS aucdatatool
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Linux desktop integration: launcher and icon (freedesktop.org application id). The
# .deb/.rpm install to /opt and link them into /usr/share (linux/postinst).
if(UNIX AND NOT APPLE)
    install(FILES linux/io.github.doluk.AUCDataTool.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES linux/io.github.doluk.AUCDataTool.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES linux/io.github.doluk.AUCDataTool.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    install(FILES linux/io.github.doluk.AUCDataTool.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
endif()

# Bundle Qt libraries, plugins and QML imports next to the executable at install time
# (windeployqt / macdeployqt / Qt's own Linux deployment, the latter needs Qt >= 6.5).
# Distribution packages that depend on system Qt set AUC_DEPLOY_QT=OFF.
option(AUC_DEPLOY_QT "Bundle the Qt runtime on install" ON)
if(AUC_DEPLOY_QT)
    # Linux: Wayland next to X11. Qt deploys only default plugins, and only xcb is a default
    # platform plugin; the Wayland shell, decoration and buffer plugins belong to WaylandClient.
    # Qt then picks Wayland or X11 to match the session.
    if(UNIX AND NOT APPLE)
        find_package(Qt6 QUIET COMPONENTS WaylandClient)
        if(TARGET Qt6::WaylandClient)
            target_link_libraries(aucdatatool PRIVATE Qt6::WaylandClient)
            # Hardware-accelerated (EGL) buffers: a platform plugin of its own up to Qt 6.11, a
            # client buffer plugin of the single Wayland platform plugin since Qt 6.12.
            set(_auc_wayland_plugins Qt6::QWaylandIntegrationPlugin)
            foreach(_p Qt6::QWaylandEglPlatformIntegrationPlugin Qt6::QWaylandEglClientBufferPlugin)
                if(TARGET ${_p})
                    list(APPEND _auc_wayland_plugins ${_p})
                endif()
            endforeach()
            qt_import_plugins(aucdatatool INCLUDE ${_auc_wayland_plugins})
        else()
            message(WARNING "Qt WaylandClient not found: the bundle runs on X11/XWayland only")
        endif()
        # HTTPS for the update check: deploy the OpenSSL TLS backend explicitly (Qt loads the
        # system's libssl at run time; without it the manual check says HTTPS is unavailable).
        if(TARGET Qt6::QTlsBackendOpenSSLPlugin)
            qt_import_plugins(aucdatatool INCLUDE Qt6::QTlsBackendOpenSSLPlugin)
        else()
            message(WARNING "Qt OpenSSL TLS backend not found: the update check needs HTTPS")
        endif()
    endif()
    # Qt 6.4 names the output argument FILENAME_VARIABLE (OUTPUT_SCRIPT since 6.5).
    if(Qt6_VERSION VERSION_LESS 6.5)
        set(deploy_output FILENAME_VARIABLE)
    else()
        set(deploy_output OUTPUT_SCRIPT)
    endif()
    # Since Qt 6.12, deployment selects plugins by name, not from qt_import_plugins (above),
    # and the default platform plugins exclude Wayland.
    set(_auc_deploy_plugins "")
    if(UNIX AND NOT APPLE AND TARGET Qt6::WaylandClient AND Qt6_VERSION VERSION_GREATER_EQUAL 6.12)
        set(_auc_deploy_plugins INCLUDE_PLUGINS qwayland)
    endif()
    qt_generate_deploy_qml_app_script(
        TARGET aucdatatool
        ${deploy_output} deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
        ${_auc_deploy_plugins}
    )
    install(SCRIPT ${deploy_script})
endif()
