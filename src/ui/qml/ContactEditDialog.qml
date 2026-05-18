import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import CompactPhone

Window {
    id: dialog
    width: 420
    height: 380
    minimumWidth: 420
    maximumWidth: 420
    minimumHeight: 380
    maximumHeight: 380
    modality: Qt.ApplicationModal
    flags: Qt.Dialog
    color: Theme.bgElevated
    title: editingContactId === -1 ? qsTr("Add Contact") : qsTr("Edit Contact")

    property int editingContactId: -1

    signal contactSaved(string displayName, string sipUri, string phone)
    signal contactEdited(int contactId, string displayName, string sipUri, string phone)

    Shortcut {
        sequences: ["Esc"]
        onActivated: dialog.close()
    }

    component AppField : Rectangle {
        id: rt
        property alias label: lbl.text
        property alias placeholder: tf.placeholderText
        property alias text: tf.text
        property bool tfFocus: tf.activeFocus
        Layout.fillWidth: true
        implicitHeight: col.implicitHeight
        color: "transparent"
        ColumnLayout {
            id: col
            anchors.left: parent.left; anchors.right: parent.right
            spacing: Theme.s6
            Text {
                id: lbl
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fxs
                font.weight: Font.Bold
                font.letterSpacing: 0.8
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                radius: Theme.r8
                color: Theme.surface
                border.color: rt.tfFocus ? Theme.accent : Theme.border
                TextField {
                    id: tf
                    anchors.fill: parent
                    anchors.leftMargin: Theme.s12
                    anchors.rightMargin: Theme.s12
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fbody
                    background: null
                    selectByMouse: true
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.s20
        spacing: Theme.s12

        Text {
            text: editingContactId === -1 ? qsTr("Add Contact") : qsTr("Edit Contact")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fxl
            font.weight: Font.Bold
            Layout.bottomMargin: Theme.s4
        }

        AppField { id: nameField;  label: qsTr("NAME");     placeholder: qsTr("e.g. Alice Smith") }
        AppField { id: uriField;   label: qsTr("SIP URI");  placeholder: qsTr("sip:1001@asterisk:5060") }
        AppField { id: phoneField; label: qsTr("PHONE (optional)"); placeholder: qsTr("+1 555 0100") }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s8
            Item { Layout.fillWidth: true }
            AppButton { variant: "secondary"; text: qsTr("Cancel"); onClicked: dialog.close() }
            AppButton {
                variant: "primary"
                iconPath: Icons.check
                text: editingContactId === -1 ? qsTr("Add") : qsTr("Save")
                onClicked: dialog._accept()
            }
        }
    }

    function _accept() {
        if (editingContactId === -1) {
            dialog.contactSaved(nameField.text, uriField.text, phoneField.text)
        } else {
            dialog.contactEdited(editingContactId, nameField.text, uriField.text, phoneField.text)
        }
        dialog.close()
    }

    function openForAdd() {
        editingContactId = -1
        nameField.text = ""; uriField.text = ""; phoneField.text = ""
        show(); raise(); requestActivate()
    }
    function openForEdit(contact) {
        editingContactId = contact.contactId
        nameField.text = contact.displayName
        uriField.text = contact.sipUri
        phoneField.text = contact.phone || ""
        show(); raise(); requestActivate()
    }
}
