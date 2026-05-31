#include "BootConfig.h"
#include "BootConfig_internal.h"

#include <QFile>
#include <QTextStream>
#include <QtGlobal>

namespace compactphone::bootconfig {

QString resolvePassword(const QString &spec)
{
    if (spec.isEmpty()) return {};

    if (spec.startsWith(QStringLiteral("@env:"))) {
        const QString var = spec.mid(5);
        const QByteArray val = qgetenv(var.toUtf8().constData());
        if (val.isNull()) {
            detail::warn(QStringLiteral("env var %1 not set; password empty").arg(var));
            return {};
        }
        return QString::fromLocal8Bit(val);
    }

    if (spec.startsWith(QStringLiteral("@file:"))) {
        const QString path = spec.mid(6);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            detail::warn(QStringLiteral("cannot read password file %1: %2")
                             .arg(path, f.errorString()));
            return {};
        }
        return QString::fromUtf8(f.readLine()).trimmed();
    }

    if (spec == QStringLiteral("@stdin")) {
        QTextStream in(stdin, QIODevice::ReadOnly);
        return in.readLine();
    }

    return spec; // literal
}

} // namespace compactphone::bootconfig
