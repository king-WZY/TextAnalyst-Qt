// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// SettingsManager.cpp: QSettings 封装
// 路径：QSettings 走 XDG（~/.config/textanalyst-qt/textanalyst-qt.conf），
// 通过 QStandardPaths::AppConfigLocation 由 Qt 自动落位（§0.3 硬约束 5）。
// =============================================================================

#include "io/SettingsManager.h"

#include <QStandardPaths>

namespace tat {

SettingsManager::SettingsManager()
    : m_settings(QSettings::IniFormat, QSettings::UserScope,
                 QStringLiteral("textanalyst-qt"), QStringLiteral("textanalyst-qt")) {
    // Qt 6 下 UserScope + 组织/应用名自动解析为 XDG 配置目录
    (void)QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

SettingsManager::~SettingsManager() { sync(); }

QByteArray SettingsManager::geometry() const {
    return m_settings.value("geometry/main").toByteArray();
}
void SettingsManager::setGeometry(const QByteArray& geo) {
    m_settings.setValue("geometry/main", geo);
}
QByteArray SettingsManager::dockState() const {
    return m_settings.value("state/main").toByteArray();
}
void SettingsManager::setDockState(const QByteArray& s) {
    m_settings.setValue("state/main", s);
}

QStringList SettingsManager::recentFiles() const {
    return m_settings.value("recentFiles").toStringList();
}
void SettingsManager::addRecentFile(const QString& path) {
    QStringList list = recentFiles();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 10) list.removeLast();
    setRecentFiles(list);
}
void SettingsManager::setRecentFiles(const QStringList& list) {
    m_settings.setValue("recentFiles", list);
}

QFont SettingsManager::editorFont() const {
    return m_settings.value("font/editor", QFont(QStringLiteral("monospace"), 10))
        .value<QFont>();
}
void SettingsManager::setEditorFont(const QFont& f) {
    m_settings.setValue("font/editor", f);
}
int SettingsManager::tabWidth() const {
    return m_settings.value("tab/width", 4).toInt();
}
void SettingsManager::setTabWidth(int w) { m_settings.setValue("tab/width", w); }

int SettingsManager::debounceMs() const {
    return m_settings.value("filter/debounceMs", 200).toInt();
}
void SettingsManager::setDebounceMs(int ms) {
    m_settings.setValue("filter/debounceMs", ms);
}
int SettingsManager::parallelThreads() const {
    return m_settings.value("filter/parallelThreads", 0).toInt();
}
void SettingsManager::setParallelThreads(int n) {
    m_settings.setValue("filter/parallelThreads", n);
}

QString SettingsManager::language() const {
    return m_settings.value("language", "zh_CN").toString();
}
void SettingsManager::setLanguage(const QString& lang) {
    m_settings.setValue("language", lang);
}

bool SettingsManager::sessionRestore() const {
    return m_settings.value("session/restore", true).toBool();
}
void SettingsManager::setSessionRestore(bool on) {
    m_settings.setValue("session/restore", on);
}

std::vector<Marker> SettingsManager::sessionMarkers() const {
    std::vector<Marker> out;
    const QStringList list =
        m_settings.value("session/markers").toStringList();
    out.reserve(static_cast<size_t>(list.size()));
    for (const QString& s : list) {
        const auto parts = s.split(':');
        if (parts.size() == 2) {
            const int line = parts[0].toInt();
            const int id = parts[1].toInt();
            if (line > 0 && id >= 1 && id <= kMarkerCount)
                out.push_back({line, id});
        }
    }
    return out;
}
void SettingsManager::setSessionMarkers(const std::vector<Marker>& markers) {
    QStringList list;
    // 容量上限 10000（DISPLAYDESIGN §3.4.5）
    size_t n = 0;
    for (const auto& m : markers) {
        if (++n > 10000) break;
        list << QString("%1:%2").arg(m.line).arg(m.markerId);
    }
    m_settings.setValue("session/markers", list);
}

void SettingsManager::sync() { m_settings.sync(); }

}  // namespace tat