// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// MarkerManager.cpp: 有序 vector + 二分实现
// 复杂度：add/remove/has/next 均 O(log n)；100 万标记内存仅 ~4 MB（§3.4.4）
// =============================================================================

#include "engine/MarkerManager.h"

#include <algorithm>

namespace tat {

bool MarkerManager::add(int line, int markerId) {
    if (markerId < 1 || markerId > kMarkerCount || line < 1) return false;
    auto& v = m_markers[static_cast<size_t>(markerId - 1)];
    const auto it = std::lower_bound(v.begin(), v.end(), line);
    if (it != v.end() && *it == line) return false;  // 已存在
    v.insert(it, line);
    return true;
}

bool MarkerManager::remove(int line, int markerId) {
    if (markerId < 1 || markerId > kMarkerCount || line < 1) return false;
    auto& v = m_markers[static_cast<size_t>(markerId - 1)];
    const auto it = std::lower_bound(v.begin(), v.end(), line);
    if (it == v.end() || *it != line) return false;
    v.erase(it);
    return true;
}

bool MarkerManager::toggle(int line, int markerId) {
    if (has(line, markerId)) return !remove(line, markerId);
    return add(line, markerId);
}

bool MarkerManager::has(int line, int markerId) const noexcept {
    if (markerId < 1 || markerId > kMarkerCount || line < 1) return false;
    const auto& v = m_markers[static_cast<size_t>(markerId - 1)];
    return std::binary_search(v.begin(), v.end(), line);
}

int MarkerManager::markerOf(int line) const noexcept {
    for (int i = 0; i < kMarkerCount; ++i) {
        if (std::binary_search(m_markers[static_cast<size_t>(i)].begin(),
                               m_markers[static_cast<size_t>(i)].end(), line))
            return i + 1;
    }
    return 0;
}

std::optional<int> MarkerManager::next(int line, int markerId,
                                       bool forward) const {
    if (markerId < 1 || markerId > kMarkerCount) return std::nullopt;
    const auto& v = m_markers[static_cast<size_t>(markerId - 1)];
    if (v.empty()) return std::nullopt;
    if (forward) {
        auto it = std::upper_bound(v.begin(), v.end(), line);
        if (it == v.end()) it = v.begin();  // 环绕
        return *it;
    }
    auto it = std::lower_bound(v.begin(), v.end(), line);
    if (it == v.begin()) it = v.end();  // 环绕
    --it;
    return *it;
}

int MarkerManager::count(int markerId) const noexcept {
    if (markerId < 1 || markerId > kMarkerCount) return 0;
    return static_cast<int>(
        m_markers[static_cast<size_t>(markerId - 1)].size());
}

int MarkerManager::totalCount() const noexcept {
    int n = 0;
    for (const auto& v : m_markers) n += static_cast<int>(v.size());
    return n;
}

std::array<std::vector<int>, kMarkerCount> MarkerManager::dump() const {
    return m_markers;
}

void MarkerManager::save(std::vector<Marker>* out) const {
    if (!out) return;
    out->clear();
    for (int id = 1; id <= kMarkerCount; ++id) {
        const auto& v = m_markers[static_cast<size_t>(id - 1)];
        out->reserve(out->size() + v.size());
        for (int line : v) out->push_back({line, id});
    }
}

void MarkerManager::load(const std::vector<Marker>& markers) {
    clear();
    for (const auto& m : markers) add(m.line, m.markerId);
}

void MarkerManager::clear() noexcept {
    for (auto& v : m_markers) v.clear();
}

}  // namespace tat