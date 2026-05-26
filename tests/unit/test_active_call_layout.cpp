#include <gtest/gtest.h>

#include <QFile>
#include <QString>
#include <QTextStream>

namespace {

QString activeCallViewQml()
{
    QFile file(QStringLiteral(COMPACTPHONE_SOURCE_DIR)
               + QStringLiteral("/src/ui/qml/ActiveCallView.qml"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ADD_FAILURE() << "Could not open ActiveCallView.qml";
        return {};
    }

    QTextStream in(&file);
    return in.readAll();
}

qsizetype countOccurrences(const QString &haystack, const QString &needle)
{
    qsizetype count = 0;
    qsizetype offset = 0;
    while ((offset = haystack.indexOf(needle, offset)) >= 0) {
        ++count;
        offset += needle.size();
    }
    return count;
}

} // namespace

TEST(ActiveCallLayout, ActionsFitWithExpandedSidebarWidth)
{
    const auto qml = activeCallViewQml();
    ASSERT_FALSE(qml.isEmpty());

    EXPECT_TRUE(qml.contains(QStringLiteral(
        "objectName: \"activeCallActionsRow\"")));
    EXPECT_TRUE(qml.contains(QStringLiteral(
        "readonly property int visibleActionCount")));
    EXPECT_TRUE(qml.contains(QStringLiteral(
        "readonly property real contentWidth")));
    EXPECT_TRUE(qml.contains(QStringLiteral(
        "Layout.preferredWidth: parent.width")));
    EXPECT_TRUE(qml.contains(QStringLiteral(
        "visibleActionCount * actionButtonDiameter")));
    EXPECT_TRUE(qml.contains(QStringLiteral(
        "Math.max(0, visibleActionCount - 1) * actionSpacing")));
    EXPECT_TRUE(qml.contains(QStringLiteral(
        "anchors.leftMargin: Theme.s14")));
    EXPECT_TRUE(qml.contains(QStringLiteral(
        "anchors.rightMargin: Theme.s14")));
    EXPECT_GE(countOccurrences(
                  qml,
                  QStringLiteral("Layout.preferredWidth: actionsRow.actionSlotWidth")),
              5);
    EXPECT_GE(countOccurrences(
                  qml,
                  QStringLiteral("diameter: actionsRow.actionButtonDiameter")),
              5);
}
