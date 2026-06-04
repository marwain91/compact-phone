import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

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
                            isCurrent: PhoneController.settings.themeId === modelData.id
                            onClicked: PhoneController.settings.themeId = modelData.id
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
                        currentIndex: model.indexOf(PhoneController.settings.logLevel)
                        onActivated: PhoneController.settings.logLevel = currentText
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
                        checked: PhoneController.settings.alwaysOnTop
                        onToggled: PhoneController.settings.alwaysOnTop = checked
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s10
                    visible: PhoneController.settings.autostartSupported
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: qsTr("Launch on startup")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fbody
                            font.weight: Font.Medium
                        }
                        Text {
                            text: qsTr("Start Compact Phone when you log in")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                        }
                    }
                    Item { Layout.fillWidth: true; implicitHeight: 1 }
                    AppSwitch {
                        checked: PhoneController.settings.launchOnStartup
                        onToggled: PhoneController.settings.launchOnStartup = checked
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: qsTr("Start minimized to tray")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fbody
                            font.weight: Font.Medium
                        }
                        Text {
                            text: qsTr("Open hidden in the tray instead of showing the window")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                        }
                    }
                    Item { Layout.fillWidth: true; implicitHeight: 1 }
                    AppSwitch {
                        checked: PhoneController.settings.startMinimizedToTray
                        onToggled: PhoneController.settings.startMinimizedToTray = checked
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
