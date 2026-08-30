// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// MarkerManager.h: 8 种行标记管理
// 文档：DISPLAYDESIGN.md §3.4（R 修订：有序 vector 替代 unordered_set）
// 线程安全：主线程独占（MUST）；操作 O(log n)，1 亿行标记跳转 < 1 ms
// =============================================================================
#pragma once

#include <array>
#include <optional>
#include <vector>

#include "models/Marker.h"
#include "models/common.h"

namespace tat {

class MarkerManager {
public:
    explicit MarkerManager(int /*capacity*/ = 0) {}  // 预留接口，v1.0 不预分配

    // 切换：存在则移除，否则添加。返回 true = 添加，false = 移除/参数非法。
    bool toggle(int line, int markerId);

    bool add(int line, int markerId);
    bool remove(int line, int markerId);
    bool has(int line, int markerId) const noexcept;
    int  markerOf(int line) const noexcept;  // 返回 1..8 或 0（无标记）

    // 循环跳转：forward=true 找第一个 > line 的标记；false 找第一个 < line。
    // 到头后环绕（末 → 首 / 首 → 末）。无标记返回 nullopt。
    std::optional<int> next(int line, int markerId, bool forward = true) const;

    int  count(int markerId) const noexcept;
    int  totalCount() const noexcept;
    std::array<std::vector<int>, kMarkerCount> dump() const;

    // 会话序列化（.tat 不含标记，标记是运行时状态，存 QSettings）
    void save(std::vector<Marker>* out) const;
    void load(const std::vector<Marker>& markers);

    void clear() noexcept;

private:
    std::array<std::vector<int>, kMarkerCount> m_markers{};
};

}  // namespace tat