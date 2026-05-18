import QtQuick
import QtQuick.Controls
import CompactPhone

CheckBox {
    id: control
    spacing: Theme.s8

    indicator: Rectangle {
        x: control.leftPadding
        y: control.height / 2 - height / 2
        implicitWidth: 16
        implicitHeight: 16
        radius: 4
        border.color: control.checked ? Theme.accent : Theme.border
        border.width: 1.5
        color: control.checked ? Theme.accent : "transparent"
        AppIcon {
            anchors.centerIn: parent
            visible: control.checked
            path: Icons.check
            color: "#FFFFFF"
            stroke: 3.0
            width: 12; height: 12
        }
    }

    contentItem: Text {
        text: control.text
        color: control.enabled ? Theme.textPrimary : Theme.textDisabled
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fbody
        font.weight: Font.Medium
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }
}
