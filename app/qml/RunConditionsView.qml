// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Rotor speed, temperature and ω²t of every scan of the current channel against time
// (from the scan headers), with a straight-line fit of ω²t.
Item {
    id: root
    property var controller
    readonly property alias speedPlot: speedView.plot
    readonly property alias temperaturePlot: temperatureView.plot
    readonly property alias omega2tPlot: omega2tView.plot

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Label {
            Layout.fillWidth: true
            padding: 8
            text: root.controller.runConditions
            wrapMode: Text.WordWrap
            opacity: 0.85
        }

        SplitView {
            orientation: Qt.Vertical
            Layout.fillWidth: true
            Layout.fillHeight: true

            PlotView {
                id: speedView
                SplitView.preferredHeight: root.height / 3
                SplitView.minimumHeight: 120
                xLabel: qsTr("Time (min)")
                yLabel: qsTr("Speed (rpm)")
                placeholder: qsTr("Rotor speed per scan")
                Label {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: 24
                    anchors.topMargin: 16
                    visible: speedView.plot.curveCount > 1
                    text: qsTr("● measured   - - set")
                    font.pixelSize: 12
                    opacity: 0.8
                }
            }
            PlotView {
                id: temperatureView
                SplitView.preferredHeight: root.height / 3
                SplitView.minimumHeight: 120
                xLabel: qsTr("Time (min)")
                yLabel: qsTr("Temperature (°C)")
                placeholder: qsTr("Temperature per scan")
            }
            PlotView {
                id: omega2tView
                SplitView.fillHeight: true
                SplitView.minimumHeight: 120
                xLabel: qsTr("Time (min)")
                yLabel: qsTr("ω²t (rad²/s)")
                placeholder: qsTr("ω²t per scan")
                Label {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: 24
                    anchors.topMargin: 16
                    visible: omega2tView.plot.curveCount > 1
                    text: qsTr("● ω²t   - - ω²·(t − t₀)")
                    font.pixelSize: 12
                    opacity: 0.8
                }
            }
        }
    }
}
