// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// Searcher.cpp: 搜索实现
// 快速路径（Substring）：FilterEngine::findSubstring（含词边界）
// Regex 路径：注入的 CompileFn/MatchFn（v1.0 命中 offset=0，见文档 §3.5.4）
// =============================================================================

#include "engine/Searcher.h"

#include <chrono>

#include "buffer/TextBuffer.h"
#include "engine/FilterEngine.h"

namespace tat {

namespace {
using Clock = std::chrono::steady_clock;
}

Error Searcher::search(const TextBuffer& buffer, const SearchOptions& opt,
                       const CompileFn& compile, const MatchFn& match,
                       SearchResult* out, const TokenSnapshot* tok,
                       Progress* progress, int maxHits) {
    if (!out) return {ErrCode::InvalidArgument, "search", "null out"};
    const auto t0 = Clock::now();
    out->hits.clear();
    out->truncated = 0;
    out->cancelled = false;

    std::string compiled;  // Regex 模式下的编译产物
    if (opt.useRegex) {
        if (opt.pattern.empty())
            return {ErrCode::InvalidArgument, "search", "empty regex pattern"};
        std::string err;
        if (!compile || compile(opt.pattern, opt.caseSensitive, opt.wholeWord,
                                &compiled, &err) != 0)
            return {ErrCode::RegexError, "search", err};
    }

    const int total = buffer.rowCount();
    const int start = std::max(1, opt.startLine);  // 1-based
    const size_t reserveN = static_cast<size_t>(
        std::min(maxHits, std::max(0, total - start + 1)));
    out->hits.reserve(reserveN);

    for (int line = start; line <= total; ++line) {
        if (out->hits.size() >= static_cast<size_t>(maxHits)) {
            out->truncated = static_cast<int>(out->hits.size()) - maxHits +
                             (line <= total ? 1 : 0);
            break;
        }
        if (line % 10000 == 0 && tok && !tok->valid()) {
            out->cancelled = true;
            break;
        }
        std::string_view sv = buffer.textAt(line - 1);  // 内部 0-based
        if (opt.useRegex) {
            if (match && match(sv, compiled, opt.pattern, opt.caseSensitive))
                // v1.0：Regex 命中行整行高亮（offset=0），精确偏移 v1.1 提供
                out->hits.push_back(
                    {line, 0, opt.pattern.empty() ? sv.size() : opt.pattern.size()});
        } else {
            const size_t pos = FilterEngine::findSubstring(
                sv, opt.pattern, opt.caseSensitive, opt.wholeWord);
            if (pos != std::string_view::npos)
                out->hits.push_back({line, pos, opt.pattern.size()});
        }
        if (progress) {
            progress->current = static_cast<size_t>(line);
            progress->total = static_cast<size_t>(total);
            progress->percent = total > 0 ? 100.0 * (line - start + 1) / total
                                          : 100.0;
        }
    }
    out->elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - t0)
                         .count();
    return Error::none();
}

Error Searcher::searchViewport(const TextBuffer& buffer, const SearchOptions& opt,
                               int anchorLine, int radius, const MatchFn& match,
                               std::vector<SearchHit>* out) {
    if (!out) return {ErrCode::InvalidArgument, "searchViewport", "null out"};
    out->clear();
    (void)match;  // v1.0 视口搜索仅 Substring 路径，不调用 match
    if (opt.useRegex) {
        // 视口搜索同样需要编译：调用方必须已编译（UI 层缓存）。
        // v1.0 简化：视口搜索仅支持 Substring 快速路径。
        return {ErrCode::UnsupportedFormat, "searchViewport",
                "regex viewport search not available (v1.0)"};
    }
    const int total = buffer.rowCount();
    if (total == 0) return Error::none();
    // anchor 先夹到有效范围，避免 lo > hi 导致视图外锚点搜不到（例如从超界跳转回）
    const int anchor = std::max(1, std::min(anchorLine, total));
    const int lo = std::max(1, anchor - radius);
    const int hi = std::min(total, anchor + radius);
    for (int line = lo; line <= hi; ++line) {
        const std::string_view sv = buffer.textAt(line - 1);
        const size_t pos = FilterEngine::findSubstring(
            sv, opt.pattern, opt.caseSensitive, opt.wholeWord);
        if (pos != std::string_view::npos)
            out->push_back({line, pos, opt.pattern.size()});
    }
    return Error::none();
}

}  // namespace tat