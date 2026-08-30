// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// LogRotator.cpp: 轮转实现
// rotate 语义：
//   1. 删除最旧备份 <path>.N
//   2. <path>.i → <path>.(i+1)（i 从 N-1 到 1）
//   3. 当前 <path> → <path>.1
//   4. 重新打开 <path>（截断）
// =============================================================================

#include "io/LogRotator.h"

#include <QFileInfo>
#include <QTextStream>

namespace tat {

LogRotator::LogRotator(const QString& filePath, qint64 maxSize, int maxBackups)
    : m_path(filePath), m_maxSize(maxSize), m_maxBackups(maxBackups) {
    m_file.setFileName(m_path);
    // Append：轮转作为"换档"实现，档内持续追加
    if (m_file.open(QIODevice::WriteOnly | QIODevice::Append |
                    QIODevice::Text)) {
        // 打开成功即可；失败（目录不可写等）静默降级为不落盘
    } else {
        m_file.setFileName(QString());  // 失效
    }
}

LogRotator::~LogRotator() {
    if (m_file.isOpen()) m_file.close();
}

void LogRotator::append(const QString& line) {
    QMutexLocker locker(&m_mtx);
    if (!m_file.isOpen()) return;

    // 尺寸上限：预告式轮转（写入前判断，避免超限后才换档）
    if (m_file.size() > 0 &&
        m_file.size() + line.size() + 1 > m_maxSize) {
        rotateLocked();
    }
    if (m_file.isOpen()) {
        m_file.write(line.toUtf8());
        m_file.write("\n", 1);
        m_file.flush();
    }
}

void LogRotator::rotateLocked() {
    if (m_file.isOpen()) m_file.close();

    // 1. 删除最旧档
    QFile::remove(m_path + QStringLiteral(".%1").arg(m_maxBackups));
    // 2. 逐级后移
    for (int i = m_maxBackups - 1; i >= 1; --i) {
        const QString from = m_path + QStringLiteral(".%1").arg(i);
        const QString to = m_path + QStringLiteral(".%1").arg(i + 1);
        QFile::remove(to);
        QFile::rename(from, to);
    }
    // 3. 当前 → .1
    QFile::rename(m_path, m_path + QStringLiteral(".1"));

    // 4. 重开当前档
    m_file.setFileName(m_path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate |
                     QIODevice::Text)) {
        m_file.setFileName(QString());
    }
}

}  // namespace tat