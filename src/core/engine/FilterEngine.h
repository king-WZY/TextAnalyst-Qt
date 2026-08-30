// =============================================================================
// FilterEngine.h: 过滤规则执行引擎（数据驱动，无状态静态方法）
// 文档：DISPLAYDESIGN.md §3.2（状态决策表 §3.2.2 / 分片并发 §3.2.3 /
//                         取消协议 §3.2.4 / 超时 §3.2.6）
// 线程安全：apply/applyParallel [W]；classifyLine [A]；buildRuleSet [A]
// 性能：applyParallel O(N/W)；1 亿行 / 8 线程 < 3 s（含正则，见 §9）
// 约束：零 Qt 依赖；正则经注入的 CompileFn/MatchFn 回调（§4.1）
// =============================================================================
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/FilterResult.h"
#include "engine/ThreadPool.h"
#include "models/Error.h"
#include "models/FilterRule.h"
#include "models/RowState.h"
#include "models/Task.h"
#include "models/common.h"

namespace tat {

class TextBuffer;

// 正则编译回调：BLL 不依赖 Qt，编译逻辑由 UI 层注入（DISPLAYDESIGN §4.1）。
// 返回 0 成功（*outCompiled 存放不透明编译产物）；非 0 失败（*err 描述）。
using CompileFn = std::function<int(const std::string& pattern, bool caseSensitive,
                                    bool wholeWord, std::string* outCompiled,
                                    std::string* err)>;

// 匹配回调：Regex 规则的行级匹配（Substring 规则走引擎内置快速路径）。
// line 为行内容（UTF-8 字节）；compiled 为 buildRuleSet 时编译的产物；
// 返回 true 表示命中。
using MatchFn = std::function<bool(std::string_view line, const std::string& compiled,
                                   const std::string& pattern, bool caseSensitive)>;

class FilterEngine {
public:
    // 构造不可变规则快照：过滤 isEnabled、编译 Regex 规则（compile 为空且
    // 存在 Regex 规则时返回 RegexError）、计算指纹与 hasInclude/hasExclude。
    static Error buildRuleSet(const std::vector<FilterRule>& rules, int generation,
                              const CompileFn& compile,
                              std::shared_ptr<RuleSet>* out);

    // 串行应用（单测与 --export 命令行用）。
    static Error apply(const TextBuffer& buffer, const RuleSet& rules,
                       FilterResult* out, const TokenSnapshot* tok,
                       Progress* progress, const MatchFn& match);

    // 分片并发应用（生产路径）。workerCount==0 用理想线程数；
    // pool==nullptr 时内部使用 SimpleThreadPool（无 Qt 环境可运行）。
    // 取消：token 失效后返回 ErrCode::Cancelled，已写入的 states 部分保留。
    static Error applyParallel(const TextBuffer& buffer, const RuleSet& rules,
                               int workerCount, FilterResult* out,
                               const TokenSnapshot* tok, Progress* progress,
                               const MatchFn& match, ThreadPool* pool);

    // 单行判定（§3.2.2 状态决策表）。ruleRef 输出 rules 数组索引（非 ruleId）。
    static ResultState classifyLine(std::string_view line, const RuleSet& rules,
                                    const MatchFn& match, int* ruleRef);

    // 规则集内容指纹（FNV-1a），用于缓存失效判断。
    static uint64_t fingerprint(const RuleSet& rules);

    // 双击行 → "以该行为过滤条件"（pattern 取行文本，trim 首尾空白）。
    static FilterRule ruleFromLine(const std::string& line, FilterAction action,
                                   MatchMode mode);

    // 内置子串匹配（零 Qt 依赖；大小写不敏感走逐字符 tolower 比较）。
    // wholeWord 时要求命中的前后边界均为非单词字符或行边界。
    // 返回行内字节偏移；未命中返回 npos。
    static size_t findSubstring(std::string_view line, std::string_view needle,
                                bool caseSensitive, bool wholeWord);

private:
    static bool ruleHit(std::string_view line, const FilterRule& r, size_t ruleIndex,
                        const RuleSet& rules, const MatchFn& match);
    static bool isWordChar(char c) noexcept;
    static size_t findFrom(std::string_view line, std::string_view needle,
                           bool caseSensitive, size_t from);
};

}  // namespace tat