import QtQuick
import QtQuick.Layouts
import CompactPhone

Rectangle {
    id: root
    property string status: "idle" // idle | active | warning | danger | info | accent
    property string label: ""
    property bool pulse: false

    readonly property color base:
        status === "active"  ? Theme.success :
        status === "warning" ? Theme.warning :
        status === "danger"  ? Theme.danger  :
        status === "info"    ? Theme.info    :
        status === "accent"  ? Theme.accent  :
                               Theme.textTertiary
    readonly property color soft:
        status === "active"  ? Theme.successSoft :
        status === "warning" ? Theme.warningSoft :
        status === "danger"  ? Theme.dangerSoft  :
        status === "accent"  ? Theme.accentSoft  :
                               Qt.rgba(1, 1, 1, 0.06)

    radius: Theme.rFull
    color: soft
    implicitHeight: label.length > 0 ? 16 : 10
    implicitWidth: label.length > 0
                   ? labelText.implicitWidth + Theme.s4 + dot.width + Theme.s8 * 2
                   : 10

    RowLayout {
        anchors.centerIn: parent
        spacing: Theme.s4
        Rectangle {
            id: dot
            width: 5; height: 5; radius: 3
            color: root.base
            SequentialAnimation on opacity {
                running: root.pulse
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.35; duration: 700; easing.type: Easing.InOutQuad }
                NumberAnimation { from: 0.35; to: 1.0; duration: 700; easing.type: Easing.InOutQuad }
            }
        }
        Text {
            id: labelText
            visible: root.label.length > 0
            text: root.label
            color: root.base
            font.family: Theme.fontFamily
            font.pixelSize: 9
            font.weight: Font.DemiBold
            font.letterSpacing: 0.3
        }
    }
}
