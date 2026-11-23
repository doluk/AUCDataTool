// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A titled frame whose content folds away on a click on the title (used like GroupBox:
// one child, typically a layout with anchors.fill: parent). The folded state is kept per
// title across sessions.
ColumnLayout {
    id: root
    property string title
    property bool collapsed: false
    /// Enables the controls of the section; the title always stays clickable.
    property bool contentEnabled: true
    /// Key under which the folded state is stored; empty: not stored.
    property string settingsKey: title
    default property alias content: frame.contentData
    spacing: 2
    Layout.fillWidth: true

    Settings {
        id: settings
        category: "optionSections"
    }
    Component.onCompleted: if (settingsKey !== "") collapsed = settings.value(settingsKey, false) === true
                                                              || settings.value(settingsKey, false) === "true"
    function toggle() {
        collapsed = !collapsed
        if (settingsKey !== "") settings.setValue(settingsKey, collapsed)
    }

    ItemDelegate {
        id: header
        Layout.fillWidth: true
        topPadding: 4
        bottomPadding: 4
        leftPadding: 2
        onClicked: root.toggle()
        ToolTip.visible: hovered
        ToolTip.delay: 800
        ToolTip.text: root.collapsed ? qsTr("Show %1").arg(root.title) : qsTr("Hide %1").arg(root.title)
        contentItem: RowLayout {
            spacing: 6
            Label {
                text: root.collapsed ? "▸" : "▾"
                opacity: 0.7
                Layout.preferredWidth: 10
            }
            Label {
                text: root.title
                font.bold: true
                opacity: root.contentEnabled ? 1 : 0.6
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }

    Frame {
        id: frame
        visible: !root.collapsed
        enabled: root.contentEnabled
        Layout.fillWidth: true
    }
}
