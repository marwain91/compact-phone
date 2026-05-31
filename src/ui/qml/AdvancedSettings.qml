import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CompactPhone

ScrollView {
    id: pane
    signal showLog()
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
                        checked: PhoneController.settings.enterpriseFeaturesEnabled
                        onToggled: PhoneController.settings.enterpriseFeaturesEnabled = checked
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
                        onClicked: pane.showLog()
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
                            text: PhoneController.crashReportingAvailable
                                ? qsTr("Anonymous, opt-in. SIP credentials are never sent.")
                                : qsTr("Unavailable in this build.")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fxs
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                    Item { Layout.fillWidth: true; implicitHeight: 1 }
                    AppSwitch {
                        enabled: PhoneController.crashReportingAvailable
                        checked: PhoneController.crashReportingAvailable
                            && PhoneController.settings.crashReportingEnabled
                        onToggled: if (PhoneController.crashReportingAvailable)
                            PhoneController.settings.crashReportingEnabled = checked
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
                RowLayout {
                    Layout.topMargin: Theme.s4
                    spacing: Theme.s8
                    AppButton {
                        variant: "ghost"
                        size: "sm"
                        text: qsTr("Check for updates")
                        onClicked: PhoneController.checkForUpdates()
                    }
                    AppButton {
                        visible: PhoneController.latestUpdateUrl.length > 0
                        variant: "primary"
                        size: "sm"
                        iconPath: Icons.download
                        text: PhoneController.latestUpdateVersion.length > 0
                            ? qsTr("Download %1").arg(PhoneController.latestUpdateVersion)
                            : qsTr("Download update")
                        onClicked: PhoneController.openLatestUpdateUrl()
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
