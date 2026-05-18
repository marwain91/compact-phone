import QtQuick
import QtQuick.Controls
import CompactPhone

Button {
    id: root
    property string variant: "primary"   // primary | secondary | ghost | danger | success
    property string iconPath: ""
    property string size: "md"           // sm | md | lg

    readonly property int padH: size === "sm" ? Theme.s12 : size === "lg" ? Theme.s20 : Theme.s16
    readonly property int padV: size === "sm" ? Theme.s6  : size === "lg" ? Theme.s12 : Theme.s8
    readonly property int fSize: size === "sm" ? Theme.fsm : size === "lg" ? Theme.flg : Theme.fbody

    readonly property color bgIdle:
        variant === "primary"  ? Theme.accent :
        variant === "success"  ? "#10B981" :
        variant === "danger"   ? Theme.danger :
        variant === "ghost"    ? "transparent" :
                                 Theme.surfaceHi
    readonly property color bgHover:
        variant === "primary"  ? Theme.accentHi :
        variant === "success"  ? "#0EA873" :
        variant === "danger"   ? Theme.dangerHi :
        variant === "ghost"    ? Theme.surface :
                                 Theme.border
    readonly property color fgColor:
        !root.enabled ? Theme.textTertiary :
        variant === "primary" || variant === "danger" || variant === "success" ? "#FFFFFF" :
        Theme.textPrimary
    readonly property color borderColor:
        variant === "secondary" ? Theme.border : "transparent"

    leftPadding: padH; rightPadding: padH
    topPadding: padV;  bottomPadding: padV
    hoverEnabled: true

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight
        Row {
            id: contentRow
            anchors.centerIn: parent
            spacing: Theme.s8
            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.iconPath.length > 0
                path: root.iconPath
                color: root.fgColor
                width: root.fSize + 4
                height: root.fSize + 4
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.text
                color: root.fgColor
                font.family: Theme.fontFamily
                font.pixelSize: root.fSize
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    background: Rectangle {
        radius: Theme.r8
        color: !root.enabled
            ? Theme.surfaceHi
            : (root.pressed ? Qt.darker(root.bgHover, 1.1)
              : root.hovered ? root.bgHover : root.bgIdle)
        border.width: root.variant === "secondary" ? 1 : 0
        border.color: root.borderColor
    }
}
