import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

ColumnLayout {
    id: root
    spacing: Theme.s8

    property string dialTarget: ""
    property bool _syncingDialField: false

    readonly property var keys: [
        ["1", ""], ["2", "ABC"], ["3", "DEF"],
        ["4", "GHI"], ["5", "JKL"], ["6", "MNO"],
        ["7", "PQRS"], ["8", "TUV"], ["9", "WXYZ"],
        ["*", ""], ["0", "+"], ["#", ""]
    ]

    function appendKey(k) {
        dialTarget = dialTarget + k
        syncDialField()
    }

    function clearDialTarget() {
        dialTarget = ""
        syncDialField()
    }

    function isPhoneLike(value) {
        if (value.length === 0) return true
        for (let i = 0; i < value.length; i++) {
            const ch = value.charAt(i)
            const code = ch.charCodeAt(0)
            const isDigit = code >= 48 && code <= 57
            if (!isDigit && ch !== "+" && ch !== "*" && ch !== "#"
                    && ch !== " " && ch !== "-" && ch !== "("
                    && ch !== ")" && ch !== ".") {
                return false
            }
        }
        return true
    }

    function normalizeDialInput(value) {
        if (!isPhoneLike(value)) return value

        let normalized = ""
        for (let i = 0; i < value.length; i++) {
            const ch = value.charAt(i)
            const code = ch.charCodeAt(0)
            const isDigit = code >= 48 && code <= 57
            if (isDigit || ch === "*" || ch === "#") {
                normalized += ch
            } else if (ch === "+" && normalized.length === 0) {
                normalized += ch
            }
        }
        return normalized
    }

    function formatDialTarget(value) {
        if (!isPhoneLike(value)) return value
        if (value.indexOf("*") >= 0 || value.indexOf("#") >= 0) return value

        const hasPlus = value.charAt(0) === "+"
        const digits = hasPlus ? value.substring(1) : value
        if (digits.length <= 6) return value

        if (hasPlus && digits.charAt(0) === "1" && digits.length === 11) {
            return "+1 " + digits.substring(1, 4) + " "
                    + digits.substring(4, 7) + " " + digits.substring(7)
        }
        if (!hasPlus && digits.length === 7) {
            return digits.substring(0, 3) + " " + digits.substring(3)
        }
        if (!hasPlus && digits.length === 10) {
            return digits.substring(0, 3) + " " + digits.substring(3, 6)
                    + " " + digits.substring(6)
        }

        let formatted = hasPlus ? "+" : ""
        for (let i = 0; i < digits.length; i += 3) {
            if (formatted.length > (hasPlus ? 1 : 0)) formatted += " "
            formatted += digits.substring(i, Math.min(i + 3, digits.length))
        }
        return formatted
    }

    function syncDialField() {
        _syncingDialField = true
        dialField.text = formatDialTarget(dialTarget)
        dialField.cursorPosition = dialField.text.length
        _syncingDialField = false
    }

    // Pull the currently active account's display info so the status pill
    // can read "Registered · ext 204" the way the design calls for.
    // Provider identity comes from the account's provider role (set by the
    // provisioning layer), not a label/domain substring guess.
    function _isDaktelaAccount(provider) {
        return (provider || "").toLowerCase() === "daktela"
    }

    function _activeAccountInfo() {
        const m = PhoneController.accounts
        if (!m || m.rowCount() === 0) {
            return { hasAny: false, registered: false, ext: "", label: "", domain: "", isDaktela: false }
        }
        const aid = PhoneController.activeAccountId
        const usernameRole = Qt.UserRole + 3       // UsernameRole
        const domainRole   = Qt.UserRole + 4       // DomainRole
        const regStateRole = Qt.UserRole + 8       // RegistrationStateRole
        const labelRole    = Qt.UserRole + 10      // LabelRole
        const providerRole = Qt.UserRole + 11      // ProviderRole
        const idRole       = Qt.UserRole + 1       // IdRole
        let row = -1
        for (let i = 0; i < m.rowCount(); i++) {
            if (m.data(m.index(i, 0), idRole) === aid) { row = i; break }
        }
        if (row < 0) row = 0
        const idx = m.index(row, 0)
        const label = m.data(idx, labelRole) || ""
        const domain = m.data(idx, domainRole) || ""
        return {
            hasAny: true,
            registered: m.data(idx, regStateRole) === "registered",
            ext: m.data(idx, usernameRole) || "",
            label: label,
            domain: domain,
            isDaktela: root._isDaktelaAccount(m.data(idx, providerRole) || "")
        }
    }

    Item {
        id: idleView
        visible: callsRep.count === 0
        Layout.fillWidth: true
        Layout.fillHeight: true

        ColumnLayout {
            id: dialerContent
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width, 320)
            spacing: Theme.s12

            // ── Header: status pill, optional account combo, voicemail
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.s4
                spacing: Theme.s6

                Rectangle {
                    id: statusPill
                    property var info: root._activeAccountInfo()
                    readonly property bool ok: info.registered
                    readonly property string ext: info.ext
                    // With more than one account the pill doubles as the account
                    // switcher (chevron + popup). One dynamic element instead of a
                    // separate combo, which used to overflow the narrow header row.
                    readonly property bool multiAccount: PhoneController.accounts.count > 1
                    implicitHeight: 24
                    implicitWidth: pillRow.implicitWidth + Theme.s16
                    radius: Theme.rFull
                    color: statusPill.multiAccount && pillTap.containsMouse
                           ? (Theme.isDark ? Theme.surface : "#EEF1F7")
                           : (Theme.isDark ? Theme.surfaceHi : "#F7F8FC")
                    border.width: Theme.isDark ? 1 : 0
                    border.color: Theme.border

                    RowLayout {
                        id: pillRow
                        anchors.centerIn: parent
                        spacing: Theme.s6
                        Rectangle {
                            width: 7; height: 7; radius: 4
                            color: statusPill.ok ? Theme.success : Theme.textTertiary
                        }
                        Text {
                            text: {
                                if (!statusPill.info.hasAny) return qsTr("No account")
                                if (!statusPill.ok)          return qsTr("Not registered")
                                return statusPill.ext.length > 0
                                       ? qsTr("Registered · ext %1").arg(statusPill.ext)
                                       : qsTr("Registered")
                            }
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            font.letterSpacing: 0
                        }
                        AppIcon {
                            visible: statusPill.multiAccount
                            Layout.preferredWidth: 12
                            Layout.preferredHeight: 12
                            Layout.alignment: Qt.AlignVCenter
                            path: Icons.chevronDown
                            color: Theme.textTertiary
                        }
                    }

                    MouseArea {
                        id: pillTap
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: statusPill.multiAccount
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: accountMenu.opened ? accountMenu.close()
                                                      : accountMenu.open()
                    }

                    Popup {
                        id: accountMenu
                        y: statusPill.height + 4
                        width: Math.max(statusPill.width, 200)
                        padding: Theme.s4
                        background: Rectangle {
                            radius: Theme.r8
                            color: Theme.bgElevated
                            border.color: Theme.border
                        }
                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: accountMenu.visible ? PhoneController.accounts : null
                            delegate: ItemDelegate {
                                id: acctDlg
                                width: ListView.view ? ListView.view.width : 0
                                height: 34
                                required property int accountId
                                required property string label
                                required property string registrationState
                                readonly property bool isActive:
                                    accountId === PhoneController.activeAccountId
                                contentItem: RowLayout {
                                    spacing: Theme.s8
                                    Rectangle {
                                        width: 7; height: 7; radius: 4
                                        color: acctDlg.registrationState === "registered"
                                               ? Theme.success : Theme.textTertiary
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: acctDlg.label
                                        color: acctDlg.isActive ? Theme.accent : Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fbody
                                        font.weight: acctDlg.isActive ? Font.DemiBold : Font.Medium
                                        elide: Text.ElideRight
                                    }
                                    AppIcon {
                                        visible: acctDlg.isActive
                                        Layout.preferredWidth: 13
                                        Layout.preferredHeight: 13
                                        Layout.alignment: Qt.AlignVCenter
                                        path: Icons.check
                                        color: Theme.accent
                                    }
                                }
                                background: Rectangle {
                                    radius: Theme.r6
                                    color: acctDlg.hovered ? Theme.accentSoft : "transparent"
                                }
                                onClicked: {
                                    PhoneController.activeAccountId = acctDlg.accountId
                                    accountMenu.close()
                                }
                            }
                        }
                    }

                    Connections {
                        target: PhoneController
                        function onActiveAccountIdChanged() { statusPill.info = root._activeAccountInfo() }
                        function onRegisteredAccountCountChanged() { statusPill.info = root._activeAccountInfo() }
                    }
                }

                Item { Layout.fillWidth: true }

                DaktelaMark {
                    objectName: "dialerDaktelaMark"
                    visible: statusPill.info.isDaktela
                    markWidth: 54
                    markHeight: 16
                    Layout.preferredWidth: markWidth
                    Layout.preferredHeight: markHeight
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                }

                IconButton {
                    visible: PhoneController.newVoicemailCount > 0
                             && PhoneController.activeVoicemailNumber.length > 0
                    iconPath: Icons.voicemail
                    diameter: 24
                    iconSize: 12
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Dial voicemail (%1 new)")
                        .arg(PhoneController.newVoicemailCount)
                    onClicked: PhoneController.dialVoicemail()
                }
            }

            // ── Favorites speed-dial strip (kept; subtle).
            Flow {
                Layout.fillWidth: true
                spacing: Theme.s6
                visible: favoritesRep.count > 0
                Repeater {
                    id: favoritesRep
                    model: PhoneController.contacts.model
                    delegate: Rectangle {
                        visible: favorite
                        width: visible ? favLabel.implicitWidth + Theme.s14 : 0
                        height: visible ? 24 : 0
                        radius: 12
                        color: favTap.containsMouse ? Theme.accentSoft : Theme.surfaceHi
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
                            onClicked: PhoneController.contacts.dial(contactId)
                        }
                    }
                }
            }

            // ── Number display. A bare TextField (no chrome) renders huge
            //    and centered with a hairline underline — the design's hero.
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                Layout.topMargin: favoritesRep.count > 0 ? 0 : Theme.s2

                TextField {
                    id: dialField
                    anchors.fill: parent
                    anchors.leftMargin: Theme.s20
                    anchors.rightMargin: Theme.s20
                    horizontalAlignment: TextInput.AlignHCenter
                    verticalAlignment: TextInput.AlignVCenter
                    placeholderText: ""
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: dialField.text.length > 16 ? 22 : 27
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0
                    background: null
                    selectByMouse: true
                    onTextEdited: {
                        if (!root._syncingDialField) {
                            root.dialTarget = root.normalizeDialInput(text)
                            root.syncDialField()
                        }
                    }
                    onAccepted: { if (callButton.enabled) callButton.clicked() }
                    Connections {
                        target: PhoneController
                        function onDialerUriChanged() {
                            if (PhoneController.dialerUri.length > 0) {
                                root.dialTarget = root.normalizeDialInput(PhoneController.dialerUri)
                                root.syncDialField()
                            }
                        }
                    }
                }

                IconButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.dialTarget.length > 0
                    opacity: hovered ? 0.55 : 0
                    iconPath: Icons.close
                    diameter: 22
                    iconSize: 11
                    Behavior on opacity { NumberAnimation { duration: Theme.dur } }
                    onClicked: root.clearDialTarget()
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: Theme.s10
                    anchors.rightMargin: Theme.s10
                    height: 1
                    color: dialField.activeFocus ? Theme.borderStrong : Theme.border
                }
            }

            // ── Dial pad.
            GridLayout {
                Layout.fillWidth: true
                Layout.topMargin: 0
                columns: 3
                rowSpacing: Theme.s8
                columnSpacing: Theme.s8
                Repeater {
                    model: root.keys
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 60
                        radius: Theme.r10
                        color: tap.pressed
                            ? (Theme.isDark ? Theme.border : Qt.darker(Theme.surfaceHi, 1.08))
                            : tap.containsMouse
                                ? (Theme.isDark ? Theme.surface : "#FAFBFD")
                                : (Theme.isDark ? Theme.surfaceHi : "#F5F7FA")
                        border.width: 0

                        Behavior on color { ColorAnimation { duration: Theme.dur } }

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 2
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: modelData[0]
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: 17
                                font.weight: Font.Medium
                                font.letterSpacing: 0
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                visible: modelData[1].length > 0
                                text: modelData[1]
                                color: Theme.textTertiary
                                font.family: Theme.fontFamily
                                font.pixelSize: 7
                                font.weight: Font.Medium
                                font.letterSpacing: 1.4
                            }
                        }
                        MouseArea {
                            id: tap
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.appendKey(modelData[0])
                        }
                    }
                }
            }

            // ── Call button: large, warm-orange pill with a soft glow.
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                Layout.topMargin: Theme.s8

                // Previous "glow" was a Rectangle with an 8px solid border —
                // it renders as a hard ring around the pill, not a soft
                // shadow. Removed; the green fill is enough signal on its
                // own and matches the rest of the surface styling.

                Rectangle {
                    id: callBg
                    anchors.fill: parent
                    radius: Theme.r10
                    opacity: 1.0
                    color: !callButton.enabled     ? (Theme.isDark ? Theme.surfaceHi : "#F5F7FA")
                         : callMouse.pressed       ? Theme.callLo
                         : callMouse.containsMouse ? Theme.callHi
                         :                           Theme.call

                    Behavior on color   { ColorAnimation  { duration: Theme.dur } }
                    Behavior on opacity { NumberAnimation { duration: Theme.dur } }

                    Row {
                        anchors.centerIn: parent
                        spacing: Theme.s10
                        AppIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            path: Icons.phone
                            color: callButton.enabled ? "#FFFFFF" : Theme.textDisabled
                            width: 17; height: 17
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Call")
                            color: callButton.enabled ? "#FFFFFF" : Theme.textDisabled
                            font.family: Theme.fontFamily
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0
                        }
                    }

                    MouseArea {
                        id: callMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: callButton.enabled
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: callButton.clicked()
                    }
                }

                // Invisible Button preserves keyboard activation; Enter on
                // the dial field clicks it, and other components can target
                // callButton.clicked() as a single signal source.
                Button {
                    id: callButton
                    anchors.fill: parent
                    opacity: 0
                    enabled: PhoneController.activeAccountId > 0
                             && root.dialTarget.length > 0
                    onClicked: PhoneController.dial(root.dialTarget)
                }
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
            daktelaAccount: root._activeAccountInfo().isDaktela
        }
    }
}
