// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// FilterResult.h: 每行最终结果快照（双缓冲的载荷）
// 文档：DISPLAYDESIGN.md §3.2.1 / §5.4
// 线程安全：states 在发布前写完，发布后只读；matchCounts 是 atomic（worker 汇入）
// =============================================================================
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "models/common.h"
#include "models/RowState.h"

namespace tat {

// 颜色不在 Buffer 中存放，按 ruleRef 查 RuleSet.rules（§3.3）。
struct FilterResult {
    std::vector<uint8_t> states;   // size == rowCount
    int                  ruleCount = 0;
    uint64_t             ruleFingerprint = 0;
    int                  generation = 0;
    // 按 ruleId 索引的命中计数（ruleId 由 FilterEngine 分配，删除后不复用）。
    // 多 worker 在 chunk 结束时 fetch_add 汇入（每 chunk ≤ 64 次原子操作，可忽略）。
    std::array<std::atomic<int>, kMaxRules> matchCounts;

    FilterResult() {
        for (auto& c : matchCounts) c.store(0);
    }
    FilterResult(const FilterResult&) = delete;
    FilterResult& operator=(const FilterResult&) = delete;
};

}  // namespace tat