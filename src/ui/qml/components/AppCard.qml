import QtQuick
import CompactPhone

Rectangle {
    property alias content: contentItem.data
    color: Theme.surface
    radius: Theme.r12
    border.width: 1
    border.color: Theme.border
    implicitWidth: contentItem.implicitWidth + Theme.s16 * 2
    implicitHeight: contentItem.implicitHeight + Theme.s16 * 2

    Item {
        id: contentItem
        anchors.fill: parent
        anchors.margins: Theme.s16
    }
}
