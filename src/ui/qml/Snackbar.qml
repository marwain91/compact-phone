import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

Rectangle {
    id: bar
    color: Theme.surfaceHi
    opacity: PhoneController.notice.length > 0 ? 1.0 : 0
    visible: opacity > 0
    height: 44
    Rectangle {
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 1
        color: Theme.border
    }
    Behavior on opacity { NumberAnimation { duration: Theme.dur } }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.s16
        anchors.rightMargin: Theme.s8
        spacing: Theme.s12
        Rectangle {
            width: 4; height: 20; radius: 2; color: Theme.accent
        }
        Text {
            text: PhoneController.notice
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fbody
            font.weight: Font.Medium
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        IconButton {
            iconPath: Icons.close
            diameter: 30
            iconSize: 14
            onClicked: PhoneController.dismissNotice()
        }
    }
}
