import QtQuick
import QtQuick.Controls
import CompactPhone

AbstractButton {
    id: root
    property string themeId: ""
    property string name: ""
    property bool isCurrent: false

    readonly property QtObject p: Theme.paletteFor(themeId)

    implicitWidth: 124
    implicitHeight: 72
    hoverEnabled: true

    background: Rectangle {
        radius: Theme.r10
        color: Theme.surface
        border.color: root.isCurrent ? Theme.accent
                    : (root.hovered ? Theme.borderStrong : Theme.border)
        border.width: root.isCurrent ? 2 : 1
    }

    contentItem: Item {
        Rectangle {
            id: preview
            anchors.top: parent.top; anchors.topMargin: Theme.s10
            anchors.left: parent.left; anchors.leftMargin: Theme.s10
            anchors.right: parent.right; anchors.rightMargin: Theme.s10
            height: 36
            radius: Theme.r6
            color: root.p.bg
            border.color: root.p.border
            border.width: 1
            clip: true

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 32
                color: root.p.bgElevated
                Column {
                    anchors.left: parent.left; anchors.leftMargin: 4
                    anchors.top: parent.top; anchors.topMargin: 6
                    spacing: 3
                    Repeater {
                        model: 3
                        delegate: Rectangle {
                            width: 22; height: 4; radius: 2
                            color: index === 0 ? root.p.accent : root.p.surfaceHi
                        }
                    }
                }
            }
            Rectangle {
                anchors.left: parent.left; anchors.leftMargin: 36
                anchors.right: parent.right; anchors.rightMargin: 4
                anchors.top: parent.top; anchors.topMargin: 6
                anchors.bottom: parent.bottom; anchors.bottomMargin: 6
                radius: 4
                color: root.p.surface
                border.color: root.p.border
                Rectangle {
                    anchors.right: parent.right; anchors.rightMargin: 4
                    anchors.top: parent.top; anchors.topMargin: 4
                    width: 14; height: 4; radius: 2
                    color: root.p.accent
                }
            }
        }

        Text {
            anchors.left: parent.left; anchors.leftMargin: Theme.s10
            anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.s10
            text: root.name
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsm
            font.weight: Font.DemiBold
        }
        AppIcon {
            visible: root.isCurrent
            anchors.right: parent.right; anchors.rightMargin: Theme.s10
            anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.s10
            width: 14; height: 14
            path: Icons.check
            color: Theme.accent
            stroke: 2.6
        }
    }
}
