// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root
    property var controller
    padding: 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Label {
            text: qsTr("Data files (%1)").arg(root.controller.files.length)
            font.bold: true
            padding: 8
            Layout.fillWidth: true
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.controller.files
            currentIndex: root.controller.currentIndex
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: d
                required property var modelData
                required property int index
                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                onClicked: root.controller.currentIndex = index
                contentItem: ColumnLayout {
                    spacing: 1
                    Label {
                        text: d.modelData.name
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                        font.bold: d.highlighted
                    }
                    Label {
                        text: d.modelData.error
                              ? qsTr("⚠ %1").arg(d.modelData.error)
                              : qsTr("%1 · %2 · %3 scans").arg(d.modelData.type || "?")
                                    .arg(d.modelData.triple || "").arg(d.modelData.scans || 0)
                        color: d.modelData.error ? "#c0262d" : palette.text
                        opacity: d.modelData.error ? 1 : 0.65
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                text: qsTr("No files")
                opacity: 0.5
            }
        }

        Button {
            text: qsTr("Close all")
            flat: true
            enabled: root.controller.files.length > 0
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
