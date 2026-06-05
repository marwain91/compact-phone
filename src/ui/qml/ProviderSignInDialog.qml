import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import CompactPhone

// Multi-step provisioning wizard.
//   1. host         — user enters the server URL
//   2. methods      — user picks Username/Password or Access Token
//   3a. password    — username + password form
//   3b. token       — opens the Daktela web UI in the browser so the user
//                     can sign in and generate a personal access token,
//                     which they paste back into Compact Phone
//   4. provisioning — spinner while we fetch the SIP extension
//
// We don't surface Daktela's SSO methods (Google / Azure / Daktela SSO)
// because the redirect_uri is hardcoded to Daktela's web frontend, so
// a desktop client cannot drive the OAuth handshake honestly. Token paste
// is what users would do anyway after a browser SSO round-trip. The auth
// method list is therefore static — no /internal/globalsettings.json
// probe needed before showing it.
Window {
    id: dialog
    width: 380
    height: 380
    minimumWidth: 380
    maximumWidth: 380
    minimumHeight: 380
    maximumHeight: 380
    modality: Qt.ApplicationModal
    flags: Qt.Dialog
    color: Theme.bgElevated
    title: qsTr("Sign in")

    property string providerId: ""
    property var    providerDescriptor: ({ id: "", displayName: "", hostPlaceholder: "", markPath: "" })
    property string step: "host"     // host | methods | password | token | extension-password | provisioning
    property string normalizedHost: ""
    // Methods are static — Daktela has no desktop-friendly OAuth, so we
    // always offer the same two paths regardless of what /internal/
    // globalsettings.json advertises. Hardcoded here so the wizard doesn't
    // need a network round-trip before the user picks a method.
    property var    methods: []
    property var    selectedMethod: ({})
    property string errorMessage: ""
    // The SIP extension/username we resolved when the automatic secret fetch
    // was denied; shown on the manual-password step.
    property string pendingUsername: ""

    function staticMethodsFor(host) {
        return [
            {
                "id": "password",
                "kind": "password",
                "displayName": qsTr("Username & password"),
                "openUrl": "",
                "instructions": ""
            },
            {
                "id": "token",
                "kind": "token",
                "displayName": qsTr("Access token"),
                "openUrl": host,
                "instructions": qsTr("Open Daktela, sign in, generate a personal access token under Account → API tokens, then paste it below.")
            }
        ]
    }

    function openFor(id) {
        const list = PhoneController.provisioning.providers()
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
        dialog.pendingUsername = ""
        dialog.show()
        dialog.raise()
        dialog.requestActivate()
    }

    // --- Per-step submit actions. Shared by the primary buttons and the
    //     Enter key so the wizard is fully keyboard-drivable. Each no-ops
    //     unless its step's inputs are valid (mirrors the button enabled:).
    function submitHost() {
        if (hostField.text.length === 0) return
        dialog.errorMessage = ""
        dialog.normalizedHost = hostField.text
        dialog.methods = dialog.staticMethodsFor(hostField.text)
        dialog.step = "methods"
    }
    function submitPassword() {
        if (userField.text.length === 0 || passField.text.length === 0) return
        dialog.errorMessage = ""
        PhoneController.provisioning.provision(
            dialog.providerId, dialog.normalizedHost,
            userField.text, passField.text)
    }
    function submitToken() {
        if (tokenField.text.trim().length === 0) return
        dialog.errorMessage = ""
        PhoneController.provisioning.provisionWithToken(
            dialog.providerId, dialog.normalizedHost, tokenField.text.trim())
    }
    function submitExtensionPassword() {
        if (extPassField.text.length === 0) return
        dialog.errorMessage = ""
        PhoneController.provisioning.provideManualPassword(
            dialog.providerId, extPassField.text)
    }

    Connections {
        target: PhoneController.provisioning
        function onProvisioningProgress(provId, stage) {
            if (provId !== dialog.providerId) return
            // Once provisioning starts, ignore any further user interactions.
            if (stage !== "done") dialog.step = "provisioning"
        }
        function onPasswordRequired(provId, partial) {
            if (provId !== dialog.providerId) return
            // Auto-fetch of the SIP secret was denied — collect it by hand.
            dialog.pendingUsername = (partial && partial.username) ? partial.username : ""
            dialog.errorMessage = ""
            extPassField.text = ""
            dialog.step = "extension-password"
        }
        function onProvisioningFailed(provId, err) {
            if (provId !== dialog.providerId) return
            dialog.errorMessage = err
            // Stay on the manual-password step so the user can retry the SIP
            // secret; otherwise hop back to whichever input screen we came from.
            if (dialog.step === "extension-password")
                return
            if (dialog.selectedMethod && dialog.selectedMethod.kind === "token")
                dialog.step = "token"
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
        enabled: dialog.step !== "provisioning"
        onActivated: dialog.close()
    }

    component AppField : Rectangle {
        id: rt
        property alias label: lbl.text
        property alias placeholder: tf.placeholderText
        property alias text: tf.text
        property alias echoMode: tf.echoMode
        property bool tfFocus: tf.activeFocus
        // Fired when the user presses Enter/Return in the field, so each step
        // can submit (or advance focus) without a mouse.
        signal accepted()
        function focusField() { tf.forceActiveFocus() }
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
                    onAccepted: rt.accepted()
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
                    : dialog.step === "token"           ? dialog.selectedMethod.displayName || qsTr("Sign in")
                    : dialog.step === "extension-password" ? qsTr("Enter your SIP password")
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
                // Static methods, no server probe — Daktela has no
                // desktop-friendly OAuth so the auth method list doesn't depend
                // on the tenant. submitHost() normalizes the host downstream.
                onAccepted: dialog.submitHost()
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
                    onClicked: dialog.submitHost()
                }
            }
        }

        // (No "discover" step — kept the placeholder ColumnLayout out so
        //  the layout doesn't reserve vertical space for a step we never
        //  enter.)
        Item {
            visible: false
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
                            if (modelData.kind === "token") {
                                dialog.step = "token"
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
                        // AppIcon is an Image; inside a RowLayout we need
                        // Layout.preferredWidth/Height to force the box, otherwise
                        // RowLayout uses the SVG's intrinsic size and icons render
                        // at different sizes depending on the source SVG.
                        AppIcon {
                            path: modelData.kind === "token" ? Icons.key : Icons.user
                            color: Theme.textSecondary
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                            Layout.alignment: Qt.AlignVCenter
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
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                            Layout.alignment: Qt.AlignVCenter
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
                // Enter moves to the password field, or submits if it's filled.
                onAccepted: passField.text.length === 0
                            ? passField.focusField()
                            : dialog.submitPassword()
            }
            AppField {
                id: passField
                label: qsTr("PASSWORD")
                echoMode: TextInput.Password
                onAccepted: dialog.submitPassword()
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
                    onClicked: dialog.submitPassword()
                }
            }
        }

        // ===== STEP: token ====================================================
        ColumnLayout {
            visible: dialog.step === "token"
            Layout.fillWidth: true
            spacing: Theme.s12

            Text {
                Layout.fillWidth: true
                text: dialog.selectedMethod.instructions ||
                      qsTr("Open Daktela, sign in, generate a personal access token under Account → API tokens, then paste it below.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsm
                wrapMode: Text.WordWrap
            }

            AppButton {
                variant: "secondary"
                text: qsTr("Open %1").arg(dialog.normalizedHost)
                Layout.fillWidth: true
                onClicked: {
                    if (dialog.selectedMethod.openUrl)
                        Qt.openUrlExternally(dialog.selectedMethod.openUrl)
                }
            }

            AppField {
                id: tokenField
                label: qsTr("ACCESS TOKEN")
                placeholder: qsTr("Paste the token from Daktela here")
                onAccepted: dialog.submitToken()
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
                    onClicked: dialog.submitToken()
                }
            }
        }

        // ===== STEP: extension-password ======================================
        // Reached when login + whoim succeeded but the SIP secret couldn't be
        // fetched automatically (the account may lack permission to read the
        // device record). We have every other account field already; the user
        // just supplies the SIP password.
        ColumnLayout {
            visible: dialog.step === "extension-password"
            Layout.fillWidth: true
            spacing: Theme.s12

            Text {
                Layout.fillWidth: true
                text: dialog.pendingUsername.length > 0
                      ? qsTr("We couldn't read the SIP password for extension %1 automatically. Enter it to finish.").arg(dialog.pendingUsername)
                      : qsTr("Enter the SIP password for your extension to finish.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsm
                wrapMode: Text.WordWrap
            }
            AppField {
                id: extPassField
                label: qsTr("SIP PASSWORD")
                echoMode: TextInput.Password
                onAccepted: dialog.submitExtensionPassword()
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
                    enabled: extPassField.text.length > 0
                    onClicked: dialog.submitExtensionPassword()
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
