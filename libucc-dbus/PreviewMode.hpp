#pragma once
#include <QString>

namespace ucc {
inline constexpr bool readOnlyPreview =
#ifdef UCC_READ_ONLY_PREVIEW
  true;
#else
  false;
#endif

// Fail closed for commands, including saving/deleting profiles and cooler discovery.
inline bool mayDispatchUccdMethod(const QString &method) {
  return !readOnlyPreview || method.startsWith(QStringLiteral("Get")) ||
         method.startsWith(QStringLiteral("Is")) || method == QStringLiteral("ODMPowerLimitsJSON");
}
}
