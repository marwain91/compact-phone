import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

ColumnLayout {
    id: root
    spacing: Theme.s16

    property int callId: -1
    property string remoteUri: ""
    property string state: "idle"
    property bool held: false
    property bool muted: false
    property bool recording: false
    property bool daktelaAccount: false

    property bool showDtmf: false
    readonly property bool ended: state === "ended"

    // Forward physical-keyboard digits (0-9, *, #) to PJSIP as DTMF while a
    // call is live. The dialer's TextField is hidden once a call exists, so
    // we don't have to worry about stealing keystrokes from text input.
    focus: true
    Keys.onPressed: (event) => {
        if (root.ended) return
        let digit = ""
        if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
            digit = String.fromCharCode(event.key)
        } else if (event.key === Qt.Key_Asterisk) {
            digit = "*"
        } else if (event.key === Qt.Key_NumberSign) {
            digit = "#"
        }
        if (digit.length > 0) {
            PhoneController.sendDtmf(root.callId, digit)
            event.accepted = true
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: Theme.r16
        color: Theme.surface
        border.color: Theme.border
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.s14
            anchors.rightMargin: Theme.s14
            anchors.topMargin: Theme.s24
            anchors.bottomMargin: Theme.s24
            spacing: Theme.s16

            DaktelaMark {
                objectName: "activeCallDaktelaMark"
                visible: root.daktelaAccount && !root.showDtmf
                markWidth: 126
                markHeight: 32
                Layout.preferredWidth: markWidth
                Layout.preferredHeight: markHeight
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Theme.s2
            }

            // Compact caller header — shown only when the keypad is open so the
            // user can still see who they're talking to.
            RowLayout {
                visible: root.showDtmf
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Theme.s8
                spacing: Theme.s8
                StatusBadge {
                    label: root.ended ? qsTr("Call ended")
                          : root.held ? qsTr("On hold")
                          : root.state === "active" ? qsTr("Connected")
                          : root.state === "calling" ? qsTr("Calling…")
                          : root.state === "ringing" ? qsTr("Ringing…")
                          : root.state === "early" ? qsTr("Calling…")
                          : root.state
                    status: root.ended ? "muted"
                          : root.held ? "warning"
                          : root.state === "active" ? "active"
                          : "accent"
                    pulse: !root.ended && (root.state !== "active" || root.held)
                }
                Text {
                    text: root.remoteUri
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.flg
                    font.weight: Font.DemiBold
                    font.letterSpacing: -0.2
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: 240
                }
            }

            // Top filler — keeps the caller block vertically centered in the
            // gap between the card's top edge and the action row. Hidden in
            // DTMF mode so the compact header can sit flush to the top.
            Item {
                visible: !root.showDtmf
                Layout.fillHeight: true
            }

            ColumnLayout {
                visible: !root.showDtmf
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.s10
                opacity: root.ended ? 0.6 : 1.0
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 56; height: 56; radius: 28
                    gradient: Gradient {
                        orientation: Gradient.Vertical
                        GradientStop { position: 0; color: Theme.accent }
                        GradientStop { position: 1; color: Theme.violet }
                    }
                    AppIcon {
                        anchors.centerIn: parent
                        path: Icons.user
                        color: "#FFFFFF"
                        width: 32; height: 32
                        stroke: 2.0
                    }
                    SequentialAnimation on scale {
                        running: root.state === "calling" || root.state === "ringing"
                        loops: Animation.Infinite
                        NumberAnimation { from: 1.0; to: 1.05; duration: 900; easing.type: Easing.InOutQuad }
                        NumberAnimation { from: 1.05; to: 1.0; duration: 900; easing.type: Easing.InOutQuad }
                    }
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.remoteUri
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.f2xl
                    font.weight: Font.Bold
                    font.letterSpacing: -0.4
                    elide: Text.ElideMiddle
                }
                StatusBadge {
                    Layout.alignment: Qt.AlignHCenter
                    label: root.ended ? qsTr("Call ended")
                          : root.held ? qsTr("On hold")
                          : root.state === "active" ? qsTr("Connected")
                          : root.state === "calling" ? qsTr("Calling…")
                          : root.state === "ringing" ? qsTr("Ringing…")
                          : root.state === "early" ? qsTr("Calling…")
                          : root.state
                    status: root.ended ? "muted"
                          : root.held ? "warning"
                          : root.state === "active" ? "active"
                          : "accent"
                    pulse: !root.ended && (root.state !== "active" || root.held)
                }

                // Call-quality indicator: 5 bars driven by MOS estimate (≥4.2
                // = full, ≥3.8 = 4, ≥3.4 = 3, ≥2.8 = 2, else 1 or hidden).
                // Refreshed every 2 s while the call is active; tooltip
                // exposes raw RTCP numbers.
                Item {
                    id: quality
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.state === "active" && !root.held && !root.ended
                    implicitWidth: 30
                    implicitHeight: 18

                    property real mos: -1
                    property real lossPct: -1
                    property int rttMs: -1
                    property int jitterMs: -1
                    property int bars: mos < 0 ? 0
                                      : mos >= 4.2 ? 5
                                      : mos >= 3.8 ? 4
                                      : mos >= 3.4 ? 3
                                      : mos >= 2.8 ? 2
                                      : 1
                    property color barColor:
                        bars >= 4 ? Theme.success :
                        bars >= 3 ? Theme.warning : Theme.danger

                    Timer {
                        interval: 2000
                        repeat: true
                        running: quality.visible
                        triggeredOnStart: true
                        onTriggered: {
                            const s = PhoneController.streamStats(root.callId)
                            quality.mos = s.mos
                            quality.lossPct = s.lossPct
                            quality.rttMs = s.rttMs
                            quality.jitterMs = s.jitterMs
                        }
                    }

                    Row {
                        anchors.centerIn: parent
                        spacing: 2
                        Repeater {
                            model: 5
                            delegate: Rectangle {
                                width: 3
                                height: 4 + (index * 3)
                                radius: 1
                                anchors.verticalCenter: parent.verticalCenter
                                color: index < quality.bars
                                    ? quality.barColor : Theme.border
                            }
                        }
                    }

                    ToolTip.visible: barsHover.containsMouse
                    ToolTip.delay: 350
                    ToolTip.text: quality.mos < 0
                        ? qsTr("Gathering quality data…")
                        : qsTr("MOS %1 · loss %2% · RTT %3 ms · jitter %4 ms")
                              .arg(quality.mos.toFixed(1))
                              .arg(quality.lossPct.toFixed(1))
                              .arg(quality.rttMs)
                              .arg(quality.jitterMs)
                    MouseArea {
                        id: barsHover
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                id: actionsRow
                objectName: "activeCallActionsRow"
                Layout.fillWidth: true
                Layout.preferredWidth: parent.width
                Layout.alignment: Qt.AlignHCenter
                spacing: actionSpacing
                enabled: !root.ended
                opacity: root.ended ? 0.45 : 1.0

                readonly property int visibleActionCount: 5 + (mergeAction.visible ? 1 : 0)
                readonly property int actionSpacing: width < 250 ? Theme.s4 : Theme.s8
                readonly property real actionSlotWidth: visibleActionCount <= 0 ? 0
                    : Math.max(34, Math.floor((width - Math.max(0, visibleActionCount - 1) * actionSpacing) / visibleActionCount))
                readonly property int actionButtonDiameter: Math.max(34, Math.min(44, Math.floor(actionSlotWidth)))
                readonly property int actionIconSize: actionButtonDiameter < 40 ? 16 : 18
                readonly property int actionRecordIconSize: actionButtonDiameter < 40 ? 12 : 14
                readonly property int actionLabelSize: actionSlotWidth < 39 ? 8 : Theme.fxs
                readonly property real contentWidth: visibleActionCount * actionButtonDiameter
                    + Math.max(0, visibleActionCount - 1) * actionSpacing

                ColumnLayout {
                    Layout.preferredWidth: actionsRow.actionSlotWidth
                    Layout.minimumWidth: actionsRow.actionSlotWidth
                    Layout.maximumWidth: actionsRow.actionSlotWidth
                    spacing: Theme.s2
                    IconButton {
                        Layout.alignment: Qt.AlignHCenter
                        iconPath: root.muted ? Icons.micOff : Icons.mic
                        diameter: actionsRow.actionButtonDiameter
                        iconSize: actionsRow.actionIconSize
                        bgColor: root.muted ? Theme.accentSoft : Theme.surfaceHi
                        bgHover: root.muted ? Theme.accentSoft : Theme.border
                        tint: root.muted ? Theme.accent : Theme.textPrimary
                        hoverTint: root.muted ? Theme.accent : Theme.textPrimary
                        onClicked: PhoneController.setMuted(root.callId, !root.muted)
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        text: root.muted ? qsTr("Unmute") : qsTr("Mute")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: actionsRow.actionLabelSize
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }
                ColumnLayout {
                    Layout.preferredWidth: actionsRow.actionSlotWidth
                    Layout.minimumWidth: actionsRow.actionSlotWidth
                    Layout.maximumWidth: actionsRow.actionSlotWidth
                    spacing: Theme.s4
                    IconButton {
                        Layout.alignment: Qt.AlignHCenter
                        iconPath: root.held ? Icons.play : Icons.pause
                        diameter: actionsRow.actionButtonDiameter
                        iconSize: actionsRow.actionIconSize
                        bgColor: root.held ? Theme.warningSoft : Theme.surfaceHi
                        bgHover: root.held ? Theme.warning : Theme.border
                        tint: root.held ? Theme.warning : Theme.textPrimary
                        hoverTint: root.held ? "#FFFFFF" : Theme.textPrimary
                        onClicked: root.held ? PhoneController.unhold(root.callId)
                                             : PhoneController.hold(root.callId)
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        text: root.held ? qsTr("Resume") : qsTr("Hold")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: actionsRow.actionLabelSize
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }
                ColumnLayout {
                    Layout.preferredWidth: actionsRow.actionSlotWidth
                    Layout.minimumWidth: actionsRow.actionSlotWidth
                    Layout.maximumWidth: actionsRow.actionSlotWidth
                    spacing: Theme.s4
                    IconButton {
                        Layout.alignment: Qt.AlignHCenter
                        iconPath: Icons.keypad
                        diameter: actionsRow.actionButtonDiameter
                        iconSize: actionsRow.actionIconSize
                        bgColor: root.showDtmf ? Theme.accentSoft : Theme.surfaceHi
                        bgHover: Theme.border
                        tint: root.showDtmf ? Theme.accent : Theme.textPrimary
                        hoverTint: Theme.textPrimary
                        onClicked: root.showDtmf = !root.showDtmf
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        text: qsTr("Keypad")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: actionsRow.actionLabelSize
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }
                ColumnLayout {
                    Layout.preferredWidth: actionsRow.actionSlotWidth
                    Layout.minimumWidth: actionsRow.actionSlotWidth
                    Layout.maximumWidth: actionsRow.actionSlotWidth
                    spacing: Theme.s4
                    IconButton {
                        Layout.alignment: Qt.AlignHCenter
                        iconPath: Icons.transfer
                        diameter: actionsRow.actionButtonDiameter
                        iconSize: actionsRow.actionIconSize
                        bgColor: Theme.surfaceHi
                        bgHover: Theme.border
                        onClicked: transferDialog.openFor(root.callId)
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        text: qsTr("Transfer")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: actionsRow.actionLabelSize
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }
                ColumnLayout {
                    Layout.preferredWidth: actionsRow.actionSlotWidth
                    Layout.minimumWidth: actionsRow.actionSlotWidth
                    Layout.maximumWidth: actionsRow.actionSlotWidth
                    spacing: Theme.s4
                    IconButton {
                        Layout.alignment: Qt.AlignHCenter
                        iconPath: Icons.record
                        diameter: actionsRow.actionButtonDiameter
                        iconSize: actionsRow.actionRecordIconSize
                        bgColor: root.recording ? Theme.dangerSoft : Theme.surfaceHi
                        bgHover: root.recording ? Theme.dangerSoft : Theme.border
                        tint: root.recording ? Theme.danger : Theme.textPrimary
                        hoverTint: root.recording ? Theme.danger : Theme.textPrimary
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: root.recording
                            ? qsTr("Stop recording")
                            : qsTr("Record this call")
                        onClicked: root.recording
                            ? PhoneController.stopRecording(root.callId)
                            : PhoneController.startRecording(root.callId)
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        text: root.recording ? qsTr("Recording") : qsTr("Record")
                        color: root.recording ? Theme.danger : Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: actionsRow.actionLabelSize
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }
                ColumnLayout {
                    id: mergeAction
                    // Merge appears only when there's another held call to
                    // bridge with — the C++ side returns -1 otherwise.
                    visible: !root.ended
                            && PhoneController.firstHeldCallId(root.callId) > 0
                    Layout.preferredWidth: actionsRow.actionSlotWidth
                    Layout.minimumWidth: actionsRow.actionSlotWidth
                    Layout.maximumWidth: actionsRow.actionSlotWidth
                    spacing: Theme.s4
                    IconButton {
                        Layout.alignment: Qt.AlignHCenter
                        iconPath: Icons.users
                        diameter: actionsRow.actionButtonDiameter
                        iconSize: actionsRow.actionIconSize
                        bgColor: Theme.surfaceHi
                        bgHover: Theme.border
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("Merge calls (3-way)")
                        onClicked: {
                            const other = PhoneController.firstHeldCallId(root.callId)
                            if (other > 0) PhoneController.mergeCalls(root.callId, other)
                        }
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        text: qsTr("Merge")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: actionsRow.actionLabelSize
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }
            }

            // DTMF keypad slides in between the action row and the hangup
            // button when toggled. Window stays locked at 440×440; the filler
            // above absorbs the height delta when the caller-info block hides.
            GridLayout {
                visible: root.showDtmf
                Layout.fillWidth: true
                Layout.topMargin: Theme.s4
                columns: 3
                rowSpacing: Theme.s4
                columnSpacing: Theme.s6
                Repeater {
                    model: ["1","2","3","4","5","6","7","8","9","*","0","#"]
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        radius: Theme.r8
                        color: dtmfTap.pressed ? Theme.accentSoft
                              : dtmfTap.hovered ? Theme.surfaceHi
                              : Theme.bgElevated
                        border.color: Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxl
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: dtmfTap
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: PhoneController.sendDtmf(root.callId, modelData)
                        }
                    }
                }
            }

            IconButton {
                Layout.alignment: Qt.AlignHCenter
                iconPath: Icons.phone
                diameter: 52
                iconSize: 22
                bgColor: root.ended ? Theme.surfaceHi : Theme.danger
                bgHover: root.ended ? Theme.surfaceHi : Theme.dangerHi
                tint: root.ended ? Theme.textTertiary : "#FFFFFF"
                hoverTint: root.ended ? Theme.textTertiary : "#FFFFFF"
                enabled: !root.ended
                onClicked: PhoneController.hangup(root.callId)
            }
        }
    }

    TransferDialog { id: transferDialog }
}
