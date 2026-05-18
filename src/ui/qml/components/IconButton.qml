import QtQuick
import QtQuick.Controls
import CompactPhone

AbstractButton {
    id: root
    property string iconPath: ""
    property color tint: Theme.textSecondary
    property color hoverTint: Theme.textPrimary
    property color bgColor: "transparent"
    property color bgHover: Theme.surfaceHi
    property int diameter: 36
    property int iconSize: 18
    property bool circular: true

    implicitWidth: diameter
    implicitHeight: diameter
    hoverEnabled: true

    background: Rectangle {
        anchors.fill: parent
        radius: root.circular ? width / 2 : Theme.r8
        color: !root.enabled ? "transparent"
            : root.pressed ? Qt.darker(root.bgHover, 1.1)
            : root.hovered ? root.bgHover
            : root.bgColor
    }

    contentItem: Item {
        anchors.fill: parent
        AppIcon {
            anchors.centerIn: parent
            path: root.iconPath
            color: root.hovered ? root.hoverTint : root.tint
            width: root.iconSize
            height: root.iconSize
        }
    }
}
