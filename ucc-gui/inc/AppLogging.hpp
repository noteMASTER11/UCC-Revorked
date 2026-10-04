// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
namespace ucc::AppLogging {
void initialize();
void shutdown();
bool isEnabled();
bool setEnabled(bool enabled);
QString directory();
QString currentFile();
QString lastError();
inline constexpr qint64 maxFileBytes = 5 * 1024 * 1024;
inline constexpr int maxFiles = 10;
}
