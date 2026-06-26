import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

// Theme picker as a compact select box with a live palette swatch. Replaces the
// wrapping row of ThemeCard chips, which overflowed the 380px window and clipped
// the right-most card. The closed box shows the active theme's swatch + name;
// the popup lists every theme with its swatch and a check on the current one.
ComboBox {
    id: control
    implicitHeight: 38
    model: Theme.themes
    leftPadding: Theme.s10
    rightPadding: Theme.s32

    // Keep the shown row bound to the active theme; selecting a row writes
    // settings.themeId (onActivated), which flows back here through this binding.
    currentIndex: {
        const id = PhoneController.settings.themeId
        for (let i = 0; i < Theme.themes.length; i++)
            if (Theme.themes[i].id === id) return i
        return 0
    }
    onActivated: PhoneController.settings.themeId = Theme.themes[currentIndex].id

    // A mini "window" preview: background, a sidebar band, and an accent pip —
    // mirrors the swatch the old ThemeCard drew so the visuals are unchanged.
    component ThemeSwatch: Rectangle {
        property var pal
        width: 30; height: 19; radius: 5
        color: pal.bg
        border.color: pal.border
        border.width: 1
        clip: true
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 9
            color: pal.bgElevated
        }
        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            width: 9; height: 4; radius: 2
            color: pal.accent
        }
    }

    background: Rectangle {
        radius: Theme.r8
        color: Theme.surface
        border.color: control.activeFocus || control.popup.visible
                      ? Theme.accent : Theme.border
        border.width: 1
    }

    contentItem: RowLayout {
        spacing: Theme.s10
        ThemeSwatch {
            pal: Theme.paletteFor(Theme.themes[control.currentIndex].id)
            Layout.alignment: Qt.AlignVCenter
        }
        Text {
            Layout.fillWidth: true
            text: Theme.themes[control.currentIndex].name
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fbody
            font.weight: Font.Medium
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
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
        height: 38
        // Accessible name for screen readers; the visible label is the
        // contentItem below, so this `text` is announced, not drawn twice.
        text: modelData.name
        readonly property bool isCurrent:
            PhoneController.settings.themeId === modelData.id
        contentItem: RowLayout {
            spacing: Theme.s10
            ThemeSwatch {
                pal: Theme.paletteFor(modelData.id)
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                Layout.fillWidth: true
                text: modelData.name
                color: dlg.isCurrent ? Theme.accent : Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fbody
                font.weight: dlg.isCurrent ? Font.DemiBold : Font.Medium
                verticalAlignment: Text.AlignVCenter
            }
            AppIcon {
                visible: dlg.isCurrent
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                Layout.alignment: Qt.AlignVCenter
                path: Icons.check
                color: Theme.accent
            }
        }
        background: Rectangle {
            radius: Theme.r6
            color: dlg.highlighted ? Theme.accentSoft : "transparent"
        }
        highlighted: control.highlightedIndex === index
    }
}
