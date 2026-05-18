import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

ColumnLayout {
    id: root
    spacing: Theme.s10

    property string selectedPeer: ""

    function fmtPreview(s) {
        if (!s) return ""
        return s.length > 56 ? s.substring(0, 56) + "…" : s
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s10
        Text {
            text: qsTr("Messages")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.f2xl
            font.weight: Font.Bold
            font.letterSpacing: -0.4
            Layout.fillWidth: true
        }
        AppButton {
            variant: "primary"
            size: "sm"
            iconPath: Icons.plus
            text: qsTr("New")
            onClicked: newPeerDialog.open()
        }
    }

    // Two-pane: left = conversations, right = thread + compose. When a
    // peer is selected we hide the conversation list to keep the 410-wide
    // window usable.
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true

        // Conversation list (default view).
        ColumnLayout {
            visible: root.selectedPeer.length === 0
            anchors.fill: parent
            spacing: Theme.s6

            Column {
                visible: convList.count === 0
                anchors.centerIn: parent
                spacing: Theme.s8
                AppIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    path: Icons.chat
                    color: Theme.textTertiary
                    width: 36; height: 36
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("No messages yet")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.flg
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Send a SIP MESSAGE to start a conversation")
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fsm
                }
            }

            ListView {
                id: convList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: Theme.s6
                model: PhoneController.conversations
                delegate: Rectangle {
                    width: convList.width
                    height: 56
                    radius: Theme.r10
                    color: ma.containsMouse ? Theme.surfaceHi : Theme.surface
                    border.color: Theme.border
                    border.width: 1
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.s12
                        anchors.rightMargin: Theme.s12
                        spacing: Theme.s10
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            RowLayout {
                                spacing: Theme.s6
                                Text {
                                    text: peer
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fbody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Rectangle {
                                    visible: unread
                                    width: 8; height: 8; radius: 4
                                    color: Theme.accent
                                }
                            }
                            Text {
                                text: (lastDirection === "out" ? qsTr("You: ") : "")
                                      + root.fmtPreview(lastBody)
                                color: Theme.textTertiary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fxs
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                    MouseArea {
                        id: ma
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            root.selectedPeer = peer
                            PhoneController.selectConversation(peer)
                        }
                    }
                }
            }
        }

        // Thread view.
        ColumnLayout {
            visible: root.selectedPeer.length > 0
            anchors.fill: parent
            spacing: Theme.s8

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s8
                IconButton {
                    iconPath: Icons.chevronLeft
                    diameter: 26
                    iconSize: 14
                    onClicked: root.selectedPeer = ""
                }
                Text {
                    text: root.selectedPeer
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.flg
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }

            ListView {
                id: thread
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: Theme.s6
                model: PhoneController.messages
                verticalLayoutDirection: ListView.TopToBottom
                onCountChanged: positionViewAtEnd()
                delegate: Item {
                    width: thread.width
                    height: bubble.height + Theme.s2
                    Rectangle {
                        id: bubble
                        anchors.left: direction === "out" ? undefined : parent.left
                        anchors.right: direction === "out" ? parent.right : undefined
                        width: Math.min(parent.width * 0.8,
                                        msgText.implicitWidth + Theme.s20)
                        height: msgText.implicitHeight + Theme.s12
                        radius: Theme.r10
                        color: direction === "out" ? Theme.accentSoft : Theme.surface
                        border.color: Theme.border
                        border.width: 1
                        Text {
                            id: msgText
                            anchors.fill: parent
                            anchors.margins: Theme.s10
                            text: body
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fsm
                            wrapMode: Text.WordWrap
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                radius: Theme.r10
                color: Theme.surface
                border.color: composeField.activeFocus ? Theme.accent : Theme.border
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.s12
                    anchors.rightMargin: Theme.s6
                    spacing: Theme.s6
                    TextField {
                        id: composeField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Type a message…")
                        placeholderTextColor: Theme.textTertiary
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fsm
                        background: null
                        selectByMouse: true
                        onAccepted: sendBtn.clicked()
                    }
                    IconButton {
                        id: sendBtn
                        iconPath: Icons.send
                        diameter: 28
                        iconSize: 14
                        bgColor: composeField.text.length > 0
                            ? Theme.accentSoft : "transparent"
                        tint: composeField.text.length > 0
                            ? Theme.accent : Theme.textTertiary
                        onClicked: {
                            const t = composeField.text.trim()
                            if (t.length === 0) return
                            PhoneController.sendMessage(root.selectedPeer, t)
                            composeField.text = ""
                        }
                    }
                }
            }
        }
    }

    // Minimal "start a new conversation" prompt.
    Dialog {
        id: newPeerDialog
        modal: true
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.s32, 320)
        title: qsTr("New conversation")
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: Rectangle {
            color: Theme.bg
            border.color: Theme.border
            radius: Theme.r10
        }
        contentItem: ColumnLayout {
            spacing: Theme.s8
            Text {
                text: qsTr("Send to (SIP URI or extension)")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fxs
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                radius: Theme.r8
                color: Theme.surface
                border.color: peerTf.activeFocus ? Theme.accent : Theme.border
                TextField {
                    id: peerTf
                    anchors.fill: parent
                    anchors.leftMargin: Theme.s12
                    anchors.rightMargin: Theme.s12
                    color: Theme.textPrimary
                    placeholderText: qsTr("sip:user@host")
                    placeholderTextColor: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fsm
                    background: null
                    selectByMouse: true
                }
            }
        }
        onAccepted: {
            const t = peerTf.text.trim()
            if (t.length > 0) {
                root.selectedPeer = t
                PhoneController.selectConversation(t)
            }
            peerTf.text = ""
        }
    }
}
