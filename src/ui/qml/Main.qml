import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import CompactPhone

ApplicationWindow {
    id: window
    width: 380
    height: 500
    minimumWidth: 380
    maximumWidth: 380
    minimumHeight: 500
    maximumHeight: 500
    // alwaysOnTop is a user setting; ringingLift transiently forces on-top
    // while a call is ringing so the call dialog can't be hidden behind a
    // fullscreen app. Either being true pins the window above siblings.
    property bool ringingLift: PhoneController.incomingCallId >= 0
    flags: (Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint
            | Qt.WindowCloseButtonHint | Qt.WindowMinimizeButtonHint)
           | ((PhoneController.alwaysOnTop || ringingLift)
              ? Qt.WindowStaysOnTopHint : 0)
    Component.onCompleted: {
        Theme.setTheme(PhoneController.themeId)
        window.width = 380
        window.height = 500
    }
    visible: false
    title: qsTr("Compact Phone")
    color: Theme.bg

    property bool sidebarExpanded: true

    Action {
        id: toggleSidebarAction
        text: qsTr("Toggle Sidebar")
        shortcut: "Ctrl+B"
        onTriggered: window.sidebarExpanded = !window.sidebarExpanded
    }

    // Native macOS menu bar (Qt also renders this on platforms that have
    // a window-attached menu bar). The action carries the shortcut so it
    // shows up in the menu *and* fires from anywhere in the window.
    menuBar: MenuBar {
        Menu {
            title: qsTr("View")
            MenuItem { action: toggleSidebarAction }
        }
    }

    onClosing: (close) => {
        // Close X = hide to tray, not quit. PhoneController.requestQuit()
        // from the tray menu (or another platform-quit path) actually exits.
        close.accepted = false
        window.hide()
    }

    Connections {
        target: PhoneController
        function onThemeIdChanged() {
            Theme.setTheme(PhoneController.themeId)
        }
        function onTrayShowRequested() {
            window.show()
            window.raise()
            window.requestActivate()
        }
        function onTrayHideRequested() {
            window.hide()
        }
        function onEnterpriseFeaturesEnabledChanged() {
            // Messages (idx 2) and Lines (idx 4) live behind the toggle.
            // If the user turns it off while viewing either, snap back to
            // the dialer so they don't end up stuck on a hidden tab.
            if (!PhoneController.enterpriseFeaturesEnabled
                && (stack.currentIndex === 2 || stack.currentIndex === 4)) {
                stack.currentIndex = 0
            }
        }
    }

    Timer {
        interval: 50; running: true; repeat: false
        onTriggered: {
            window.show()
            window.raise()
            window.requestActivate()
            console.warn("DKPHN show fired visible=" + window.visible)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 200
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.gradTint }
                GradientStop { position: 1.0; color: Theme.bg }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                id: sidebar
                readonly property int expandedWidth: 58
                Layout.preferredWidth: window.sidebarExpanded ? expandedWidth : 0
                Layout.minimumWidth: Layout.preferredWidth
                Layout.maximumWidth: Layout.preferredWidth
                Layout.fillHeight: true
                color: "transparent"
                clip: true
                Behavior on Layout.preferredWidth {
                    NumberAnimation { duration: 180; easing.type: Easing.OutQuad }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.topMargin: Theme.s10
                    anchors.bottomMargin: Theme.s10
                    anchors.leftMargin: Theme.s6
                    anchors.rightMargin: Theme.s6
                    spacing: Theme.s4
                    opacity: window.sidebarExpanded ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 120 } }

                    Item {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.bottomMargin: Theme.s8
                        implicitWidth: 36
                        implicitHeight: 36
                        BrandMark {
                            anchors.centerIn: parent
                            size: 28
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            ToolTip.visible: containsMouse
                            ToolTip.delay: 380
                            ToolTip.text: qsTr("Collapse sidebar (⌘B)")
                            onClicked: window.sidebarExpanded = false
                        }
                    }

                    NavItem {
                        Layout.alignment: Qt.AlignHCenter
                        iconPath: Icons.dialpad
                        label: qsTr("Dialer")
                        active: stack.currentIndex === 0
                        onClicked: stack.currentIndex = 0
                    }
                    NavItem {
                        Layout.alignment: Qt.AlignHCenter
                        iconPath: Icons.clock
                        label: qsTr("History")
                        active: stack.currentIndex === 1
                        onClicked: stack.currentIndex = 1
                    }
                    NavItem {
                        Layout.alignment: Qt.AlignHCenter
                        visible: PhoneController.enterpriseFeaturesEnabled
                        iconPath: Icons.chat
                        label: qsTr("Messages")
                        active: stack.currentIndex === 2
                        onClicked: stack.currentIndex = 2
                    }
                    NavItem {
                        Layout.alignment: Qt.AlignHCenter
                        iconPath: Icons.users
                        label: qsTr("Contacts")
                        active: stack.currentIndex === 3
                        onClicked: stack.currentIndex = 3
                    }
                    NavItem {
                        Layout.alignment: Qt.AlignHCenter
                        visible: PhoneController.enterpriseFeaturesEnabled
                        iconPath: Icons.idCard
                        label: qsTr("Lines")
                        active: stack.currentIndex === 4
                        onClicked: stack.currentIndex = 4
                    }
                    NavItem {
                        Layout.alignment: Qt.AlignHCenter
                        iconPath: Icons.server
                        label: qsTr("Accounts")
                        active: stack.currentIndex === 5
                        onClicked: stack.currentIndex = 5
                    }
                    NavItem {
                        Layout.alignment: Qt.AlignHCenter
                        iconPath: Icons.settings
                        label: qsTr("Settings")
                        active: stack.currentIndex === 6
                        onClicked: stack.currentIndex = 6
                    }

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 8; height: 8; radius: 4
                        color: PhoneController.registeredAccountCount > 0
                               ? Theme.success : Theme.textTertiary
                        ToolTip.visible: regHover.containsMouse
                        ToolTip.delay: 380
                        ToolTip.text: PhoneController.registeredAccountCount > 0
                              ? qsTr("%1 account registered").arg(PhoneController.registeredAccountCount)
                              : (PhoneController.accounts.rowCount() === 0
                                 ? qsTr("No accounts")
                                 : qsTr("Not registered"))
                        MouseArea { id: regHover; anchors.fill: parent; hoverEnabled: true }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: window.sidebarExpanded ? 1 : 0
                Layout.fillHeight: true
                color: Theme.border
                Behavior on Layout.preferredWidth {
                    NumberAnimation { duration: 180; easing.type: Easing.OutQuad }
                }
            }

            StackLayout {
                id: stack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: 0

                Item { DialerPane    { anchors.fill: parent; anchors.margins: Theme.s20 } }
                Item { HistoryPane   { anchors.fill: parent; anchors.margins: Theme.s20 } }
                Item { MessagesPane  { anchors.fill: parent; anchors.margins: Theme.s20 } }
                Item { ContactsPane  { anchors.fill: parent; anchors.margins: Theme.s20 } }
                Item { LinesPane     { anchors.fill: parent; anchors.margins: Theme.s20 } }
                Item { AccountsPane  { anchors.fill: parent; anchors.margins: Theme.s20 } }
                Item { SettingsPane  { anchors.fill: parent; anchors.margins: Theme.s20 } }
            }
        }

        Snackbar {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
        }
    }


    IncomingCallDialog { id: incomingDialog }

    // In-app keyboard shortcuts. These fire when the Compact Phone window
    // has focus. System-wide global hotkeys (working even when minimized)
    // require platform-specific event capture (QHotkey on Win, NSEvent on
    // macOS with Accessibility permission, XGrabKey on X11). See task #10
    // in the backlog for the global-grab follow-up.
    Shortcut {
        sequence: "Ctrl+Shift+A"
        onActivated: {
            if (PhoneController.incomingCallId >= 0) {
                PhoneController.acceptIncoming()
            }
        }
    }
    Shortcut {
        sequence: "Ctrl+Shift+H"
        onActivated: {
            if (PhoneController.incomingCallId >= 0) {
                PhoneController.declineIncoming()
            } else if (CallsController && CallsController.callState !== "idle") {
                // Hang up the most recent active call (callsModel row 0).
                const m = PhoneController.callsModel
                if (m && m.rowCount() > 0) {
                    PhoneController.hangup(m.data(m.index(0, 0), Qt.UserRole + 1))
                }
            }
        }
    }
    Shortcut {
        sequence: "Ctrl+Shift+D"
        onActivated: PhoneController.dndEnabled = !PhoneController.dndEnabled
    }
    Shortcut {
        sequence: "Ctrl+Shift+R"
        onActivated: {
            const m = PhoneController.historyModel
            if (m && m.rowCount() > 0) {
                PhoneController.redialFromHistory(
                    m.data(m.index(0, 0), Qt.UserRole + 1))
            }
        }
    }
}
