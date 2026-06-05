import QtQuick
import QtQuick.Controls
import CompactPhone

// Compact theme picker chip: a small palette swatch + the theme name, sized
// to its content so a row of them wraps in a Flow. Selection is shown with an
// accent border + soft fill (no checkmark) so picking one doesn't change the
// chip's width and reflow the row.
AbstractButton {
    id: root
    property string themeId: ""
    property string name: ""
    property bool isCurrent: false

    readonly property QtObject p: Theme.paletteFor(themeId)

    implicitHeight: 28
    implicitWidth: Theme.s10 + sw.width + Theme.s8 + lbl.implicitWidth + Theme.s10
    hoverEnabled: true

    background: Rectangle {
        radius: Theme.r8
        color: root.isCurrent ? Theme.accentSoft : Theme.surface
        border.color: root.isCurrent ? Theme.accent
                    : (root.hovered ? Theme.borderStrong : Theme.border)
        border.width: root.isCurrent ? 2 : 1
    }

    contentItem: Item {
        Rectangle {
            id: sw
            anchors.left: parent.left
            anchors.leftMargin: Theme.s10
            anchors.verticalCenter: parent.verticalCenter
            width: 22; height: 15; radius: 4
            color: root.p.bg
            border.color: root.p.border
            border.width: 1
            clip: true
            // mini "sidebar" band
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 7
                color: root.p.bgElevated
            }
            // accent dot
            Rectangle {
                anchors.right: parent.right; anchors.rightMargin: 3
                anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 3; radius: 1.5
                color: root.p.accent
            }
        }
        Text {
            id: lbl
            anchors.left: sw.right
            anchors.leftMargin: Theme.s8
            anchors.verticalCenter: parent.verticalCenter
            text: root.name
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsm
            font.weight: Font.DemiBold
        }
    }
}
