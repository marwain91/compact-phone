import QtQuick
import QtQuick.Controls
import CompactPhone

Switch {
    id: control
    implicitWidth: 40
    implicitHeight: 22

    indicator: Rectangle {
        implicitWidth: 36
        implicitHeight: 20
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: 10
        color: control.checked ? Theme.accent : Theme.surfaceHi
        border.color: control.checked ? Theme.accent : Theme.border
        Rectangle {
            x: control.checked ? parent.width - width - 2 : 2
            y: 2
            width: 16; height: 16
            radius: 8
            color: "#FFFFFF"
            Behavior on x { NumberAnimation { duration: Theme.dur; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Item {}
}
