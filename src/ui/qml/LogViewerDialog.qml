import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import CompactPhone

Window {
    id: dialog
    width: 720
    height: 480
    minimumWidth: 480
    minimumHeight: 320
    flags: Qt.Dialog | Qt.WindowTitleHint | Qt.WindowCloseButtonHint
    title: qsTr("Log Viewer")
    color: Theme.bg

    property string _allText: ""
    property string _filter: ""

    function refresh() {
        const lines = PhoneController.recentLogLines()
        _allText = lines.join("\n")
        applyFilter()
    }

    function applyFilter() {
        if (_filter.length === 0) {
            logView.text = _allText
        } else {
            const lower = _filter.toLowerCase()
            const lines = _allText.split("\n").filter(l =>
                l.toLowerCase().indexOf(lower) >= 0)
            logView.text = lines.join("\n")
        }
        logView.cursorPosition = logView.length
    }

    onVisibleChanged: if (visible) refresh()

    FileDialog {
        id: saveDialog
        title: qsTr("Save log")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Text files (*.txt)"), qsTr("All files (*)")]
        defaultSuffix: "txt"
        onAccepted: {
            const path = String(selectedFile).replace(/^file:\/\//, "")
            PhoneController.exportDiagnostics(path)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.s12
        spacing: Theme.s10

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s8

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                radius: Theme.r8
                color: Theme.surface
                border.color: filterField.activeFocus ? Theme.accent : Theme.border
                TextField {
                    id: filterField
                    anchors.fill: parent
                    anchors.leftMargin: Theme.s10
                    anchors.rightMargin: Theme.s10
                    placeholderText: qsTr("Filter (substring, case-insensitive)")
                    placeholderTextColor: Theme.textTertiary
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fsm
                    background: null
                    selectByMouse: true
                    onTextChanged: {
                        dialog._filter = text
                        dialog.applyFilter()
                    }
                }
            }
            AppButton {
                variant: "ghost"
                size: "sm"
                text: qsTr("Refresh")
                onClicked: dialog.refresh()
            }
            AppButton {
                variant: "ghost"
                size: "sm"
                text: qsTr("Save…")
                onClicked: saveDialog.open()
            }
            AppButton {
                variant: "secondary"
                size: "sm"
                text: qsTr("Close")
                onClicked: dialog.close()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.r10
            color: Theme.surface
            border.color: Theme.border
            ScrollView {
                anchors.fill: parent
                anchors.margins: Theme.s6
                clip: true
                TextArea {
                    id: logView
                    readOnly: true
                    wrapMode: TextArea.NoWrap
                    selectByMouse: true
                    color: Theme.textPrimary
                    background: null
                    font.family: Qt.platform.os === "osx" ? "Menlo" : "monospace"
                    font.pixelSize: Theme.fsm
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Showing the last 500 log entries. Save…  writes a full diagnostics file (log + version + accounts with passwords redacted).")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fxs
            wrapMode: Text.WordWrap
        }
    }
}
