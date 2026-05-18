import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

ColumnLayout {
    id: root
    spacing: Theme.s8

    readonly property var keys: [
        ["1", ""], ["2", "ABC"], ["3", "DEF"],
        ["4", "GHI"], ["5", "JKL"], ["6", "MNO"],
        ["7", "PQRS"], ["8", "TUV"], ["9", "WXYZ"],
        ["∗", ""], ["0", "+"], ["#", ""]
    ]

    function appendKey(k) {
        const cur = dialField.text
        if (cur.startsWith("sip:")) {
            dialField.text = cur + k
        } else {
            dialField.text = cur + k
        }
    }

    Item {
        visible: callsRep.count === 0
        Layout.fillWidth: true
        Layout.fillHeight: true

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.s8

            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: Theme.s4
                spacing: Theme.s10
                Text {
                    text: qsTr("New Call")
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.f2xl
                    font.weight: Font.Bold
                    font.letterSpacing: -0.4
                    Layout.fillWidth: true
                }
                AppComboBox {
                    id: accountCombo
                    visible: PhoneController.accounts.rowCount() > 0
                    implicitWidth: 130
                    implicitHeight: 26
                    flat: true
                    model: PhoneController.accounts
                    textRole: "label"
                    valueRole: "accountId"
                    background: Rectangle {
                        radius: Theme.r6
                        color: accountCombo.hovered ? Theme.surfaceHi : "transparent"
                        border.width: 1
                        border.color: Theme.border
                    }
                    currentIndex: {
                        const aid = PhoneController.activeAccountId
                        if (aid <= 0) return 0
                        for (let i = 0; i < model.rowCount(); i++) {
                            if (model.data(model.index(i, 0), Qt.UserRole + 1) === aid) return i
                        }
                        return 0
                    }
                    onActivated: PhoneController.activeAccountId = currentValue
                }
            }

            // Speed-dial strip: contacts marked favorite render as a row of
            // chips. Tap → dial. Hidden when no favorites exist.
            Flow {
                Layout.fillWidth: true
                spacing: Theme.s6
                visible: favoritesRep.count > 0
                Repeater {
                    id: favoritesRep
                    model: PhoneController.contacts
                    delegate: Rectangle {
                        visible: favorite
                        width: visible ? favLabel.implicitWidth + Theme.s14 : 0
                        height: visible ? 28 : 0
                        radius: 14
                        color: favTap.containsMouse
                            ? Theme.accentSoft : Theme.surface
                        border.color: Theme.border
                        Text {
                            id: favLabel
                            anchors.centerIn: parent
                            text: displayName.length > 0
                                ? displayName : (sipUri || phone)
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fsm
                            font.weight: Font.Medium
                        }
                        MouseArea {
                            id: favTap
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: PhoneController.dialContact(contactId)
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                radius: Theme.r10
                color: Theme.surface
                border.color: dialField.activeFocus ? Theme.accent : Theme.border
                border.width: 1

                TextField {
                    id: dialField
                    anchors.fill: parent
                    anchors.leftMargin: Theme.s16
                    anchors.rightMargin: Theme.s10
                    placeholderText: qsTr("Extension or sip:user@host")
                    placeholderTextColor: Theme.textTertiary
                    text: ""
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.flg
                    font.weight: Font.DemiBold
                    background: null
                    selectByMouse: true
                    onAccepted: {
                        // Trigger the Call button directly so Enter produces
                        // the same visual press animation as a real click.
                        if (callButton.enabled) callButton.clicked()
                    }
                    Connections {
                        target: PhoneController
                        function onDialerUriChanged() {
                            if (PhoneController.dialerUri.length > 0) {
                                dialField.text = PhoneController.dialerUri
                            }
                        }
                    }
                }
                IconButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: Theme.s6
                    visible: dialField.text.length > 0
                    iconPath: Icons.close
                    diameter: 24
                    iconSize: 12
                    onClicked: dialField.text = ""
                }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.s4
                columns: 3
                rowSpacing: Theme.s6
                columnSpacing: Theme.s6
                Repeater {
                    model: root.keys
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        radius: Theme.r10
                        color: tap.pressed ? Theme.accentSoft : Theme.surface
                        border.color: tap.pressed ? Theme.accent : Theme.border
                        border.width: 1

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: -2
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: modelData[0]
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fxl
                                font.weight: Font.DemiBold
                                font.letterSpacing: -0.3
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                visible: modelData[1].length > 0
                                text: modelData[1]
                                color: Theme.textTertiary
                                font.family: Theme.fontFamily
                                font.pixelSize: 9
                                font.letterSpacing: 0.8
                            }
                        }
                        MouseArea {
                            id: tap
                            anchors.fill: parent
                            onClicked: root.appendKey(modelData[0])
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.s4
                spacing: Theme.s10
                AppButton {
                    id: callButton
                    variant: "success"
                    size: "lg"
                    iconPath: Icons.phone
                    text: qsTr("Call")
                    enabled: PhoneController.activeAccountId > 0
                             && dialField.text.length > 0
                    Layout.fillWidth: true
                    onClicked: PhoneController.dial(dialField.text)
                }
                AppButton {
                    visible: PhoneController.newVoicemailCount > 0
                             && PhoneController.activeVoicemailNumber.length > 0
                    variant: "secondary"
                    size: "lg"
                    iconPath: Icons.voicemail
                    text: PhoneController.newVoicemailCount > 9
                        ? "9+"
                        : "" + PhoneController.newVoicemailCount
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Dial voicemail (%1 new)")
                        .arg(PhoneController.newVoicemailCount)
                    onClicked: PhoneController.dialVoicemail()
                }
            }

            Item {
                visible: false
                Layout.preferredHeight: 0
            }
        }
    }

    Repeater {
        id: callsRep
        model: PhoneController.calls
        delegate: ActiveCallView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            callId: model.callId
            remoteUri: model.remoteUri
            state: model.state
            held: model.held
            muted: model.muted
            recording: model.recording
        }
    }
}
