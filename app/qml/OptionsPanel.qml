// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Auc.DataTool

ColumnLayout {
    id: root
    property var controller
    property ScanPlot plot: null
    /// [{ label, plot: ScanPlot }] – plots visible on the current tab.
    property var plotChoices: []
    signal plotSelected(var plot)
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

    OptionSection {
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

    OptionSection {
        title: qsTr("Data")
        Layout.fillWidth: true
        Layout.topMargin: root.controller.wavelengthCount > 1 ? 0 : 6
        contentEnabled: root.hasData
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
                ToolTip.text: qsTr("Channel whose intensity is used as I₀ in A = −log₁₀(I/I₀). Default: channel B of the same cell.\nWith “Radial area” and None, the region of this channel itself is used.")
            }
            ComboBox {
                visible: !root.controller.dataIsAbsorbance
                         && (root.controller.referenceChoice > 0 || root.controller.displayMode === 1)
                Layout.fillWidth: true
                model: [qsTr("Scan by scan"), qsTr("Mean of reference scans"), qsTr("Radial area (per scan)")]
                currentIndex: root.controller.referenceMode
                onActivated: (i) => root.controller.referenceMode = i
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Radial area: I₀ of scan i is the mean intensity of scan i over r₁…r₂ of the reference channel, or of this channel if the reference is None.")
            }
            GridLayout {
                columns: 2
                visible: !root.controller.dataIsAbsorbance && root.controller.referenceMode === 2
                Layout.fillWidth: true
                Label { text: qsTr("r₁ (cm)") }
                NumberField {
                    Layout.fillWidth: true
                    value: root.controller.refR1
                    onCommitted: (v) => root.controller.refR1 = v
                }
                Label { text: qsTr("r₂ (cm)") }
                NumberField {
                    Layout.fillWidth: true
                    value: root.controller.refR2
                    onCommitted: (v) => root.controller.refR2 = v
                }
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

    OptionSection {
        title: qsTr("Scans")
        Layout.fillWidth: true
        contentEnabled: root.hasData
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

    OptionSection {
        title: qsTr("Noise")
        Layout.fillWidth: true
        contentEnabled: root.hasData
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

    OptionSection {
        title: qsTr("Corrections")
        Layout.fillWidth: true
        contentEnabled: root.hasData
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

    // Interference workflow of S. Xu et al. (AUC 2026): jitter correction with the baseline
    // offset over the air–air region, then fringe-jump removal held at an alignment radius.
    OptionSection {
        title: qsTr("Fringe-jump correction")
        Layout.fillWidth: true
        contentEnabled: root.hasData
        visible: root.controller.isInterference
        ColumnLayout {
            id: fringe
            anchors.fill: parent
            readonly property var info: root.controller.fringeInfo
            readonly property bool limitsSet: root.controller.fringeR2 > root.controller.fringeR1
            function setLimits(a, b) {
                root.controller.fringeR1 = Math.min(a, b)
                root.controller.fringeR2 = Math.max(a, b)
            }
            Label {
                visible: root.controller.offsetMode !== 2
                text: qsTr("Correct the air–air jitter first: Offset → Baseline region over the air–air region.")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                opacity: 0.6
                font.pixelSize: 11
            }
            CheckBox {
                text: qsTr("Remove fringe jumps")
                checked: root.controller.fringeJumps
                onToggled: root.controller.fringeJumps = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("At the alignment radius, a change |ΔF| > threshold from one scan to the next is a jump of round(ΔF) fringes; the scan is shifted back by it")
            }
            GridLayout {
                columns: 2
                visible: root.controller.fringeJumps
                Layout.fillWidth: true
                // Shows the limits in use: set ones, else the estimate (meniscus where the
                // scans start to differ, bottom = last point). Typing one fixes both.
                Label { text: qsTr("Meniscus r_a (cm)") }
                NumberField {
                    Layout.fillWidth: true
                    value: fringe.limitsSet ? root.controller.fringeR1 : (fringe.info.r1 ?? 0)
                    onCommitted: (v) => fringe.setLimits(v, fringe.limitsSet ? root.controller.fringeR2 : (fringe.info.r2 ?? 0))
                    ToolTip.visible: hovered
                    ToolTip.text: fringe.limitsSet ? qsTr("Set manually") : qsTr("Automatic: where the scans start to differ")
                }
                Label { text: qsTr("Bottom r_b (cm)") }
                NumberField {
                    Layout.fillWidth: true
                    value: fringe.limitsSet ? root.controller.fringeR2 : (fringe.info.r2 ?? 0)
                    onCommitted: (v) => fringe.setLimits(fringe.limitsSet ? root.controller.fringeR1 : (fringe.info.r1 ?? 0), v)
                    ToolTip.visible: hovered
                    ToolTip.text: fringe.limitsSet ? qsTr("Set manually") : qsTr("Automatic: last radial point")
                }
                Label { text: qsTr("Threshold (fringes)") }
                NumberField {
                    Layout.fillWidth: true
                    decimals: 2
                    value: root.controller.fringeThreshold
                    onCommitted: (v) => root.controller.fringeThreshold = v
                }
                Label { text: qsTr("Alignment (cm)") }
                NumberField {
                    Layout.fillWidth: true
                    value: root.controller.fringeAlign > 0 ? root.controller.fringeAlign : (fringe.info.alignment ?? 0)
                    onCommitted: (v) => root.controller.fringeAlign = v
                    ToolTip.visible: hovered
                    ToolTip.text: root.controller.fringeAlign > 0 ? qsTr("Set manually")
                                                                  : qsTr("Automatic (route A, else route B)")
                }
            }
            Flow {
                visible: root.controller.fringeJumps
                Layout.fillWidth: true
                spacing: 6
                Button {
                    text: qsTr("Stable interval (A)")
                    enabled: fringe.info.stableValid ?? false
                    onClicked: root.controller.fringeAlign = fringe.info.stableAlign
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Align inside the widest interval around the centre in which no scan has a jump")
                }
                Button {
                    text: qsTr("Self-consistent (B)")
                    enabled: fringe.info.selfConsistentValid ?? false
                    onClicked: root.controller.fringeAlign = fringe.info.selfConsistent
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Align at r* where the corrected first/last-scan crossing T(r*) ≈ r*")
                }
                Button {
                    text: qsTr("Auto alignment")
                    enabled: root.controller.fringeAlign > 0
                    onClicked: root.controller.fringeAlign = 0
                }
                Button {
                    text: qsTr("Auto limits")
                    enabled: fringe.limitsSet
                    onClicked: fringe.setLimits(0, 0)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Meniscus and bottom estimated from the scans")
                }
            }
            Label {
                visible: root.controller.fringeJumps && fringe.info.valid !== undefined
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                text: {
                    const f = fringe.info
                    const cm = (v) => Number(v).toFixed(4) + " cm"
                    let t = []
                    t.push(qsTr("%n jump(s) found", "", Number(f.jumps ?? 0)))
                    t.push(qsTr("Route A: %1").arg(f.stableValid ? cm(f.stableLo) + " – " + cm(f.stableHi)
                                                                 : qsTr("not identified")))
                    t.push(qsTr("Route B: %1").arg(f.selfConsistentValid ? cm(f.selfConsistent) : qsTr("not identified")))
                    if (f.iso !== undefined)
                        t.push(qsTr("Isoconcentration point: %1 (%2 fringes)").arg(cm(f.iso)).arg(Number(f.isoFringes).toFixed(4)))
                    const ra = f.r1 ?? 0, rb = f.r2 ?? 0
                    if (rb > ra && ra > 0)
                        t.push(qsTr("Theoretical iso √((r_b² + r_a²)/2): %1").arg(cm(Math.sqrt((rb * rb + ra * ra) / 2))))
                    return t.join("\n")
                }
            }
        }
    }

    OptionSection {
        title: qsTr("Radial integration")
        Layout.fillWidth: true
        contentEnabled: root.hasData
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

    OptionSection {
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
                visible: root.plot !== null
                text: qsTr("Grid")
                checked: root.plot ? root.plot.showGrid : false
                onToggled: if (root.plot) root.plot.showGrid = checked
            }
        }
    }

    OptionSection {
        title: qsTr("Curves")
        Layout.fillWidth: true
        visible: root.plot !== null
        contentEnabled: root.plot !== null && (root.plot.hasData || root.plotChoices.length > 1)
        ColumnLayout {
            anchors.fill: parent
            spacing: 6

            RowLayout {
                visible: root.plotChoices.length > 1
                Layout.fillWidth: true
                spacing: 8
                Label { text: qsTr("Plot") }
                ComboBox {
                    Layout.fillWidth: true
                    model: root.plotChoices.map(c => c.label)
                    currentIndex: {
                        for (let i = 0; i < root.plotChoices.length; ++i)
                            if (root.plotChoices[i].plot === root.plot) return i
                        return 0
                    }
                    onActivated: (i) => {
                        if (i >= 0 && i < root.plotChoices.length)
                            root.plotSelected(root.plotChoices[i].plot)
                    }
                }
            }

            PlotStylePanel {
                Layout.fillWidth: true
                plot: root.plot
                showGridToggle: false
                listHeight: 170
            }
        }
    }

    Item { Layout.fillHeight: true }
}
