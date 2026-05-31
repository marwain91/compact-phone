import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

ScrollView {
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ColumnLayout {
        width: parent.width
        spacing: Theme.s10

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: callsCol.implicitHeight + Theme.s24
            radius: Theme.r10
            color: Theme.surface
            border.color: Theme.border
            ColumnLayout {
                id: callsCol
                anchors.fill: parent
                anchors.margins: Theme.s14
                spacing: Theme.s12

                Text {
                    text: qsTr("CALLS")
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
                            text: qsTr("Do not disturb")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fbody
                            font.weight: Font.Medium
                        }
                        Text {
                            text: qsTr("Reject incoming calls with 486 Busy Here")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                        }
                    }
                    Item { Layout.fillWidth: true; implicitHeight: 1 }
                    AppSwitch {
                        checked: PhoneController.settings.dndEnabled
                        onToggled: PhoneController.settings.dndEnabled = checked
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: qsTr("Auto-answer")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fbody
                            font.weight: Font.Medium
                        }
                        Text {
                            text: qsTr("Pick up incoming calls automatically")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                        }
                    }
                    Item { Layout.fillWidth: true; implicitHeight: 1 }
                    AppSwitch {
                        checked: PhoneController.settings.autoAnswerEnabled
                        onToggled: PhoneController.settings.autoAnswerEnabled = checked
                    }
                }

                RowLayout {
                    visible: PhoneController.settings.autoAnswerEnabled
                    Layout.fillWidth: true
                    spacing: Theme.s10
                    Text {
                        text: qsTr("Delay before pickup")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fsm
                        Layout.fillWidth: true
                    }
                    AppComboBox {
                        implicitWidth: 110
                        implicitHeight: 26
                        model: [
                            { label: qsTr("Immediately"), ms: 0 },
                            { label: qsTr("1 second"),    ms: 1000 },
                            { label: qsTr("3 seconds"),   ms: 3000 },
                            { label: qsTr("5 seconds"),   ms: 5000 },
                            { label: qsTr("10 seconds"),  ms: 10000 }
                        ]
                        textRole: "label"
                        valueRole: "ms"
                        currentIndex: {
                            const cur = PhoneController.settings.autoAnswerDelayMs
                            for (let i = 0; i < model.length; i++) {
                                if (model[i].ms === cur) return i
                            }
                            return 0
                        }
                        onActivated: PhoneController.settings.autoAnswerDelayMs = currentValue
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: qsTr("Auto-record calls")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fbody
                            font.weight: Font.Medium
                        }
                        Text {
                            text: qsTr("Save every call to WAV in the recordings folder")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                        }
                    }
                    Item { Layout.fillWidth: true; implicitHeight: 1 }
                    AppSwitch {
                        checked: PhoneController.settings.autoRecordEnabled
                        onToggled: PhoneController.settings.autoRecordEnabled = checked
                    }
                }
            }
        }

        // Forwarding card — moved here from the standalone
        // "Forward" tab. Keeps the two call-handling concerns
        // side-by-side on the same scroll surface.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: cfwdCol.implicitHeight + Theme.s24
            radius: Theme.r10
            color: Theme.surface
            border.color: Theme.border
            ColumnLayout {
                id: cfwdCol
                anchors.fill: parent
                anchors.margins: Theme.s14
                spacing: Theme.s12

                Text {
                    text: qsTr("FORWARDING")
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
                            text: qsTr("Forward all calls")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fbody
                            font.weight: Font.Medium
                        }
                        Text {
                            text: qsTr("Every incoming call redirects with 302")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                        }
                    }
                    Item { Layout.fillWidth: true; implicitHeight: 1 }
                    AppSwitch {
                        checked: PhoneController.settings.cfwdAlwaysEnabled
                        onToggled: PhoneController.settings.cfwdAlwaysEnabled = checked
                    }
                }
                Rectangle {
                    visible: PhoneController.settings.cfwdAlwaysEnabled
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    radius: Theme.r8
                    color: Theme.bg
                    border.color: cfwdAlwaysTf.activeFocus ? Theme.accent : Theme.border
                    TextField {
                        id: cfwdAlwaysTf
                        anchors.fill: parent
                        anchors.leftMargin: Theme.s12
                        anchors.rightMargin: Theme.s12
                        text: PhoneController.settings.cfwdAlwaysTarget
                        placeholderText: qsTr("sip:user@host or extension")
                        placeholderTextColor: Theme.textTertiary
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fsm
                        background: null
                        selectByMouse: true
                        onEditingFinished: PhoneController.settings.cfwdAlwaysTarget = text
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: qsTr("Forward when busy")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fbody
                            font.weight: Font.Medium
                        }
                        Text {
                            text: qsTr("Redirects when another call is already active")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                        }
                    }
                    Item { Layout.fillWidth: true; implicitHeight: 1 }
                    AppSwitch {
                        checked: PhoneController.settings.cfwdBusyEnabled
                        onToggled: PhoneController.settings.cfwdBusyEnabled = checked
                    }
                }
                Rectangle {
                    visible: PhoneController.settings.cfwdBusyEnabled
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    radius: Theme.r8
                    color: Theme.bg
                    border.color: cfwdBusyTf.activeFocus ? Theme.accent : Theme.border
                    TextField {
                        id: cfwdBusyTf
                        anchors.fill: parent
                        anchors.leftMargin: Theme.s12
                        anchors.rightMargin: Theme.s12
                        text: PhoneController.settings.cfwdBusyTarget
                        placeholderText: qsTr("sip:user@host or extension")
                        placeholderTextColor: Theme.textTertiary
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fsm
                        background: null
                        selectByMouse: true
                        onEditingFinished: PhoneController.settings.cfwdBusyTarget = text
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: qsTr("Forward when no answer")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fbody
                            font.weight: Font.Medium
                        }
                        Text {
                            text: qsTr("Redirects if you don't pick up in time")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                        }
                    }
                    Item { Layout.fillWidth: true; implicitHeight: 1 }
                    AppSwitch {
                        checked: PhoneController.settings.cfwdNoAnswerEnabled
                        onToggled: PhoneController.settings.cfwdNoAnswerEnabled = checked
                    }
                }
                ColumnLayout {
                    visible: PhoneController.settings.cfwdNoAnswerEnabled
                    Layout.fillWidth: true
                    spacing: Theme.s6
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        radius: Theme.r8
                        color: Theme.bg
                        border.color: cfwdNaTf.activeFocus ? Theme.accent : Theme.border
                        TextField {
                            id: cfwdNaTf
                            anchors.fill: parent
                            anchors.leftMargin: Theme.s12
                            anchors.rightMargin: Theme.s12
                            text: PhoneController.settings.cfwdNoAnswerTarget
                            placeholderText: qsTr("sip:user@host or extension")
                            placeholderTextColor: Theme.textTertiary
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fsm
                            background: null
                            selectByMouse: true
                            onEditingFinished: PhoneController.settings.cfwdNoAnswerTarget = text
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.s10
                        Text {
                            text: qsTr("Forward after")
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fsm
                            Layout.fillWidth: true
                        }
                        AppComboBox {
                            implicitWidth: 110
                            implicitHeight: 26
                            model: [
                                { label: qsTr("5 seconds"),  ms: 5000  },
                                { label: qsTr("10 seconds"), ms: 10000 },
                                { label: qsTr("20 seconds"), ms: 20000 },
                                { label: qsTr("30 seconds"), ms: 30000 },
                                { label: qsTr("60 seconds"), ms: 60000 }
                            ]
                            textRole: "label"
                            valueRole: "ms"
                            currentIndex: {
                                const cur = PhoneController.settings.cfwdNoAnswerTimeoutMs
                                for (let i = 0; i < model.length; i++) {
                                    if (model[i].ms === cur) return i
                                }
                                return 2
                            }
                            onActivated: PhoneController.settings.cfwdNoAnswerTimeoutMs = currentValue
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
