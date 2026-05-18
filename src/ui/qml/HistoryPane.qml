import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

ColumnLayout {
    spacing: Theme.s12

    function relTime(ts) {
        const d = new Date(ts)
        const now = new Date()
        const diff = (now - d) / 1000
        if (diff < 60) return qsTr("just now")
        if (diff < 3600) return Math.floor(diff/60) + qsTr("m ago")
        if (diff < 86400) return Math.floor(diff/3600) + qsTr("h ago")
        if (diff < 86400 * 7) return Math.floor(diff/86400) + qsTr("d ago")
        return d.toLocaleString(Qt.locale(), "MMM d, hh:mm")
    }
    function fmtDur(ms) {
        const s = Math.round(ms/1000)
        if (s < 60) return s + "s"
        const m = Math.floor(s/60); const r = s%60
        return m + "m " + r + "s"
    }

    Text {
        text: qsTr("Call History")
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.f2xl
        font.weight: Font.Bold
        font.letterSpacing: -0.4
        Layout.fillWidth: true
    }

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true

    ListView {
        id: list
        anchors.fill: parent
        visible: count > 0
        model: PhoneController.history
        spacing: Theme.s4
        clip: true
        cacheBuffer: 0
        reuseItems: false

        delegate: Rectangle {
            width: list.width
            height: 40
            radius: Theme.r8
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
                    width: 26; height: 26; radius: 13
                    color: durationMs === 0 && direction === "in" ? Theme.dangerSoft
                          : direction === "in" ? Theme.successSoft
                          : Theme.accentSoft
                    AppIcon {
                        anchors.centerIn: parent
                        path: durationMs === 0 && direction === "in" ? Icons.phoneMissed
                            : direction === "in" ? Icons.phoneIncoming
                            : Icons.phoneOutgoing
                        color: durationMs === 0 && direction === "in" ? Theme.danger
                            : direction === "in" ? Theme.success
                            : Theme.accent
                        width: 13; height: 13
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        text: remoteUri
                        color: durationMs === 0 && direction === "in" ? Theme.danger : Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fbody
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        text: relTime(startedAt) +
                              (durationMs > 0 ? "  •  " + fmtDur(durationMs) : "")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fxs
                    }
                }
                IconButton {
                    iconPath: Icons.phone
                    diameter: 30
                    iconSize: 13
                    bgColor: Theme.successSoft
                    bgHover: "#10B981"
                    tint: "#10B981"
                    hoverTint: "#FFFFFF"
                    onClicked: PhoneController.redialFromHistory(historyId)
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
                path: Icons.clock
                color: Theme.textTertiary
                width: 36; height: 36
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("No call history")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.flg
                font.weight: Font.DemiBold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Calls will appear here")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsm
            }
        }
    }
}
