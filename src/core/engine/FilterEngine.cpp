// =============================================================================
// FilterEngine.cpp: 过滤引擎实现
// 实现要点（DISPLAYDESIGN.md §3.2）：
//   - 状态决策表（§3.2.2）：白名单优先，黑名单其次
//   - 分片并发（§3.2.3）：chunkSize = max(1024, N/(W*8))
//   - 取消（§3.2.4）：每 8 KB 检查 token；进度每 10% 上报
// =============================================================================

#include "engine/FilterEngine.h"

#include <cctype>
#include <mutex>
#include <new>

#include "buffer/TextBuffer.h"

namespace tat {

namespace {

constexpr size_t kCancelCheckBytes = 8 * 1024;

// FNV-1a 64 位
uint64_t fnv1a64(const void* data, size_t len, uint64_t seed) {
    uint64_t h = seed;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

}  // namespace

bool FilterEngine::isWordChar(char c) noexcept {
    const unsigned char u = static_cast<unsigned char>(c);
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
           (u >= '0' && u <= '9') || u == '_';
}

size_t FilterEngine::findFrom(std::string_view line, std::string_view needle,
                              bool caseSensitive, size_t from) {
    if (needle.empty() || from > line.size()) return std::string_view::npos;
    if (caseSensitive) {
        return line.find(needle, from);
    }
    // 大小写不敏感：谓词式 std::search（逐字符 tolower，纯 ASCII 语义）
    const auto it = std::search(
        line.begin() + static_cast<ptrdiff_t>(from), line.end(),
        needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return it == line.end() ? std::string_view::npos
                            : static_cast<size_t>(it - line.begin());
}

size_t FilterEngine::findSubstring(std::string_view line, std::string_view needle,
                                   bool caseSensitive, bool wholeWord) {
    if (needle.empty()) return std::string_view::npos;  // 空模式不匹配（DontMatchEmptyString）
    size_t from = 0;
    for (;;) {
        const size_t pos = findFrom(line, needle, caseSensitive, from);
        if (pos == std::string_view::npos) return std::string_view::npos;
        if (!wholeWord) return pos;
        // 词边界检查（§3.5.3）：边界失败则从 pos+1 继续找下一处
        const bool leftOk =
            pos == 0 || !isWordChar(line[pos - 1]);
        const size_t after = pos + needle.size();
        const bool rightOk = after >= line.size() || !isWordChar(line[after]);
        if (leftOk && rightOk) return pos;
        from = pos + 1;
    }
}

namespace {

// 忽略大小写的逐字符比较（ASCII 语义，逐字符 tolower；
// 调用方需先按 caseSensitive 分流）。
bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

}  // namespace

bool FilterEngine::ruleHit(std::string_view line, const FilterRule& r,
                           size_t ruleIndex, const RuleSet& rules,
                           const MatchFn& match) {
    if (r.mode == MatchMode::Regex) {
        const std::string& compiled =
            ruleIndex < rules.compiled.size() ? rules.compiled[ruleIndex]
                                               : r.pattern;
        // 无注入回调时 Regex 规则视为不命中（由 buildRuleSet 保证 compile 可用，
        // 双保险：避免空函数调用）
        if (!match) return false;
        return match(line, compiled, r.pattern, r.caseSensitive);
    }
    // 非正则：按 FilterMatch 锚定方式判定（Filter 下拉的四类匹配）
    switch (r.matchType) {
    case FilterMatch::Exact:
        if (r.caseSensitive) return line == r.pattern;
        return iequals(line, r.pattern);
    case FilterMatch::StartsWith:
        return findSubstring(line, r.pattern, r.caseSensitive, false) == 0 &&
               !r.pattern.empty();
    case FilterMatch::EndsWith: {
        if (r.pattern.empty() || line.size() < r.pattern.size()) return false;
        if (r.caseSensitive)
            return line.compare(line.size() - r.pattern.size(),
                                std::string_view::npos, r.pattern) == 0;
        return iequals(line.substr(line.size() - r.pattern.size()),
                       r.pattern);
    }
    case FilterMatch::Contains:
    default:
        return findSubstring(line, r.pattern, r.caseSensitive, r.wholeWord) !=
               std::string_view::npos;
    }
}

Error FilterEngine::buildRuleSet(const std::vector<FilterRule>& rules,
                                 int generation, const CompileFn& compile,
                                 std::shared_ptr<RuleSet>* out) {
    if (!out) return {ErrCode::InvalidArgument, "buildRuleSet", "null out"};
    auto rs = std::make_shared<RuleSet>();
    rs->generation = generation;
    const size_t enabledCount = [&] {
        size_t n = 0;
        for (const auto& r : rules)
            if (r.isEnabled) ++n;
        return n;
    }();
    if (enabledCount > static_cast<size_t>(kMaxRules))
        return {ErrCode::InvalidArgument, "buildRuleSet",
                "enabled rules exceed kMaxRules(64)"};
    rs->rules.reserve(enabledCount);
    rs->compiled.reserve(enabledCount);

    for (const auto& r : rules) {
        if (!r.isEnabled) continue;
        if (r.mode == MatchMode::Regex) {
            if (!compile)
                return {ErrCode::RegexError, "buildRuleSet",
                        "Regex rule without CompileFn"};
            std::string compiled, err;
            const int rc = compile(r.pattern, r.caseSensitive, r.wholeWord,
                                   &compiled, &err);
            if (rc != 0)
                return {ErrCode::RegexError, "buildRuleSet", err};
            rs->compiled.push_back(compiled);
        } else {
            rs->compiled.emplace_back();  // Substring 规则无编译产物
        }
        if (r.action == FilterAction::Include) rs->hasInclude = true;
        else rs->hasExclude = true;
        rs->ruleIds.push_back(r.id);
        rs->rules.push_back(r);
    }
    rs->anyRule = !rs->rules.empty();
    rs->fingerprint = fingerprint(*rs);
    *out = std::move(rs);
    return Error::none();
}

uint64_t FilterEngine::fingerprint(const RuleSet& rules) {
    uint64_t h = 1469598103934665603ULL;  // FNV offset basis
    for (const auto& r : rules.rules) {
        h = fnv1a64(r.pattern.data(), r.pattern.size(), h);
        h ^= static_cast<uint64_t>(static_cast<uint8_t>(r.action)) << 8;
        h ^= static_cast<uint64_t>(static_cast<uint8_t>(r.mode)) << 16;
        h ^= (r.caseSensitive ? 1ull : 0ull) << 24;
        h ^= (r.wholeWord ? 1ull : 0ull) << 25;
        h ^= static_cast<uint64_t>(static_cast<uint8_t>(r.matchType)) << 26;
        h ^= static_cast<uint64_t>(r.id) << 32;
        h *= 0x100000001b3ULL;
    }
    return h;
}

FilterRule FilterEngine::ruleFromLine(const std::string& line,
                                      FilterAction action, MatchMode mode) {
    FilterRule r;
    r.action = action;
    r.mode = mode;
    // trim 首尾空白，限制长度（全行长日志不适合做过滤条件）
    size_t b = 0, e = line.size();
    while (b < e && (line[b] == ' ' || line[b] == '\t')) ++b;
    while (e > b && (line[e - 1] == ' ' || line[e - 1] == '\t' ||
                     line[e - 1] == '\r' || line[e - 1] == '\n'))
        --e;
    constexpr size_t kMaxPattern = 4096;
    if (e - b > kMaxPattern) e = b + kMaxPattern;
    r.pattern = line.substr(b, e - b);
    return r;
}

ResultState FilterEngine::classifyLine(std::string_view line,
                                       const RuleSet& rules,
                                       const MatchFn& match, int* ruleRef) {
    *ruleRef = 0;
    if (!rules.anyRule) return ResultState::Normal;
    const auto& rs = rules.rules;

    // 白名单优先（§3.2.2 场景 4/6）：首个命中的 Include → Highlighted
    for (size_t i = 0; i < rs.size(); ++i) {
        const auto& r = rs[i];
        if (r.action != FilterAction::Include) continue;
        if (ruleHit(line, r, i, rules, match)) {
            *ruleRef = static_cast<int>(i);
            return ResultState::Highlighted;
        }
    }
    // R-19：存在白名单但未命中 → Normal（回归原版 TAT 观感：Include 规则
    // 只负责给命中行着色，未命中行正常显示；此前判 Dimmed 会让添加一条
    // Include 规则后整屏未命中行变灰，被用户视为"行不可见"问题的延续）
    // 黑名单（场景 3/8）：首个命中的 Exclude → Hidden
    for (size_t i = 0; i < rs.size(); ++i) {
        const auto& r = rs[i];
        if (r.action != FilterAction::Exclude) continue;
        if (ruleHit(line, r, i, rules, match)) {
            *ruleRef = static_cast<int>(i);
            return ResultState::Hidden;
        }
    }
    return ResultState::Normal;  // 场景 1/2
}

Error FilterEngine::apply(const TextBuffer& buffer, const RuleSet& rules,
                          FilterResult* out, const TokenSnapshot* tok,
                          Progress* progress, const MatchFn& match) {
    if (!out) return {ErrCode::InvalidArgument, "apply", "null out"};
    const int N = buffer.rowCount();
    try {
        out->states.resize(static_cast<size_t>(N));
    } catch (const std::bad_alloc&) {
        return {ErrCode::OutOfMemory, "apply", "states allocation failed"};
    }
    out->ruleCount = static_cast<int>(rules.rules.size());
    out->ruleFingerprint = rules.fingerprint;
    out->generation = rules.generation;
    for (auto& c : out->matchCounts) c.store(0);

    size_t bytesSinceCheck = 0;
    for (int i = 0; i < N; ++i) {
        std::string_view sv = buffer.textAt(i);
        int ref = 0;
        const ResultState st = classifyLine(sv, rules, match, &ref);
        out->states[static_cast<size_t>(i)] = RowState::make(st, ref);
        // 命中计数：Highlighted（Include 命中）与 Hidden（Exclude 命中）
        // 都由规则决定状态，均应计入该规则的 matchCount。
        if ((st == ResultState::Highlighted || st == ResultState::Hidden) &&
            ref >= 0 && ref < static_cast<int>(rules.ruleIds.size())) {
            const int id = rules.ruleIds[static_cast<size_t>(ref)];
            if (id >= 0 && id < kMaxRules)
                out->matchCounts[static_cast<size_t>(id)].fetch_add(
                    1, std::memory_order_relaxed);
        }
        bytesSinceCheck += sv.size() + 1;
        if (bytesSinceCheck >= kCancelCheckBytes) {
            bytesSinceCheck = 0;
            if (tok && !tok->valid())
                return {ErrCode::Cancelled, "apply", "cancelled"};
        }
    }
    if (progress) {
        progress->current = static_cast<size_t>(N);
        progress->total = static_cast<size_t>(N);
        progress->percent = 100.0;
    }
    return Error::none();
}

Error FilterEngine::applyParallel(const TextBuffer& buffer, const RuleSet& rules,
                                  int workerCount, FilterResult* out,
                                  const TokenSnapshot* tok, Progress* progress,
                                  const MatchFn& match, ThreadPool* pool) {
    if (!out) return {ErrCode::InvalidArgument, "applyParallel", "null out"};
    const int N = buffer.rowCount();
    try {
        out->states.resize(static_cast<size_t>(N));
    } catch (const std::bad_alloc&) {
        return {ErrCode::OutOfMemory, "applyParallel", "states allocation failed"};
    }
    out->ruleCount = static_cast<int>(rules.rules.size());
    out->ruleFingerprint = rules.fingerprint;
    out->generation = rules.generation;
    for (auto& c : out->matchCounts) c.store(0);
    if (N == 0) {
        if (progress) progress->percent = 100.0;
        return Error::none();
    }

    const int W = workerCount > 0 ? workerCount : ThreadPool::idealThreadCount();
    const int chunkSize = std::max(1024, N / (W * 8 > 0 ? W * 8 : 1));
    const int jobs = (N + chunkSize - 1) / chunkSize;

    SimpleThreadPool local{W};
    ThreadPool* tp = pool ? pool : &local;

    std::atomic<size_t> doneLines{0};
    std::atomic<int> lastBucket{0};
    std::mutex progressMtx;

    for (int j = 0; j < jobs; ++j) {
        tp->submit([&, j] {
            const int start = j * chunkSize;
            const int end = std::min(start + chunkSize, N);
            if (tok && !tok->valid()) return;  // 取消：静默放弃，调用方判定
            int localCounts[kMaxRules] = {0};
            size_t bytes = 0;
            for (int i = start; i < end; ++i) {
                std::string_view sv = buffer.textAt(i);
                int ref = 0;
                const ResultState st = classifyLine(sv, rules, match, &ref);
                out->states[static_cast<size_t>(i)] = RowState::make(st, ref);
                if ((st == ResultState::Highlighted || st == ResultState::Hidden) &&
                    ref >= 0 && ref < static_cast<int>(rules.ruleIds.size())) {
                    const int id = rules.ruleIds[static_cast<size_t>(ref)];
                    if (id >= 0 && id < kMaxRules) ++localCounts[id];
                }
                bytes += sv.size() + 1;
                if (bytes >= kCancelCheckBytes) {
                    bytes = 0;
                    if (tok && !tok->valid()) return;
                }
            }
            for (int id = 0; id < kMaxRules; ++id) {
                if (localCounts[id] != 0)
                    out->matchCounts[static_cast<size_t>(id)].fetch_add(
                        localCounts[id], std::memory_order_relaxed);
            }
            if (progress) {
                const size_t done =
                    doneLines.fetch_add(static_cast<size_t>(end - start)) +
                    static_cast<size_t>(end - start);
                const int bucket =
                    static_cast<int>(done * 10 / static_cast<size_t>(N));
                int last = lastBucket.load(std::memory_order_relaxed);
                if (bucket > last &&
                    lastBucket.compare_exchange_strong(last, bucket)) {
                    std::lock_guard<std::mutex> lk(progressMtx);
                    progress->current = done;
                    progress->total = static_cast<size_t>(N);
                    progress->percent = 100.0 * done / N;
                }
            }
        });
    }

    (void)tp->waitForDone(-1);
    if (tok && !tok->valid() &&
        doneLines.load(std::memory_order_relaxed) < static_cast<size_t>(N))
        return {ErrCode::Cancelled, "applyParallel", "cancelled"};
    if (progress) {
        progress->current = static_cast<size_t>(N);
        progress->total = static_cast<size_t>(N);
        progress->percent = 100.0;
    }
    return Error::none();
}

}  // namespace tat