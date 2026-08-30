// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// CommandLineParser.cpp
// =============================================================================

#include "controller/CommandLineParser.h"

#include <QCommandLineParser>

namespace tat {

CommandLineOptions CommandLineParser::parse(const QStringList& args, bool* ok) {
    CommandLineOptions opts;
    QCommandLineParser p;
    p.setApplicationDescription(
        "TextAnalyst-Qt: high-performance text filtering & marking tool");
    p.addHelpOption();
    p.addVersionOption();

    const QCommandLineOption fileOpt({"f", "file"}, "Open file", "path");
    const QCommandLineOption lineOpt("line", "Jump to line n", "n");
    const QCommandLineOption grepOpt("grep", "Filter by pattern", "pattern");
    const QCommandLineOption loadOpt("load", "Load filter rules from .tat", "file");
    const QCommandLineOption exportOpt("export", "Export matched lines to file", "file");
    const QCommandLineOption noRestoreOpt("no-restore", "Skip last-session restore");
    const QCommandLineOption verboseOpt("verbose", "Verbose logging");
    const QCommandLineOption compactOpt("compact-memory",
                                        "Enable MADV_DONTNEED (experimental)");
    p.addOption(fileOpt);
    p.addOption(lineOpt);
    p.addOption(grepOpt);
    p.addOption(loadOpt);
    p.addOption(exportOpt);
    p.addOption(noRestoreOpt);
    p.addOption(verboseOpt);
    p.addOption(compactOpt);
    // 与文档保持一致的长选项别名：--file 之外的便捷写法
    p.addPositionalArgument("file", "Open file", "[file]");

    p.process(args);  // --help/--version 由 Qt 处理并退出（parse 不触发）
    *ok = true;
    opts.file = p.isSet(fileOpt) ? p.value(fileOpt)
                                 : p.positionalArguments().value(0);
    opts.line = p.value(lineOpt).toInt();
    opts.grep = p.value(grepOpt);
    opts.load = p.value(loadOpt);
    opts.exportPath = p.value(exportOpt);
    opts.noRestore = p.isSet(noRestoreOpt);
    opts.verbose = p.isSet(verboseOpt);
    opts.compactMemory = p.isSet(compactOpt);
    return opts;
}

QString CommandLineParser::helpText() {
    QStringList argv{"textanalyst-qt", "--help"};
    QCommandLineParser p;
    p.setApplicationDescription(
        "TextAnalyst-Qt: high-performance text filtering & marking tool");
    p.addHelpOption();
    p.addVersionOption();
    p.addOption({"f", "file", "Open file", "path"});
    p.addOption({"line", "Jump to line n", "n"});
    p.addOption({"grep", "Filter by pattern", "pattern"});
    p.addOption({"load", "Load filter rules from .tat", "file"});
    p.addOption({"export", "Export matched lines to file", "file"});
    p.addOption({"no-restore", "Skip last-session restore"});
    p.addOption({"verbose", "Verbose logging"});
    p.addOption({"compact-memory", "Enable MADV_DONTNEED (experimental)"});
    p.addPositionalArgument("file", "Open file", "[file]");
    p.process(argv);
    return p.helpText();
}

}  // namespace tat