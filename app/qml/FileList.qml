// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Opened channels as a collapsible tree: folder (only when channels come from several
// folders) → cell → channel. Selecting a channel row sets controller.currentIndex.
Pane {
    id: root
    property var controller
    padding: 0

    // Collapsed node keys ("f:<group>", "c:<group>/<cell>") → true.
    property var collapsed: ({})
    readonly property var channels: controller.channels
    readonly property var groups: {
        const seen = []
        for (const ch of channels)
            if (seen.indexOf(ch.group) < 0) seen.push(ch.group)
        return seen
    }
    readonly property bool showFolders: groups.length > 1
    readonly property var rows: buildRows(channels, collapsed)

    function folderKey(group) { return "f:" + group }
    function cellKey(group, cell) { return "c:" + group + "/" + cell }

    // Flattened visible rows. kind: "folder" | "cell" | "channel".
    function buildRows(chs, coll) {
        const out = []
        const multi = groups.length > 1
        for (const g of groups) {
            const idx = []
            for (let i = 0; i < chs.length; ++i)
                if (chs[i].group === g) idx.push(i)
            idx.sort((a, b) => chs[a].cell - chs[b].cell
                     || chs[a].channel.localeCompare(chs[b].channel) || a - b)
            const fk = folderKey(g)
            if (multi) {
                const runs = []
                for (const i of idx)
                    if (chs[i].run && runs.indexOf(chs[i].run) < 0) runs.push(chs[i].run)
                out.push({ kind: "folder", key: fk, depth: 0, label: g, detail: runs.join(", "),
                           count: idx.length, members: idx })
                if (coll[fk]) continue
            }
            const base = multi ? 1 : 0
            let k = 0
            while (k < idx.length) {
                const cell = chs[idx[k]].cell
                const members = []
                while (k < idx.length && chs[idx[k]].cell === cell) members.push(idx[k++])
                const ck = cellKey(g, cell)
                const letters = []
                for (const i of members)
                    if (letters.indexOf(chs[i].channel) < 0) letters.push(chs[i].channel)
                out.push({ kind: "cell", key: ck, depth: base, label: qsTr("Cell %1").arg(cell),
                           detail: letters.join(" "), count: members.length, members: members })
                if (coll[ck]) continue
                for (const i of members)
                    out.push({ kind: "channel", key: "", depth: base + 1, index: i, members: [i] })
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
            keys.push(folderKey(ch.group))
            keys.push(cellKey(ch.group, ch.cell))
        }
        return keys
    }
    // Expands the ancestors of a channel so it is visible.
    function reveal(i) {
        if (i < 0 || i >= channels.length) return
        const ch = channels[i]
        const fk = folderKey(ch.group), ck = cellKey(ch.group, ch.cell)
        if (collapsed[fk] || collapsed[ck]) setCollapsed([fk, ck], false)
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
                readonly property bool isChannel: modelData.kind === "channel"
                readonly property var ch: isChannel ? root.channels[modelData.index] : null
                readonly property bool expanded: !isChannel && !root.collapsed[modelData.key]
                // A collapsed node containing the current channel is highlighted instead.
                readonly property bool containsCurrent: modelData.members.indexOf(root.controller.currentIndex) >= 0
                width: ListView.view.width
                highlighted: isChannel ? containsCurrent : (!expanded && containsCurrent)
                leftPadding: 8 + modelData.depth * 14
                topPadding: isChannel ? 6 : 4
                bottomPadding: isChannel ? 6 : 4
                onClicked: {
                    if (isChannel) root.controller.currentIndex = modelData.index
                    else root.setCollapsed([modelData.key], expanded)
                }

                contentItem: Loader {
                    sourceComponent: d.isChannel ? channelRow : nodeRow
                }

                Component {
                    id: nodeRow
                    RowLayout {
                        spacing: 4
                        Label {
                            text: d.expanded ? "▾" : "▸"
                            opacity: 0.7
                            Layout.preferredWidth: 12
                        }
                        Label {
                            text: d.modelData.label
                            font.bold: true
                            elide: Text.ElideMiddle
                            Layout.fillWidth: d.modelData.kind === "folder"
                            Layout.maximumWidth: d.width * 0.6
                        }
                        Label {
                            text: d.modelData.detail
                            opacity: 0.55
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            horizontalAlignment: d.modelData.kind === "folder" ? Text.AlignRight : Text.AlignLeft
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
                    ColumnLayout {
                        spacing: 1
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: qsTr("Channel %1").arg(d.ch ? d.ch.channel : "")
                                font.bold: d.highlighted
                            }
                            Label {
                                text: d.ch ? d.ch.run : ""
                                opacity: 0.55
                                font.pixelSize: 11
                                elide: Text.ElideLeft
                                horizontalAlignment: Text.AlignRight
                                Layout.fillWidth: true
                            }
                        }
                        Label {
                            text: d.ch ? d.ch.subtitle : ""
                            opacity: 0.65
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
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
