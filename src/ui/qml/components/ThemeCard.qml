import QtQuick
import QtQuick.Controls
import CompactPhone

// Theme picker chip: a readable palette swatch + the theme name, sized
// to its content so a row of them wraps in a Flow. Selection is shown with an
// accent border + soft fill (no checkmark) so picking one does not change the
// chip width and reflow the row.
AbstractButton {
    id: root
    property string themeId: ""
    property string name: ""
    property bool isCurrent: false

    readonly property QtObject p: Theme.paletteFor(themeId)

    implicitHeight: 34
    implicitWidth: Theme.s12 + sw.width + Theme.s10 + lbl.implicitWidth + Theme.s12
    hoverEnabled: true

    Accessible.role: Accessible.RadioButton
    Accessible.name: root.name
    Accessible.checked: root.isCurrent

    background: Rectangle {
        radius: Theme.r8
        color: root.isCurrent ? Theme.accentSoft : (root.hovered ? Theme.surfaceHi : Theme.surface)
        border.color: root.isCurrent || root.activeFocus ? Theme.accent
                    : (root.hovered ? Theme.borderStrong : Theme.border)
        border.width: root.isCurrent ? 2 : 1
    }

    contentItem: Item {
        Rectangle {
            id: sw
            anchors.left: parent.left
            anchors.leftMargin: Theme.s12
            anchors.verticalCenter: parent.verticalCenter
            width: 28; height: 18; radius: 5
            color: root.p.bg
            border.color: root.p.border
            border.width: 1
            clip: true
            // mini "sidebar" band
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 8
                color: root.p.bgElevated
            }
            // accent dot
            Rectangle {
                anchors.right: parent.right; anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                width: 9; height: 4; radius: 2
                color: root.p.accent
            }
        }
        Text {
            id: lbl
            anchors.left: sw.right
            anchors.leftMargin: Theme.s10
            anchors.verticalCenter: parent.verticalCenter
            text: root.name
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fbody
            font.weight: Font.DemiBold
        }
    }
}
