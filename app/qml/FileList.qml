// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Opened channels as a collapsible tree: run → optics (what was measured, from which file
// format) → channel. Runs carry a colour accent that continues down their channel rows;
// channels show a "cell + channel" badge in the colour of their optics. Selecting a channel
// row sets controller.currentIndex.
Pane {
    id: root
    property var controller
    /// Sidebar pinned (shown beside the plots) or floating (slides in from the edge).
    property bool pinned: true
    property bool showPin: true
    signal pinToggled()
    padding: 0

    // Collapsed node keys ("r:<run>", "o:<run>/<optics>") → true.
    property var collapsed: ({})
    readonly property var channels: controller.channels
    readonly property var rows: buildRows(channels, collapsed)

    readonly property var runPalette: ["#2563eb", "#d97706", "#059669", "#db2777", "#7c3aed", "#0891b2", "#65a30d", "#dc2626"]
    function opticsColor(optics) {
        if (optics.startsWith(qsTr("Absorbance"))) return "#2563eb"
        if (optics.startsWith(qsTr("Intensity"))) return "#d97706"
        if (optics === qsTr("Interference")) return "#7c3aed"
        if (optics === qsTr("Fluorescence")) return "#059669"
        return "#6b7280"
    }

    function runKey(ch) { return ch.group + "\u0001" + ch.run }
    function opticsName(ch) { return ch.optics + " · " + ch.source }

    // Flattened visible rows. kind: "run" | "optics" | "channel".
    function buildRows(chs, coll) {
        const out = []
        const runs = []
        for (const ch of chs)
            if (runs.indexOf(runKey(ch)) < 0) runs.push(runKey(ch))
        for (let r = 0; r < runs.length; ++r) {
            const rk = runs[r]
            const color = runPalette[r % runPalette.length]
            const idx = []
            for (let i = 0; i < chs.length; ++i)
                if (runKey(chs[i]) === rk) idx.push(i)
            const first = chs[idx[0]]
            const name = first.run !== "" ? first.run : first.group
            out.push({ kind: "run", key: "r:" + rk, label: name,
                       detail: first.group !== name ? first.group : "", tooltip: first.folder,
                       count: idx.length, members: idx, runColor: color })
            if (coll["r:" + rk]) continue

            const optics = []
            for (const i of idx)
                if (optics.indexOf(opticsName(chs[i])) < 0) optics.push(opticsName(chs[i]))
            for (const o of optics) {
                const members = idx.filter(i => opticsName(chs[i]) === o)
                members.sort((a, b) => chs[a].cell - chs[b].cell || chs[a].channel.localeCompare(chs[b].channel) || a - b)
                const ok = "o:" + rk + "/" + o
                const ch0 = chs[members[0]]
                out.push({ kind: "optics", key: ok, label: ch0.optics, detail: ch0.source,
                           count: members.length, members: members, runColor: color,
                           opticsColor: opticsColor(ch0.optics) })
                if (coll[ok]) continue
                for (const i of members)
                    out.push({ kind: "channel", key: "", index: i, members: [i], runColor: color,
                               opticsColor: opticsColor(chs[i].optics) })
            }
        }
        return out
    }

    function setCollapsed(keys, value) {
        const c = Object.assign({}, collapsed)
        for (const key of keys) {
            if (value) c[key] = true
            else delete c[key]
        }
        collapsed = c
    }
    function allNodeKeys() {
        const keys = []
        for (const ch of channels) {
            keys.push("r:" + runKey(ch))
            keys.push("o:" + runKey(ch) + "/" + opticsName(ch))
        }
        return keys
    }
    // Expands the ancestors of a channel so it is visible.
    function reveal(i) {
        if (i < 0 || i >= channels.length) return
        const ch = channels[i]
        const rk = "r:" + runKey(ch), ok = "o:" + runKey(ch) + "/" + opticsName(ch)
        if (collapsed[rk] || collapsed[ok]) setCollapsed([rk, ok], false)
        for (let r = 0; r < rows.length; ++r)
            if (rows[r].kind === "channel" && rows[r].index === i) {
                list.positionViewAtIndex(r, ListView.Contain)
                break
            }
    }

    Connections {
        target: root.controller
        function onCurrentIndexChanged() { root.reveal(root.controller.currentIndex) }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Label {
                text: qsTr("Channels (%1)").arg(root.channels.length)
                font.bold: true
                padding: 8
                Layout.fillWidth: true
            }
            ToolButton {
                text: "⊞"
                enabled: root.channels.length > 0
                onClicked: root.collapsed = ({})
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Expand all")
            }
            ToolButton {
                text: "⊟"
                enabled: root.channels.length > 0
                onClicked: root.setCollapsed(root.allNodeKeys(), true)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Collapse all")
            }
            PinButton {
                visible: root.showPin
                pinned: root.pinned
                onClicked: root.pinToggled()
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.rows
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: d
                required property var modelData
                required property int index
                readonly property string kind: modelData.kind
                readonly property bool isChannel: kind === "channel"
                readonly property var ch: isChannel ? root.channels[modelData.index] : null
                readonly property bool expanded: !isChannel && !root.collapsed[modelData.key]
                // A collapsed node containing the current channel is highlighted instead.
                readonly property bool containsCurrent: modelData.members.indexOf(root.controller.currentIndex) >= 0
                width: ListView.view.width
                highlighted: isChannel ? containsCurrent : (!expanded && containsCurrent)
                leftPadding: (kind === "run" ? 10 : kind === "optics" ? 18 : 26)
                rightPadding: 8
                topPadding: kind === "run" ? 8 : kind === "optics" ? 4 : 5
                bottomPadding: kind === "run" ? 6 : kind === "optics" ? 3 : 5
                onClicked: {
                    if (isChannel) root.controller.currentIndex = modelData.index
                    else root.setCollapsed([modelData.key], expanded)
                }
                ToolTip.visible: hovered && kind === "run" && (modelData.tooltip ?? "") !== ""
                ToolTip.delay: 600
                ToolTip.text: modelData.tooltip ?? ""

                // Run colour: a band on the left of every row of the run.
                Rectangle {
                    x: 0
                    width: d.kind === "run" ? 5 : 3
                    height: parent.height
                    color: d.modelData.runColor
                    opacity: d.kind === "run" ? 1 : 0.55
                }
                // Separator above each run (except the first).
                Rectangle {
                    visible: d.kind === "run" && d.index > 0
                    width: parent.width
                    height: 1
                    color: d.palette.mid
                    opacity: 0.6
                }

                contentItem: Loader {
                    sourceComponent: d.kind === "run" ? runRow : d.kind === "optics" ? opticsRow : channelRow
                }

                Component {
                    id: runRow
                    RowLayout {
                        spacing: 6
                        Label {
                            text: d.expanded ? "▾" : "▸"
                            opacity: 0.7
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: 10
                        }
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            Label {
                                text: d.modelData.label
                                font.bold: true
                                font.pixelSize: 14
                                color: d.modelData.runColor
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: text !== ""
                                text: d.modelData.detail
                                opacity: 0.6
                                font.pixelSize: 11
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                        Label {
                            text: d.modelData.count
                            opacity: 0.55
                            font.pixelSize: 11
                            Layout.alignment: Qt.AlignTop
                        }
                    }
                }

                Component {
                    id: opticsRow
                    RowLayout {
                        spacing: 6
                        Label {
                            text: d.expanded ? "▾" : "▸"
                            opacity: 0.6
                            Layout.preferredWidth: 10
                        }
                        Rectangle {
                            width: 9; height: 9; radius: 4.5
                            color: d.modelData.opticsColor
                        }
                        Label {
                            text: d.modelData.label
                            font.bold: true
                            font.pixelSize: 12
                            color: d.modelData.opticsColor
                        }
                        Label {
                            text: d.modelData.detail
                            opacity: 0.6
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: d.modelData.count
                            opacity: 0.55
                            font.pixelSize: 11
                        }
                    }
                }

                Component {
                    id: channelRow
                    RowLayout {
                        spacing: 8
                        // Cell + channel badge, e.g. "2A", in the optics colour.
                        Rectangle {
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 24
                            Layout.alignment: Qt.AlignVCenter
                            radius: 4
                            color: Qt.alpha(d.modelData.opticsColor, d.highlighted ? 0.9 : 0.15)
                            border.color: d.modelData.opticsColor
                            border.width: 1
                            Label {
                                anchors.centerIn: parent
                                text: d.ch ? d.ch.cell + d.ch.channel : ""
                                font.bold: true
                                font.pixelSize: 13
                                color: d.highlighted ? "white" : d.modelData.opticsColor
                            }
                        }
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            Label {
                                text: d.ch ? (d.ch.description !== "" ? d.ch.description : qsTr("Cell %1, channel %2").arg(d.ch.cell).arg(d.ch.channel)) : ""
                                font.bold: d.highlighted
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: {
                                    if (!d.ch) return ""
                                    const parts = []
                                    if (d.ch.wavelengthText !== "") parts.push(d.ch.wavelengthText)
                                    parts.push(d.ch.scanText)
                                    if (d.ch.isReference) parts.push(qsTr("reference"))
                                    return parts.join(" · ")
                                }
                                opacity: 0.6
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                text: qsTr("No data")
                opacity: 0.5
            }
        }

        Button {
            text: qsTr("Close all")
            flat: true
            enabled: root.channels.length > 0
            onClicked: root.controller.closeAll()
            Layout.fillWidth: true
        }
    }

    // Drag and drop of files/folders onto the list.
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            if (!drop.hasUrls) return
            root.controller.openFiles(drop.urls)
        }
    }
}
