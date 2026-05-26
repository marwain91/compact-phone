import QtQuick
import CompactPhone

Item {
    id: root

    property int markWidth: 58
    property int markHeight: 18

    implicitWidth: markWidth
    implicitHeight: markHeight

    Image {
        anchors.fill: parent
        sourceSize.width: root.markWidth * 2
        sourceSize.height: root.markHeight * 2
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        source: Theme.isDark
            ? "qrc:/branding/daktela-mark-dark.svg"
            : "qrc:/branding/daktela-mark-light.svg"
    }
}
