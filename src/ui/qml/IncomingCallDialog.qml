import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

Dialog {
    id: dialog
    modal: true
    standardButtons: Dialog.NoButton
    closePolicy: Popup.NoAutoClose
    width: 400
    padding: 0

    background: Rectangle {
        color: Theme.bgElevated
        radius: Theme.r16
        border.color: Theme.border
    }
    header: null

    property bool active: PhoneController.incomingCallId >= 0
    onActiveChanged: { if (active) open(); else close(); }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.s24
        spacing: Theme.s16

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
                variant: "danger"
                iconPath: Icons.phone
                text: qsTr("Decline")
                onClicked: PhoneController.declineIncoming()
            }
            AppButton {
                Layout.fillWidth: true
                variant: "primary"
                iconPath: Icons.phone
                text: qsTr("Accept")
                onClicked: PhoneController.acceptIncoming()
            }
        }
    }
}
