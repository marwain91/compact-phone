import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import CompactPhone

Item {
    id: root

    FileDialog {
        id: ringtonePicker
        title: qsTr("Choose ringtone")
        nameFilters: [qsTr("WAV files (*.wav)")]
        onAccepted: PhoneController.settings.ringtonePath = selectedFile
    }

    LogViewerDialog { id: logViewer }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.s10

        Text {
            text: qsTr("Settings")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.f2xl
            font.weight: Font.Bold
            font.letterSpacing: -0.4
            Layout.fillWidth: true
        }

        // Custom-styled tab strip. Each TabButton renders as a transparent
        // label with an accent underline when checked — same treatment as
        // AccountEditDialog. The active underline overlaps the strip's
        // bottom hairline so it reads as one line, not two.
        TabBar {
            id: tabs
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.border
                }
            }

            component SettingsTab: TabButton {
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsm
                font.weight: Font.DemiBold
                padding: 0
                implicitHeight: 34
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? Theme.accent
                          : parent.hovered ? Theme.textPrimary
                          : Theme.textSecondary
                    font: parent.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    Behavior on color { ColorAnimation { duration: Theme.dur } }
                }
                background: Rectangle {
                    color: "transparent"
                    // Accent bar sits at the strip's bottom edge and is 2 px
                    // tall — covers the 1 px border so the two read as a
                    // single coloured line for the active tab.
                    Rectangle {
                        visible: parent.parent.checked
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 2
                        color: Theme.accent
                    }
                }
            }

            SettingsTab { text: qsTr("General") }
            SettingsTab { text: qsTr("Audio") }
            // The former separate "Forward" tab is now the second card on
            // the Calls tab — both are call-handling settings and the tab
            // strip was getting crowded.
            SettingsTab { text: qsTr("Calls") }
            SettingsTab { text: qsTr("Advanced") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // -------- General tab --------
            GeneralSettings {}

            // -------- Audio tab --------
            AudioSettings { onPickRingtone: ringtonePicker.open() }

            // -------- Calls tab --------
            CallsSettings {}

            // -------- Advanced tab --------
            AdvancedSettings { onShowLog: logViewer.show() }
        }
    }
}
