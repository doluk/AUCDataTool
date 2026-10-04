// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtGraphs
import Auc.DataTool

// 3D surface: the scans at the current wavelength as a radius × time surface, or radius ×
// wavelength of one scan.
// Drag rotates, wheel zooms, click shows the value under the cursor.
Item {
    id: root
    required property var controller
    /// Item to grab for printing/saving (the graph with its toolbar).
    readonly property Item graphItem: graph

    SurfaceFeeder {
        id: feeder
        controller: root.controller
        series: surfaceSeries
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Wraps onto a second line when the pane is narrow.
        Pane {
            Layout.fillWidth: true
            padding: 4
            leftPadding: 8
            Flow {
                width: parent.width
                spacing: 6
                Label { text: qsTr("Surface"); height: modeBox.height; verticalAlignment: Text.AlignVCenter }
                ComboBox {
                    id: modeBox
                    // Radius × wavelength needs multi-wavelength data; otherwise radius × time is used.
                    model: [qsTr("Radius × time (scans at the current wavelength)"), qsTr("Radius × wavelength (one scan)")]
                    currentIndex: root.controller.hasSpectra ? root.controller.surfaceMode : 0
                    enabled: root.controller.hasSpectra
                    onActivated: (i) => root.controller.surfaceMode = i
                    implicitContentWidthPolicy: ComboBox.WidestText
                }
                ComboBox {
                    visible: modeBox.currentIndex === 0
                    model: [qsTr("t (min)"), qsTr("ω²t")]
                    currentIndex: root.controller.surfaceOmega2t ? 1 : 0
                    onActivated: (i) => root.controller.surfaceOmega2t = i === 1
                    implicitContentWidthPolicy: ComboBox.WidestText
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Time axis of the surface")
                }
                Label {
                    text: qsTr("Scan")
                    visible: modeBox.currentIndex === 1
                    height: modeBox.height
                    verticalAlignment: Text.AlignVCenter
                }
                Slider {
                    id: scanSlider
                    visible: modeBox.currentIndex === 1
                    from: 1
                    to: Math.max(1, root.controller.scanCount)
                    stepSize: 1
                    snapMode: Slider.SnapAlways
                    value: root.controller.surfaceScan < 0 ? root.controller.scanCount : root.controller.surfaceScan + 1
                    // Commit on release: every scan change re-reads a whole scan file.
                    onPressedChanged: if (!pressed) commit()
                    onMoved: if (!pressed) commit()
                    function commit() {
                        const v = Math.round(value)
                        root.controller.surfaceScan = v >= root.controller.scanCount ? -1 : v - 1
                    }
                    width: 200
                }
                Label {
                    visible: modeBox.currentIndex === 1
                    text: Math.round(scanSlider.value) + " / " + root.controller.scanCount
                    height: modeBox.height
                    verticalAlignment: Text.AlignVCenter
                }
                ComboBox {
                    id: drawBox
                    model: [qsTr("Surface"), qsTr("Surface + grid"), qsTr("Grid")]
                    implicitContentWidthPolicy: ComboBox.WidestText
                    currentIndex: 0
                }
                ToolButton {
                    text: qsTr("Reset view")
                    onClicked: {
                        graph.cameraPreset = Graphs3D.CameraPreset.NoPreset
                        graph.cameraPreset = Graphs3D.CameraPreset.IsometricRightHigh
                        graph.cameraZoomLevel = 100
                    }
                }
            }
        }

        Surface3D {
            id: graph
            Layout.fillWidth: true
            Layout.fillHeight: true
            shadowQuality: Graphs3D.ShadowQuality.None
            cameraPreset: Graphs3D.CameraPreset.IsometricRightHigh
            selectionMode: Graphs3D.SelectionFlag.Item
            // Offscreen rendering keeps normal QML stacking and allows grabToImage().
            renderingMode: Graphs3D.RenderingMode.Indirect
            msaaSamples: 4
            aspectRatio: 1.6
            horizontalAspectRatio: 1.0

            theme: GraphsTheme {
                colorScheme: root.palette.window.hslLightness < 0.5 ? GraphsTheme.ColorScheme.Dark : GraphsTheme.ColorScheme.Light
                backgroundColor: root.palette.base
                plotAreaBackgroundColor: root.palette.base
                labelBackgroundVisible: false
                labelBorderVisible: false
                labelTextColor: root.palette.text
                labelFont.pointSize: 30
                gridVisible: true
                grid.mainColor: Qt.rgba(root.palette.text.r, root.palette.text.g, root.palette.text.b, 0.35)
            }

            axisX: Value3DAxis {
                title: feeder.xTitle
                titleVisible: true
                min: feeder.xMin
                max: feeder.xMax
                labelFormat: "%.2f"
            }
            axisY: Value3DAxis {
                title: feeder.yTitle
                titleVisible: true
                min: feeder.yMin
                max: feeder.yMax
                labelFormat: "%.3g"
            }
            axisZ: Value3DAxis {
                title: feeder.zTitle
                titleVisible: true
                min: feeder.zMin
                max: feeder.zMax
                labelFormat: root.controller.surfaceOmega2t && modeBox.currentIndex === 0 ? "%.2e" : "%.0f"
            }

            Surface3DSeries {
                id: surfaceSeries
                drawMode: drawBox.currentIndex === 0 ? Surface3DSeries.DrawSurface
                        : drawBox.currentIndex === 1 ? Surface3DSeries.DrawSurfaceAndWireframe
                        : Surface3DSeries.DrawWireframe
                shading: Surface3DSeries.Shading.Smooth
                colorStyle: GraphsTheme.ColorStyle.RangeGradient
                itemLabelFormat: "@xTitle: @xLabel · @zTitle: @zLabel · @yTitle: @yLabel"
                // Viridis, matching the default scan colours
                baseGradient: Gradient {
                    GradientStop { position: 0.0; color: "#440154" }
                    GradientStop { position: 0.25; color: "#3b528b" }
                    GradientStop { position: 0.5; color: "#21918c" }
                    GradientStop { position: 0.75; color: "#5ec962" }
                    GradientStop { position: 1.0; color: "#fde725" }
                }
            }
        }
    }

    Label {
        x: graph.x + 12
        y: graph.y + 8
        text: root.controller.surfaceTitle
        opacity: 0.8
    }

    Label {
        anchors.centerIn: parent
        visible: !feeder.hasData
        text: root.controller.scanCount > 0 ? qsTr("Computing surface…") : qsTr("Open data to show a surface.")
        opacity: 0.55
    }
}
