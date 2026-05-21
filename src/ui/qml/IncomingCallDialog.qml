import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

// Modal popup shown when a SIP INVITE arrives. Sized by content (we use
// `contentItem` so the ColumnLayout's implicitHeight becomes the dialog's
// content height — previously the buttons spilled below the card because
// width was pinned but height wasn't).
Dialog {
    id: dialog
    modal: true
    standardButtons: Dialog.NoButton
    closePolicy: Popup.NoAutoClose
    padding: Theme.s24

    // Center on the parent window — Dialog doesn't do this automatically
    // when only width is set.
    anchors.centerIn: parent
    width: Math.min(420, (parent ? parent.width : 420) - Theme.s16 * 2)

    background: Rectangle {
        color: Theme.bgElevated
        radius: Theme.r16
        border.color: Theme.border
        border.width: 1
    }
    header: null

    property bool active: PhoneController.incomingCallId >= 0
    onActiveChanged: { if (active) open(); else close(); }

    contentItem: ColumnLayout {
        spacing: Theme.s12

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Incoming Call")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fxs
            font.weight: Font.Bold
            font.letterSpacing: 1.5
        }
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.s4
            width: 68; height: 68; radius: 34
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0; color: Theme.accent }
                GradientStop { position: 1; color: Theme.violet }
            }
            AppIcon {
                anchors.centerIn: parent
                path: Icons.user
                color: "#FFFFFF"
                width: 30; height: 30
                stroke: 2.0
            }
            SequentialAnimation on scale {
                running: dialog.active
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 1.08; duration: 700; easing.type: Easing.OutQuad }
                NumberAnimation { from: 1.08; to: 1.0; duration: 700; easing.type: Easing.InQuad }
            }
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            text: PhoneController.incomingCallFrom
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fxl
            font.weight: Font.Bold
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("is calling…")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsm
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s16
            spacing: Theme.s12
            AppButton {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                variant: "danger"
                iconPath: Icons.phone
                text: qsTr("Decline")
                onClicked: PhoneController.declineIncoming()
            }
            AppButton {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                // Green Accept signals "answer this call" — matches the
                // dialer's green place-call button and the iOS/Android
                // convention. Red Decline keeps the visual contrast.
                variant: "success"
                iconPath: Icons.phone
                text: qsTr("Accept")
                onClicked: PhoneController.acceptIncoming()
            }
        }
    }
}
