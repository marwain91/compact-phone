import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import CompactPhone

// Modal prompt shown when a newer version is found (auto-check on startup or
// the manual Settings button). Offers Download / Ignore for now / Ignore this
// version. Download opens the release page in the browser (the app does not
// self-install); "ignore this version" persists so the auto-check stays quiet
// about it.
Window {
    id: dialog
    width: 380
    height: 210
    minimumWidth: 380
    maximumWidth: 380
    minimumHeight: 210
    maximumHeight: 210
    modality: Qt.ApplicationModal
    flags: Qt.Dialog
    color: Theme.bgElevated
    title: qsTr("Update available")

    property string version: ""

    function openFor(v) {
        dialog.version = v
        dialog.show()
        dialog.raise()
        dialog.requestActivate()
    }

    function download() {
        PhoneController.openLatestUpdateUrl()
        dialog.close()
    }

    Shortcut { sequences: ["Esc"]; onActivated: dialog.close() }
    Shortcut { sequences: ["Return", "Enter"]; onActivated: dialog.download() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.s16
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

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
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
