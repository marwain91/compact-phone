import QtQuick
import QtQuick.Controls
import CompactPhone

AbstractButton {
    id: root
    property string iconPath: ""
    property string label: ""
    property bool active: false

    padding: 0
    width: 38
    height: 38
    hoverEnabled: true

    ToolTip.visible: hovered && label.length > 0
    ToolTip.text: label
    ToolTip.delay: 400

    background: Rectangle {
        anchors.fill: parent
        radius: Theme.r8
        color: root.active
            ? Theme.accentSoft
            : (root.hovered ? Theme.surfaceHi : "transparent")

        Rectangle {
            visible: root.active
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: parent.height * 0.5
            radius: 2
            color: Theme.accent
        }
    }

    contentItem: Item {
        anchors.fill: parent
        AppIcon {
            anchors.centerIn: parent
            width: 18; height: 18
            path: root.iconPath
            color: root.active ? Theme.accent : Theme.textSecondary
        }
    }
}
