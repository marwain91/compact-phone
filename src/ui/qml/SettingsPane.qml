import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import CompactPhone

Item {
    id: root

    FileDialog {
        id: ringtonePicker
        title: qsTr("Choose ringtone")
        nameFilters: [qsTr("WAV files (*.wav)")]
        onAccepted: PhoneController.ringtonePath = selectedFile
    }

    LogViewerDialog { id: logViewer }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.s10

        Text {
            text: qsTr("Settings")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.f2xl
            font.weight: Font.Bold
            font.letterSpacing: -0.4
            Layout.fillWidth: true
        }

        // Custom-styled tab strip. Each TabButton renders as a transparent
        // label with an accent underline when checked — same treatment as
        // AccountEditDialog. The active underline overlaps the strip's
        // bottom hairline so it reads as one line, not two.
        TabBar {
            id: tabs
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.border
                }
            }

            component SettingsTab: TabButton {
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsm
                font.weight: Font.DemiBold
                padding: 0
                implicitHeight: 34
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? Theme.accent
                          : parent.hovered ? Theme.textPrimary
                          : Theme.textSecondary
                    font: parent.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    Behavior on color { ColorAnimation { duration: Theme.dur } }
                }
                background: Rectangle {
                    color: "transparent"
                    // Accent bar sits at the strip's bottom edge and is 2 px
                    // tall — covers the 1 px border so the two read as a
                    // single coloured line for the active tab.
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

            SettingsTab { text: qsTr("General") }
            SettingsTab { text: qsTr("Audio") }
            // The former separate "Forward" tab is now the second card on
            // the Calls tab — both are call-handling settings and the tab
            // strip was getting crowded.
            SettingsTab { text: qsTr("Calls") }
            SettingsTab { text: qsTr("Advanced") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // -------- General tab --------
            ScrollView {
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.s10

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: contentG.implicitHeight + Theme.s24
                        radius: Theme.r10
                        color: Theme.surface
                        border.color: Theme.border
                        ColumnLayout {
                            id: contentG
                            anchors.fill: parent
                            anchors.margins: Theme.s14
                            spacing: Theme.s12
                            Text {
                                text: qsTr("THEME")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fxs
                                font.weight: Font.Bold
                                font.letterSpacing: 1.2
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                rowSpacing: Theme.s8
                                columnSpacing: Theme.s8
                                Repeater {
                                    model: Theme.themes
                                    delegate: ThemeCard {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 72
                                        themeId: modelData.id
                                        name: modelData.name
                                        isCurrent: PhoneController.themeId === modelData.id
                                        onClicked: PhoneController.themeId = modelData.id
                                    }
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                            Text {
                                text: qsTr("GENERAL")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fxs
                                font.weight: Font.Bold
                                font.letterSpacing: 1.2
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: qsTr("Log level")
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fbody
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        text: qsTr("Verbosity of internal logs")
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fxs
                                    }
                                }
                                AppComboBox {
                                    model: ["trace", "debug", "info", "warn", "error"]
                                    currentIndex: model.indexOf(PhoneController.logLevel)
                                    onActivated: PhoneController.logLevel = currentText
                                    implicitWidth: 110
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: qsTr("Always on top")
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fbody
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        text: qsTr("Keep the window above other apps")
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fxs
                                    }
                                }
                                Item { Layout.fillWidth: true; implicitHeight: 1 }
                                AppSwitch {
                                    checked: PhoneController.alwaysOnTop
                                    onToggled: PhoneController.alwaysOnTop = checked
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                            Text {
                                text: qsTr("KEYBOARD")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fxs
                                font.weight: Font.Bold
                                font.letterSpacing: 1.2
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: qsTr("Toggle sidebar")
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fbody
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        text: qsTr("Show or hide the navigation rail")
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fxs
                                    }
                                }
                                Rectangle {
                                    Layout.preferredHeight: 22
                                    Layout.preferredWidth: kbdLbl.implicitWidth + Theme.s12
                                    radius: Theme.r6
                                    color: Theme.surfaceHi
                                    border.color: Theme.border
                                    border.width: 1
                                    Text {
                                        id: kbdLbl
                                        anchors.centerIn: parent
                                        text: Qt.platform.os === "osx" ? "⌘ B" : "Ctrl + B"
                                        color: Theme.textSecondary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fsm
                                        font.weight: Font.Medium
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // -------- Audio tab --------
            ScrollView {
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.s10

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: audioCol.implicitHeight + Theme.s24
                        radius: Theme.r10
                        color: Theme.surface
                        border.color: Theme.border
                        ColumnLayout {
                            id: audioCol
                            anchors.fill: parent
                            anchors.margins: Theme.s14
                            spacing: Theme.s12

                            Text {
                                text: qsTr("AUDIO")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fxs
                                font.weight: Font.Bold
                                font.letterSpacing: 1.2
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s6
                                Text {
                                    text: qsTr("Microphone")
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fbody
                                    font.weight: Font.Medium
                                }
                                AppComboBox {
                                    id: micCombo
                                    Layout.fillWidth: true
                                    model: PhoneController.audioInputs
                                    textRole: "name"
                                    valueRole: "id"
                                    currentIndex: {
                                        const cid = PhoneController.captureDeviceId
                                        for (let i = 0; i < model.length; i++) {
                                            if (model[i].id === cid) return i
                                        }
                                        return 0
                                    }
                                    onActivated: PhoneController.captureDeviceId = currentValue
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s6
                                Text {
                                    text: qsTr("Speaker")
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fbody
                                    font.weight: Font.Medium
                                }
                                AppComboBox {
                                    id: spkCombo
                                    Layout.fillWidth: true
                                    model: PhoneController.audioOutputs
                                    textRole: "name"
                                    valueRole: "id"
                                    currentIndex: {
                                        const pid = PhoneController.playbackDeviceId
                                        for (let i = 0; i < model.length; i++) {
                                            if (model[i].id === pid) return i
                                        }
                                        return 0
                                    }
                                    onActivated: PhoneController.playbackDeviceId = currentValue
                                }
                            }

                            AppButton {
                                variant: "ghost"
                                size: "sm"
                                text: qsTr("Refresh devices")
                                onClicked: PhoneController.refreshAudioDevices()
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: ringCol.implicitHeight + Theme.s24
                        radius: Theme.r10
                        color: Theme.surface
                        border.color: Theme.border
                        ColumnLayout {
                            id: ringCol
                            anchors.fill: parent
                            anchors.margins: Theme.s14
                            spacing: Theme.s12

                            Text {
                                text: qsTr("RINGTONE")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fxs
                                font.weight: Font.Bold
                                font.letterSpacing: 1.2
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: qsTr("Play ringtone on inbound calls")
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fbody
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        text: qsTr("A short tone when someone calls you")
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fxs
                                    }
                                }
                                Item { Layout.fillWidth: true; implicitHeight: 1 }
                                AppSwitch {
                                    checked: PhoneController.ringtoneEnabled
                                    onToggled: PhoneController.ringtoneEnabled = checked
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        text: {
                                            const p = PhoneController.ringtonePath
                                            if (!p || p === PhoneController.defaultRingtonePath) return qsTr("Built-in tone")
                                            const i = p.lastIndexOf("/")
                                            return i >= 0 ? p.substring(i + 1) : p
                                        }
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fbody
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: qsTr("Played when an inbound call rings")
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fxs
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s8
                                AppButton {
                                    variant: "secondary"
                                    size: "sm"
                                    text: qsTr("Choose…")
                                    onClicked: ringtonePicker.open()
                                }
                                AppButton {
                                    variant: "ghost"
                                    size: "sm"
                                    text: qsTr("Reset")
                                    visible: PhoneController.ringtonePath !== PhoneController.defaultRingtonePath
                                    onClicked: PhoneController.ringtonePath = PhoneController.defaultRingtonePath
                                }
                                Item { Layout.fillWidth: true }
                                AppButton {
                                    variant: "ghost"
                                    size: "sm"
                                    iconPath: Icons.play
                                    text: qsTr("Test")
                                    onClicked: PhoneController.testRingtone(2000)
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // -------- Calls tab --------
            ScrollView {
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.s10

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: callsCol.implicitHeight + Theme.s24
                        radius: Theme.r10
                        color: Theme.surface
                        border.color: Theme.border
                        ColumnLayout {
                            id: callsCol
                            anchors.fill: parent
                            anchors.margins: Theme.s14
                            spacing: Theme.s12

                            Text {
                                text: qsTr("CALLS")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fxs
                                font.weight: Font.Bold
                                font.letterSpacing: 1.2
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: qsTr("Do not disturb")
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fbody
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        text: qsTr("Reject incoming calls with 486 Busy Here")
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fxs
                                    }
                                }
                                Item { Layout.fillWidth: true; implicitHeight: 1 }
                                AppSwitch {
                                    checked: PhoneController.dndEnabled
                                    onToggled: PhoneController.dndEnabled = checked
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: qsTr("Auto-answer")
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fbody
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        text: qsTr("Pick up incoming calls automatically")
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fxs
                                    }
                                }
                                Item { Layout.fillWidth: true; implicitHeight: 1 }
                                AppSwitch {
                                    checked: PhoneController.autoAnswerEnabled
                                    onToggled: PhoneController.autoAnswerEnabled = checked
                                }
                            }

                            RowLayout {
                                visible: PhoneController.autoAnswerEnabled
                                Layout.fillWidth: true
                                spacing: Theme.s10
                                Text {
                                    text: qsTr("Delay before pickup")
                                    color: Theme.textSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fsm
                                    Layout.fillWidth: true
                                }
                                AppComboBox {
                                    implicitWidth: 110
                                    implicitHeight: 26
                                    model: [
                                        { label: qsTr("Immediately"), ms: 0 },
                                        { label: qsTr("1 second"),    ms: 1000 },
                                        { label: qsTr("3 seconds"),   ms: 3000 },
                                        { label: qsTr("5 seconds"),   ms: 5000 },
                                        { label: qsTr("10 seconds"),  ms: 10000 }
                                    ]
                                    textRole: "label"
                                    valueRole: "ms"
                                    currentIndex: {
                                        const cur = PhoneController.autoAnswerDelayMs
                                        for (let i = 0; i < model.length; i++) {
                                            if (model[i].ms === cur) return i
                                        }
                                        return 0
                                    }
                                    onActivated: PhoneController.autoAnswerDelayMs = currentValue
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: qsTr("Auto-record calls")
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fbody
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        text: qsTr("Save every call to WAV in the recordings folder")
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fxs
                                    }
                                }
                                Item { Layout.fillWidth: true; implicitHeight: 1 }
                                AppSwitch {
                                    checked: PhoneController.autoRecordEnabled
                                    onToggled: PhoneController.autoRecordEnabled = checked
                                }
                            }
                        }
                    }

                    // Forwarding card — moved here from the standalone
                    // "Forward" tab. Keeps the two call-handling concerns
                    // side-by-side on the same scroll surface.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: cfwdCol.implicitHeight + Theme.s24
                        radius: Theme.r10
                        color: Theme.surface
                        border.color: Theme.border
                        ColumnLayout {
                            id: cfwdCol
                            anchors.fill: parent
                            anchors.margins: Theme.s14
                            spacing: Theme.s12

                            Text {
                                text: qsTr("FORWARDING")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fxs
                                font.weight: Font.Bold
                                font.letterSpacing: 1.2
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: qsTr("Forward all calls")
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fbody
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        text: qsTr("Every incoming call redirects with 302")
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fxs
                                    }
                                }
                                Item { Layout.fillWidth: true; implicitHeight: 1 }
                                AppSwitch {
                                    checked: PhoneController.cfwdAlwaysEnabled
                                    onToggled: PhoneController.cfwdAlwaysEnabled = checked
                                }
                            }
                            Rectangle {
                                visible: PhoneController.cfwdAlwaysEnabled
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                radius: Theme.r8
                                color: Theme.bg
                                border.color: cfwdAlwaysTf.activeFocus ? Theme.accent : Theme.border
                                TextField {
                                    id: cfwdAlwaysTf
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.s12
                                    anchors.rightMargin: Theme.s12
                                    text: PhoneController.cfwdAlwaysTarget
                                    placeholderText: qsTr("sip:user@host or extension")
                                    placeholderTextColor: Theme.textTertiary
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fsm
                                    background: null
                                    selectByMouse: true
                                    onEditingFinished: PhoneController.cfwdAlwaysTarget = text
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: qsTr("Forward when busy")
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fbody
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        text: qsTr("Redirects when another call is already active")
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fxs
                                    }
                                }
                                Item { Layout.fillWidth: true; implicitHeight: 1 }
                                AppSwitch {
                                    checked: PhoneController.cfwdBusyEnabled
                                    onToggled: PhoneController.cfwdBusyEnabled = checked
                                }
                            }
                            Rectangle {
                                visible: PhoneController.cfwdBusyEnabled
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                radius: Theme.r8
                                color: Theme.bg
                                border.color: cfwdBusyTf.activeFocus ? Theme.accent : Theme.border
                                TextField {
                                    id: cfwdBusyTf
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.s12
                                    anchors.rightMargin: Theme.s12
                                    text: PhoneController.cfwdBusyTarget
                                    placeholderText: qsTr("sip:user@host or extension")
                                    placeholderTextColor: Theme.textTertiary
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fsm
                                    background: null
                                    selectByMouse: true
                                    onEditingFinished: PhoneController.cfwdBusyTarget = text
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: qsTr("Forward when no answer")
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fbody
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        text: qsTr("Redirects if you don't pick up in time")
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fxs
                                    }
                                }
                                Item { Layout.fillWidth: true; implicitHeight: 1 }
                                AppSwitch {
                                    checked: PhoneController.cfwdNoAnswerEnabled
                                    onToggled: PhoneController.cfwdNoAnswerEnabled = checked
                                }
                            }
                            ColumnLayout {
                                visible: PhoneController.cfwdNoAnswerEnabled
                                Layout.fillWidth: true
                                spacing: Theme.s6
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 32
                                    radius: Theme.r8
                                    color: Theme.bg
                                    border.color: cfwdNaTf.activeFocus ? Theme.accent : Theme.border
                                    TextField {
                                        id: cfwdNaTf
                                        anchors.fill: parent
                                        anchors.leftMargin: Theme.s12
                                        anchors.rightMargin: Theme.s12
                                        text: PhoneController.cfwdNoAnswerTarget
                                        placeholderText: qsTr("sip:user@host or extension")
                                        placeholderTextColor: Theme.textTertiary
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fsm
                                        background: null
                                        selectByMouse: true
                                        onEditingFinished: PhoneController.cfwdNoAnswerTarget = text
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.s10
                                    Text {
                                        text: qsTr("Forward after")
                                        color: Theme.textSecondary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fsm
                                        Layout.fillWidth: true
                                    }
                                    AppComboBox {
                                        implicitWidth: 110
                                        implicitHeight: 26
                                        model: [
                                            { label: qsTr("5 seconds"),  ms: 5000  },
                                            { label: qsTr("10 seconds"), ms: 10000 },
                                            { label: qsTr("20 seconds"), ms: 20000 },
                                            { label: qsTr("30 seconds"), ms: 30000 },
                                            { label: qsTr("60 seconds"), ms: 60000 }
                                        ]
                                        textRole: "label"
                                        valueRole: "ms"
                                        currentIndex: {
                                            const cur = PhoneController.cfwdNoAnswerTimeoutMs
                                            for (let i = 0; i < model.length; i++) {
                                                if (model[i].ms === cur) return i
                                            }
                                            return 2
                                        }
                                        onActivated: PhoneController.cfwdNoAnswerTimeoutMs = currentValue
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // -------- Advanced tab --------
            ScrollView {
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.s10

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: advancedCol.implicitHeight + Theme.s24
                        radius: Theme.r10
                        color: Theme.surface
                        border.color: Theme.border
                        ColumnLayout {
                            id: advancedCol
                            anchors.fill: parent
                            anchors.margins: Theme.s14
                            spacing: Theme.s12

                            Text {
                                text: qsTr("ADVANCED")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fxs
                                font.weight: Font.Bold
                                font.letterSpacing: 1.2
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: qsTr("Enable enterprise features")
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fbody
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        text: qsTr("Shows Messages (SIP IM) and Lines (BLF) tabs in the sidebar")
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fxs
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                }
                                Item { Layout.fillWidth: true; implicitHeight: 1 }
                                AppSwitch {
                                    checked: PhoneController.enterpriseFeaturesEnabled
                                    onToggled: PhoneController.enterpriseFeaturesEnabled = checked
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: qsTr("Diagnostics")
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fbody
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        text: qsTr("View live log or save a redacted bundle for support")
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fxs
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                }
                                AppButton {
                                    variant: "ghost"
                                    size: "sm"
                                    text: qsTr("View log…")
                                    onClicked: logViewer.show()
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.s10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: qsTr("Send crash reports")
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fbody
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        text: qsTr("Anonymous, opt-in. SIP credentials are never sent.")
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fxs
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                }
                                Item { Layout.fillWidth: true; implicitHeight: 1 }
                                AppSwitch {
                                    checked: PhoneController.crashReportingEnabled
                                    onToggled: PhoneController.crashReportingEnabled = checked
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: aboutCol.implicitHeight + Theme.s24
                        radius: Theme.r10
                        color: Theme.surface
                        border.color: Theme.border
                        ColumnLayout {
                            id: aboutCol
                            anchors.fill: parent
                            anchors.margins: Theme.s14
                            spacing: Theme.s10
                            Text {
                                text: qsTr("ABOUT")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fxs
                                font.weight: Font.Bold
                                font.letterSpacing: 1.2
                            }
                            RowLayout {
                                spacing: Theme.s10
                                BrandMark { size: 30 }
                                ColumnLayout {
                                    spacing: -2
                                    Text {
                                        text: "CompactPhone"
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fbody
                                        font.weight: Font.Bold
                                    }
                                    Text {
                                        text: qsTr("Version %1 • GPL-3.0-or-later").arg(Qt.application.version)
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fxs
                                    }
                                }
                            }
                            Text {
                                text: qsTr("Free multiplatform SIP softphone")
                                color: Theme.textTertiary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fxs
                            }
                            Text {
                                text: qsTr("Copyright © 2026 Jiri Havlicek. Distributed under the GNU GPL v3 or later. Uses Qt (LGPLv3), PJSIP (GPLv2+), OpenSSL (Apache-2.0), spdlog (MIT) and SQLite (public domain).")
                                color: Theme.textTertiary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fxs
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Text {
                                textFormat: Text.RichText
                                text: qsTr("Source code: <a href=\"https://github.com/marwain91/compact-phone\">github.com/marwain91/compact-phone</a> · <a href=\"https://github.com/marwain91/compact-phone/blob/main/THIRD_PARTY_LICENSES.md\">Third-party licences</a>")
                                color: Theme.textTertiary
                                linkColor: Theme.accent
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fxs
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                                onLinkActivated: (link) => Qt.openUrlExternally(link)
                            }
                            AppButton {
                                Layout.topMargin: Theme.s4
                                variant: "ghost"
                                size: "sm"
                                text: qsTr("Check for updates")
                                onClicked: PhoneController.checkForUpdates()
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
