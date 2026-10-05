// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Auc.DataTool

// Axes, labels, interaction and draggable vertical markers around a ScanPlot.
Item {
    id: root

    property alias plot: plot
    property string xLabel
    property string yLabel
    property string placeholder
    /// [{ value, color, label, key }] – vertical lines in data coordinates, draggable.
    property var markers: []
    signal markerMoved(string key, real value)
    /// Left click selects the curve under the cursor (plot.selectedCurve).
    property bool curvesSelectable: false
    /// Finger input: wider marker grips; long press + drag draws the zoom box.
    readonly property bool touchUi: Qt.platform.os === "android" || Qt.platform.os === "ios"

    readonly property int leftMargin: 66
    readonly property int bottomMargin: 46
    readonly property int topMargin: 12
    readonly property int rightMargin: 18

    Rectangle { anchors.fill: parent; color: palette.base }

    ScanPlot {
        id: plot
        objectName: "scanPlot"
        anchors.fill: parent
        anchors.leftMargin: root.leftMargin
        anchors.bottomMargin: root.bottomMargin
        anchors.topMargin: root.topMargin
        anchors.rightMargin: root.rightMargin
        gridColor: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, 0.12)

        // Own render pass: the curve buffers are owned by the layer's renderer and are
        // never re-batched because of unrelated scene changes (labels, read-outs, markers).
        // Zoom/pan then only updates the transform matrix.
        layer.enabled: true
    }

    // Markers: overlay above the plot, outside the layer.
    Item {
        id: markerLayer
        x: plot.x; y: plot.y
        width: plot.width; height: plot.height
        clip: true
        z: 1
        // Markers (drawn above the curves, clipped with the plot)
        // The model is the marker count, not the array: `markers` is rebuilt on every value
        // change, and an array model would recreate the delegate under the cursor and end
        // the drag after a single mouse move.
        Repeater {
            model: root.markers.length
            delegate: Item {
                id: marker
                required property int index
                readonly property var modelData: root.markers[index] ?? { value: 0, color: "transparent", label: "", key: "" }
                property real px: { plot.viewRect; plot.width; return plot.toPixelX(modelData.value) }
                x: px - width / 2
                y: 0
                width: root.touchUi ? 40 : 12
                height: markerLayer.height
                visible: px >= -width / 2 && px <= plot.width + width / 2
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: dragArea.containsMouse || dragArea.pressed ? 3 : 2
                    height: parent.height
                    color: marker.modelData.color
                }
                Label {
                    text: marker.modelData.label
                    color: marker.modelData.color
                    font.pixelSize: 11
                    x: marker.width / 2 + 3
                    y: 4
                }
                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.SizeHorCursor
                    preventStealing: true
                    onPositionChanged: (mouse) => {
                        if (!pressed) return
                        const p = mapToItem(plot, mouse.x, mouse.y)
                        root.markerMoved(marker.modelData.key, plot.toDataX(p.x))
                    }
                }
            }
        }
    }

    // Plot frame
    Rectangle {
        x: plot.x - 1; y: plot.y - 1
        width: plot.width + 2; height: plot.height + 2
        color: "transparent"
        border.color: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, 0.45)
    }

    // Tick labels use a fixed pool of items: creating/destroying items on every zoom step
    // would restructure the scene graph and force the renderer to re-batch the curves.
    readonly property int maxTicks: 24
    Repeater {
        model: root.maxTicks
        delegate: Label {
            required property int index
            readonly property var tick: index < plot.xTicks.length ? plot.xTicks[index] : null
            visible: tick !== null && plot.hasData
            text: tick ? tick.label : ""
            font.pixelSize: 11
            x: tick ? plot.x + plot.toPixelX(tick.value) - width / 2 : 0
            y: plot.y + plot.height + 5
        }
    }
    Repeater {
        model: root.maxTicks
        delegate: Label {
            required property int index
            readonly property var tick: index < plot.yTicks.length ? plot.yTicks[index] : null
            visible: tick !== null && plot.hasData
            text: tick ? tick.label : ""
            font.pixelSize: 11
            x: plot.x - width - 6
            y: tick ? plot.y + plot.toPixelY(tick.value) - height / 2 : 0
        }
    }

    Label {
        text: root.xLabel
        anchors.horizontalCenter: plot.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 6
        font.pixelSize: 12
    }
    Label {
        text: root.yLabel
        font.pixelSize: 12
        rotation: -90
        transformOrigin: Item.Center
        x: 14 - width / 2
        y: plot.y + plot.height / 2 - height / 2
    }

    Label {
        anchors.centerIn: plot
        visible: !plot.hasData
        text: root.placeholder
        horizontalAlignment: Text.AlignHCenter
        width: Math.min(implicitWidth, plot.width - 16)
        wrapMode: Text.WordWrap
        opacity: 0.55
    }

    // Cursor read-out
    Label {
        id: readout
        anchors.left: plot.left
        anchors.top: plot.top
        anchors.margins: 6
        font.pixelSize: 11
        visible: (mouse.containsMouse || mouse.pressed) && plot.hasData
        text: { plot.viewRect; plot.width; plot.height  // re-evaluate when the view changes
                return "x = " + plot.toDataX(mouse.mouseX).toPrecision(5) + "   y = " + plot.toDataY(mouse.mouseY).toPrecision(4) }
        background: Rectangle { color: palette.base; opacity: 0.85; radius: 3 }
        padding: 3
    }

    // Rubber band
    Rectangle {
        id: band
        visible: false
        color: Qt.rgba(0.11, 0.31, 0.85, 0.12)
        border.color: Qt.rgba(0.11, 0.31, 0.85, 0.6)
    }

    MouseArea {
        id: mouse
        x: plot.x; y: plot.y
        width: plot.width; height: plot.height
        z: -1  // below markers so they stay draggable
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        property point last
        property point start

        function startBand(px, py) {
            start = Qt.point(px, py)
            band.x = plot.x + px; band.y = plot.y + py
            band.width = 0; band.height = 0
            band.visible = true
        }

        onPressed: (e) => {
            last = Qt.point(e.x, e.y)
            start = last
            if (e.button === Qt.RightButton) startBand(e.x, e.y)
        }
        // Touch has no right button: long press, then drag the zoom box.
        onPressAndHold: (e) => { if (root.touchUi && !band.visible) startBand(e.x, e.y) }
        onPositionChanged: (e) => {
            if (band.visible) {
                band.x = plot.x + Math.min(start.x, e.x)
                band.y = plot.y + Math.min(start.y, e.y)
                band.width = Math.abs(e.x - start.x)
                band.height = Math.abs(e.y - start.y)
            } else if (pressedButtons & Qt.LeftButton) {
                plot.panByPixels(e.x - last.x, e.y - last.y)
                last = Qt.point(e.x, e.y)
            }
        }
        onReleased: (e) => {
            if (band.visible) {
                band.visible = false
                if (band.width > 6 && band.height > 6) plot.zoomToPixelRect(start.x, start.y, e.x, e.y)
            } else if (e.button === Qt.LeftButton && root.curvesSelectable
                       && Math.abs(e.x - start.x) + Math.abs(e.y - start.y) < (root.touchUi ? 16 : 4)) {
                plot.selectedCurve = plot.curveAt(e.x, e.y)  // click (not a pan)
            }
        }
        onDoubleClicked: plot.autoscale()
        onWheel: (w) => {
            const f = Math.pow(1.0015, w.angleDelta.y)
            // Shift: x only, Ctrl: y only (LabVIEW users expect separate axis zoom)
            const fx = (w.modifiers & Qt.ControlModifier) ? 1 : f
            const fy = (w.modifiers & Qt.ShiftModifier) ? 1 : f
            plot.zoomAt(w.x, w.y, fx, fy)
        }
    }

    // Touch: pinch zoom (Android/tablets)
    PinchHandler {
        target: null
        property real lastScale: 1
        onActiveChanged: lastScale = 1
        onActiveScaleChanged: {
            const f = activeScale / lastScale
            lastScale = activeScale
            const p = plot.mapFromItem(root, centroid.position.x, centroid.position.y)
            plot.zoomAt(p.x, p.y, f, f)
        }
    }
}
