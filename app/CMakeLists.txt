# SPDX-FileCopyrightText: 2026 Lukas Dobler
# SPDX-License-Identifier: LGPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Gui Quick QuickControls2 Concurrent)

qt_add_executable(aucdatatool WIN32 MACOSX_BUNDLE
    main.cpp
)

qt_add_qml_module(aucdatatool
    URI Auc.DataTool
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/PlotView.qml
        qml/FileList.qml
        qml/OptionsPanel.qml
        qml/ColorBar.qml
    SOURCES
        AppController.h AppController.cpp
        ScanPlot.h ScanPlot.cpp
        PlotSeries.h
        CurveStyle.h
        Colormap.h Colormap.cpp
)

target_compile_definitions(aucdatatool PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(aucdatatool PRIVATE
    Auc::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Concurrent
)

set_target_properties(aucdatatool PROPERTIES
    OUTPUT_NAME "AUCDataTool"
    MACOSX_BUNDLE_GUI_IDENTIFIER de.uni-konstanz.coelfen.aucdatatool
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

install(TARGETS aucdatatool
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Bundle Qt libraries, plugins and QML imports next to the executable at install time
# (windeployqt / macdeployqt / Qt's own Linux deployment, the latter needs Qt >= 6.5).
# Distribution packages that depend on system Qt set AUC_DEPLOY_QT=OFF.
option(AUC_DEPLOY_QT "Bundle the Qt runtime on install" ON)
if(AUC_DEPLOY_QT)
    # Qt 6.4 names the output argument FILENAME_VARIABLE (OUTPUT_SCRIPT since 6.5).
    if(Qt6_VERSION VERSION_LESS 6.5)
        set(deploy_output FILENAME_VARIABLE)
    else()
        set(deploy_output OUTPUT_SCRIPT)
    endif()
    qt_generate_deploy_qml_app_script(
        TARGET aucdatatool
        ${deploy_output} deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
endif()
