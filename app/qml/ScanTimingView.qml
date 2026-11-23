// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Auc.DataTool

// Scan timing of an absorbance/intensity run (any file format) (all cells of the current channel's run):
// the interval from every scan to the next one (any cell), the intervals per cell
// transition, and per cell the interval between its consecutive scans. Red dashed lines
// mark scans the instrument skipped.
Item {
    id: root
    property var controller
    readonly property alias timingPlot: timingView.plot
    readonly property alias transitionPlot: transitionView.plot
    readonly property alias sweepPlot: sweepView.plot
    readonly property alias sweepSpeedPlot: sweepSpeedView.plot

    Label {
        anchors.centerIn: parent
        visible: !root.controller.hasScanTiming
        width: Math.min(implicitWidth, parent.width - 32)
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        text: qsTr("Scan timing is available for absorbance and intensity data (not interference or fluorescence).")
        opacity: 0.55
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: root.controller.hasScanTiming

        Label {
            Layout.fillWidth: true
            padding: 8
            text: root.controller.timingSummary
            wrapMode: Text.WordWrap
            opacity: 0.85
        }

        SplitView {
            orientation: Qt.Vertical
            Layout.fillWidth: true
            Layout.fillHeight: true

            SplitView {
                orientation: Qt.Horizontal
                SplitView.preferredHeight: root.height * 0.38
                SplitView.minimumHeight: 160

                PlotView {
                    id: timingView
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 240
                    xLabel: root.controller.timingTimeLabel
                    yLabel: qsTr("Interval to next scan, any cell (s)")
                    placeholder: qsTr("Interval to the next scan")
                    markers: root.controller.timingSkipMarkers
                    Flow {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: timingView.leftMargin + 8
                        anchors.rightMargin: timingView.rightMargin + 8
                        anchors.topMargin: timingView.topMargin + 26
                        spacing: 10
                        visible: timingView.plot.hasData
                        Repeater {
                            model: root.controller.timingLegend
                            delegate: Label {
                                required property var modelData
                                text: "● " + modelData.label
                                color: modelData.color
                                font.pixelSize: 11
                                background: Rectangle { color: palette.base; opacity: 0.75; radius: 2 }
                            }
                        }
                    }
                }

                PlotView {
                    id: transitionView
                    SplitView.preferredWidth: Math.min(420, root.width * 0.34)
                    SplitView.minimumWidth: 220
                    leftMargin: 120
                    xLabel: qsTr("Interval by cell transition (s)")
                    yLabel: ""
                    placeholder: qsTr("Intervals by cell transition")
                    yTickLabels: root.controller.timingTransitionTicks
                    annotations: root.controller.timingTransitionNotes
                    Component.onCompleted: plot.xAxis.log = true
                }
            }

            // Sweep logs of the multi-wavelength detector (.tr files).
            SplitView {
                orientation: Qt.Horizontal
                visible: root.controller.timingHasSweeps
                SplitView.preferredHeight: root.height * 0.3
                SplitView.minimumHeight: 150

                PlotView {
                    id: sweepView
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 240
                    xLabel: qsTr("Radius (cm)")
                    yLabel: qsTr("Time since sweep start (s)")
                    placeholder: qsTr("Sweep logs (.tr)")
                    Label {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: sweepView.leftMargin + 8
                        anchors.rightMargin: sweepView.rightMargin + 8
                        anchors.topMargin: sweepView.topMargin + 4
                        text: qsTr("<b>Detector sweeps</b> (.tr logs, one line per scan, dashed: fit)  ") + root.controller.timingSweepInfo
                        textFormat: Text.StyledText
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        background: Rectangle { color: palette.base; opacity: 0.8; radius: 2 }
                        padding: 2
                    }
                }

                PlotView {
                    id: sweepSpeedView
                    SplitView.preferredWidth: Math.min(520, root.width * 0.4)
                    SplitView.minimumWidth: 220
                    xLabel: root.controller.timingTimeLabel
                    yLabel: qsTr("Rotor speed during sweeps (rpm)")
                    placeholder: qsTr("Rotor speed")
                    markers: root.controller.timingSkipMarkers
                }
            }

            ScrollView {
                id: cellScroll
                SplitView.fillHeight: true
                SplitView.minimumHeight: 160
                contentWidth: availableWidth
                clip: true

                Column {
                    width: cellScroll.availableWidth
                    Label {
                        width: parent.width
                        padding: 6
                        leftPadding: 8
                        text: qsTr("Per cell: interval between consecutive scans of the same cell (acquisition order)")
                        font.pixelSize: 12
                        opacity: 0.8
                    }
                    Repeater {
                        model: root.controller.timingCells
                        delegate: PlotView {
                            id: cellView
                            required property var modelData
                            required property int index
                            width: parent.width
                            height: 170
                            xLabel: index === root.controller.timingCells.length - 1 ? root.controller.timingTimeLabel : ""
                            yLabel: qsTr("Interval (s)")
                            placeholder: qsTr("Fewer than two scans")
                            markers: root.controller.timingSkipMarkers
                            annotations: modelData.notes
                            Component.onCompleted: root.controller.setTimingCellPlot(index, plot)
                            Label {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.rightMargin: cellView.rightMargin + 8
                                anchors.topMargin: cellView.topMargin + 4
                                text: "<b>" + cellView.modelData.title + "</b>  " + cellView.modelData.info
                                textFormat: Text.StyledText
                                color: cellView.modelData.color
                                font.pixelSize: 12
                                background: Rectangle { color: palette.base; opacity: 0.8; radius: 2 }
                                padding: 2
                            }
                        }
                    }
                }
            }
        }
    }
}
