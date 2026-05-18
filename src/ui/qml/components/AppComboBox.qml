import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

ComboBox {
    id: control
    implicitHeight: 34
    leftPadding: Theme.s12
    rightPadding: Theme.s32

    background: Rectangle {
        radius: Theme.r8
        color: Theme.surface
        border.color: control.activeFocus ? Theme.accent : Theme.border
        border.width: 1
    }

    contentItem: Text {
        text: control.displayText
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fbody
        font.weight: Font.Medium
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: AppIcon {
        x: control.width - width - Theme.s10
        y: (control.height - height) / 2
        width: 14; height: 14
        path: Icons.chevronDown
        color: Theme.textSecondary
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        padding: Theme.s4
        background: Rectangle {
            radius: Theme.r8
            color: Theme.bgElevated
            border.color: Theme.border
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
        }
    }

    delegate: ItemDelegate {
        id: dlg
        width: control.width
        height: 32
        readonly property string _txt: {
            if (!control.textRole) return String(modelData)
            if (modelData && typeof modelData === "object") return String(modelData[control.textRole] || "")
            if (model && model[control.textRole] !== undefined) return String(model[control.textRole])
            return String(modelData)
        }
        contentItem: Text {
            text: dlg._txt
            color: dlg.highlighted ? Theme.accent : Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fbody
            font.weight: dlg.highlighted ? Font.DemiBold : Font.Medium
            verticalAlignment: Text.AlignVCenter
            leftPadding: Theme.s8
        }
        background: Rectangle {
            radius: Theme.r6
            color: dlg.highlighted ? Theme.accentSoft : "transparent"
        }
        highlighted: control.highlightedIndex === index
    }
}
