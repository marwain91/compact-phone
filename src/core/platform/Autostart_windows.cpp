#include "Autostart_windows.h"

#ifdef _WIN32

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace compactphone::platform {

namespace {
constexpr auto kRunKey =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto kValueName = "Compact Phone";
}

bool WindowsAutostart::isEnabled() const
{
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    return run.contains(QString::fromLatin1(kValueName));
}

bool WindowsAutostart::setEnabled(bool on)
{
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (on) {
        // Quote the path — install dir "Program Files\Compact Phone" has a space.
        const QString cmd = QLatin1Char('"')
            + QDir::toNativeSeparators(QCoreApplication::applicationFilePath())
            + QLatin1Char('"');
        run.setValue(QString::fromLatin1(kValueName), cmd);
    } else {
        run.remove(QString::fromLatin1(kValueName));
    }
    run.sync();
    return run.status() == QSettings::NoError;
}

} // namespace compactphone::platform

#endif // _WIN32
