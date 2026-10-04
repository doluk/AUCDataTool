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
            text: qsTr("Channels (%1)").arg(root.controller.channels.length)
            font.bold: true
            padding: 8
            Layout.fillWidth: true
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.controller.channels
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
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: d.modelData.title
                            font.bold: d.highlighted
                        }
                        Label {
                            text: d.modelData.run
                            opacity: 0.55
                            font.pixelSize: 11
                            elide: Text.ElideLeft
                            horizontalAlignment: Text.AlignRight
                            Layout.fillWidth: true
                        }
                    }
                    Label {
                        text: d.modelData.subtitle
                        opacity: 0.65
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
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
            enabled: root.controller.channels.length > 0
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
