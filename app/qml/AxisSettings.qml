// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Auc.DataTool

// Scale (linear/log) and limits of plot axes. A typed limit is locked: autoscaling keeps it
// and only fits the free limits. Unlocking a limit lets it follow the data again.
Popup {
    id: root
    /// [{ title, axis: PlotAxis }]
    property var axes: []
    /// 2D plot whose autoscale this panel controls; null for the 3D surface, whose free
    /// limits always follow the data.
    property ScanPlot plot: null
    modal: false
    focus: true

    property double closedAt: 0
    onClosed: closedAt = Date.now()
    /// For a toggle button: the press on it that closed the popup is followed by a click.
    function closedJustNow() { return Date.now() - closedAt < 300 }
    margins: 8
    padding: 10

    // Shows the axis limit; Enter/focus loss with an edited value locks it there.
    component LimitField: TextField {
        id: lf
        required property PlotAxis axis
        required property bool upper
        readonly property real shown: upper ? (axis.maxLocked ? axis.maxLimit : axis.max)
                                            : (axis.minLocked ? axis.minLimit : axis.min)
        function format(v) { return Number(v.toPrecision(6)).toString() }
        text: format(shown)
        onShownChanged: if (!activeFocus) text = format(shown)
        Connections {  // a rejected value: show the limit again
            target: lf.axis
            function onRangeChanged() { if (!lf.activeFocus) lf.text = lf.format(lf.shown) }
        }
        validator: DoubleValidator { locale: "C" }
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        horizontalAlignment: Text.AlignRight
        selectByMouse: true
        implicitWidth: 110
        onEditingFinished: {
            if (text === format(shown)) return  // focus moved on without an edit
            if (!axis.setLimit(upper, Number(text))) text = format(shown)
        }
        ToolTip.visible: hovered && axis.log
        ToolTip.text: qsTr("Logarithmic axis: limits must be > 0")
    }

    contentItem: ColumnLayout {
        spacing: 6

        RowLayout {
            visible: root.plot !== null
            Layout.fillWidth: true
            CheckBox {
                text: qsTr("Autoscale continuously")
                checked: root.plot ? root.plot.autoscaleContinuous : false
                onToggled: root.plot.autoscaleContinuous = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Fit the free limits whenever the data changes, e.g. while dragging the offset marker")
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Autoscale")
                enabled: root.plot ? root.plot.hasData : false
                onClicked: root.plot.autoscale()
            }
        }
        Label {
            visible: root.plot === null
            text: qsTr("Unlocked limits follow the data.")
            opacity: 0.7
            font.pixelSize: 11
        }

        Repeater {
            model: root.axes
            delegate: GridLayout {
                id: axisRow
                required property var modelData
                readonly property PlotAxis axis: modelData.axis
                columns: 3
                columnSpacing: 8
                rowSpacing: 2
                Layout.fillWidth: true
                Layout.topMargin: 6

                Label {
                    text: axisRow.modelData.title
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.maximumWidth: 180
                }
                ComboBox {
                    model: [qsTr("Linear"), qsTr("Logarithmic")]
                    currentIndex: axisRow.axis.log ? 1 : 0
                    onActivated: (i) => axisRow.axis.log = i === 1
                    implicitContentWidthPolicy: ComboBox.WidestText
                    Layout.columnSpan: 2
                    Layout.alignment: Qt.AlignRight
                }

                Label { text: qsTr("Min") }
                LimitField { axis: axisRow.axis; upper: false; Layout.fillWidth: true }
                CheckBox {
                    text: qsTr("Lock")
                    checked: axisRow.axis.minLocked
                    onToggled: axisRow.axis.minLocked = checked
                }
                Label { text: qsTr("Max") }
                LimitField { axis: axisRow.axis; upper: true; Layout.fillWidth: true }
                CheckBox {
                    text: qsTr("Lock")
                    checked: axisRow.axis.maxLocked
                    onToggled: axisRow.axis.maxLocked = checked
                }
            }
        }
    }
}
