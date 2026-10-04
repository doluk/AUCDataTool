import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var controller
    property var plot
    readonly property bool hasData: controller.scanCount > 0
    spacing: 10

    // Numeric entry bound to a controller property; commits on Enter/focus loss.
    component NumberField: TextField {
        id: nf
        property real value
        property int decimals: 4
        signal committed(real v)
        text: value.toFixed(decimals)
        validator: DoubleValidator { locale: "C" }
        horizontalAlignment: Text.AlignRight
        selectByMouse: true
        onEditingFinished: committed(Number(text))
        onValueChanged: if (!activeFocus) text = value.toFixed(decimals)
    }

    GroupBox {
        title: qsTr("Scans")
        Layout.fillWidth: true
        Layout.topMargin: 6
        enabled: root.hasData
        GridLayout {
            anchors.fill: parent
            columns: 2
            Label { text: qsTr("From") }
            SpinBox {
                from: 1; to: Math.max(1, root.controller.scanCount)
                value: root.controller.firstScan + 1
                editable: true
                onValueModified: root.controller.firstScan = value - 1
                Layout.fillWidth: true
            }
            Label { text: qsTr("To") }
            SpinBox {
                from: 1; to: Math.max(1, root.controller.scanCount)
                value: root.controller.lastScan < 0 ? root.controller.scanCount : root.controller.lastScan + 1
                editable: true
                onValueModified: root.controller.lastScan = (value >= root.controller.scanCount ? -1 : value - 1)
                Layout.fillWidth: true
            }
            Label { text: qsTr("Every n-th") }
            SpinBox {
                from: 1; to: 1000
                value: root.controller.everyNth
                editable: true
                onValueModified: root.controller.everyNth = value
                Layout.fillWidth: true
            }
        }
    }

    GroupBox {
        title: qsTr("Corrections")
        Layout.fillWidth: true
        enabled: root.hasData
        ColumnLayout {
            anchors.fill: parent
            CheckBox {
                text: qsTr("Reverse radius")
                checked: root.controller.reverse
                onToggled: root.controller.reverse = checked
            }
            CheckBox {
                text: qsTr("Remove spikes (median 3)")
                checked: root.controller.removeSpikes
                onToggled: root.controller.removeSpikes = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Median filter, left rank 2 / right rank 0 – as in the LabVIEW AUC-Viewer")
            }
            Label { text: qsTr("Offset"); Layout.topMargin: 4 }
            ComboBox {
                Layout.fillWidth: true
                model: [qsTr("None"), qsTr("Point (drag marker)"), qsTr("Baseline region (mean)")]
                currentIndex: root.controller.offsetMode
                onActivated: (i) => root.controller.offsetMode = i
            }
            GridLayout {
                columns: 2
                visible: root.controller.offsetMode > 0
                Layout.fillWidth: true
                Label { text: root.controller.offsetMode === 1 ? qsTr("r (cm)") : qsTr("r₁ (cm)") }
                NumberField {
                    Layout.fillWidth: true
                    value: root.controller.offsetR1
                    onCommitted: (v) => root.controller.offsetR1 = v
                }
                Label { text: qsTr("r₂ (cm)"); visible: root.controller.offsetMode === 2 }
                NumberField {
                    Layout.fillWidth: true
                    visible: root.controller.offsetMode === 2
                    value: root.controller.offsetR2
                    onCommitted: (v) => root.controller.offsetR2 = v
                }
            }
        }
    }

    GroupBox {
        title: qsTr("Radial integration")
        Layout.fillWidth: true
        enabled: root.hasData
        ColumnLayout {
            anchors.fill: parent
            CheckBox {
                text: qsTr("Integrate each scan")
                checked: root.controller.integrate
                onToggled: root.controller.integrate = checked
            }
            GridLayout {
                columns: 2
                visible: root.controller.integrate
                Layout.fillWidth: true
                Label { text: qsTr("r₁ (cm)") }
                NumberField {
                    Layout.fillWidth: true
                    value: root.controller.intR1
                    onCommitted: (v) => root.controller.intR1 = v
                }
                Label { text: qsTr("r₂ (cm)") }
                NumberField {
                    Layout.fillWidth: true
                    value: root.controller.intR2
                    onCommitted: (v) => root.controller.intR2 = v
                }
            }
            CheckBox {
                visible: root.controller.integrate
                text: qsTr("Weight with r (∫A·r dr)")
                checked: root.controller.radialWeight
                onToggled: root.controller.radialWeight = checked
            }
        }
    }

    GroupBox {
        title: qsTr("Display")
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent
            Label { text: qsTr("Scan colours") }
            ComboBox {
                Layout.fillWidth: true
                model: ["Viridis", "Turbo", qsTr("Rainbow (LabVIEW)"), qsTr("Grey")]
                currentIndex: root.controller.colormap
                onActivated: (i) => root.controller.colormap = i
            }
            CheckBox {
                text: qsTr("Grid")
                checked: root.plot.showGrid
                onToggled: root.plot.showGrid = checked
            }
        }
    }

    Item { Layout.fillHeight: true }
}
