import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

ColumnLayout {
    id: root
    spacing: Theme.s10

    function pillColor(state) {
        if (state === "idle")    return Theme.success
        if (state === "busy")    return Theme.danger
        if (state === "offline") return Theme.textTertiary
        return Theme.warning   // unknown — pending NOTIFY
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s10
        Text {
            text: qsTr("Lines")
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
            text: qsTr("Add")
            onClicked: addLineDialog.open()
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true

        ListView {
            id: list
            anchors.fill: parent
            visible: count > 0
            clip: true
            spacing: Theme.s6
            model: PhoneController.lines
            delegate: Rectangle {
                width: list.width
                height: 48
                radius: Theme.r10
                color: hov.containsMouse ? Theme.surfaceHi : Theme.surface
                border.color: Theme.border
                border.width: 1
                MouseArea { id: hov; anchors.fill: parent; hoverEnabled: true }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.s12
                    anchors.rightMargin: Theme.s6
                    spacing: Theme.s10
                    Rectangle {
                        width: 12; height: 12; radius: 6
                        color: root.pillColor(state)
                        SequentialAnimation on opacity {
                            running: state === "unknown"
                            loops: Animation.Infinite
                            NumberAnimation { from: 1.0; to: 0.3; duration: 700 }
                            NumberAnimation { from: 0.3; to: 1.0; duration: 700 }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: (label && label.length > 0) ? label : uri
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fbody
                            font.weight: Font.DemiBold
                            elide: Text.ElideMiddle
                        }
                        Text {
                            visible: label && label.length > 0
                            text: uri
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                            elide: Text.ElideMiddle
                        }
                    }
                    IconButton {
                        iconPath: Icons.phone
                        diameter: 28
                        iconSize: 13
                        bgColor: Theme.successSoft
                        bgHover: "#10B981"
                        tint: "#10B981"
                        hoverTint: "#FFFFFF"
                        onClicked: PhoneController.dialLine(lineId)
                    }
                    IconButton {
                        iconPath: Icons.trash
                        diameter: 28
                        iconSize: 13
                        hoverTint: Theme.danger
                        onClicked: PhoneController.removeWatchedLine(lineId)
                    }
                }
            }
        }

        Column {
            anchors.centerIn: parent
            spacing: Theme.s8
            visible: list.count === 0
            AppIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                path: Icons.users
                color: Theme.textTertiary
                width: 36; height: 36
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("No watched lines")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.flg
                font.weight: Font.DemiBold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Add an extension to see its live status")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsm
            }
        }
    }

    Dialog {
        id: addLineDialog
        modal: true
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.s32, 320)
        title: qsTr("Watch a line")
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: Rectangle {
            color: Theme.bg
            border.color: Theme.border
            radius: Theme.r10
        }
        contentItem: ColumnLayout {
            spacing: Theme.s8
            Text {
                text: qsTr("SIP URI or extension")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fxs
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                radius: Theme.r8
                color: Theme.surface
                border.color: uriTf.activeFocus ? Theme.accent : Theme.border
                TextField {
                    id: uriTf
                    anchors.fill: parent
                    anchors.leftMargin: Theme.s12
                    anchors.rightMargin: Theme.s12
                    color: Theme.textPrimary
                    placeholderText: qsTr("sip:1001@host")
                    placeholderTextColor: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fsm
                    background: null
                    selectByMouse: true
                }
            }
            Text {
                text: qsTr("Label (optional)")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fxs
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                radius: Theme.r8
                color: Theme.surface
                border.color: labelTf.activeFocus ? Theme.accent : Theme.border
                TextField {
                    id: labelTf
                    anchors.fill: parent
                    anchors.leftMargin: Theme.s12
                    anchors.rightMargin: Theme.s12
                    color: Theme.textPrimary
                    placeholderText: qsTr("e.g. Reception")
                    placeholderTextColor: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fsm
                    background: null
                    selectByMouse: true
                }
            }
        }
        onAccepted: {
            const uri = uriTf.text.trim()
            if (uri.length === 0) return
            PhoneController.addWatchedLine(uri, labelTf.text.trim())
            uriTf.text = ""
            labelTf.text = ""
        }
    }
}
