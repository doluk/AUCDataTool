// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls

// Pin toggle of a sidebar: upright pin = pinned (stays beside the plots), tilted = floating
// (hides and slides in when the mouse reaches the window edge).
ToolButton {
    id: root
    property bool pinned: true
    implicitWidth: 34
    implicitHeight: 34
    checkable: false
    ToolTip.visible: hovered
    ToolTip.delay: 500
    ToolTip.text: pinned ? qsTr("Unpin: hide the sidebar, show it when the mouse reaches the window edge")
                         : qsTr("Pin: keep the sidebar beside the plots")

    contentItem: Canvas {
        id: canvas
        implicitWidth: 18
        implicitHeight: 18
        readonly property color ink: root.palette.buttonText
        readonly property bool pinned: root.pinned
        onInkChanged: requestPaint()
        onPinnedChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.reset()
            const s = Math.min(width, height) / 24
            c.translate(width / 2, height / 2)
            c.scale(s, s)
            if (!pinned) c.rotate(Math.PI / 4)
            c.strokeStyle = ink
            c.fillStyle = ink
            c.lineWidth = 2
            c.lineCap = "round"
            c.lineJoin = "round"
            // Head, collar and needle of a push pin, centred at the origin.
            c.beginPath()
            c.moveTo(-4, -10); c.lineTo(4, -10); c.lineTo(3, -2); c.lineTo(-3, -2); c.closePath()
            if (pinned) c.fill(); else c.stroke()
            c.beginPath()
            c.moveTo(-7, -1); c.lineTo(7, -1)
            c.moveTo(0, -1); c.lineTo(0, 10)
            c.stroke()
        }
    }
}
