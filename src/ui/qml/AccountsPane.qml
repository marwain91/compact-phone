import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

ColumnLayout {
    spacing: Theme.s12

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s10
        Text {
            text: qsTr("Accounts")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.f2xl
            font.weight: Font.Bold
            font.letterSpacing: -0.4
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        AppButton {
            variant: "primary"
            size: "sm"
            iconPath: Icons.plus
            text: qsTr("Add")
            onClicked: editDialog.openForAdd()
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true

    ListView {
        id: list
        anchors.fill: parent
        visible: count > 0
        model: PhoneController.accounts
        spacing: Theme.s6
        clip: true
        cacheBuffer: 0
        reuseItems: false

        delegate: Rectangle {
            width: list.width
            // Multi-line: row height grows with content. Users typically
            // have 1-2 accounts, so we'd rather show the SIP URI in full
            // than crunch it into an elided one-liner. A 56 px floor keeps
            // a single-line row from looking weirdly short.
            height: Math.max(56, row.implicitHeight + Theme.s12 * 2)
            radius: Theme.r10
            color: hoverArea.containsMouse ? Theme.surfaceHi : Theme.surface
            border.color: Theme.border
            border.width: 1

            MouseArea { id: hoverArea; anchors.fill: parent; hoverEnabled: true }

            RowLayout {
                id: row
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.s12
                anchors.rightMargin: Theme.s6
                spacing: Theme.s10

                Rectangle {
                    id: statusDot
                    Layout.alignment: Qt.AlignTop
                    width: 30; height: 30; radius: 15
                    color: registrationState === "registered" ? Theme.successSoft
                          : registrationState === "registering" ? Theme.warningSoft
                          : registrationState === "failed" ? Theme.dangerSoft
                          : Qt.rgba(1,1,1,0.05)
                    Rectangle {
                        anchors.centerIn: parent
                        width: 10; height: 10; radius: 5
                        color: registrationState === "registered" ? Theme.success
                              : registrationState === "registering" ? Theme.warning
                              : registrationState === "failed" ? Theme.danger
                              : Theme.textTertiary
                        SequentialAnimation on opacity {
                            running: registrationState === "registering"
                            loops: Animation.Infinite
                            NumberAnimation { from: 1.0; to: 0.3; duration: 700 }
                            NumberAnimation { from: 0.3; to: 1.0; duration: 700 }
                        }
                    }
                    MouseArea {
                        id: dotHover
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                    ToolTip.visible: dotHover.containsMouse
                    ToolTip.delay: 400
                    ToolTip.text: registrationState === "registered" ? qsTr("Registered")
                                : registrationState === "registering" ? qsTr("Registering…")
                                : registrationState === "failed"
                                    ? (registrationError && registrationError.length > 0
                                       ? qsTr("Registration failed: %1").arg(registrationError)
                                       : qsTr("Registration failed"))
                                : qsTr("Not registered")
                }

                // Three single-line stanzas: title → identity → metadata.
                // The title gets the full first line so it never competes
                // with the "default" tag. Default-account status moves to
                // the third (metadata) line where it belongs.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: (label && label.length > 0) ? label : displayName
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.flg
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: username + "@" + domain
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fxs
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.s6
                        Text {
                            text: transport.toUpperCase()
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                            font.weight: Font.Medium
                            font.letterSpacing: 0.5
                        }
                        Text {
                            visible: isDefault
                            text: "·  " + qsTr("DEFAULT")
                            color: Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                            font.weight: Font.Medium
                            font.letterSpacing: 0.5
                        }
                        Text {
                            visible: registrationState === "failed"
                                     && registrationError && registrationError.length > 0
                            Layout.fillWidth: true
                            text: "·  " + (registrationError || "")
                            color: Theme.danger
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                            elide: Text.ElideRight
                            wrapMode: Text.NoWrap
                        }
                        Item { Layout.fillWidth: true }
                    }
                }

                IconButton {
                    Layout.alignment: Qt.AlignTop
                    visible: !isDefault
                    iconPath: Icons.star
                    diameter: 26
                    iconSize: 13
                    hoverTint: Theme.accent
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Set as default")
                    onClicked: PhoneController.setDefaultAccount(accountId)
                }
                AppSwitch {
                    Layout.alignment: Qt.AlignTop
                    checked: model.enabled
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: checked ? qsTr("Enabled — click to disable") : qsTr("Disabled — click to enable")
                    onToggled: PhoneController.setAccountEnabled(accountId, checked)
                }
                IconButton {
                    id: moreBtn
                    Layout.alignment: Qt.AlignTop
                    iconPath: Icons.moreVertical
                    diameter: 26
                    iconSize: 14
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("More actions")
                    onClicked: moreMenu.popup(moreBtn,
                        moreBtn.width - moreMenu.implicitWidth,
                        moreBtn.height)

                    Menu {
                        id: moreMenu
                        modal: false
                        MenuItem {
                            text: qsTr("Edit")
                            icon.source: ""
                            onTriggered: editDialog.openForEdit(
                                PhoneController.accountSnapshot(accountId))
                        }
                        MenuItem {
                            text: qsTr("Delete")
                            onTriggered: PhoneController.removeAccount(accountId)
                        }
                    }
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
                path: Icons.server
                color: Theme.textTertiary
                width: 36; height: 36
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Add your first SIP account")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.flg
                font.weight: Font.DemiBold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Register with your SIP server to start calling")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsm
            }
        }
    }

    // "Sign in with" footer — one tile per known provisioning provider. The
    // tile shows the provider's wordmark (qrc:/branding/<provider>-mark.svg)
    // and opens the wizard preselected for that provider.
    Item {
        readonly property var providers: PhoneController.provisioningProviders()
        visible: providers.length > 0
        Layout.fillWidth: true
        Layout.preferredHeight: providers.length > 0 ? footerCol.implicitHeight : 0
        ColumnLayout {
            id: footerCol
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Theme.s8
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }
            Text {
                text: qsTr("Sign in with")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fxs
                font.weight: Font.Bold
                font.letterSpacing: 0.8
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.s8
                Repeater {
                    model: PhoneController.provisioningProviders()
                    delegate: Rectangle {
                        required property var modelData
                        width: 132; height: 40
                        radius: Theme.r8
                        color: tileHov.containsMouse ? Theme.surfaceHi : Theme.surface
                        border.color: tileHov.containsMouse ? Theme.accent : Theme.border
                        border.width: 1
                        Behavior on border.color { ColorAnimation { duration: 120 } }
                        MouseArea {
                            id: tileHov
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: providerDialog.openFor(modelData.id)
                        }
                        Image {
                            anchors.centerIn: parent
                            source: Theme.isDark
                                    ? (modelData.markPathDark || modelData.markPath || "")
                                    : (modelData.markPath || "")
                            fillMode: Image.PreserveAspectFit
                            height: 24
                            width: 110
                            visible: source !== ""
                        }
                        // Text fallback when no mark is provided.
                        Text {
                            anchors.centerIn: parent
                            visible: !modelData.markPath || modelData.markPath === ""
                            text: modelData.displayName
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fsm
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }
    }

    AccountEditDialog {
        id: editDialog
        onAccountSaved: (params) => PhoneController.addAccount(params)
        onAccountEdited: (accountId, params) => PhoneController.updateAccount(accountId, params)
    }

    ProviderSignInDialog {
        id: providerDialog
    }
}
