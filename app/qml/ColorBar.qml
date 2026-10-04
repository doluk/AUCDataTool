import QtQuick
import QtQuick.Controls

// Small legend mapping scan order to colour.
Rectangle {
    id: root
    property int colormap: 0
    property string firstLabel
    property string lastLabel

    width: 150
    height: 38
    radius: 4
    color: Qt.rgba(palette.base.r, palette.base.g, palette.base.b, 0.88)
    border.color: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, 0.2)

    // Stops approximate the C++ colour maps (Viridis, Turbo, Rainbow, Grey).
    readonly property var stops: [
        ["#440154", "#3b528b", "#21918c", "#5ec962", "#fde725"],
        ["#30123b", "#4686fb", "#1ae4b6", "#a2fc3c", "#fb7e21", "#7a0403"],
        ["#cc00e6", "#0033e6", "#00e6e6", "#00e600", "#e6e600", "#e60000"],
        ["#bfbfbf", "#8c8c8c", "#595959", "#262626"]
    ]

    Rectangle {
        id: bar
        x: 8; y: 6
        width: parent.width - 16
        height: 9
        radius: 2
        gradient: Gradient {
            id: grad
            orientation: Gradient.Horizontal
        }
        Component.onCompleted: rebuild()
        function rebuild() {
            const s = root.stops[Math.max(0, Math.min(3, root.colormap))]
            const list = []
            for (let i = 0; i < s.length; ++i)
                list.push(Qt.createQmlObject('import QtQuick; GradientStop {}', grad))
            for (let i = 0; i < s.length; ++i) {
                list[i].position = i / (s.length - 1)
                list[i].color = s[i]
            }
            grad.stops = list
        }
        Connections {
            target: root
            function onColormapChanged() { bar.rebuild() }
        }
    }
    Label { text: root.firstLabel; font.pixelSize: 10; x: 8; y: 18; opacity: 0.8 }
    Label { text: root.lastLabel; font.pixelSize: 10; anchors.right: parent.right; anchors.rightMargin: 8; y: 18; opacity: 0.8 }
}
