#include <gtest/gtest.h>

#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QTextStream>

namespace {

int firstCapturedInt(const QString &text, const QString &pattern)
{
    const auto match = QRegularExpression(pattern).match(text);
    if (!match.hasMatch()) {
        ADD_FAILURE() << "Pattern not found: " << pattern.toStdString();
        return -1;
    }
    return match.captured(1).toInt();
}

QRegularExpressionMatch firstMatch(const QString &text, const QString &pattern)
{
    const auto match = QRegularExpression(pattern).match(text);
    if (!match.hasMatch()) {
        ADD_FAILURE() << "Pattern not found: " << pattern.toStdString();
    }
    return match;
}

QString readQml(const QString &relativePath)
{
    QFile file(QStringLiteral(COMPACTPHONE_SOURCE_DIR) + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ADD_FAILURE() << "Could not open " << relativePath.toStdString();
        return {};
    }

    QTextStream in(&file);
    return in.readAll();
}

} // namespace

TEST(DaktelaBrandingLayout, DialerShowsDaktelaMarkForActiveDaktelaAccount)
{
    const auto qml = readQml(QStringLiteral("/src/ui/qml/DialerPane.qml"));
    ASSERT_FALSE(qml.isEmpty());

    EXPECT_TRUE(qml.contains(QStringLiteral("function _isDaktelaAccount")));
    EXPECT_TRUE(qml.contains(QStringLiteral("objectName: \"dialerDaktelaMark\"")));
    EXPECT_TRUE(qml.contains(QStringLiteral("visible: statusPill.info.isDaktela")));
    EXPECT_TRUE(qml.contains(QStringLiteral("DaktelaMark")));
}

TEST(DaktelaBrandingLayout, ActiveCallShowsCenteredDaktelaMark)
{
    const auto qml = readQml(QStringLiteral("/src/ui/qml/ActiveCallView.qml"));
    ASSERT_FALSE(qml.isEmpty());

    EXPECT_TRUE(qml.contains(QStringLiteral("property bool daktelaAccount")));
    EXPECT_TRUE(qml.contains(QStringLiteral("objectName: \"activeCallDaktelaMark\"")));
    EXPECT_TRUE(qml.contains(QStringLiteral("visible: root.daktelaAccount && !root.showDtmf")));
    EXPECT_TRUE(qml.contains(QStringLiteral("markWidth: 126")));
    EXPECT_TRUE(qml.contains(QStringLiteral("markHeight: 32")));
    EXPECT_TRUE(qml.contains(QStringLiteral("Layout.alignment: Qt.AlignHCenter")));
}

TEST(DaktelaBrandingLayout, SidebarHasCollapsedOpenHandle)
{
    const auto qml = readQml(QStringLiteral("/src/ui/qml/Main.qml"));
    ASSERT_FALSE(qml.isEmpty());

    EXPECT_TRUE(qml.contains(QStringLiteral("objectName: \"sidebarOpenHandle\"")));
    EXPECT_TRUE(qml.contains(QStringLiteral("visible: !window.sidebarExpanded")));
    EXPECT_TRUE(qml.contains(QStringLiteral(
        "anchors.left: parent.left\n"
        "        anchors.leftMargin: Theme.s6\n"
        "        anchors.verticalCenter: parent.verticalCenter")));
    EXPECT_TRUE(qml.contains(QStringLiteral("path: Icons.chevronRight")));
    EXPECT_TRUE(qml.contains(QStringLiteral("onClicked: window.sidebarExpanded = true")));
}

TEST(DaktelaBrandingLayout, DaktelaMarkUsesBundledBrandingAssets)
{
    const auto qml = readQml(QStringLiteral("/src/ui/qml/components/DaktelaMark.qml"));
    ASSERT_FALSE(qml.isEmpty());

    EXPECT_TRUE(qml.contains(QStringLiteral("qrc:/branding/daktela-mark-light.svg")));
    EXPECT_TRUE(qml.contains(QStringLiteral("qrc:/branding/daktela-mark-dark.svg")));
    EXPECT_TRUE(qml.contains(QStringLiteral(
        "Theme.isDark\n"
        "            ? \"qrc:/branding/daktela-mark-dark.svg\"\n"
        "            : \"qrc:/branding/daktela-mark-light.svg\"")));
    EXPECT_TRUE(qml.contains(QStringLiteral("Image")));
}

TEST(DaktelaBrandingLayout, ThemeSelectorUsesReadableRadioChips)
{
    const auto cardQml = readQml(QStringLiteral("/src/ui/qml/components/ThemeCard.qml"));
    ASSERT_FALSE(cardQml.isEmpty());

    EXPECT_GE(firstCapturedInt(cardQml, QStringLiteral("implicitHeight:\\s*(\\d+)")), 34);

    const auto swatch = firstMatch(
        cardQml,
        QStringLiteral("width:\\s*(\\d+)\\s*;\\s*height:\\s*(\\d+)\\s*;\\s*radius:\\s*(\\d+)"));
    ASSERT_TRUE(swatch.hasMatch());
    EXPECT_GE(swatch.captured(1).toInt(), 28);
    EXPECT_GE(swatch.captured(2).toInt(), 18);
    EXPECT_GE(swatch.captured(3).toInt(), 5);

    EXPECT_TRUE(cardQml.contains(QStringLiteral("font.pixelSize: Theme.fbody")));
    EXPECT_FALSE(cardQml.contains(QStringLiteral("font.pixelSize: Theme.fsm")));
    EXPECT_TRUE(cardQml.contains(QStringLiteral("Accessible.role: Accessible.RadioButton")));
    EXPECT_TRUE(cardQml.contains(QStringLiteral("Accessible.checked: root.isCurrent")));

    const auto settingsQml = readQml(QStringLiteral("/src/ui/qml/GeneralSettings.qml"));
    ASSERT_FALSE(settingsQml.isEmpty());
    EXPECT_TRUE(settingsQml.contains(QRegularExpression(
        QStringLiteral("Flow\\s*\\{\\s*Layout\\.fillWidth:\\s*true\\s*spacing:\\s*Theme\\.s10"))));
}
