import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

// Qt's default Switch reserves space in several layers around the
// visible indicator: padding (4 sides), insets (4 sides), and a
// contentItem slot for an optional label. We zero them ALL plus
// pin the Layout dimensions so the switch occupies exactly 36×20
// inside a RowLayout. Callers that want the toggle flush against
// the row's right edge must insert an `Item { Layout.fillWidth: true }`
// spacer between the description block and this control — the inner
// fillWidth ColumnLayout pattern alone doesn't suffice because Qt
// Quick Layouts size that column to its children's natural width.
Switch {
    id: control
    implicitWidth: 36
    implicitHeight: 20
    padding: 0
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0
    spacing: 0
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0
    Layout.preferredWidth: 36
    Layout.preferredHeight: 20
    Layout.maximumWidth: 36
    Layout.maximumHeight: 20
    Layout.alignment: Qt.AlignVCenter | Qt.AlignRight

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
    contentItem: Item { implicitWidth: 0; implicitHeight: 0 }
}
