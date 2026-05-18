import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import CompactPhone

Window {
    id: dialog
    width: 520
    height: 560
    minimumWidth: 520
    maximumWidth: 520
    minimumHeight: 560
    maximumHeight: 560
    modality: Qt.ApplicationModal
    flags: Qt.Dialog
    color: Theme.bgElevated
    title: editingAccountId === -1 ? qsTr("Add SIP Account") : qsTr("Edit Account")

    property int editingAccountId: -1

    signal accountSaved(var params)
    signal accountEdited(int accountId, var params)

    Shortcut {
        sequences: ["Esc"]
        onActivated: dialog.close()
    }

    component AppField : Rectangle {
        id: rt
        property alias label: lbl.text
        property alias placeholder: tf.placeholderText
        property alias text: tf.text
        property alias echoMode: tf.echoMode
        property alias validator: tf.validator
        property string hint: ""
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
            Text {
                visible: rt.hint.length > 0
                text: rt.hint
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: 10
            }
        }
    }

    component RowToggle : Item {
        id: rt
        property alias label: lbl.text
        property alias hint: hintText.text
        property alias checked: sw.checked
        implicitHeight: row.implicitHeight
        RowLayout {
            id: row
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.s10
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    id: lbl
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fbody
                    font.weight: Font.Medium
                }
                Text {
                    id: hintText
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fxs
                    visible: text.length > 0
                }
            }
            AppSwitch { id: sw }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.s16
        spacing: Theme.s12

        Text {
            text: dialog.title
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fxl
            font.weight: Font.Bold
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            background: Rectangle { color: "transparent" }
            TabButton {
                text: qsTr("General")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsm
                font.weight: Font.DemiBold
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? Theme.accent : Theme.textSecondary
                    font: parent.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: "transparent"
                    Rectangle {
                        visible: parent.parent.checked
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 2
                        color: Theme.accent
                    }
                }
            }
            TabButton {
                text: qsTr("Network")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsm
                font.weight: Font.DemiBold
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? Theme.accent : Theme.textSecondary
                    font: parent.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: "transparent"
                    Rectangle {
                        visible: parent.parent.checked
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left; anchors.right: parent.right
                        height: 2
                        color: Theme.accent
                    }
                }
            }
            TabButton {
                text: qsTr("Media")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsm
                font.weight: Font.DemiBold
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? Theme.accent : Theme.textSecondary
                    font: parent.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: "transparent"
                    Rectangle {
                        visible: parent.parent.checked
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left; anchors.right: parent.right
                        height: 2
                        color: Theme.accent
                    }
                }
            }
            TabButton {
                text: qsTr("Advanced")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsm
                font.weight: Font.DemiBold
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? Theme.accent : Theme.textSecondary
                    font: parent.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: "transparent"
                    Rectangle {
                        visible: parent.parent.checked
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left; anchors.right: parent.right
                        height: 2
                        color: Theme.accent
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ScrollView {
                id: sv1
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: sv1.availableWidth
                    spacing: Theme.s10
                    AppField { id: labelField; label: qsTr("LABEL"); placeholder: qsTr("e.g. Work Office") }
                    AppField { id: displayNameField; label: qsTr("SIP DISPLAY NAME"); placeholder: qsTr("Sent to PBX in From: header"); hint: qsTr("Shown as caller name to remote party") }
                    AppField { id: usernameField; label: qsTr("USERNAME / EXTENSION"); placeholder: qsTr("e.g. 1001") }
                    AppField { id: authUserField; label: qsTr("AUTH USERNAME"); placeholder: qsTr("Leave empty to use Username"); hint: qsTr("Some PBXes require a separate auth login") }
                    AppField { id: passwordField; visible: dialog.editingAccountId === -1; label: qsTr("PASSWORD"); placeholder: qsTr("••••••••"); echoMode: TextInput.Password }
                    AppField { id: domainField; label: qsTr("SIP SERVER"); placeholder: qsTr("pbx.example.com:5060") }
                }
            }

            ScrollView {
                id: sv2
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: sv2.availableWidth
                    spacing: Theme.s10
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.s12
                        ColumnLayout {
                            spacing: Theme.s6
                            Layout.fillWidth: true
                            Text {
                                text: qsTr("TRANSPORT")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fxs
                                font.weight: Font.Bold
                                font.letterSpacing: 0.8
                            }
                            AppComboBox { id: transportCombo; Layout.fillWidth: true; model: ["udp", "tcp", "tls"] }
                        }
                    }
                    AppField { id: proxyField; label: qsTr("OUTBOUND PROXY"); placeholder: qsTr("e.g. sbc.example.com:5060"); hint: qsTr("Sends all SIP through this proxy") }
                    AppField { id: stunField; label: qsTr("STUN SERVER"); placeholder: qsTr("e.g. stun.l.google.com:19302"); hint: qsTr("Discovers your public IP for NAT") }
                    AppField { id: publicAddrField; label: qsTr("PUBLIC ADDRESS"); placeholder: qsTr("Override for SDP/Contact"); hint: qsTr("Use if STUN can't be used") }
                    RowToggle { id: iceToggle; Layout.fillWidth: true; label: qsTr("Enable ICE"); hint: qsTr("Interactive Connectivity Establishment") }
                }
            }

            ScrollView {
                id: sv3
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: sv3.availableWidth
                    spacing: Theme.s10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.s6
                        Text {
                            text: qsTr("DTMF")
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                            font.weight: Font.Bold
                            font.letterSpacing: 0.8
                        }
                        AppComboBox { id: dtmfCombo; Layout.fillWidth: true; model: ["rfc2833", "info", "inband"] }
                    }
                    ColumnLayout {
                        visible: transportCombo.currentText === "tls"
                        Layout.fillWidth: true
                        spacing: Theme.s6
                        Text {
                            text: qsTr("SRTP")
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                            font.weight: Font.Bold
                            font.letterSpacing: 0.8
                        }
                        AppComboBox { id: srtpCombo; Layout.fillWidth: true; model: ["disabled", "optional", "required"]; currentIndex: 1 }
                    }
                    AppCheckBox {
                        id: untrustedCheck
                        visible: transportCombo.currentText === "tls"
                        text: qsTr("Allow untrusted server certificate (insecure)")
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.s6
                        Text {
                            text: qsTr("CODECS")
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                            font.weight: Font.Bold
                            font.letterSpacing: 0.8
                        }
                        Text {
                            text: qsTr("Drag with ↑↓ to set priority; uncheck to disable.")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        ListView {
                            id: codecList
                            Layout.fillWidth: true
                            Layout.preferredHeight: contentHeight
                            interactive: false
                            spacing: Theme.s4
                            model: ListModel { id: codecsModel }
                            delegate: Rectangle {
                                width: codecList.width
                                height: 32
                                radius: Theme.r8
                                color: Theme.surface
                                border.color: Theme.border
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.s10
                                    anchors.rightMargin: Theme.s6
                                    spacing: Theme.s8
                                    AppCheckBox {
                                        checked: model.enabled
                                        onToggled: codecsModel.setProperty(index, "enabled", checked)
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: model.name
                                        color: model.enabled
                                            ? Theme.textPrimary
                                            : Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fsm
                                        font.weight: Font.Medium
                                    }
                                    Rectangle {
                                        width: 24; height: 24
                                        radius: Theme.r6
                                        color: upHover.containsMouse
                                            ? Theme.surfaceHi : "transparent"
                                        opacity: index > 0 ? 1.0 : 0.35
                                        Text {
                                            anchors.centerIn: parent
                                            text: "▲"
                                            font.pixelSize: Theme.fsm
                                            color: Theme.textSecondary
                                        }
                                        MouseArea {
                                            id: upHover
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            enabled: index > 0
                                            onClicked: codecsModel.move(index, index - 1, 1)
                                        }
                                    }
                                    Rectangle {
                                        width: 24; height: 24
                                        radius: Theme.r6
                                        color: dnHover.containsMouse
                                            ? Theme.surfaceHi : "transparent"
                                        opacity: index < codecsModel.count - 1 ? 1.0 : 0.35
                                        Text {
                                            anchors.centerIn: parent
                                            text: "▼"
                                            font.pixelSize: Theme.fsm
                                            color: Theme.textSecondary
                                        }
                                        MouseArea {
                                            id: dnHover
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            enabled: index < codecsModel.count - 1
                                            onClicked: codecsModel.move(index, index + 1, 1)
                                        }
                                    }
                                }
                            }
                        }
                        AppButton {
                            variant: "ghost"
                            size: "sm"
                            text: qsTr("Reset to defaults")
                            onClicked: dialog._loadCodecs("")
                        }
                    }
                }
            }

            ScrollView {
                id: sv4
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: sv4.availableWidth
                    spacing: Theme.s10
                    RowToggle { id: regOnStartToggle; Layout.fillWidth: true; label: qsTr("Register on startup"); hint: qsTr("Auto-register when app starts"); checked: true }
                    RowToggle { id: sessionTimersToggle; Layout.fillWidth: true; label: qsTr("Session timers"); hint: qsTr("RFC 4028 — detect dead calls"); checked: true }
                    RowToggle { id: presenceToggle; Layout.fillWidth: true; label: qsTr("Publish presence"); hint: qsTr("Send your online status to PBX") }
                    RowToggle { id: hideCallerIdToggle; Layout.fillWidth: true; label: qsTr("Hide caller ID"); hint: qsTr("Send anonymous on outbound") }
                    AppField { id: regIntervalField; label: qsTr("RE-REGISTER INTERVAL (sec)"); placeholder: qsTr("0 = default (300)") }
                    AppField { id: keepaliveField; label: qsTr("KEEP-ALIVE INTERVAL (sec)"); placeholder: qsTr("0 = default") }
                    AppField { id: voicemailField; label: qsTr("VOICEMAIL NUMBER"); placeholder: qsTr("e.g. *97"); hint: qsTr("Dial this to access voicemail") }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s8
            Item { Layout.fillWidth: true }
            AppButton { variant: "secondary"; text: qsTr("Cancel"); onClicked: dialog.close() }
            AppButton {
                variant: "primary"
                iconPath: Icons.check
                text: editingAccountId === -1 ? qsTr("Add") : qsTr("Save")
                onClicked: dialog._accept()
            }
        }
    }

    function _params() {
        const p = {
            label: labelField.text,
            displayName: displayNameField.text,
            username: usernameField.text,
            authUser: authUserField.text,
            domain: domainField.text,
            transport: transportCombo.currentText,
            proxy: proxyField.text,
            stunServer: stunField.text,
            publicAddress: publicAddrField.text,
            voicemailNumber: voicemailField.text,
            registerOnStartup: regOnStartToggle.checked,
            registerIntervalSec: parseInt(regIntervalField.text) || 0,
            keepaliveIntervalSec: parseInt(keepaliveField.text) || 0,
            sessionTimersEnabled: sessionTimersToggle.checked,
            publishPresenceEnabled: presenceToggle.checked,
            iceEnabled: iceToggle.checked,
            hideCallerId: hideCallerIdToggle.checked,
            srtpMode: transportCombo.currentText === "tls" ? srtpCombo.currentText : "disabled",
            allowUntrustedCert: untrustedCheck.checked,
            dtmfMethod: dtmfCombo.currentText,
            codecs: dialog._collectCodecs()
        }
        if (editingAccountId === -1 && passwordField.text.length > 0) {
            p.password = passwordField.text
        }
        return p
    }

    function _accept() {
        if (editingAccountId === -1) {
            dialog.accountSaved(_params())
        } else {
            dialog.accountEdited(editingAccountId, _params())
        }
        dialog.close()
    }

    readonly property var _knownCodecs: ["opus", "PCMA", "PCMU", "G722", "GSM", "iLBC"]

    function _loadCodecs(csv) {
        codecsModel.clear()
        const seen = {}
        const ordered = (csv || "").split(",").map(s => s.trim()).filter(s => s.length > 0)
        for (const c of ordered) {
            seen[c.toLowerCase()] = true
            codecsModel.append({name: c, enabled: true})
        }
        for (const c of _knownCodecs) {
            if (!seen[c.toLowerCase()]) {
                codecsModel.append({name: c, enabled: ordered.length === 0})
            }
        }
    }

    function _collectCodecs() {
        const out = []
        for (let i = 0; i < codecsModel.count; i++) {
            const r = codecsModel.get(i)
            if (r.enabled) out.push(r.name)
        }
        return out.join(",")
    }

    function _reset() {
        tabs.currentIndex = 0
        labelField.text = ""
        displayNameField.text = ""
        usernameField.text = ""
        authUserField.text = ""
        passwordField.text = ""
        domainField.text = ""
        transportCombo.currentIndex = 0
        proxyField.text = ""
        stunField.text = ""
        publicAddrField.text = ""
        voicemailField.text = ""
        regIntervalField.text = ""
        keepaliveField.text = ""
        regOnStartToggle.checked = true
        sessionTimersToggle.checked = true
        presenceToggle.checked = false
        iceToggle.checked = false
        hideCallerIdToggle.checked = false
        srtpCombo.currentIndex = 1
        untrustedCheck.checked = false
        dtmfCombo.currentIndex = 0
        _loadCodecs("")
    }

    function openForAdd() {
        editingAccountId = -1
        _reset()
        show(); raise(); requestActivate()
    }

    function openForEdit(snapshot) {
        editingAccountId = snapshot.accountId
        _reset()
        labelField.text = snapshot.label || ""
        displayNameField.text = snapshot.displayName || ""
        usernameField.text = snapshot.username || ""
        authUserField.text = snapshot.authUser || ""
        domainField.text = snapshot.domain || ""
        const ti = transportCombo.model.indexOf(snapshot.transport || "udp")
        transportCombo.currentIndex = ti < 0 ? 0 : ti
        proxyField.text = snapshot.proxy || ""
        stunField.text = snapshot.stunServer || ""
        publicAddrField.text = snapshot.publicAddress || ""
        voicemailField.text = snapshot.voicemailNumber || ""
        regIntervalField.text = (snapshot.registerIntervalSec || 0) > 0 ? String(snapshot.registerIntervalSec) : ""
        keepaliveField.text = (snapshot.keepaliveIntervalSec || 0) > 0 ? String(snapshot.keepaliveIntervalSec) : ""
        regOnStartToggle.checked = snapshot.registerOnStartup !== false
        sessionTimersToggle.checked = snapshot.sessionTimersEnabled !== false
        presenceToggle.checked = !!snapshot.publishPresenceEnabled
        iceToggle.checked = !!snapshot.iceEnabled
        hideCallerIdToggle.checked = !!snapshot.hideCallerId
        const si = srtpCombo.model.indexOf(snapshot.srtpMode || "optional")
        srtpCombo.currentIndex = si < 0 ? 1 : si
        untrustedCheck.checked = !!snapshot.allowUntrustedCert
        const di = dtmfCombo.model.indexOf(snapshot.dtmfMethod || "rfc2833")
        dtmfCombo.currentIndex = di < 0 ? 0 : di
        _loadCodecs(snapshot.codecs || "")
        show(); raise(); requestActivate()
    }
}
