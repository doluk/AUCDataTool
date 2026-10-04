import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Auc.Viewer

ApplicationWindow {
    id: win
    width: 1400
    height: 860
    visible: true
    title: ctrl.currentIndex >= 0 && ctrl.files.length > ctrl.currentIndex
           ? ctrl.files[ctrl.currentIndex].name + " – AUC Viewer" : "AUC Viewer"

    // Exposed for main.cpp (command-line files, benchmark).
    property alias controller: ctrl
    property alias mainPlot: scanView.plot
    property real lastProcessMs: 0

    AppController {
        id: ctrl
        scanPlot: scanView.plot
        integralPlot: integralView.plot
        onProcessed: (ms) => win.lastProcessMs = ms
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Open data files")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("openAUC data (*.auc)"), qsTr("All files (*)")]
        onAccepted: ctrl.openFiles(selectedFiles)
    }
    FolderDialog {
        id: folderDialog
        property bool live: false
        title: live ? qsTr("Watch folder (live)") : qsTr("Open folder")
        onAccepted: ctrl.openFolder(selectedFolder, live)
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export processed scans")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: [qsTr("CSV (*.csv)")]
        onAccepted: ctrl.exportCsv(selectedFile)
    }

    Shortcut { sequences: [StandardKey.Open]; onActivated: fileDialog.open() }
    Shortcut { sequence: "F1"; onActivated: scanView.plot.autoscale() }  // same key as the LabVIEW viewer

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
            ToolButton { text: qsTr("Export CSV…"); enabled: scanView.plot.hasData; onClicked: exportDialog.open() }
            ToolButton { text: qsTr("Autoscale"); enabled: scanView.plot.hasData; onClicked: scanView.plot.autoscale() }
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

        SplitView {
            orientation: Qt.Vertical
            SplitView.fillWidth: true

            PlotView {
                id: scanView
                SplitView.fillHeight: true
                SplitView.minimumHeight: 200
                xLabel: qsTr("Radius (cm)")
                yLabel: ctrl.yLabel
                placeholder: qsTr("Open .auc files or a data folder.\nWheel: zoom · Drag: pan · Right-drag: zoom box · Double-click/F1: autoscale")
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
                    return m
                }
                onMarkerMoved: (key, value) => {
                    if (key === "offR1") ctrl.offsetR1 = value
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
                id: integralView
                visible: ctrl.integrate
                SplitView.preferredHeight: 230
                SplitView.minimumHeight: 140
                xLabel: qsTr("Time (min)")
                yLabel: ctrl.radialWeight ? qsTr("∫A·r dr (OD·cm²)") : qsTr("∫A dr (OD·cm)")
                placeholder: qsTr("Radial integral per scan")
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
