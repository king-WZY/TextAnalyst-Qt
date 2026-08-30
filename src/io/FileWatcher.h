// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// FileWatcher.h: inotify 单文件监听（POSIX inotify + Qt 事件循环集成）
// 文档：ARCHITECTURE.md §3.4（v1.0.1）；DISPLAYDESIGN.md §2.6
// 线程安全：主线程独占（QSocketNotifier 绑主线程）
// 已知限制：NFS 等网络文件系统上事件可能延迟（§2.6 注）
// =============================================================================
#pragma once

#include <QObject>
#include <QSocketNotifier>
#include <QString>

namespace tat {

class FileWatcher : public QObject {
    Q_OBJECT
public:
    explicit FileWatcher(QObject* parent = nullptr);
    ~FileWatcher() override;

    // 监听单文件（再次调用先释放旧监听）。失败返回 false。
    bool watch(const QString& path);
    void unwatch();
    bool isWatching() const noexcept { return m_wd >= 0; }

signals:
    // 主线程投递：IN_MODIFY/IN_MOVED_*/IN_ATTRIB 任一发生
    void externalChanged(const QString& path);
    void watchFailed(const QString& path);

private slots:
    void onReadable();

private:
    int     m_fd = -1;
    int     m_wd = -1;
    QString m_path;
    QSocketNotifier* m_notifier = nullptr;
};

}  // namespace tat