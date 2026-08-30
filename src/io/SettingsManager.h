// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// SettingsManager.h: 用户偏好与会话（QSettings，XDG 路径）
// 文档：ARCHITECTURE.md §7.3；DISPLAYDESIGN.md §6.4
// 线程安全：主线程独占
// =============================================================================
#pragma once

#include <QByteArray>
#include <QFont>
#include <QSettings>
#include <QString>
#include <QStringList>

#include "core/models/Marker.h"

#include <vector>

namespace tat {

class SettingsManager {
public:
    SettingsManager();
    ~SettingsManager();

    // ---- 窗口/会话 ----
    QByteArray geometry() const;
    void       setGeometry(const QByteArray& geo);
    QByteArray dockState() const;
    void       setDockState(const QByteArray& state);

    // ---- 最近文件 ----
    QStringList recentFiles() const;
    void        addRecentFile(const QString& path);  // 去重 + 上限 10
    void        setRecentFiles(const QStringList& list);

    // ---- 编辑器 ----
    QFont       editorFont() const;
    void        setEditorFont(const QFont& f);
    int         tabWidth() const;
    void        setTabWidth(int w);

    // ---- 过滤/性能 ----
    int         debounceMs() const;
    void        setDebounceMs(int ms);
    int         parallelThreads() const;
    void        setParallelThreads(int n);  // 0 = 自动

    // ---- 语言 ----
    QString     language() const;
    void        setLanguage(const QString& lang);

    // ---- 会话恢复 ----
    bool        sessionRestore() const;
    void        setSessionRestore(bool on);
    std::vector<Marker> sessionMarkers() const;   // 格式 "line:markerId"
    void        setSessionMarkers(const std::vector<Marker>& markers);

    // 写入磁盘（退出时调用）
    void sync();

private:
    QSettings m_settings;
};

}  // namespace tat