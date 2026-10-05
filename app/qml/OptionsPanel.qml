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
    /// Narrow (phone drawer): wide rows wrap into two columns.
    property bool compact: false
    spacing: 10

    // Numeric entry bound to a controller property; commits on Enter/focus loss.
    component NumberField: TextField {
        id: nf
        property real value
        property int decimals: 4
        signal committed(real v)
        text: value.toFixed(decimals)
        validator: DoubleValidator { locale: "C" }
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        horizontalAlignment: Text.AlignRight
        selectByMouse: true
        onEditingFinished: committed(Number(text))
        onValueChanged: if (!activeFocus) text = value.toFixed(decimals)
    }

    // Line/marker controls for a CurveStyle map (see CurveStyle::toMap); emits partial changes.
    component StyleEditor: GridLayout {
        id: se
        property var style: ({})
        property bool showColor: true
        property bool showVisible: true
        signal edited(var changes)
        columns: 2
        columnSpacing: 8

        CheckBox {
            visible: se.showVisible
            Layout.columnSpan: 2
            text: qsTr("Visible")
            checked: se.style.visible ?? true
            onToggled: se.edited({ visible: checked })
        }
        Label { text: qsTr("Colour"); visible: se.showColor }
        Button {
            visible: se.showColor
            Layout.fillWidth: true
            implicitHeight: 28
            contentItem: Rectangle {
                color: se.style.color ?? "black"
                radius: 2
                border.color: Qt.rgba(0, 0, 0, 0.3)
            }
            onClicked: colorDialog.open()
        }
        Label { text: qsTr("Line") }
        ComboBox {
            Layout.fillWidth: true
            model: [qsTr("Solid"), qsTr("Dashed"), qsTr("Dotted"), qsTr("Dash-dot"), qsTr("None")]
            currentIndex: se.style.line ?? 0
            onActivated: (i) => se.edited({ line: i })
        }
        Label { text: qsTr("Width (px)") }
        SpinBox {
            Layout.fillWidth: true
            from: 1
            to: 20
            editable: true
            value: Math.round(se.style.width ?? 1)
            onValueModified: se.edited({ width: value })
        }
        Label { text: qsTr("Markers") }
        ComboBox {
            Layout.fillWidth: true
            model: [qsTr("None"), qsTr("Circle"), qsTr("Square"), qsTr("Triangle"), qsTr("Diamond"), qsTr("Plus")]
            currentIndex: se.style.marker ?? 0
            onActivated: (i) => se.edited({ marker: i })
        }
        Label { text: qsTr("Marker size (px)"); visible: (se.style.marker ?? 0) > 0 }
        SpinBox {
            visible: (se.style.marker ?? 0) > 0
            Layout.fillWidth: true
            from: 2
            to: 40
            editable: true
            value: Math.round(se.style.markerSize ?? 6)
            onValueModified: se.edited({ markerSize: value })
        }
        ColorDialog {
            id: colorDialog
            title: qsTr("Curve colour")
            selectedColor: se.style.color ?? "black"
            onAccepted: se.edited({ color: selectedColor })
        }
    }

    GroupBox {
        title: qsTr("Wavelength")
        Layout.fillWidth: true
        Layout.topMargin: 6
        visible: root.controller.wavelengthCount > 1
        ColumnLayout {
            anchors.fill: parent
            RowLayout {
                Layout.fillWidth: true
                ToolButton {
                    text: "◀"
                    enabled: !root.controller.mwa && root.controller.wavelengthIndex > 0
                    onClicked: root.controller.stepWavelength(-1)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Previous wavelength (Ctrl+←)")
                }
                Label {
                    text: root.controller.mwa
                          ? qsTr("%1 – %2 nm").arg(root.controller.mwaFrom.toFixed(1)).arg(root.controller.mwaTo.toFixed(1))
                          : qsTr("%1 nm").arg(root.controller.wavelength.toFixed(1))
                    font.bold: true
                    font.pixelSize: 15
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }
                ToolButton {
                    text: "▶"
                    enabled: !root.controller.mwa && root.controller.wavelengthIndex < root.controller.wavelengthCount - 1
                    onClicked: root.controller.stepWavelength(1)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Next wavelength (Ctrl+→)")
                }
            }
            Slider {
                Layout.fillWidth: true
                enabled: !root.controller.mwa
                from: 0
                to: Math.max(1, root.controller.wavelengthCount - 1)
                stepSize: 1
                snapMode: Slider.SnapAlways
                value: root.controller.wavelengthIndex
                onMoved: root.controller.wavelengthIndex = Math.round(value)
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: root.controller.wavelengthMin.toFixed(0) + " nm"; font.pixelSize: 11; opacity: 0.6 }
                Item { Layout.fillWidth: true }
                Label { text: qsTr("%1 wavelengths").arg(root.controller.wavelengthCount); font.pixelSize: 11; opacity: 0.6 }
                Item { Layout.fillWidth: true }
                Label { text: root.controller.wavelengthMax.toFixed(0) + " nm"; font.pixelSize: 11; opacity: 0.6 }
            }
            CheckBox {
                text: qsTr("Average wavelength range (MWA)")
                checked: root.controller.mwa
                onToggled: root.controller.mwa = checked
            }
            GridLayout {
                columns: 4
                visible: root.controller.mwa
                Layout.fillWidth: true
                Label { text: qsTr("from") }
                NumberField {
                    Layout.fillWidth: true
                    decimals: 1
                    value: root.controller.mwaFrom
                    onCommitted: (v) => root.controller.mwaFrom = v
                }
                Label { text: qsTr("to") }
                NumberField {
                    Layout.fillWidth: true
                    decimals: 1
                    value: root.controller.mwaTo
                    onCommitted: (v) => root.controller.mwaTo = v
                }
            }
            CheckBox {
                text: qsTr("Show spectra at a radius")
                checked: root.controller.showSpectrum
                onToggled: root.controller.showSpectrum = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("All selected scans against wavelength at the green marker (drag it in the scan plot). Reads every scan file completely.")
            }
            GridLayout {
                columns: 4
                visible: root.controller.showSpectrum
                Layout.fillWidth: true
                Label { text: qsTr("r (cm)") }
                NumberField {
                    Layout.fillWidth: true
                    decimals: 3
                    value: root.controller.spectrumRadius
                    onCommitted: (v) => root.controller.spectrumRadius = v
                }
                Label { text: "±" }
                NumberField {
                    Layout.fillWidth: true
                    decimals: 3
                    value: root.controller.spectrumWidth
                    onCommitted: (v) => root.controller.spectrumWidth = Math.max(0, v)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Half-width of the radius window averaged for the spectra (cm)")
                }
            }
        }
    }

    GroupBox {
        title: qsTr("Data")
        Layout.fillWidth: true
        Layout.topMargin: root.controller.wavelengthCount > 1 ? 0 : 6
        enabled: root.hasData
        ColumnLayout {
            anchors.fill: parent
            Label {
                visible: root.controller.dataIsAbsorbance
                text: root.controller.yLabel.startsWith("Absorbance") ? qsTr("The file contains absorbance.")
                      : qsTr("The file contains %1.").arg(root.controller.yLabel)
                opacity: 0.7
            }
            RowLayout {
                visible: !root.controller.dataIsAbsorbance
                RadioButton {
                    text: qsTr("Intensity")
                    checked: root.controller.displayMode === 0
                    onClicked: root.controller.displayMode = 0
                }
                RadioButton {
                    text: qsTr("Absorbance")
                    checked: root.controller.displayMode === 1
                    onClicked: root.controller.displayMode = 1
                }
            }
            Label {
                text: qsTr("Reference (I₀)")
                visible: !root.controller.dataIsAbsorbance
                Layout.topMargin: 4
            }
            ComboBox {
                id: refCombo
                visible: !root.controller.dataIsAbsorbance
                Layout.fillWidth: true
                model: root.controller.referenceChoices
                currentIndex: root.controller.referenceChoice
                onActivated: (i) => root.controller.referenceChoice = i
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Channel whose intensity is used as I₀ in A = −log₁₀(I/I₀). Default: channel B of the same cell.")
            }
            ComboBox {
                visible: !root.controller.dataIsAbsorbance && root.controller.referenceChoice > 0
                Layout.fillWidth: true
                model: [qsTr("Scan by scan"), qsTr("Mean of reference scans")]
                currentIndex: root.controller.referenceMode
                onActivated: (i) => root.controller.referenceMode = i
            }
            GridLayout {
                columns: root.compact ? 2 : 4
                visible: !root.controller.dataIsAbsorbance && root.controller.referenceChoice > 0
                         && root.controller.referenceMode === 1
                Layout.fillWidth: true
                Label { text: qsTr("scans") }
                SpinBox {
                    from: 1; to: Math.max(1, root.controller.referenceScanCount)
                    value: root.controller.refFirst + 1
                    editable: true
                    onValueModified: root.controller.refFirst = value - 1
                    Layout.fillWidth: true
                }
                Label { text: "–" }
                SpinBox {
                    from: 1; to: Math.max(1, root.controller.referenceScanCount)
                    value: root.controller.refLast < 0 ? root.controller.referenceScanCount : root.controller.refLast + 1
                    editable: true
                    onValueModified: root.controller.refLast = (value >= root.controller.referenceScanCount ? -1 : value - 1)
                    Layout.fillWidth: true
                }
            }
            CheckBox {
                visible: root.controller.hasDarkCurrent
                text: qsTr("Dark current subtracted")
                checked: root.controller.darkSubtracted
                onToggled: root.controller.darkSubtracted = checked
            }
        }
    }

    GroupBox {
        title: qsTr("Scans")
        Layout.fillWidth: true
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
        nameFilters: Qt.platform.os === "android" ? [] : [qsTr("Noise files (*.xml *.txt *.csv *.dat)"), qsTr("All files (*)")]
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

    GroupBox {
        title: qsTr("Curves")
        Layout.fillWidth: true
        enabled: root.plot.hasData
        ColumnLayout {
            anchors.fill: parent

            Label { text: qsTr("All curves"); font.bold: true }
            StyleEditor {
                Layout.fillWidth: true
                showColor: false
                showVisible: false
                style: root.plot.defaultStyle
                onEdited: (changes) => root.plot.defaultStyle = changes
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                Label { text: qsTr("Individual scans"); font.bold: true; Layout.fillWidth: true }
                Button {
                    text: qsTr("Reset all")
                    flat: true
                    enabled: root.plot.curves.some(c => c.custom)
                    onClicked: root.plot.resetCurveStyles()
                }
            }
            ListView {
                id: curveList
                Layout.fillWidth: true
                Layout.preferredHeight: 170
                clip: true
                // Count as model: `curves` is rebuilt on every style edit and an array model
                // would recreate all rows and lose the scroll position.
                model: root.plot.curves.length
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    id: row
                    required property int index
                    readonly property var curve: root.plot.curves[index] ?? ({})
                    width: ListView.view.width
                    height: 26
                    padding: 2
                    highlighted: curve.id === root.plot.selectedCurve
                    onClicked: root.plot.selectedCurve = (highlighted ? -1 : curve.id)
                    contentItem: RowLayout {
                        spacing: 6
                        CheckBox {
                            padding: 0
                            checked: row.curve.visible ?? true
                            onToggled: root.plot.setCurveStyle(row.curve.id, { visible: checked })
                        }
                        Rectangle {
                            implicitWidth: 18
                            implicitHeight: 10
                            radius: 2
                            color: row.curve.color ?? "transparent"
                        }
                        Label {
                            text: row.curve.label ?? ""
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            font.italic: row.curve.custom ?? false
                        }
                    }
                }
                Connections {
                    target: root.plot
                    function onStylesChanged() {
                        const i = root.plot.curves.findIndex(c => c.id === root.plot.selectedCurve)
                        if (i >= 0) curveList.positionViewAtIndex(i, ListView.Contain)
                    }
                }
            }

            ColumnLayout {
                visible: root.plot.selectedCurve >= 0
                Layout.fillWidth: true
                Label {
                    text: {
                        const c = root.plot.curves.find(c => c.id === root.plot.selectedCurve)
                        return c ? c.label : ""
                    }
                    font.bold: true
                }
                StyleEditor {
                    Layout.fillWidth: true
                    style: root.plot.selectedStyle
                    onEdited: (changes) => root.plot.setCurveStyle(root.plot.selectedCurve, changes)
                }
                RowLayout {
                    Button {
                        text: qsTr("Reset to default")
                        enabled: root.plot.selectedStyle.custom ?? false
                        onClicked: root.plot.resetCurveStyle(root.plot.selectedCurve)
                    }
                    Button {
                        text: qsTr("Deselect")
                        onClicked: root.plot.selectedCurve = -1
                    }
                }
            }
            Label {
                visible: root.plot.selectedCurve < 0
                text: qsTr("Click a curve in the plot or a scan above to edit it.")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                opacity: 0.6
                font.pixelSize: 11
            }
        }
    }

    Item { Layout.fillHeight: true }
}
