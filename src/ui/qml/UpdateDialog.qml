import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

// Modal prompt shown when a newer version is found (auto-check on startup or
// the manual Settings button). Offers Download / Ignore for now / Ignore this
// version. Download opens the release page in the browser (the app does not
// self-install); "ignore this version" persists so the auto-check stays quiet
// about it.
//
// Rendered as an in-scene Dialog (not a separate top-level Window) so it
// surfaces immediately, even a few seconds after launch. A top-level modal
// Window only rises to the front when its parent window is the key/active
// window at show() time; right after startup that often isn't true, so the
// prompt used to stay hidden behind the main window until the next focus
// change (e.g. navigating to Settings → Advanced). Matching IncomingCallDialog's
// in-scene pattern removes that race.
Dialog {
    id: dialog
    modal: true
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape
    padding: Theme.s16

    // Center on the parent window — Dialog doesn't do this automatically
    // when only width is set.
    anchors.centerIn: parent
    width: 380

    background: Rectangle {
        color: Theme.bgElevated
        radius: Theme.r16
        border.color: Theme.border
        border.width: 1
    }
    header: null

    property string version: ""

    function openFor(v) {
        dialog.version = v
        dialog.open()
    }

    function download() {
        PhoneController.openLatestUpdateUrl()
        dialog.close()
    }

    Shortcut { sequences: ["Return", "Enter"]; onActivated: dialog.download() }

    contentItem: ColumnLayout {
        spacing: Theme.s10

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s10
            Rectangle {
                Layout.preferredWidth: 34; Layout.preferredHeight: 34
                radius: 17
                color: Theme.accentSoft
                AppIcon {
                    anchors.centerIn: parent
                    path: Icons.download
                    color: Theme.accent
                    width: 17; height: 17
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("A new version is available")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fxl
                font.weight: Font.Bold
                wrapMode: Text.WordWrap
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Compact Phone %1 is ready to download. Updates open in your browser; the app does not install them itself.")
                  .arg(dialog.version)
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsm
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s6
            spacing: Theme.s8
            AppButton {
                variant: "ghost"
                size: "sm"
                text: qsTr("Ignore this version")
                onClicked: {
                    PhoneController.skipUpdateVersion(dialog.version)
                    dialog.close()
                }
            }
            Item { Layout.fillWidth: true }
            AppButton {
                variant: "secondary"
                text: qsTr("Ignore for now")
                onClicked: dialog.close()
            }
            AppButton {
                variant: "primary"
                text: qsTr("Download")
                onClicked: dialog.download()
            }
        }
    }
}
