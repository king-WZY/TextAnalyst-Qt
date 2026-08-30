// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// LogRotator.h: 运行日志轮转（大小限制 + 保留历史档）
// 文档：DISPLAYDESIGN.md §11.2（R-12 修订：4 MiB × 4 档，最多 20 MiB）
// 线程安全：append [A] 任意线程（内部互斥）；单进程单实例
// =============================================================================
#pragma once

#include <QFile>
#include <QMutex>
#include <QString>

namespace tat {

class LogRotator {
public:
    // maxSize = 单档上限（字节）；maxBackups = 保留的历史档数量（不含当前档）
    LogRotator(const QString& filePath, qint64 maxSize = 4 * 1024 * 1024,
               int maxBackups = 4);
    ~LogRotator();

    // 追加一行（调用方组织内容，本类只负责尺寸控制与轮转）。
    // 当前档达到 maxSize 时：delete .N → .i→.i+1 → current→.1 → 重开 current。
    void append(const QString& line);

    QString path() const { return m_path; }
    qint64  maxSize() const { return m_maxSize; }
    int     maxBackups() const { return m_maxBackups; }

private:
    void rotateLocked();

    QString m_path;
    qint64  m_maxSize;
    int     m_maxBackups;
    QFile   m_file;
    QMutex  m_mtx;
};

}  // namespace tat