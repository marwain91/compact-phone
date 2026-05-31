#pragma once

// Internal helpers shared across the BootConfig translation units
// (BootConfig_parser.cpp, BootConfig_file.cpp, BootConfig_secrets.cpp).
// Not part of the public BootConfig API.

#include "BootConfig.h"

#include <QString>
#include <QTextStream>

namespace compactphone::bootconfig::detail {

// All BootConfig diagnostics go to stderr with a common prefix.
inline void warn(const QString &msg)
{
    QTextStream(stderr) << "compactphone: " << msg << "\n";
}

// Merge `incoming` into `base`: accounts append; set scalars in `incoming`
// win (so CLI overrides file overrides earlier files). Defined in
// BootConfig_file.cpp.
void mergeInto(BootConfig &base, const BootConfig &incoming);

} // namespace compactphone::bootconfig::detail
