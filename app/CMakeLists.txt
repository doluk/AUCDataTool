# SPDX-FileCopyrightText: 2026 Lukas Dobler
# SPDX-License-Identifier: LGPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Gui Quick QuickControls2 Concurrent)

qt_add_executable(aucviewer WIN32 MACOSX_BUNDLE
    main.cpp
)

qt_add_qml_module(aucviewer
    URI Auc.Viewer
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
        Colormap.h Colormap.cpp
)

target_compile_definitions(aucviewer PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(aucviewer PRIVATE
    Auc::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Concurrent
)

set_target_properties(aucviewer PROPERTIES
    OUTPUT_NAME "AucViewer"
    MACOSX_BUNDLE_GUI_IDENTIFIER de.uni-konstanz.coelfen.aucviewer
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

install(TARGETS aucviewer
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
