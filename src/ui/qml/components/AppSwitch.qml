import QtQuick
import QtQuick.Controls
import CompactPhone

Switch {
    id: control
    implicitWidth: 36
    implicitHeight: 20
    // Qt's default Switch theme adds ~12px padding around the indicator,
    // which makes the control look left-aligned inside its own bounding
    // box. Zero that out so the visible toggle sits flush with its right
    // edge — important for the rows in SettingsPane where the switch is
    // the last item in a RowLayout and should hug the right margin.
    padding: 0
    leftPadding: 0
    rightPadding: 0
    spacing: 0

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
