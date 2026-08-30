// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// tst_filter.cpp: 过滤引擎单元测试（DISPLAYDESIGN.md §10.2.2）
// 覆盖：状态决策表 8 场景、子串/正则、大小写、词边界、取消、并发一致性
// =============================================================================

#include <regex>
#include <string>
#include <vector>

#include "buffer/TextBuffer.h"
#include "engine/FilterEngine.h"
#include "engine/ThreadPool.h"
#include "minitest.h"
#include "testutil.h"

using namespace tat;

namespace {

// Regex stub：core 单测环境无 Qt/PCRE2，用 std::regex 模拟注入回调（§4.1 契约）
auto stubCompile = [](const std::string& p, bool cs, bool, std::string* out,
                      std::string* err) -> int {
    try {
        std::regex re(p, cs ? std::regex::ECMAScript : std::regex::icase);
        (void)re;
        *out = p;
        return 0;
    } catch (const std::regex_error& e) {
        *err = e.what();
        return -1;
    }
};

auto stubMatch = [](std::string_view line, const std::string&,
                    const std::string& pattern, bool cs) -> bool {
    try {
        std::regex re(pattern, cs ? std::regex::ECMAScript : std::regex::icase);
        return std::regex_search(line.begin(), line.end(), re);
    } catch (...) {
        return false;
    }
};

std::shared_ptr<RuleSet> build(const std::vector<FilterRule>& rules, int gen,
                               Error* err) {
    std::shared_ptr<RuleSet> rs;
    *err = FilterEngine::buildRuleSet(rules, gen, stubCompile, &rs);
    return rs;
}

RowState row(const FilterResult& r, int i) {
    RowState s;
    s.raw = r.states[static_cast<size_t>(i)];
    return s;
}

}  // namespace

TEST(defaultActionIsInclude) {
    // R-26 回归：默认规则必须是着色（Include）——曾默认 Exclude，
    // 双击行弹窗预填默认构造规则时 Excluding 复选框被误勾选
    FilterRule r;
    CHECK(r.action == FilterAction::Include);
    CHECK(r.matchType == FilterMatch::Contains);
}

TEST(noRules) {
    auto c = testutil::makeBuffer("a\nb\n");
    CHECK(c.buffer != nullptr);
    Error e;
    auto rs = build({}, 0, &e);
    CHECK(e.ok());
    FilterResult fr;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr, nullptr, nullptr, {}).ok());
    CHECK(row(fr, 0).state() == ResultState::Normal);
    CHECK(row(fr, 1).state() == ResultState::Normal);
}

TEST(excludeOnly) {
    auto c = testutil::makeBuffer("error line\ninfo line\n");
    FilterRule r;
    r.id = 1; r.action = FilterAction::Exclude; r.pattern = "error";
    Error e;
    auto rs = build({r}, 0, &e);
    CHECK(e.ok());
    FilterResult fr;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr, nullptr, nullptr, {}).ok());
    CHECK(row(fr, 0).state() == ResultState::Hidden);   // 命中 → 隐藏
    CHECK(row(fr, 1).state() == ResultState::Normal);   // 未命中 → 正常
}

TEST(includeOnly) {
    auto c = testutil::makeBuffer("warn here\ninfo line\n");
    FilterRule r;
    r.id = 1; r.action = FilterAction::Include; r.pattern = "warn";
    Error e;
    auto rs = build({r}, 0, &e);
    CHECK(e.ok());
    FilterResult fr;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr, nullptr, nullptr, {}).ok());
    CHECK(row(fr, 0).state() == ResultState::Highlighted);
    // R-19：Include 未命中行正常显示（原版 TAT 语义，不再弱化为 Dimmed）
    CHECK(row(fr, 1).state() == ResultState::Normal);
    CHECK_EQ(row(fr, 0).ruleRef(), 0);  // 规则索引
}

TEST(includeExcludePriority) {
    // R-19 顺序语义（原版 TAT）：规则按列表顺序求值，第一条命中的规则
    // 决定行命运（Excluding → Hidden，着色 → Highlighted），无命中 → Normal。
    auto c = testutil::makeBuffer("keep me ok\nskip me\nother\n");
    FilterRule r1;
    r1.id = 1; r1.action = FilterAction::Include; r1.pattern = "keep";
    FilterRule r2;
    r2.id = 2; r2.action = FilterAction::Exclude; r2.pattern = "skip";
    Error e;
    auto rs = build({r1, r2}, 0, &e);
    CHECK(e.ok());
    FilterResult fr;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr, nullptr, nullptr, {}).ok());
    CHECK(row(fr, 0).state() == ResultState::Highlighted);  // 第 1 条 keep 命中
    CHECK(row(fr, 1).state() == ResultState::Hidden);       // keep 未命中 → skip(E) 命中 → 隐藏
    CHECK(row(fr, 2).state() == ResultState::Normal);       // 无命中 → 正常

    // 反转顺序：Exclude 在前 → 顺序决定优先级
    auto rs2 = build({r2, r1}, 0, &e);
    FilterResult fr2;
    CHECK(FilterEngine::apply(*c.buffer, *rs2, &fr2, nullptr, nullptr, {}).ok());
    CHECK(row(fr2, 0).state() == ResultState::Highlighted);  // skip 未命中 → keep 命中
    CHECK(row(fr2, 1).state() == ResultState::Hidden);       // skip 先命中
}

TEST(substringMode) {
    auto c = testutil::makeBuffer("prefix-ERROR-suffix\nno match\n");
    FilterRule r;
    r.id = 1; r.action = FilterAction::Exclude; r.pattern = "ERROR";
    Error e;
    auto rs = build({r}, 0, &e);
    CHECK(e.ok());
    FilterResult fr;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr, nullptr, nullptr, {}).ok());
    CHECK(row(fr, 0).state() == ResultState::Hidden);
    CHECK(row(fr, 1).state() == ResultState::Normal);
}

TEST(caseSensitive) {
    auto c = testutil::makeBuffer("ERROR\nerror\n");
    FilterRule r;
    r.id = 1; r.action = FilterAction::Exclude; r.pattern = "ERROR";
    r.caseSensitive = true;
    Error e;
    auto rs = build({r}, 0, &e);
    CHECK(e.ok());
    FilterResult fr;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr, nullptr, nullptr, {}).ok());
    CHECK(row(fr, 0).state() == ResultState::Hidden);
    CHECK(row(fr, 1).state() == ResultState::Normal);  // 小写不命中
}

TEST(caseInsensitive) {
    auto c = testutil::makeBuffer("ERROR\nMiXeD\n");
    FilterRule r;
    r.id = 1; r.action = FilterAction::Exclude; r.pattern = "error";
    Error e;
    auto rs = build({r}, 0, &e);
    CHECK(e.ok());
    FilterResult fr;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr, nullptr, nullptr, {}).ok());
    CHECK(row(fr, 0).state() == ResultState::Hidden);
    CHECK(row(fr, 1).state() == ResultState::Normal);
}

TEST(wholeWord) {
    auto c = testutil::makeBuffer("cat\nconcatenate\nmy cat!\n");
    FilterRule r;
    r.id = 1; r.action = FilterAction::Exclude; r.pattern = "cat";
    r.wholeWord = true;
    Error e;
    auto rs = build({r}, 0, &e);
    CHECK(e.ok());
    FilterResult fr;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr, nullptr, nullptr, {}).ok());
    CHECK(row(fr, 0).state() == ResultState::Hidden);   // 整词
    CHECK(row(fr, 1).state() == ResultState::Normal);   // 部分词不命中
    CHECK(row(fr, 2).state() == ResultState::Hidden);   // 边界空白可命中
}

TEST(wholeWordOff) {
    auto c = testutil::makeBuffer("cat\nconcatenate\n");
    FilterRule r;
    r.id = 1; r.action = FilterAction::Exclude; r.pattern = "cat";
    Error e;
    auto rs = build({r}, 0, &e);
    CHECK(e.ok());
    FilterResult fr;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr, nullptr, nullptr, {}).ok());
    CHECK(row(fr, 0).state() == ResultState::Hidden);
    CHECK(row(fr, 1).state() == ResultState::Hidden);  // 子串也命中
}

TEST(regexRule) {
    auto c = testutil::makeBuffer("apple\nbanana\ncherry\n");
    FilterRule r;
    r.id = 1; r.action = FilterAction::Exclude; r.mode = MatchMode::Regex;
    r.pattern = "ba.*";
    Error e;
    auto rs = build({r}, 0, &e);
    CHECK(e.ok());
    FilterResult fr;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr, nullptr, nullptr, stubMatch)
              .ok());
    CHECK(row(fr, 0).state() == ResultState::Normal);
    CHECK(row(fr, 1).state() == ResultState::Hidden);  // banana 命中
    CHECK(row(fr, 2).state() == ResultState::Normal);
}

TEST(regexInvalid) {
    FilterRule r;
    r.id = 1; r.action = FilterAction::Exclude; r.mode = MatchMode::Regex;
    r.pattern = "[a-";  // 非法正则
    Error e;
    auto rs = build({r}, 0, &e);
    CHECK(e.code == ErrCode::RegexError);
    CHECK(rs == nullptr);
}

TEST(matchCount) {
    auto c = testutil::makeBuffer("ERROR a\nERROR b\nok\n");
    FilterRule r;
    r.id = 7; r.action = FilterAction::Exclude; r.pattern = "ERROR";
    Error e;
    auto rs = build({r}, 0, &e);
    CHECK(e.ok());
    FilterResult fr;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr, nullptr, nullptr, {}).ok());
    CHECK_EQ(fr.matchCounts[7].load(), 2);
}

TEST(cancel) {
    // > 8 KB 文件，预取消 token → Cancelled
    std::string content;
    for (int i = 0; i < 20000; ++i) content += "line-of-log-data\n";
    auto c = testutil::makeBuffer(content);
    CHECK(c.buffer != nullptr);
    FilterRule r;
    r.id = 1; r.action = FilterAction::Exclude; r.pattern = "never-match-xyz";
    Error e;
    auto rs = build({r}, 0, &e);
    Token tok;
    const int g = tok.current();
    TokenSnapshot ts{&tok, g};
    tok.cancel();
    FilterResult fr;
    auto err = FilterEngine::apply(*c.buffer, *rs, &fr, &ts, nullptr, {});
    CHECK(err.code == ErrCode::Cancelled);
}

TEST(parallelMatchesSerial) {
    std::string content;
    for (int i = 0; i < 20000; ++i)
        content += (i % 7 == 0) ? "WARN something happened\n" : "INFO normal\n";
    auto c = testutil::makeBuffer(content);
    CHECK(c.buffer != nullptr);
    FilterRule r1;
    r1.id = 1; r1.action = FilterAction::Include; r1.pattern = "WARN";
    FilterRule r2;
    r2.id = 2; r2.action = FilterAction::Exclude; r2.pattern = "nothing";
    Error e;
    auto rs = build({r1, r2}, 7, &e);
    CHECK(e.ok());

    FilterResult ser, par;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &ser, nullptr, nullptr, {}).ok());
    SimpleThreadPool pool{4};
    CHECK(FilterEngine::applyParallel(*c.buffer, *rs, 4, &par, nullptr, nullptr,
                                      {}, &pool)
              .ok());
    CHECK_EQ(ser.states.size(), par.states.size());
    CHECK(ser.states == par.states);      // 逐行状态一致
    CHECK_EQ(ser.generation, par.generation);
    for (int id = 0; id < kMaxRules; ++id)
        CHECK_EQ(ser.matchCounts[id].load(), par.matchCounts[id].load());
}

TEST(parallelCancel) {
    std::string content;
    for (int i = 0; i < 200000; ++i) content += "array index out of range\n";
    auto c = testutil::makeBuffer(content);
    CHECK(c.buffer != nullptr);
    FilterRule r;
    r.id = 1; r.action = FilterAction::Exclude; r.pattern = "range";
    Error e;
    auto rs = build({r}, 0, &e);
    Token tok;
    const int g = tok.current();
    TokenSnapshot ts{&tok, g};
    tok.cancel();
    SimpleThreadPool pool{4};
    FilterResult fr;
    auto err = FilterEngine::applyParallel(*c.buffer, *rs, 4, &fr, &ts, nullptr,
                                           {}, &pool);
    CHECK(err.code == ErrCode::Cancelled);
    CHECK(fr.states.size() > 0);  // 部分结果保留（不可为空断言，取决于调度）
}

TEST(ruleFromLine) {
    auto r = FilterEngine::ruleFromLine("  ERROR 123  \n",
                                        FilterAction::Exclude, MatchMode::Substring);
    CHECK_EQ(r.pattern, "ERROR 123");
    CHECK(r.action == FilterAction::Exclude);
}

TEST(ruleCountLimit) {
    std::vector<FilterRule> rules;
    for (int i = 0; i < kMaxRules + 1; ++i) {
        FilterRule r;
        r.id = i + 1; r.pattern = "p" + std::to_string(i);
        rules.push_back(r);
    }
    Error e;
    auto rs = build(rules, 0, &e);
    CHECK(e.code == ErrCode::InvalidArgument);
    CHECK(rs == nullptr);
}

TEST(fingerprintStability) {
    FilterRule r1;
    r1.id = 1; r1.action = FilterAction::Exclude; r1.pattern = "x";
    Error e1, e2;
    auto a = build({r1}, 0, &e1);
    auto b = build({r1}, 5, &e2);  // generation 不同，指纹应相同
    CHECK(a->fingerprint == b->fingerprint);
    FilterRule r2 = r1;
    r2.pattern = "y";
    auto c = build({r2}, 0, &e1);
    CHECK(a->fingerprint != c->fingerprint);
}

TEST(matchTypeExact) {
    auto c = testutil::makeBuffer("ERROR\nERROR AGAIN\nerror\n");
    FilterRule r;
    r.id = 1; r.action = FilterAction::Exclude; r.pattern = "ERROR";
    Error e;
    auto rs = build({r}, 0, &e);   // 默认 Contains
    CHECK(e.ok());
    FilterResult fr;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr, nullptr, nullptr, {}).ok());
    CHECK(row(fr, 0).state() == ResultState::Hidden);
    CHECK(row(fr, 1).state() == ResultState::Hidden);  // Contains 子串命中

    r.matchType = FilterMatch::Exact;   // Matches text：仅整行相等
    rs = build({r}, 0, &e);
    FilterResult fr2;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr2, nullptr, nullptr, {}).ok());
    CHECK(row(fr2, 0).state() == ResultState::Hidden);    // 整行相等 → 命中
    CHECK(row(fr2, 1).state() == ResultState::Normal);    // 子串但非整行 → 不命中
    CHECK(row(fr2, 2).state() == ResultState::Hidden);    // 默认忽略大小写 → 命中

    r.caseSensitive = true;            // 区分大小写后再验证第二行为非命中
    rs = build({r}, 0, &e);
    FilterResult fr3;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr3, nullptr, nullptr, {}).ok());
    CHECK(row(fr3, 0).state() == ResultState::Hidden);
    CHECK(row(fr3, 2).state() == ResultState::Normal);    // 小写 'error' 不命中
}

TEST(matchTypeStartsEnds) {
    auto c = testutil::makeBuffer("INFO task done\nINFO task failed\nWARN task done\n");
    FilterRule r;
    r.id = 1; r.action = FilterAction::Exclude;
    r.matchType = FilterMatch::StartsWith; r.pattern = "INFO";
    r.caseSensitive = true;
    Error e;
    auto rs = build({r}, 0, &e);
    FilterResult fr;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr, nullptr, nullptr, {}).ok());
    CHECK(row(fr, 0).state() == ResultState::Hidden);
    CHECK(row(fr, 1).state() == ResultState::Hidden);
    CHECK(row(fr, 2).state() == ResultState::Normal);   // WARN 开头不命中

    r.matchType = FilterMatch::EndsWith; r.pattern = "done";
    rs = build({r}, 0, &e);
    FilterResult fr2;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr2, nullptr, nullptr, {}).ok());
    CHECK(row(fr2, 0).state() == ResultState::Hidden);
    CHECK(row(fr2, 1).state() == ResultState::Normal);  // failed 结尾不命中
    CHECK(row(fr2, 2).state() == ResultState::Hidden);
}

TEST(fingerprintIncludesMatchType) {
    FilterRule r1;
    r1.id = 1; r1.action = FilterAction::Exclude; r1.pattern = "x";
    FilterRule r2 = r1;
    r2.matchType = FilterMatch::Exact;   // 仅锚定方式不同 → 指纹应不同
    Error e;
    auto a = build({r1}, 0, &e);
    auto b = build({r2}, 0, &e);
    CHECK(a->fingerprint != b->fingerprint);
}

TEST(emptyPatternNoMatch) {
    auto c = testutil::makeBuffer("a\nb\n");
    FilterRule r;
    r.id = 1; r.action = FilterAction::Exclude; r.pattern = "";
    Error e;
    auto rs = build({r}, 0, &e);
    CHECK(e.ok());
    FilterResult fr;
    CHECK(FilterEngine::apply(*c.buffer, *rs, &fr, nullptr, nullptr, {}).ok());
    CHECK(row(fr, 0).state() == ResultState::Normal);  // 空模式不匹配
}

int main() { return minitest::runAll(); }