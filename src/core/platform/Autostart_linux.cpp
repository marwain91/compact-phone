#include "Autostart_linux.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtGlobal>

namespace compactphone::platform {

QString LinuxAutostart::desktopFilePath()
{
    const QString cfg =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return cfg + QStringLiteral("/autostart/compactphone.desktop");
}

QString LinuxAutostart::execLine()
{
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty()) return appImage;
    return QCoreApplication::applicationFilePath();
}

bool LinuxAutostart::isSupported() const
{
    // No desktop session → nothing to autostart into.
    return !qEnvironmentVariableIsEmpty("XDG_CURRENT_DESKTOP")
           || !qEnvironmentVariableIsEmpty("DESKTOP_SESSION");
}

bool LinuxAutostart::isEnabled() const
{
    return QFile::exists(desktopFilePath());
}

bool LinuxAutostart::setEnabled(bool on)
{
    const QString path = desktopFilePath();
    if (!on) {
        if (!QFile::exists(path)) return true;
        return QFile::remove(path);
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QString body =
        QStringLiteral("[Desktop Entry]\n"
                       "Type=Application\n"
                       "Name=Compact Phone\n"
                       "Exec=%1\n"
                       "X-GNOME-Autostart-enabled=true\n")
            .arg(execLine());
    return f.write(body.toUtf8()) > 0;
}

} // namespace compactphone::platform
