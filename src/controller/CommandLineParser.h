// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// CommandLineParser.h: 命令行解析（QCommandLineParser，GNU 风格长选项）
// 文档：ARCHITECTURE.md §6.2；DISPLAYDESIGN.md §6.5
// =============================================================================
#pragma once

#include <QString>
#include <QStringList>

namespace tat {

struct CommandLineOptions {
    QString file;
    int     line = 0;
    QString grep;
    QString load;
    QString exportPath;
    bool    noRestore     = false;
    bool    verbose       = false;
    bool    compactMemory = false;
};

class CommandLineParser {
public:
    static CommandLineOptions parse(const QStringList& args, bool* ok);
    static QString helpText();
};

}  // namespace tat