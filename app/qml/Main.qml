// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
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
        nameFilters: [qsTr("AUC data (*.auc *.mwrs *.mw *.mw? *.ra? *.ri? *.ip? *.wa? *.wi? *.fi?)"), qsTr("openAUC (*.auc)"),
                      qsTr("Multi-wavelength (*.mwrs *.mw *.mw?)"), qsTr("Beckman XL (*.ra? *.ri? *.ip? *.wa? *.wi? *.fi?)"),
                      qsTr("All files (*)")]
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

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
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
                    else { checked = false; folderDialog.live = true; folderDialog.open() }
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
            TabBar {
                id: viewTabs
                Layout.leftMargin: 8
                TabButton { text: qsTr("Scans"); width: implicitWidth }
                TabButton { text: qsTr("Run conditions"); width: implicitWidth }
                TabButton {
                    text: qsTr("3D surface")
                    visible: ctrl.surfaceSupported
                    width: visible ? implicitWidth : 0
                }
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
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        FileList {
            SplitView.preferredWidth: 250
            SplitView.minimumWidth: 160
            controller: ctrl
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
                             : qsTr("Open .auc/.mwrs/.mw/XL files or a data folder.\nWheel: zoom · Drag: pan · Right-drag: zoom box · Double-click/F1: autoscale · Click: select curve\nCtrl+←/→: previous/next wavelength")
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

        ScrollView {
            SplitView.preferredWidth: 300
            SplitView.minimumWidth: 240
            contentWidth: availableWidth
            OptionsPanel {
                width: parent.width
                controller: ctrl
                plot: scanView.plot
            }
        }
    }

    footer: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            BusyIndicator { running: ctrl.busy; Layout.preferredHeight: 22; Layout.preferredWidth: 22 }
            Label { text: ctrl.status; elide: Text.ElideRight; Layout.fillWidth: true }
            Label {
                visible: scanView.plot.hasData
                text: qsTr("%1 scans · %2 vertices · processed in %3 ms")
                      .arg(scanView.plot.curveCount)
                      .arg(scanView.plot.vertexCount.toLocaleString(Qt.locale(), "f", 0))
                      .arg(win.lastProcessMs.toFixed(1))
                opacity: 0.7
            }
        }
    }
}
