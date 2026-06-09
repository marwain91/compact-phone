#include <gtest/gtest.h>

#include <QFile>
#include <QString>
#include <QTextStream>

namespace {

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

// Regression guard for the startup update prompt not surfacing.
//
// UpdateDialog used to be a separate top-level `Window` (Qt.ApplicationModal).
// A top-level modal window only rises to the front when its parent window is
// the key/active window at show() time; ~3s after launch the app often isn't
// frontmost, so the prompt stayed hidden behind the main window until the next
// focus change (e.g. opening Settings → Advanced). The fix renders it as an
// in-scene Dialog (same pattern as IncomingCallDialog), which can't get lost
// behind the main window.
TEST(UpdateDialogSurfaces, IsInSceneDialogNotTopLevelWindow)
{
    const auto qml = readQml(QStringLiteral("/src/ui/qml/UpdateDialog.qml"));
    ASSERT_FALSE(qml.isEmpty());

    // In-scene Qt Quick Controls Dialog, opened in the main window's scene.
    EXPECT_TRUE(qml.contains(QStringLiteral("\nDialog {")));
    EXPECT_TRUE(qml.contains(QStringLiteral("modal: true")));
    EXPECT_TRUE(qml.contains(QStringLiteral("dialog.open()")));

    // The top-level-Window surfacing race must not come back.
    EXPECT_FALSE(qml.contains(QStringLiteral("import QtQuick.Window")));
    EXPECT_FALSE(qml.contains(QStringLiteral("\nWindow {")));
    EXPECT_FALSE(qml.contains(QStringLiteral("modality:")));
    EXPECT_FALSE(qml.contains(QStringLiteral("dialog.show()")));
}

// The prompt is still wired to the controller signal that both the startup
// auto-check and the manual Settings button raise.
TEST(UpdateDialogSurfaces, StillOpenedFromUpdatePromptSignal)
{
    const auto qml = readQml(QStringLiteral("/src/ui/qml/Main.qml"));
    ASSERT_FALSE(qml.isEmpty());

    EXPECT_TRUE(qml.contains(QStringLiteral("function onUpdatePromptRequested")));
    EXPECT_TRUE(qml.contains(QStringLiteral("updateDialog.openFor(version)")));
    EXPECT_TRUE(qml.contains(QStringLiteral("UpdateDialog { id: updateDialog }")));
}

// Leftover startup debug logging must not ship.
TEST(UpdateDialogSurfaces, NoLeftoverStartupDebugLogging)
{
    const auto qml = readQml(QStringLiteral("/src/ui/qml/Main.qml"));
    ASSERT_FALSE(qml.isEmpty());

    EXPECT_FALSE(qml.contains(QStringLiteral("DKPHN")));
    EXPECT_FALSE(qml.contains(QStringLiteral("console.warn")));
    EXPECT_FALSE(qml.contains(QStringLiteral("console.log")));
}
