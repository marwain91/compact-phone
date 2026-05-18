import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import CompactPhone

// Multi-step provisioning wizard.
//   1. host        — user enters the server URL
//   2. discover    — we probe /internal/globalsettings.json for SSO methods
//   3. methods     — user picks Password or one of the SSO buttons
//   4a. password   — username + password form
//   4b. sso        — opens the host in the browser, then asks the user to
//                    paste their access token back into Compact Phone
//   5. provisioning — spinner while we fetch the SIP extension
//
// If discovery fails the wizard falls back to password-only. Each provider in
// PhoneController.provisioningProviders() can drive this same wizard; today
// the only one is Daktela.
Window {
    id: dialog
    width: 480
    height: 520
    minimumWidth: 480
    maximumWidth: 480
    minimumHeight: 520
    maximumHeight: 520
    modality: Qt.ApplicationModal
    flags: Qt.Dialog
    color: Theme.bgElevated
    title: qsTr("Sign in")

    property string providerId: ""
    property var    providerDescriptor: ({ id: "", displayName: "", hostPlaceholder: "", markPath: "" })
    property string step: "host"     // host | discover | methods | password | sso | provisioning
    property string normalizedHost: ""
    property var    methods: []
    property var    selectedMethod: ({})
    property string errorMessage: ""

    function openFor(id) {
        const list = PhoneController.provisioningProviders()
        if (list.length === 0) return
        let pd = list[0]
        if (id) {
            for (var i = 0; i < list.length; ++i)
                if (list[i].id === id) { pd = list[i]; break }
        }
        dialog.providerId = pd.id
        dialog.providerDescriptor = pd
        dialog.step = "host"
        dialog.normalizedHost = ""
        dialog.methods = []
        dialog.selectedMethod = ({})
        dialog.errorMessage = ""
        hostField.text = pd.hostPlaceholder || ""
        userField.text = ""
        passField.text = ""
        tokenField.text = ""
        dialog.show()
        dialog.raise()
        dialog.requestActivate()
    }

    Connections {
        target: PhoneController
        function onAuthMethodsDiscovered(provId, host, methods) {
            if (provId !== dialog.providerId || dialog.step !== "discover") return
            dialog.normalizedHost = host
            dialog.methods = methods
            dialog.step = "methods"
        }
        function onAuthMethodsFailed(provId, host, err) {
            if (provId !== dialog.providerId || dialog.step !== "discover") return
            // Soft-fall to password — DaktelaProvider already does this for
            // network errors, but invalid hosts still bubble up here.
            dialog.errorMessage = err
            dialog.step = "host"
        }
        function onProvisioningProgress(provId, stage) {
            if (provId !== dialog.providerId) return
            // Once provisioning starts, ignore any further user interactions.
            if (stage !== "done") dialog.step = "provisioning"
        }
        function onProvisioningFailed(provId, err) {
            if (provId !== dialog.providerId) return
            dialog.errorMessage = err
            // Hop back to whichever input screen we came from.
            if (dialog.selectedMethod && dialog.selectedMethod.kind === "sso")
                dialog.step = "sso"
            else
                dialog.step = "password"
        }
        function onAccountProvisioned(provId, accountId) {
            if (provId !== dialog.providerId) return
            dialog.close()
        }
    }

    Shortcut {
        sequences: ["Esc"]
        enabled: dialog.step !== "provisioning" && dialog.step !== "discover"
        onActivated: dialog.close()
    }

    component AppField : Rectangle {
        id: rt
        property alias label: lbl.text
        property alias placeholder: tf.placeholderText
        property alias text: tf.text
        property alias echoMode: tf.echoMode
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
                Layout.preferredHeight: 34
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

    component ErrorBanner : Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: visible ? 32 : 0
        visible: dialog.errorMessage.length > 0
        radius: Theme.r8
        color: Theme.dangerSoft
        border.color: Theme.danger
        Text {
            anchors.fill: parent
            anchors.leftMargin: Theme.s10
            anchors.rightMargin: Theme.s10
            verticalAlignment: Text.AlignVCenter
            text: dialog.errorMessage
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsm
            elide: Text.ElideRight
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.s16
        spacing: Theme.s12

        // Header (always present) ---------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s10
            Image {
                source: Theme.isDark
                        ? (dialog.providerDescriptor.markPathDark
                           || dialog.providerDescriptor.markPath || "")
                        : (dialog.providerDescriptor.markPath || "")
                fillMode: Image.PreserveAspectFit
                Layout.preferredHeight: 28
                Layout.preferredWidth: 110
                visible: source !== ""
            }
            Text {
                Layout.fillWidth: true
                text: dialog.step === "host"          ? qsTr("Where is your %1?").arg(dialog.providerDescriptor.displayName)
                    : dialog.step === "methods"       ? qsTr("How would you like to sign in?")
                    : dialog.step === "password"      ? qsTr("Sign in to %1").arg(dialog.normalizedHost)
                    : dialog.step === "sso"           ? dialog.selectedMethod.displayName || qsTr("Sign in")
                    : dialog.step === "discover"      ? qsTr("Connecting to %1…").arg(hostField.text)
                    : qsTr("Setting up your account…")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fxl
                font.weight: Font.Bold
                wrapMode: Text.WordWrap
            }
        }

        ErrorBanner {}

        // ===== STEP: host =====================================================
        ColumnLayout {
            visible: dialog.step === "host"
            Layout.fillWidth: true
            spacing: Theme.s12
            Text {
                Layout.fillWidth: true
                text: qsTr("Enter the address of your Daktela instance.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsm
                wrapMode: Text.WordWrap
            }
            AppField {
                id: hostField
                label: qsTr("SERVER")
                placeholder: dialog.providerDescriptor.hostPlaceholder
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s8
                Item { Layout.fillWidth: true }
                AppButton {
                    variant: "secondary"
                    text: qsTr("Cancel")
                    onClicked: dialog.close()
                }
                AppButton {
                    variant: "primary"
                    text: qsTr("Continue")
                    enabled: hostField.text.length > 0
                    onClicked: {
                        dialog.errorMessage = ""
                        dialog.step = "discover"
                        PhoneController.discoverAuthMethods(
                            dialog.providerId, hostField.text)
                    }
                }
            }
        }

        // ===== STEP: discover =================================================
        ColumnLayout {
            visible: dialog.step === "discover"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.s12
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.s10
                BusyIndicator { running: dialog.step === "discover"; implicitWidth: 22; implicitHeight: 22 }
                Text {
                    text: qsTr("Looking up sign-in methods…")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fbody
                }
            }
            Item { Layout.fillHeight: true }
        }

        // ===== STEP: methods ==================================================
        ColumnLayout {
            visible: dialog.step === "methods"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.s10

            Text {
                Layout.fillWidth: true
                text: qsTr("Connected to %1.").arg(dialog.normalizedHost)
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsm
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: dialog.methods
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    radius: Theme.r10
                    color: hov.containsMouse ? Theme.surfaceHi : Theme.surface
                    border.color: Theme.border
                    border.width: 1
                    MouseArea {
                        id: hov
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            dialog.errorMessage = ""
                            dialog.selectedMethod = modelData
                            if (modelData.kind === "sso") {
                                dialog.step = "sso"
                            } else {
                                dialog.step = "password"
                            }
                        }
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.s12
                        anchors.rightMargin: Theme.s12
                        spacing: Theme.s10
                        AppIcon {
                            path: modelData.kind === "sso" ? Icons.globe : Icons.user
                            color: Theme.textSecondary
                            width: 16; height: 16
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.displayName
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fbody
                            font.weight: Font.DemiBold
                        }
                        AppIcon {
                            path: Icons.chevronRight
                            color: Theme.textTertiary
                            width: 14; height: 14
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                AppButton {
                    variant: "secondary"
                    text: qsTr("Back")
                    onClicked: dialog.step = "host"
                }
                Item { Layout.fillWidth: true }
            }
        }

        // ===== STEP: password =================================================
        ColumnLayout {
            visible: dialog.step === "password"
            Layout.fillWidth: true
            spacing: Theme.s12

            AppField {
                id: userField
                label: qsTr("USERNAME")
                placeholder: qsTr("yourname")
            }
            AppField {
                id: passField
                label: qsTr("PASSWORD")
                echoMode: TextInput.Password
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s8
                AppButton {
                    variant: "secondary"
                    text: qsTr("Back")
                    onClicked: dialog.step = "methods"
                }
                Item { Layout.fillWidth: true }
                AppButton {
                    variant: "secondary"
                    text: qsTr("Cancel")
                    onClicked: dialog.close()
                }
                AppButton {
                    variant: "primary"
                    text: qsTr("Sign in")
                    enabled: userField.text.length > 0 && passField.text.length > 0
                    onClicked: {
                        dialog.errorMessage = ""
                        PhoneController.provisionWithProvider(
                            dialog.providerId,
                            dialog.normalizedHost,
                            userField.text,
                            passField.text)
                    }
                }
            }
        }

        // ===== STEP: sso ======================================================
        ColumnLayout {
            visible: dialog.step === "sso"
            Layout.fillWidth: true
            spacing: Theme.s12

            Text {
                Layout.fillWidth: true
                text: dialog.selectedMethod.instructions ||
                      qsTr("After signing in, paste your access token below.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsm
                wrapMode: Text.WordWrap
            }

            AppButton {
                variant: "secondary"
                text: qsTr("Open %1 in browser").arg(dialog.normalizedHost)
                Layout.fillWidth: true
                onClicked: {
                    if (dialog.selectedMethod.openUrl)
                        Qt.openUrlExternally(dialog.selectedMethod.openUrl)
                }
            }

            AppField {
                id: tokenField
                label: qsTr("ACCESS TOKEN")
                placeholder: qsTr("Paste your Daktela API token here")
            }

            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s8
                AppButton {
                    variant: "secondary"
                    text: qsTr("Back")
                    onClicked: dialog.step = "methods"
                }
                Item { Layout.fillWidth: true }
                AppButton {
                    variant: "secondary"
                    text: qsTr("Cancel")
                    onClicked: dialog.close()
                }
                AppButton {
                    variant: "primary"
                    text: qsTr("Continue")
                    enabled: tokenField.text.trim().length > 0
                    onClicked: {
                        dialog.errorMessage = ""
                        PhoneController.provisionWithProviderToken(
                            dialog.providerId,
                            dialog.normalizedHost,
                            tokenField.text.trim())
                    }
                }
            }
        }

        // ===== STEP: provisioning =============================================
        ColumnLayout {
            visible: dialog.step === "provisioning"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.s12
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.s10
                BusyIndicator { running: dialog.step === "provisioning"; implicitWidth: 22; implicitHeight: 22 }
                Text {
                    text: qsTr("Setting up your account…")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fbody
                }
            }
            Item { Layout.fillHeight: true }
        }
    }
}
