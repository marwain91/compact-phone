import QtQuick
import CompactPhone

// CompactPhone logo mark. Loads the canonical SVG from branding/, picking
// the light variant on dark themes and the dark variant on light themes.
// Sized to share NavItem's 38×38 footprint so the sidebar icons all share
// one vertical axis. Source files: branding/compactphone-mark-{light,dark}.svg
Item {
    id: root
    property int size: 30
    property int frame: 38
    width: frame
    height: frame

    Image {
        anchors.centerIn: parent
        width: root.size
        height: root.size
        sourceSize.width: root.size * 2
        sourceSize.height: root.size * 2
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        source: Theme.isDark
            ? "qrc:/branding/mark-light.svg"
            : "qrc:/branding/mark-dark.svg"
    }
}
