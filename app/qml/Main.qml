// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import Auc.DataTool

ApplicationWindow {
    id: win
    width: 1400
    height: 860
    visible: true
    title: ctrl.currentIndex >= 0 && ctrl.channels.length > ctrl.currentIndex
           ? ctrl.channels[ctrl.currentIndex].run + " · " + ctrl.channels[ctrl.currentIndex].title + " – AUCDataTool"
           : "AUCDataTool"

    // Exposed for main.cpp (command-line files, benchmark).
    property alias controller: ctrl
    property alias mainPlot: scanView.plot
    /// 0 scans, 1 run conditions, 2 3D surface (main.cpp --view)
    property alias viewIndex: viewTabs.currentIndex
    property real lastProcessMs: 0
    readonly property bool isAndroid: Qt.platform.os === "android"
    /// Phone layout (e.g. 412 × 915 dp portrait, 915 × 412 landscape): plots use the full
    /// window, channel list and options open as drawers, actions sit in a menu.
    readonly property bool compact: width < 1000 || height < 520
    /// Phone in landscape: tabs move into the toolbar and the status bar is hidden.
    readonly property bool landscapePhone: compact && width > height
    onCompactChanged: { channelDrawer.close(); optionsDrawer.close() }

    AppController {
        id: ctrl
        scanPlot: scanView.plot
        integralPlot: integralView.plot
        spectrumPlot: spectrumView.plot
        speedPlot: runView.speedPlot
        temperaturePlot: runView.temperaturePlot
        omega2tPlot: runView.omega2tPlot
        surfaceActive: viewTabs.currentIndex === 2
        onProcessed: (ms) => win.lastProcessMs = ms
        // --set surfaceActive=true on the command line opens the tab.
        onSurfaceSettingsChanged: if (surfaceActive && surfaceSupported) viewTabs.currentIndex = 2
        onCurrentIndexChanged: channelDrawer.close()
    }

    // Grabs the visible graph area (scan/spectrum/integral plots or the 3D surface).
    function grabGraph(callback) {
        const item = viewStack.currentIndex === 2 && surfaceLoader.item ? surfaceLoader.item
                   : viewStack.currentIndex === 1 ? runView : plotArea
        item.grabToImage(callback, Qt.size(item.width * 2, item.height * 2))
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Open data files")
        fileMode: FileDialog.OpenFiles
        nameFilters: win.isAndroid ? [] : [qsTr("AUC data (*.auc *.mwrs *.mw *.mw? *.ra? *.ri? *.ip? *.wa? *.wi? *.fi?)"), qsTr("openAUC (*.auc)"),
                      qsTr("Multi-wavelength (*.mwrs *.mw *.mw?)"), qsTr("Beckman XL (*.ra? *.ri? *.ip? *.wa? *.wi? *.fi?)"),
                      qsTr("All files (*)")]  // Android: the picker maps filters to MIME types, the AUC extensions have none
        onAccepted: ctrl.openFiles(selectedFiles)
    }
    FolderDialog {
        id: folderDialog
        property bool live: false
        title: live ? qsTr("Watch folder (live)") : qsTr("Open folder")
        onAccepted: ctrl.openFolder(selectedFolder, live)
    }
    ExportDialog {
        id: exportDialog
        controller: ctrl
    }
    FileDialog {
        id: saveGraphDialog
        title: qsTr("Save graph")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG image (*.png)"), qsTr("PDF document (*.pdf)"), qsTr("JPEG image (*.jpg)")]
        onAccepted: {
            const url = selectedFile
            win.grabGraph(r => ctrl.saveImage(r.image, url, ctrl.runInfo))
        }
    }

    Shortcut { sequences: [StandardKey.Open]; onActivated: fileDialog.open() }
    Shortcut { sequences: [StandardKey.Print]; enabled: ctrl.canPrint && ctrl.scanCount > 0; onActivated: win.grabGraph(r => ctrl.printImage(r.image, ctrl.runInfo)) }
    Shortcut { sequence: "Esc"; onActivated: scanView.plot.selectedCurve = -1 }
    Shortcut { sequence: "F1"; onActivated: scanView.plot.autoscale() }  // same key as the LabVIEW viewer
    Shortcut { sequences: ["Ctrl+Right", "PgUp"]; onActivated: ctrl.stepWavelength(1) }
    Shortcut { sequences: ["Ctrl+Left", "PgDown"]; onActivated: ctrl.stepWavelength(-1) }

    function startWatch() { folderDialog.live = true; folderDialog.open() }

    Menu {
        id: actionsMenu
        MenuItem { text: qsTr("Open files…"); onTriggered: fileDialog.open() }
        MenuItem { text: qsTr("Open folder…"); onTriggered: { folderDialog.live = false; folderDialog.open() } }
        MenuItem {
            text: ctrl.live ? qsTr("Stop live mode") : qsTr("Watch folder…")
            onTriggered: ctrl.live ? ctrl.stopLive() : win.startWatch()
        }
        MenuSeparator {}
        MenuItem {
            text: ctrl.exporting ? qsTr("Exporting…") : qsTr("Export…")
            enabled: scanView.plot.hasData && !ctrl.exporting
            onTriggered: exportDialog.open()
        }
        MenuItem { text: qsTr("Save graph…"); enabled: ctrl.scanCount > 0; onTriggered: saveGraphDialog.open() }
        MenuItem {
            text: qsTr("Print…")
            visible: ctrl.canPrint
            height: visible ? implicitHeight : 0
            enabled: ctrl.scanCount > 0
            onTriggered: win.grabGraph(r => ctrl.printImage(r.image, ctrl.runInfo))
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Close all channels"); enabled: ctrl.channels.length > 0; onTriggered: ctrl.closeAll() }
    }

    header: ToolBar {
      ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Phone: channels · current channel · fit · options · menu
        RowLayout {
            visible: win.compact
            spacing: 0
            Layout.fillWidth: true
            IconButton { glyph: "menu"; onClicked: channelDrawer.open() }
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Label {
                    id: compactTitle
                    text: ctrl.currentIndex >= 0 && ctrl.channels.length > ctrl.currentIndex
                          ? ctrl.channels[ctrl.currentIndex].title : "AUCDataTool"
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    visible: text !== ""
                    text: ctrl.live ? qsTr("● Live – %1").arg(ctrl.runInfo) : ctrl.runInfo
                    color: compactTitle.color  // toolbar text colour; "● Live" marks live mode
                    font.pixelSize: 11
                    opacity: 0.8
                    elide: Text.ElideLeft
                    Layout.fillWidth: true
                }
            }
            Item {
                id: landscapeTabSlot
                visible: win.landscapePhone
                Layout.preferredWidth: 300
                Layout.fillHeight: true
            }
            BusyIndicator {
                visible: win.landscapePhone && ctrl.busy
                running: visible
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
            }
            IconButton { glyph: "fit"; enabled: scanView.plot.hasData; onClicked: scanView.plot.autoscale() }
            IconButton { glyph: "tune"; onClicked: optionsDrawer.open() }
            IconButton { id: moreButton; glyph: "more"; onClicked: actionsMenu.popup(moreButton, moreButton.width - actionsMenu.width, moreButton.height) }
        }

        RowLayout {
            visible: !win.compact
            Layout.fillWidth: true
            Layout.leftMargin: 6
            spacing: 2
            ToolButton { text: qsTr("Open files…"); onClicked: fileDialog.open() }
            ToolButton { text: qsTr("Open folder…"); onClicked: { folderDialog.live = false; folderDialog.open() } }
            ToolSeparator {}
            ToolButton {
                text: ctrl.live ? qsTr("● Live") : qsTr("Watch folder…")
                checkable: true
                checked: ctrl.live
                palette.buttonText: ctrl.live ? "#c0262d" : win.palette.buttonText
                ToolTip.visible: hovered
                ToolTip.text: ctrl.live ? qsTr("Watching %1 – click to stop").arg(ctrl.liveFolder)
                                        : qsTr("Open a folder and add/update files while the run is acquiring")
                onClicked: {
                    if (ctrl.live) ctrl.stopLive()
                    else { checked = false; win.startWatch() }
                }
            }
            ToolSeparator {}
            ToolButton {
                text: ctrl.exporting ? qsTr("Exporting…") : qsTr("Export…")
                enabled: scanView.plot.hasData && !ctrl.exporting
                onClicked: exportDialog.open()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("CSV, Origin, Beckman XL or UltraScan (.auc), one or many wavelengths")
            }
            ToolButton {
                text: qsTr("Print…")
                visible: ctrl.canPrint
                enabled: ctrl.scanCount > 0
                onClicked: win.grabGraph(r => ctrl.printImage(r.image, ctrl.runInfo))
            }
            ToolButton { text: qsTr("Save graph…"); enabled: ctrl.scanCount > 0; onClicked: saveGraphDialog.open() }
            ToolSeparator {}
            ToolButton { text: qsTr("Autoscale"); enabled: scanView.plot.hasData; onClicked: scanView.plot.autoscale() }
            Item {
                id: wideTabSlot
                Layout.leftMargin: 8
                Layout.fillHeight: true
                implicitWidth: viewTabs.implicitWidth
            }
            Item { Layout.fillWidth: true }
            Label {
                text: ctrl.runInfo
                elide: Text.ElideLeft
                Layout.maximumWidth: win.width * 0.45
                rightPadding: 10
                opacity: 0.8
            }
        }

        Item {
            id: compactTabSlot
            visible: win.compact && !win.landscapePhone
            Layout.fillWidth: true
            implicitHeight: viewTabs.implicitHeight
        }
      }
    }

    // One instance, placed in the toolbar row (desktop) or as a full-width row (phone).
    TabBar {
        id: viewTabs
        parent: !win.compact ? wideTabSlot : win.landscapePhone ? landscapeTabSlot : compactTabSlot
        anchors.fill: parent
        // Inside the toolbar, Material (Android) would hand the toolbar's white text to the tabs.
        Material.foreground: win.Material.foreground
        readonly property int tabCount: ctrl.surfaceSupported ? 3 : 2
        // Phone: equal tabs over the slot (not viewTabs.width, which depends on the tabs).
        readonly property real compactTabWidth: (win.landscapePhone ? landscapeTabSlot.width : win.width) / tabCount
        TabButton { text: qsTr("Scans"); width: win.compact ? viewTabs.compactTabWidth : implicitWidth }
        TabButton { text: win.compact ? qsTr("Run") : qsTr("Run conditions"); width: win.compact ? viewTabs.compactTabWidth : implicitWidth }
        TabButton {
            text: win.compact ? qsTr("3D") : qsTr("3D surface")
            visible: ctrl.surfaceSupported
            width: !visible ? 0 : win.compact ? viewTabs.compactTabWidth : implicitWidth
        }
    }

    // Phone: channel list and options as drawers (opened from the toolbar; no edge swipe,
    // which would collide with panning the plot and the system back gesture).
    Drawer {
        id: channelDrawer
        edge: Qt.LeftEdge
        width: Math.min(win.width * 0.85, 380)
        height: win.height
        dragMargin: 0
    }
    Drawer {
        id: optionsDrawer
        edge: Qt.RightEdge
        Material.elevation: 0  // Qt 6.8 Material: the elevation layer leaves a right-edge drawer transparent
        width: Math.min(win.width * 0.92, 420)
        height: win.height
        dragMargin: 0
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Item {
            id: channelPane
            visible: !win.compact
            SplitView.preferredWidth: 250
            SplitView.minimumWidth: 160
            FileList {
                parent: win.compact ? channelDrawer.contentItem : channelPane
                anchors.fill: parent
                controller: ctrl
            }
        }

        StackLayout {
            id: viewStack
            SplitView.fillWidth: true
            currentIndex: viewTabs.currentIndex

        SplitView {
            id: plotArea
            orientation: Qt.Vertical

            PlotView {
                id: scanView
                curvesSelectable: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 200
                xLabel: ctrl.xLabel
                yLabel: ctrl.yLabel
                placeholder: ctrl.processingError !== "" ? ctrl.processingError
                             : win.compact ? qsTr("Open files or a data folder from the menu (top right).\nDrag: pan · Pinch: zoom · Long press + drag: zoom box\nDouble tap: autoscale · Tap: select curve")
                             : qsTr("Open .auc/.mwrs/.mw/XL files or a data folder.\nWheel: zoom · Drag: pan · Right-drag/Ctrl+drag: zoom box · Double-click/F1: autoscale · Click: select curve\nCtrl+←/→: previous/next wavelength")
                markers: {
                    var m = []
                    if (ctrl.offsetMode === 1)
                        m.push({ value: ctrl.offsetR1, color: "#d97706", label: qsTr("offset"), key: "offR1" })
                    if (ctrl.offsetMode === 2) {
                        m.push({ value: ctrl.offsetR1, color: "#d97706", label: qsTr("baseline"), key: "offR1" })
                        m.push({ value: ctrl.offsetR2, color: "#d97706", label: "", key: "offR2" })
                    }
                    if (ctrl.integrate) {
                        m.push({ value: ctrl.intR1, color: "#1d4ed8", label: qsTr("∫"), key: "intR1" })
                        m.push({ value: ctrl.intR2, color: "#1d4ed8", label: "", key: "intR2" })
                    }
                    if (ctrl.showSpectrum && ctrl.hasSpectra)
                        m.push({ value: ctrl.spectrumRadius, color: "#059669", label: qsTr("spectrum"), key: "specR" })
                    return m
                }
                onMarkerMoved: (key, value) => {
                    if (key === "specR") ctrl.spectrumRadius = value
                    else if (key === "offR1") ctrl.offsetR1 = value
                    else if (key === "offR2") ctrl.offsetR2 = value
                    else if (key === "intR1") ctrl.intR1 = value
                    else if (key === "intR2") ctrl.intR2 = value
                }

                ColorBar {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: 26
                    anchors.bottomMargin: 54
                    visible: scanView.plot.hasData
                    colormap: ctrl.colormap
                    firstLabel: qsTr("first scan")
                    lastLabel: qsTr("last scan")
                }
            }

            PlotView {
                id: spectrumView
                visible: ctrl.showSpectrum && ctrl.hasSpectra
                curvesSelectable: true
                SplitView.preferredHeight: 260
                SplitView.minimumHeight: 140
                xLabel: qsTr("Wavelength (nm)")
                yLabel: ctrl.yLabel
                placeholder: qsTr("Spectra at r = %1 cm (reading all scans…)").arg(ctrl.spectrumRadius.toFixed(3))
                markers: ctrl.mwa
                         ? [{ value: ctrl.mwaFrom, color: "#c2410c", label: qsTr("MWA"), key: "mwaFrom" },
                            { value: ctrl.mwaTo, color: "#c2410c", label: "", key: "mwaTo" }]
                         : [{ value: ctrl.wavelength, color: "#c2410c", label: ctrl.wavelength.toFixed(0) + " nm", key: "wl" }]
                onMarkerMoved: (key, value) => {
                    if (key === "wl") ctrl.wavelengthIndex = ctrl.wavelengthIndexOf(value)
                    else if (key === "mwaFrom") ctrl.mwaFrom = value
                    else if (key === "mwaTo") ctrl.mwaTo = value
                }
                Label {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: 24
                    anchors.topMargin: 16
                    visible: spectrumView.plot.hasData
                    text: qsTr("r = %1 ± %2 cm").arg(ctrl.spectrumRadius.toFixed(3)).arg(ctrl.spectrumWidth.toFixed(3))
                    color: "#059669"
                    font.pixelSize: 12
                }
            }

            PlotView {
                id: integralView
                visible: ctrl.integrate
                SplitView.preferredHeight: 230
                SplitView.minimumHeight: 140
                xLabel: qsTr("Time (min)")
                yLabel: ctrl.radialWeight ? qsTr("∫A·r dr (OD·cm²)") : qsTr("∫A dr (OD·cm)")
                placeholder: qsTr("Radial integral per scan")
            }
        }

        RunConditionsView {
            id: runView
            controller: ctrl
        }

        Loader {
            id: surfaceLoader
            // Created on first use: Qt Quick 3D starts only when the tab is opened.
            active: ctrl.surfaceSupported && viewTabs.currentIndex === 2
            Component.onCompleted: if (ctrl.surfaceSupported) setSource(Qt.resolvedUrl("SurfaceView.qml"), { controller: ctrl })
        }
        }

        Item {
            id: optionsPane
            visible: !win.compact
            SplitView.preferredWidth: 300
            SplitView.minimumWidth: 240
            ScrollView {
                parent: win.compact ? optionsDrawer.contentItem : optionsPane
                anchors.fill: parent
                anchors.margins: win.compact ? 12 : 0
                contentWidth: availableWidth
                OptionsPanel {
                    width: parent.width
                    controller: ctrl
                    plot: scanView.plot
                    compact: win.compact
                }
            }
        }
    }

    footer: ToolBar {
        visible: !win.landscapePhone
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            BusyIndicator { running: ctrl.busy; Layout.preferredHeight: 22; Layout.preferredWidth: 22 }
            Label { text: ctrl.status; elide: Text.ElideRight; Layout.fillWidth: true }
            Label {
                visible: scanView.plot.hasData && !win.compact
                text: qsTr("%1 scans · %2 vertices · processed in %3 ms")
                      .arg(scanView.plot.curveCount)
                      .arg(scanView.plot.vertexCount.toLocaleString(Qt.locale(), "f", 0))
                      .arg(win.lastProcessMs.toFixed(1))
                opacity: 0.7
            }
        }
    }
}
