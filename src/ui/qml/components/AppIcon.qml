import QtQuick
import CompactPhone

Image {
    id: root
    property string path: ""
    property color color: Theme.textPrimary
    property real stroke: 1.5
    sourceSize.width: width > 0 ? width * 2 : 32
    sourceSize.height: height > 0 ? height * 2 : 32
    smooth: true
    mipmap: true
    fillMode: Image.PreserveAspectFit
    width: 20
    height: 20
    source: path.length > 0 ? Icons.svg(path, color, stroke) : ""
}
