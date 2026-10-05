// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls

// Tool button with a small vector icon (no image plugins or symbol fonts needed).
ToolButton {
    id: root
    /// "menu" (channels), "more" (overflow menu), "tune" (options), "fit" (autoscale)
    property string glyph: "menu"
    implicitWidth: 48
    implicitHeight: 48

    contentItem: Canvas {
        id: canvas
        implicitWidth: 24
        implicitHeight: 24
        readonly property color ink: root.icon.color  // style's text colour (white on a Material toolbar)
        onInkChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.reset()
            const s = Math.min(width, height) / 24
            c.translate((width - 24 * s) / 2, (height - 24 * s) / 2)
            c.scale(s, s)
            c.strokeStyle = ink
            c.fillStyle = ink
            c.lineWidth = 2
            c.lineCap = "round"
            c.beginPath()
            switch (root.glyph) {
            case "menu":
                for (const y of [6, 12, 18]) { c.moveTo(4, y); c.lineTo(20, y) }
                c.stroke()
                break
            case "more":
                for (const y of [5, 12, 19]) { c.moveTo(14, y); c.arc(12, y, 2, 0, 2 * Math.PI) }
                c.fill()
                break
            case "tune":
                for (const y of [6, 12, 18]) { c.moveTo(4, y); c.lineTo(20, y) }
                c.stroke()
                c.beginPath()
                for (const [y, k] of [[6, 15], [12, 8], [18, 13]]) { c.moveTo(k + 2.5, y); c.arc(k, y, 2.5, 0, 2 * Math.PI) }
                c.fill()
                break
            case "fit":
                for (const [x, y, dx, dy] of [[4, 4, 1, 1], [20, 4, -1, 1], [4, 20, 1, -1], [20, 20, -1, -1]]) {
                    c.moveTo(x, y + 5 * dy); c.lineTo(x, y); c.lineTo(x + 5 * dx, y)
                }
                c.stroke()
                break
            }
        }
    }
}
