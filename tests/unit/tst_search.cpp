// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// tst_search.cpp: 搜索单元测试（DISPLAYDESIGN.md §10.2.4）
// =============================================================================

#include <regex>
#include <string>
#include <vector>

#include "engine/Searcher.h"
#include "minitest.h"
#include "testutil.h"

using namespace tat;

namespace {

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

SearchResult runSearch(const testutil::Ctx& c, const SearchOptions& opt,
                       const TokenSnapshot* tok = nullptr, int maxHits = 10000) {
    SearchResult out;
    Searcher::search(*c.buffer, opt, stubCompile, stubMatch, &out, tok, nullptr,
                     maxHits);
    return out;
}

}  // namespace

TEST(simpleContains) {
    auto c = testutil::makeBuffer("apple\nbanana\napple pie\n");
    SearchOptions opt;
    opt.pattern = "apple";
    auto r = runSearch(c, opt);
    CHECK_EQ(r.hits.size(), 2u);
    CHECK_EQ(r.hits[0].line, 1);
    CHECK_EQ(r.hits[0].offset, 0u);
    CHECK_EQ(r.hits[1].line, 3);
}

TEST(caseSensitive) {
    auto c = testutil::makeBuffer("ERROR\nerror\n");
    SearchOptions opt;
    opt.pattern = "ERROR";
    opt.caseSensitive = true;
    auto r = runSearch(c, opt);
    CHECK_EQ(r.hits.size(), 1u);
    CHECK_EQ(r.hits[0].line, 1);
}

TEST(caseInsensitive) {
    auto c = testutil::makeBuffer("ERROR\nerror\n");
    SearchOptions opt;
    opt.pattern = "error";  // 默认大小写不敏感
    auto r = runSearch(c, opt);
    CHECK_EQ(r.hits.size(), 2u);
}

TEST(regexMode) {
    auto c = testutil::makeBuffer("abc123\ndef\nxyz456\n");
    SearchOptions opt;
    opt.pattern = "[0-9]+";
    opt.useRegex = true;
    auto r = runSearch(c, opt);
    CHECK_EQ(r.hits.size(), 2u);
    CHECK_EQ(r.hits[0].line, 1);
    CHECK_EQ(r.hits[1].line, 3);
}

TEST(wholeWord) {
    auto c = testutil::makeBuffer("cat\nconcatenate\ncat!\n");
    SearchOptions opt;
    opt.pattern = "cat";
    opt.wholeWord = true;
    auto r = runSearch(c, opt);
    CHECK_EQ(r.hits.size(), 2u);   // 第 1、3 行
    CHECK_EQ(r.hits[0].line, 1);
    CHECK_EQ(r.hits[1].line, 3);
}

TEST(startLine) {
    auto c = testutil::makeBuffer("hit\nhit\nhit\n");
    SearchOptions opt;
    opt.pattern = "hit";
    opt.startLine = 2;
    auto r = runSearch(c, opt);
    CHECK_EQ(r.hits.size(), 2u);
    CHECK_EQ(r.hits[0].line, 2);
    CHECK_EQ(r.hits[1].line, 3);
}

TEST(maxHitsTruncated) {
    auto c = testutil::makeBuffer("hit\nhit\nhit\nhit\nhit\n");
    SearchOptions opt;
    opt.pattern = "hit";
    auto r = runSearch(c, opt, nullptr, 3);
    CHECK_EQ(r.hits.size(), 3u);
    CHECK(r.truncated > 0);
}

TEST(cancel) {
    std::string content;
    for (int i = 0; i < 30000; ++i) content += "normalline\n";
    auto c = testutil::makeBuffer(content);
    CHECK(c.buffer != nullptr);
    Token tok;
    const int g = tok.current();
    TokenSnapshot ts{&tok, g};
    tok.cancel();
    SearchOptions opt;
    opt.pattern = "nomatch-zzz";
    auto r = runSearch(c, opt, &ts);
    CHECK(r.cancelled);  // 第 10000 行处检查取消
}

TEST(viewportRange) {
    auto c = testutil::makeBuffer("a\nb\nc\nd\ne\nf\ng\n");
    SearchOptions opt;
    opt.pattern = "c";
    std::vector<SearchHit> hits;
    auto err = Searcher::searchViewport(*c.buffer, opt, 4, 2, stubMatch, &hits);
    CHECK(err.ok());
    CHECK_EQ(hits.size(), 1u);
    CHECK_EQ(hits[0].line, 3);  // anchor=4, radius=2 → [2,6] 内命中第 3 行
}

TEST(viewportOutOfRangeLine) {
    auto c = testutil::makeBuffer("x\ny\n");
    SearchOptions opt;
    opt.pattern = "x";
    std::vector<SearchHit> hits;
    auto err = Searcher::searchViewport(*c.buffer, opt, 100, 5, stubMatch,
                                        &hits);  // anchor 超界
    CHECK(err.ok());
    CHECK_EQ(hits.size(), 1u);  // 视口收敛到 [1,2]
}

TEST(emptyPatternSubstring) {
    auto c = testutil::makeBuffer("a\nbc\n");
    SearchOptions opt;  // 空模式 substring
    auto r = runSearch(c, opt);
    CHECK_EQ(r.hits.size(), 0u);  // 空模式不匹配（DontMatchEmptyString 语义）
}

TEST(invalidRegex) {
    auto c = testutil::makeBuffer("a\n");
    SearchOptions opt;
    opt.useRegex = true;
    opt.pattern = "[a-";
    SearchResult out;
    auto err = Searcher::search(*c.buffer, opt, stubCompile, stubMatch, &out,
                                nullptr, nullptr);
    CHECK(err.code == ErrCode::RegexError);
}

TEST(offsetIsByteOffset) {
    // UTF-8 中文行：字节偏移 ≠ 字符偏移（§3.5.4）
    auto c = testutil::makeBuffer("\xE4\xBD\xA0\xE5\xA5\xBDok\n");  // "你好ok"
    SearchOptions opt;
    opt.pattern = "ok";
    auto r = runSearch(c, opt);
    CHECK_EQ(r.hits.size(), 1u);
    CHECK_EQ(r.hits[0].offset, 6u);  // 6 个 UTF-8 字节之后
}

TEST(searchAllLines1Based) {
    auto c = testutil::makeBuffer("first\nsecond\nthird\n");
    SearchOptions opt;
    opt.pattern = "second";
    auto r = runSearch(c, opt);
    CHECK_EQ(r.hits[0].line, 2);  // 1-based
}

int main() { return minitest::runAll(); }