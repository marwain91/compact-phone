#include <gtest/gtest.h>

#include "core/platform/Autostart_linux.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtGlobal>

using compactphone::platform::LinuxAutostart;

class AutostartLinuxTest : public ::testing::Test {
protected:
    QTemporaryDir tmp;
    QByteArray savedXdg;
    bool hadXdg = false;

    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        hadXdg = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
        if (hadXdg) savedXdg = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", tmp.path().toUtf8());
    }
    void TearDown() override {
        if (hadXdg) qputenv("XDG_CONFIG_HOME", savedXdg);
        else qunsetenv("XDG_CONFIG_HOME");
    }
    QString desktopPath() const {
        return tmp.path() + "/autostart/compactphone.desktop";
    }
};

TEST_F(AutostartLinuxTest, EnableWritesDesktopFileAndDisableRemovesIt)
{
    LinuxAutostart a;
    EXPECT_FALSE(a.isEnabled());

    EXPECT_TRUE(a.setEnabled(true));
    EXPECT_TRUE(a.isEnabled());
    ASSERT_TRUE(QFile::exists(desktopPath()));

    QFile f(desktopPath());
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QString body = QString::fromUtf8(f.readAll());
    EXPECT_TRUE(body.contains("[Desktop Entry]"));
    EXPECT_TRUE(body.contains("Type=Application"));
    EXPECT_TRUE(body.contains("Exec="));
    EXPECT_TRUE(body.contains("X-GNOME-Autostart-enabled=true"));

    EXPECT_TRUE(a.setEnabled(false));
    EXPECT_FALSE(a.isEnabled());
    EXPECT_FALSE(QFile::exists(desktopPath()));
}

TEST_F(AutostartLinuxTest, ExecUsesAppImageWhenSet)
{
    qputenv("APPIMAGE", "/home/u/Apps/Compact Phone.AppImage");
    LinuxAutostart a;
    ASSERT_TRUE(a.setEnabled(true));
    QFile f(desktopPath());
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_TRUE(QString::fromUtf8(f.readAll())
                    .contains("Exec=/home/u/Apps/Compact Phone.AppImage"));
    qunsetenv("APPIMAGE");
}
