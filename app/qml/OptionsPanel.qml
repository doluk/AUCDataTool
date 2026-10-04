// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: root
    property var controller
    property var plot
    readonly property bool hasData: controller.scanCount > 0
    spacing: 10

    // Numeric entry bound to a controller property; commits on Enter/focus loss.
    component NumberField: TextField {
        id: nf
        property real value
        property int decimals: 4
        signal committed(real v)
        text: value.toFixed(decimals)
        validator: DoubleValidator { locale: "C" }
        horizontalAlignment: Text.AlignRight
        selectByMouse: true
        onEditingFinished: committed(Number(text))
        onValueChanged: if (!activeFocus) text = value.toFixed(decimals)
    }

    GroupBox {
        title: qsTr("Scans")
        Layout.fillWidth: true
        Layout.topMargin: 6
        enabled: root.hasData
        GridLayout {
            anchors.fill: parent
            columns: 2
            Label { text: qsTr("From") }
            SpinBox {
                from: 1; to: Math.max(1, root.controller.scanCount)
                value: root.controller.firstScan + 1
                editable: true
                onValueModified: root.controller.firstScan = value - 1
                Layout.fillWidth: true
            }
            Label { text: qsTr("To") }
            SpinBox {
                from: 1; to: Math.max(1, root.controller.scanCount)
                value: root.controller.lastScan < 0 ? root.controller.scanCount : root.controller.lastScan + 1
                editable: true
                onValueModified: root.controller.lastScan = (value >= root.controller.scanCount ? -1 : value - 1)
                Layout.fillWidth: true
            }
            Label { text: qsTr("Every n-th") }
            SpinBox {
                from: 1; to: 1000
                value: root.controller.everyNth
                editable: true
                onValueModified: root.controller.everyNth = value
                Layout.fillWidth: true
            }
        }
    }

    FileDialog {
        id: noiseDialog
        property bool ti: true
        title: ti ? qsTr("Load time-invariant (TI) noise") : qsTr("Load radially invariant (RI) noise")
        nameFilters: [qsTr("Noise files (*.xml *.txt *.csv *.dat)"), qsTr("All files (*)")]
        onAccepted: root.controller.loadNoise(selectedFile, ti)
    }

    component NoiseRow: RowLayout {
        id: row
        property bool ti
        property string fileName
        property bool applied
        signal appliedToggled(bool on)
        Layout.fillWidth: true
        CheckBox {
            text: row.ti ? "TI" : "RI"
            enabled: row.fileName !== ""
            checked: row.applied
            onToggled: row.appliedToggled(checked)
            ToolTip.visible: hovered
            ToolTip.text: row.ti ? qsTr("Subtract time-invariant noise (one value per radius point)")
                                 : qsTr("Subtract radially invariant noise (one value per scan)")
        }
        Label {
            text: row.fileName !== "" ? row.fileName : qsTr("none")
            elide: Text.ElideMiddle
            opacity: row.fileName !== "" ? 1 : 0.5
            Layout.fillWidth: true
        }
        Button {
            text: row.fileName !== "" ? "✕" : qsTr("Load…")
            flat: row.fileName !== ""
            onClicked: {
                if (row.fileName !== "") root.controller.clearNoise(row.ti)
                else { noiseDialog.ti = row.ti; noiseDialog.open() }
            }
        }
    }

    GroupBox {
        title: qsTr("Noise")
        Layout.fillWidth: true
        enabled: root.hasData
        ColumnLayout {
            anchors.fill: parent
            NoiseRow {
                ti: true
                fileName: root.controller.tiNoiseName
                applied: root.controller.applyTiNoise
                onAppliedToggled: (on) => root.controller.applyTiNoise = on
            }
            NoiseRow {
                ti: false
                fileName: root.controller.riNoiseName
                applied: root.controller.applyRiNoise
                onAppliedToggled: (on) => root.controller.applyRiNoise = on
            }
            Label {
                visible: root.controller.noiseError !== ""
                text: root.controller.noiseError
                color: "#c0262d"
                wrapMode: Text.Wrap
                font.pixelSize: 11
                Layout.fillWidth: true
            }
        }
    }

    GroupBox {
        title: qsTr("Corrections")
        Layout.fillWidth: true
        enabled: root.hasData
        ColumnLayout {
            anchors.fill: parent
            CheckBox {
                text: qsTr("Reverse radius")
                checked: root.controller.reverse
                onToggled: root.controller.reverse = checked
            }
            CheckBox {
                text: qsTr("Remove spikes (median 3)")
                checked: root.controller.removeSpikes
                onToggled: root.controller.removeSpikes = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Median filter, left rank 2 / right rank 0 – as in the LabVIEW AUC-Viewer")
            }
            Label { text: qsTr("Offset"); Layout.topMargin: 4 }
            ComboBox {
                Layout.fillWidth: true
                model: [qsTr("None"), qsTr("Point (drag marker)"), qsTr("Baseline region (mean)")]
                currentIndex: root.controller.offsetMode
                onActivated: (i) => root.controller.offsetMode = i
            }
            GridLayout {
                columns: 2
                visible: root.controller.offsetMode > 0
                Layout.fillWidth: true
                Label { text: root.controller.offsetMode === 1 ? qsTr("r (cm)") : qsTr("r₁ (cm)") }
                NumberField {
                    Layout.fillWidth: true
                    value: root.controller.offsetR1
                    onCommitted: (v) => root.controller.offsetR1 = v
                }
                Label { text: qsTr("r₂ (cm)"); visible: root.controller.offsetMode === 2 }
                NumberField {
                    Layout.fillWidth: true
                    visible: root.controller.offsetMode === 2
                    value: root.controller.offsetR2
                    onCommitted: (v) => root.controller.offsetR2 = v
                }
            }
        }
    }

    GroupBox {
        title: qsTr("Radial integration")
        Layout.fillWidth: true
        enabled: root.hasData
        ColumnLayout {
            anchors.fill: parent
            CheckBox {
                text: qsTr("Integrate each scan")
                checked: root.controller.integrate
                onToggled: root.controller.integrate = checked
            }
            GridLayout {
                columns: 2
                visible: root.controller.integrate
                Layout.fillWidth: true
                Label { text: qsTr("r₁ (cm)") }
                NumberField {
                    Layout.fillWidth: true
                    value: root.controller.intR1
                    onCommitted: (v) => root.controller.intR1 = v
                }
                Label { text: qsTr("r₂ (cm)") }
                NumberField {
                    Layout.fillWidth: true
                    value: root.controller.intR2
                    onCommitted: (v) => root.controller.intR2 = v
                }
            }
            CheckBox {
                visible: root.controller.integrate
                text: qsTr("Weight with r (∫A·r dr)")
                checked: root.controller.radialWeight
                onToggled: root.controller.radialWeight = checked
            }
        }
    }

    GroupBox {
        title: qsTr("Display")
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent
            Label { text: qsTr("Scan colours") }
            ComboBox {
                Layout.fillWidth: true
                model: ["Viridis", "Turbo", qsTr("Rainbow (LabVIEW)"), qsTr("Grey")]
                currentIndex: root.controller.colormap
                onActivated: (i) => root.controller.colormap = i
            }
            CheckBox {
                text: qsTr("Grid")
                checked: root.plot.showGrid
                onToggled: root.plot.showGrid = checked
            }
        }
    }

    Item { Layout.fillHeight: true }
}
