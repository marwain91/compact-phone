import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import CompactPhone

Window {
    id: dialog
    width: 460
    height: PhoneController.calls.rowCount() > 1 ? 480 : 320
    minimumWidth: 460
    maximumWidth: 460
    modality: Qt.ApplicationModal
    flags: Qt.Dialog
    color: Theme.bgElevated
    title: qsTr("Transfer Call")

    property int callId: -1

    Shortcut {
        sequences: ["Esc"]
        onActivated: dialog.close()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.s20
        spacing: Theme.s12

        RowLayout {
            spacing: Theme.s10
            AppIcon {
                path: Icons.transfer
                color: Theme.accent
                width: 18; height: 18
                stroke: 2.0
            }
            Text {
                text: qsTr("Transfer Call")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fxl
                font.weight: Font.Bold
            }
        }

        Text {
            text: qsTr("BLIND TRANSFER")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fxs
            font.weight: Font.Bold
            font.letterSpacing: 1.2
            Layout.topMargin: Theme.s4
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            radius: Theme.r8
            color: Theme.surface
            border.color: blindTargetField.activeFocus ? Theme.accent : Theme.border
            TextField {
                id: blindTargetField
                anchors.fill: parent
                anchors.leftMargin: Theme.s12
                anchors.rightMargin: Theme.s12
                placeholderText: qsTr("sip:user@host or extension")
                placeholderTextColor: Theme.textTertiary
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fbody
                background: null
                selectByMouse: true
            }
        }
        AppButton {
            Layout.fillWidth: true
            variant: "primary"
            iconPath: Icons.transfer
            text: qsTr("Transfer Now")
            enabled: blindTargetField.text.length > 0
            onClicked: {
                PhoneController.blindTransfer(dialog.callId, blindTargetField.text)
                dialog.close()
            }
        }

        Rectangle {
            visible: PhoneController.calls.rowCount() > 1
            Layout.fillWidth: true
            height: 1
            color: Theme.border
            Layout.topMargin: Theme.s8
        }

        ColumnLayout {
            visible: PhoneController.calls.rowCount() > 1
            spacing: Theme.s10
            Layout.fillWidth: true
            Text {
                text: qsTr("ATTENDED TRANSFER")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fxs
                font.weight: Font.Bold
                font.letterSpacing: 1.2
            }
            AppComboBox {
                id: heldCombo
                Layout.fillWidth: true
                model: PhoneController.calls
                textRole: "remoteUri"
                valueRole: "callId"
            }
            AppButton {
                Layout.fillWidth: true
                variant: "secondary"
                iconPath: Icons.transfer
                text: qsTr("Transfer to Selected")
                enabled: heldCombo.currentValue !== undefined &&
                         heldCombo.currentValue !== dialog.callId
                onClicked: {
                    PhoneController.attendedTransfer(dialog.callId, heldCombo.currentValue)
                    dialog.close()
                }
            }
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { variant: "ghost"; text: qsTr("Cancel"); onClicked: dialog.close() }
        }
    }

    function openFor(cid) {
        dialog.callId = cid
        blindTargetField.text = ""
        show(); raise(); requestActivate()
    }
}
