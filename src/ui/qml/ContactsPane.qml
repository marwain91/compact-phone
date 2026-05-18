import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import CompactPhone

ColumnLayout {
    spacing: Theme.s12

    function initials(name) {
        if (!name || name.length === 0) return "?"
        const parts = name.trim().split(/\s+/)
        if (parts.length === 1) return parts[0].substring(0, 2).toUpperCase()
        return (parts[0][0] + parts[parts.length - 1][0]).toUpperCase()
    }
    function avatarColor(name) {
        const palette = [Theme.accent, Theme.sky, Theme.violet, "#EC4899", "#F59E0B", "#22C55E"]
        let h = 0
        for (let i = 0; i < name.length; i++) h = (h * 31 + name.charCodeAt(i)) % palette.length
        return palette[Math.abs(h)]
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s10
        Text {
            text: qsTr("Contacts")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.f2xl
            font.weight: Font.Bold
            font.letterSpacing: -0.4
            Layout.fillWidth: true
        }
        AppButton {
            variant: "ghost"
            size: "sm"
            text: qsTr("Import…")
            onClicked: importPicker.open()
        }
        AppButton {
            variant: "primary"
            size: "sm"
            iconPath: Icons.plus
            text: qsTr("Add")
            onClicked: editDialog.openForAdd()
        }
    }

    FileDialog {
        id: importPicker
        title: qsTr("Import contacts")
        nameFilters: [
            qsTr("vCard / CSV (*.vcf *.csv)"),
            qsTr("vCard files (*.vcf)"),
            qsTr("CSV files (*.csv)"),
            qsTr("All files (*)")
        ]
        onAccepted: {
            // FileDialog gives back a file:// URL; PhoneController wants a path.
            const url = String(selectedFile)
            const path = url.replace(/^file:\/\//, "")
            const n = PhoneController.importContactsFromFile(path)
            // No snackbar API in scope here, log via console.
            console.log("Imported", n, "contacts from", path)
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true

    ListView {
        id: list
        anchors.fill: parent
        visible: count > 0
        model: PhoneController.contacts
        spacing: Theme.s6
        clip: true

        delegate: Rectangle {
            width: list.width
            height: 44
            radius: Theme.r10
            color: hover.containsMouse ? Theme.surfaceHi : Theme.surface
            border.color: Theme.border
            border.width: 1

            MouseArea { id: hover; anchors.fill: parent; hoverEnabled: true }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.s12
                anchors.rightMargin: Theme.s8
                spacing: Theme.s12
                Rectangle {
                    width: 30; height: 30; radius: 15
                    color: avatarColor(displayName)
                    Text {
                        anchors.centerIn: parent
                        text: initials(displayName)
                        color: "#FFFFFF"
                        font.family: Theme.fontFamily
                        font.pixelSize: 10
                        font.weight: Font.Bold
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        text: displayName
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fbody
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        text: sipUri + (phone.length > 0 ? "  •  " + phone : "")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fxs
                        elide: Text.ElideRight
                    }
                }
                IconButton {
                    iconPath: Icons.star
                    diameter: 30
                    iconSize: 14
                    tint: favorite ? "#F59E0B" : Theme.textTertiary
                    hoverTint: "#F59E0B"
                    onClicked: PhoneController.setContactFavorite(contactId, !favorite)
                }
                IconButton {
                    iconPath: Icons.phone
                    diameter: 32
                    iconSize: 13
                    bgColor: Theme.successSoft
                    bgHover: "#10B981"
                    tint: "#10B981"
                    hoverTint: "#FFFFFF"
                    onClicked: PhoneController.dialContact(contactId)
                }
                IconButton {
                    iconPath: Icons.edit
                    diameter: 30
                    iconSize: 14
                    onClicked: editDialog.openForEdit({
                        contactId: contactId,
                        displayName: displayName,
                        sipUri: sipUri,
                        phone: phone
                    })
                }
                IconButton {
                    iconPath: Icons.trash
                    diameter: 30
                    iconSize: 14
                    hoverTint: Theme.danger
                    onClicked: PhoneController.removeContact(contactId)
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
                text: qsTr("Create your first contact")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.flg
                font.weight: Font.DemiBold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Save SIP addresses for quick dialing")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsm
            }
        }
    }

    ContactEditDialog {
        id: editDialog
        onContactSaved: (displayName, sipUri, phone) => {
            PhoneController.addContact(displayName, sipUri, phone)
        }
        onContactEdited: (contactId, displayName, sipUri, phone) => {
            PhoneController.updateContact(contactId, displayName, sipUri, phone)
        }
    }
}
