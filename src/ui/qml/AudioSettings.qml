import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

ScrollView {
    id: pane
    signal pickRingtone()
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ColumnLayout {
        width: parent.width
        spacing: Theme.s10

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: audioCol.implicitHeight + Theme.s24
            radius: Theme.r10
            color: Theme.surface
            border.color: Theme.border
            ColumnLayout {
                id: audioCol
                anchors.fill: parent
                anchors.margins: Theme.s14
                spacing: Theme.s12

                Text {
                    text: qsTr("AUDIO")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fxs
                    font.weight: Font.Bold
                    font.letterSpacing: 1.2
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s6
                    Text {
                        text: qsTr("Microphone")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fbody
                        font.weight: Font.Medium
                    }
                    AppComboBox {
                        id: micCombo
                        Layout.fillWidth: true
                        model: PhoneController.settings.audioInputs
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: {
                            const cid = PhoneController.settings.captureDeviceId
                            for (let i = 0; i < model.length; i++) {
                                if (model[i].id === cid) return i
                            }
                            return 0
                        }
                        onActivated: PhoneController.settings.captureDeviceId = currentValue
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s6
                    Text {
                        text: qsTr("Speaker")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fbody
                        font.weight: Font.Medium
                    }
                    AppComboBox {
                        id: spkCombo
                        Layout.fillWidth: true
                        model: PhoneController.settings.audioOutputs
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: {
                            const pid = PhoneController.settings.playbackDeviceId
                            for (let i = 0; i < model.length; i++) {
                                if (model[i].id === pid) return i
                            }
                            return 0
                        }
                        onActivated: PhoneController.settings.playbackDeviceId = currentValue
                    }
                }

                AppButton {
                    variant: "ghost"
                    size: "sm"
                    text: qsTr("Refresh devices")
                    onClicked: PhoneController.settings.refreshAudioDevices()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: ringCol.implicitHeight + Theme.s24
            radius: Theme.r10
            color: Theme.surface
            border.color: Theme.border
            ColumnLayout {
                id: ringCol
                anchors.fill: parent
                anchors.margins: Theme.s14
                spacing: Theme.s12

                Text {
                    text: qsTr("RINGTONE")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fxs
                    font.weight: Font.Bold
                    font.letterSpacing: 1.2
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: qsTr("Play ringtone on inbound calls")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fbody
                            font.weight: Font.Medium
                        }
                        Text {
                            text: qsTr("A short tone when someone calls you")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                        }
                    }
                    Item { Layout.fillWidth: true; implicitHeight: 1 }
                    AppSwitch {
                        checked: PhoneController.settings.ringtoneEnabled
                        onToggled: PhoneController.settings.ringtoneEnabled = checked
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: {
                                const p = PhoneController.settings.ringtonePath
                                if (!p || p === PhoneController.settings.defaultRingtonePath) return qsTr("Built-in tone")
                                const i = p.lastIndexOf("/")
                                return i >= 0 ? p.substring(i + 1) : p
                            }
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fbody
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: qsTr("Played when an inbound call rings")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s8
                    AppButton {
                        variant: "secondary"
                        size: "sm"
                        text: qsTr("Choose…")
                        onClicked: pane.pickRingtone()
                    }
                    AppButton {
                        variant: "ghost"
                        size: "sm"
                        text: qsTr("Reset")
                        visible: PhoneController.settings.ringtonePath !== PhoneController.settings.defaultRingtonePath
                        onClicked: PhoneController.settings.ringtonePath = PhoneController.settings.defaultRingtonePath
                    }
                    Item { Layout.fillWidth: true }
                    AppButton {
                        variant: "ghost"
                        size: "sm"
                        iconPath: Icons.play
                        text: qsTr("Test")
                        onClicked: PhoneController.settings.testRingtone(2000)
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
