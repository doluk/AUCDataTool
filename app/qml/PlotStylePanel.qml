// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Auc.DataTool

// Curve and grid styling controls for any 2D ScanPlot (reused in the Options panel and in
// each plot's Axes & Style popup).
ColumnLayout {
    id: root
    property ScanPlot plot: null
    property bool showGridToggle: false
    property int listHeight: 170
    spacing: 6

    readonly property bool singleCurve: root.plot !== null && root.plot.curveCount === 1
    readonly property var singleCurveInfo: {
        if (!root.singleCurve) return ({})
        root.plot.styleRevision
        return root.plot.curveInfo(0)
    }
    readonly property var singleCurveStyle: {
        if (!root.singleCurve || singleCurveInfo.id === undefined) return ({})
        root.plot.styleRevision
        return root.plot.curveStyle(singleCurveInfo.id)
    }

    // Line/marker controls for a CurveStyle map (see CurveStyle::toMap); emits partial changes.
    component StyleEditor: GridLayout {
        id: se
        property var style: ({})
        property bool showColor: true
        property bool showVisible: true
        signal edited(var changes)
        columns: 2
        columnSpacing: 8

        CheckBox {
            visible: se.showVisible
            Layout.columnSpan: 2
            text: qsTr("Visible")
            checked: se.style.visible ?? true
            onToggled: se.edited({ visible: checked })
        }
        Label { text: qsTr("Colour"); visible: se.showColor }
        Button {
            visible: se.showColor
            Layout.fillWidth: true
            implicitHeight: 28
            contentItem: Rectangle {
                color: se.style.color ?? "black"
                radius: 2
                border.color: Qt.rgba(0, 0, 0, 0.3)
            }
            onClicked: colorDialog.open()
        }
        Label { text: qsTr("Line") }
        ComboBox {
            Layout.fillWidth: true
            model: [qsTr("Solid"), qsTr("Dashed"), qsTr("Dotted"), qsTr("Dash-dot"), qsTr("None")]
            currentIndex: se.style.line ?? 0
            onActivated: (i) => se.edited({ line: i })
        }
        Label { text: qsTr("Width (px)") }
        SpinBox {
            Layout.fillWidth: true
            from: 1
            to: 20
            editable: true
            value: Math.round(se.style.width ?? 1)
            onValueModified: se.edited({ width: value })
        }
        Label { text: qsTr("Markers") }
        ComboBox {
            Layout.fillWidth: true
            model: [qsTr("None"), qsTr("Circle"), qsTr("Square"), qsTr("Triangle"), qsTr("Diamond"), qsTr("Plus")]
            currentIndex: se.style.marker ?? 0
            onActivated: (i) => se.edited({ marker: i })
        }
        Label { text: qsTr("Marker size (px)"); visible: (se.style.marker ?? 0) > 0 }
        SpinBox {
            visible: (se.style.marker ?? 0) > 0
            Layout.fillWidth: true
            from: 2
            to: 40
            editable: true
            value: Math.round(se.style.markerSize ?? 6)
            onValueModified: se.edited({ markerSize: value })
        }
        ColorDialog {
            id: colorDialog
            title: qsTr("Curve colour")
            selectedColor: se.style.color ?? "black"
            onAccepted: se.edited({ color: selectedColor })
        }
    }

    CheckBox {
        visible: root.showGridToggle && root.plot !== null
        text: qsTr("Show grid")
        checked: root.plot ? root.plot.showGrid : false
        onToggled: if (root.plot) root.plot.showGrid = checked
    }

    // Single-curve plots (e.g. temperature, integral, per-cell timing): edit colour, line and
    // markers directly without requiring a list selection.
    ColumnLayout {
        visible: root.singleCurve
        Layout.fillWidth: true
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.singleCurveInfo.label ?? qsTr("Curve")
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Reset")
                flat: true
                enabled: root.plot !== null && (root.plot.hasCustomStyles || root.plot.hasCustomDefaultStyle)
                onClicked: {
                    root.plot.resetCurveStyles()
                    root.plot.resetDefaultStyle()
                }
            }
        }
        StyleEditor {
            Layout.fillWidth: true
            style: root.singleCurveStyle
            onEdited: (changes) => {
                if (root.plot && root.singleCurveInfo.id !== undefined)
                    root.plot.setCurveStyle(root.singleCurveInfo.id, changes)
            }
        }
    }

    // Multi-curve plots (or empty plots): default style for all curves + individual overrides.
    ColumnLayout {
        visible: !root.singleCurve
        enabled: root.plot !== null && root.plot.hasData
        Layout.fillWidth: true
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("All curves"); font.bold: true; Layout.fillWidth: true }
            Button {
                text: qsTr("Reset")
                flat: true
                enabled: root.plot !== null && root.plot.hasCustomDefaultStyle
                onClicked: root.plot.resetDefaultStyle()
            }
        }
        StyleEditor {
            Layout.fillWidth: true
            showColor: false
            showVisible: false
            style: root.plot ? root.plot.defaultStyle : ({})
            onEdited: (changes) => { if (root.plot) root.plot.defaultStyle = changes }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            Label { text: qsTr("Individual curves"); font.bold: true; Layout.fillWidth: true }
            Button {
                text: qsTr("Reset all")
                flat: true
                enabled: root.plot !== null && root.plot.hasCustomStyles
                onClicked: root.plot.resetCurveStyles()
            }
        }
        ListView {
            id: curveList
            Layout.fillWidth: true
            Layout.preferredHeight: root.plot ? Math.min(root.listHeight, Math.max(52, root.plot.curveCount * 26)) : 52
            clip: true
            // Count as model, rows fetch their curve: an array model would recreate all
            // rows on every style edit and lose the scroll position.
            model: root.plot ? root.plot.curveCount : 0
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property int index
                readonly property var curve: {
                    if (!root.plot) return ({})
                    root.plot.styleRevision
                    return root.plot.curveInfo(index)
                }
                width: ListView.view.width
                height: 26
                padding: 2
                highlighted: root.plot !== null && curve.id === root.plot.selectedCurve
                onClicked: if (root.plot) root.plot.selectedCurve = (highlighted ? -1 : curve.id)
                contentItem: RowLayout {
                    spacing: 6
                    CheckBox {
                        padding: 0
                        checked: row.curve.visible ?? true
                        onToggled: if (root.plot) root.plot.setCurveStyle(row.curve.id, { visible: checked })
                    }
                    Rectangle {
                        implicitWidth: 18
                        implicitHeight: 10
                        radius: 2
                        color: row.curve.color ?? "transparent"
                    }
                    Label {
                        text: row.curve.label ?? ""
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        font.italic: row.curve.custom ?? false
                    }
                }
            }
            Connections {
                target: root.plot
                function onStylesChanged() {
                    if (!root.plot) return
                    const i = root.plot.selectedIndex
                    if (i >= 0) curveList.positionViewAtIndex(i, ListView.Contain)
                }
            }
        }

        ColumnLayout {
            visible: root.plot !== null && root.plot.selectedCurve >= 0
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: root.plot && root.plot.selectedIndex >= 0
                      ? (root.plot.curveInfo(root.plot.selectedIndex).label ?? "")
                      : ""
                font.bold: true
            }
            StyleEditor {
                Layout.fillWidth: true
                style: root.plot ? root.plot.selectedStyle : ({})
                onEdited: (changes) => {
                    if (root.plot) root.plot.setCurveStyle(root.plot.selectedCurve, changes)
                }
            }
            RowLayout {
                Button {
                    text: qsTr("Reset to default")
                    enabled: root.plot !== null && (root.plot.selectedStyle.custom ?? false)
                    onClicked: root.plot.resetCurveStyle(root.plot.selectedCurve)
                }
                Button {
                    text: qsTr("Deselect")
                    onClicked: root.plot.selectedCurve = -1
                }
            }
        }
        Label {
            visible: root.plot === null || root.plot.selectedCurve < 0
            text: qsTr("Click a curve in the plot or in the list above to edit it.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            opacity: 0.6
            font.pixelSize: 11
        }
    }
}
