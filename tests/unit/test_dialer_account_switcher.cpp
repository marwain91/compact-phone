#include <gtest/gtest.h>

#include <QFile>
#include <QString>
#include <QTextStream>

namespace {

int countOccurrences(const QString &text, const QString &needle)
{
    int count = 0;
    qsizetype pos = 0;
    while ((pos = text.indexOf(needle, pos)) >= 0) {
        ++count;
        pos += needle.size();
    }
    return count;
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

// Regression guard for the v0.1.1 dialpad overflow: the separate ~110px
// account ComboBox in the dialer header pushed the column past the window
// edge. The fix merges status + account switching into the one status pill.
TEST(DialerAccountSwitcher, NoStandaloneAccountComboInHeader)
{
    const auto qml = readQml(QStringLiteral("/src/ui/qml/DialerPane.qml"));
    ASSERT_FALSE(qml.isEmpty());

    // The overflowing, fixed-width combo must not come back.
    EXPECT_FALSE(qml.contains(QStringLiteral("id: accountCombo")));
    EXPECT_FALSE(qml.contains(QStringLiteral("implicitWidth: 110")));
}

TEST(DialerAccountSwitcher, StatusPillDoublesAsAccountSwitcher)
{
    const auto qml = readQml(QStringLiteral("/src/ui/qml/DialerPane.qml"));
    ASSERT_FALSE(qml.isEmpty());

    // The single dynamic element: the pill becomes the switcher when there is
    // more than one account, opening a popup that sets the active account.
    EXPECT_TRUE(qml.contains(QStringLiteral("readonly property bool multiAccount")));
    EXPECT_TRUE(qml.contains(QStringLiteral("id: accountMenu")));
    EXPECT_TRUE(qml.contains(
        QStringLiteral("PhoneController.activeAccountId = acctDlg.accountId")));
}

TEST(DialerAccountSwitcher, DialActionSuppressesRepeatedEnterSubmits)
{
    const auto qml = readQml(QStringLiteral("/src/ui/qml/DialerPane.qml"));
    ASSERT_FALSE(qml.isEmpty());

    EXPECT_TRUE(qml.contains(QStringLiteral("property bool _dialSubmitPending: false")));
    EXPECT_TRUE(qml.contains(QStringLiteral("function requestDial()")));
    EXPECT_TRUE(qml.contains(QStringLiteral("if (!callButton.enabled || root._dialSubmitPending) return")));
    EXPECT_TRUE(qml.contains(QStringLiteral("root._dialSubmitPending = true")));
    EXPECT_TRUE(qml.contains(QStringLiteral("dialRetryTimer.restart()")));

    EXPECT_EQ(countOccurrences(qml, QStringLiteral("PhoneController.dial(root.dialTarget)")), 1);
    EXPECT_EQ(countOccurrences(qml, QStringLiteral("callButton.clicked()")), 0);
    EXPECT_TRUE(qml.contains(QStringLiteral("onAccepted: root.requestDial()")));
    EXPECT_GE(countOccurrences(qml, QStringLiteral("onClicked: root.requestDial()")), 2);

    EXPECT_TRUE(qml.contains(QStringLiteral("enabled: idleView.visible")));
    EXPECT_TRUE(qml.contains(QStringLiteral("&& !root._dialSubmitPending")));
    EXPECT_TRUE(qml.contains(QStringLiteral("function onRowsInserted() { root._dialSubmitPending = false }")));
    EXPECT_TRUE(qml.contains(QStringLiteral("function onRowsRemoved() { root._dialSubmitPending = false }")));
    EXPECT_TRUE(qml.contains(QStringLiteral("function onModelReset() { root._dialSubmitPending = false }")));
}
