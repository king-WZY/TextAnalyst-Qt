// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// Searcher.h: 搜索（独立于过滤器，字节偏移语义）
// 文档：DISPLAYDESIGN.md §3.5（快速路径 §3.5.3 / 字节偏移 §3.5.4 /
//                         增量搜索 §3.5.5）
// 线程安全：search [W]；searchViewport [M]
// =============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "engine/FilterEngine.h"  // CompileFn / MatchFn
#include "models/Error.h"
#include "models/Task.h"

namespace tat {

class TextBuffer;

struct SearchOptions {
    std::string pattern;
    bool        useRegex      = false;
    bool        caseSensitive = false;
    bool        wholeWord     = false;
    int         startLine     = 1;  // 1-based，从该行开始
};

struct SearchHit {
    int    line   = 0;  // 1-based
    size_t offset = 0;  // 行内字节偏移（§3.5.4）
    // Regex 路径的精确偏移由注入的 MatchFn 提供（v1.0 为 0，见头文件注记）。
    size_t length = 0;
};

struct SearchResult {
    std::vector<SearchHit> hits;
    int     truncated = 0;
    bool    cancelled = false;
    double  elapsedMs = 0;
};

class Searcher {
public:
    // CompileFn / MatchFn 为命名空间级别名（定义于 FilterEngine.h）
    // 全量搜索（后台线程）。compile 仅 Regex 模式需要；命中上限 maxHits。
    // 取消：每 10000 行检查 token。
    static Error search(const TextBuffer& buffer, const SearchOptions& opt,
                        const CompileFn& compile, const MatchFn& match,
                        SearchResult* out, const TokenSnapshot* tok,
                        Progress* progress, int maxHits = 10000);

    // 增量搜索（搜索框输入时，限当前视口 ±radius 行，主线程调用）。
    static Error searchViewport(const TextBuffer& buffer, const SearchOptions& opt,
                                int anchorLine, int radius, const MatchFn& match,
                                std::vector<SearchHit>* out);
};

}  // namespace tat