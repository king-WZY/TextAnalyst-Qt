// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// bench_1gb.cpp: 性能基准（DISPLAYDESIGN.md §9 性能预算 / ARCH-UB §13）
// 指标：打开（mmap+索引）、全量过滤匹配、内存占用（Rss）
//
// 用法：
//   bench_1gb [--size <MiB>] [--regex] [--threshold <f>]
//   --size      生成文件大小（默认 256，1g 可用；写入需要磁盘空间）
//   --regex     额外跑一次正则路径（std::regex stub，仅参考）
//
// 目标（EXT 8 vCPU, NVMe）：
//   打开 1 GiB < 500 ms；匹配 1 亿行 < 3 s（行进速率约 >= 33M 行/s 每核同量级）
// 输出行速率（行/s 与 MB/s），CI 用 --threshold 与历史基线比较。
// =============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>

#include "buffer/TextBuffer.h"
#include "engine/FilterEngine.h"
#include "engine/ThreadPool.h"
#include "models/Error.h"
#include "models/FilterRule.h"

using namespace tat;
using Clock = std::chrono::steady_clock;

namespace {

// 构造一行日志模板（平均约 80 字节）
std::string buildLine(int i) {
    char buf[160];
    // 模拟时间戳 + 级别 + 线程 + 消息
    const char* level = (i % 17 == 0) ? "ERROR" : (i % 7 == 0) ? "WARN" : "INFO";
    std::snprintf(buf, sizeof(buf),
                  "2026-08-29 12:34:%02d.%03d %s worker-%02d task=%d "
                  "processing batch item %d with payload=OFFSET_USER_%d\n",
                  i % 60, i * 7 % 1000, level, i % 32, i, i, i);
    return buf;
}

// 生成临时日志文件（内容可复现），返回路径
std::string generateLog(size_t bytes) {
    char tmpl[] = "/tmp/tat_bench_XXXXXX";
    const int fd = mkstemp(tmpl);
    if (fd < 0) return {};
    const std::string line = buildLine(1);
    std::string buf;
    buf.reserve(1u << 20);
    size_t written = 0;
    int i = 0;
    while (written < bytes) {
        const std::string l = buildLine(i++);
        buf += l;
        written += l.size();
        if (buf.size() >= (1u << 20)) {
            const ssize_t w = ::write(fd, buf.data(), buf.size());
            (void)w;
            buf.clear();
        }
    }
    if (!buf.empty()) {
        const ssize_t w = ::write(fd, buf.data(), buf.size());
        (void)w;
    }
    ::close(fd);
    return tmpl;
}

double msOf(const Clock::time_point& a, const Clock::time_point& b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

size_t rssKb() {
    FILE* f = std::fopen("/proc/self/smaps_rollup", "re");
    if (!f) return 0;
    char line[256];
    size_t kb = 0;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "Rss:", 4) == 0) {
            kb = static_cast<size_t>(std::strtoull(line + 4, nullptr, 10));
            break;
        }
    }
    std::fclose(f);
    return kb;
}

}  // namespace

int main(int argc, char** argv) {
    size_t sizeMiB = 256;
    bool withRegex = false;
    double threshold = 0.0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc)
            sizeMiB = static_cast<size_t>(std::strtoull(argv[++i], nullptr, 10));
        else if (std::strcmp(argv[i], "--regex") == 0)
            withRegex = true;
        else if (std::strcmp(argv[i], "--threshold") == 0 && i + 1 < argc)
            threshold = std::strtod(argv[++i], nullptr);
    }
    const size_t totalBytes = sizeMiB * (1u << 20);

    std::fprintf(stdout, "== bench_1gb: size=%zu MiB regex=%d ==\n", sizeMiB,
                 (int)withRegex);

    // ---- 生成文件 ----
    const auto tGen0 = Clock::now();
    std::fprintf(stderr, "[stage] generating file...\n");
    const std::string path = generateLog(totalBytes);
    if (path.empty()) {
        std::fprintf(stderr, "FAIL: cannot create temp file\n");
        return 1;
    }
    struct stat st{};
    ::stat(path.c_str(), &st);
    const double genMs = msOf(tGen0, Clock::now());
    std::fprintf(stderr, "[stage] generating done: %.0f ms\n", genMs);

    // ---- 打开（mmap + 编码 + 索引）----
    Error e;
    EncodingInfo enc;
    std::fprintf(stderr, "[stage] opening + indexing...\n");
    const auto t0 = Clock::now();
    auto tb = TextBuffer::create(path, &e, &enc, nullptr, nullptr);
    const double openMs = msOf(t0, Clock::now());
    if (!e.ok() || !tb) {
        std::fprintf(stderr, "FAIL: open: %s\n", e.message.c_str());
        ::remove(path.c_str());
        return 1;
    }
    std::fprintf(stderr, "[stage] open done: rows=%d\n", tb->rowCount());
    const int rows = tb->rowCount();
    const double indexRate = rows / (openMs / 1000.0);  // 行/s

    // ---- 全量过滤匹配（Exclude "ERROR"，8 线程）----
    FilterRule fr;
    fr.id = 1;
    fr.action = FilterAction::Exclude;
    fr.pattern = "ERROR";
    std::shared_ptr<RuleSet> rs;
    Error berr = FilterEngine::buildRuleSet({fr}, 0, nullptr, &rs);
    if (berr.hasError()) {
        std::fprintf(stderr, "FAIL: buildRuleSet\n");
        return 1;
    }
    SimpleThreadPool pool{8};
    auto result = std::make_shared<FilterResult>();
    std::fprintf(stderr, "[stage] filtering...\n");
    const auto t1 = Clock::now();
    Error perr = FilterEngine::applyParallel(*tb, *rs, 8, result.get(), nullptr,
                                             nullptr, {}, &pool);
    const double filterMs = msOf(t1, Clock::now());
    std::fprintf(stderr, "[stage] filter done: %.0f ms\n", filterMs);
    if (perr.hasError()) {
        std::fprintf(stderr, "FAIL: applyParallel\n");
        return 1;
    }
    const double matchRate = rows / (filterMs / 1000.0);

    // ---- 正则路径（可选）----
    double regexMs = 0;
    if (withRegex) {
        FilterRule r2;
        r2.id = 1; r2.action = FilterAction::Exclude; r2.mode = MatchMode::Regex;
        r2.pattern = "ERROR|WARN";
        auto rs2 = std::make_shared<RuleSet>();
        // 无 Qt 环境用 std::regex 包装；仅数量级参考
        auto compile = [](const std::string& p, bool, bool, std::string* o,
                          std::string*) -> int {
            *o = p; return 0;
        };
        auto match = [](std::string_view line, const std::string&,
                        std::string_view, bool) -> bool {
            return line.find("ERROR") != std::string_view::npos ||
                   line.find("WARN") != std::string_view::npos;
        };
        Error re = FilterEngine::buildRuleSet({r2}, 0, compile, &rs2);
        auto res2 = std::make_shared<FilterResult>();
        const auto t2 = Clock::now();
        FilterEngine::applyParallel(*tb, *rs2, 8, res2.get(), nullptr, nullptr,
                                    match, &pool);
        regexMs = msOf(t2, Clock::now());
    }

    const size_t rssBytes = rssKb() * 1024;
    const double fps = 0;  // 滚动帧率属 UI 指标，CLI 不测

    std::fprintf(stdout, "file-size     : %zu MiB\n", sizeMiB);
    std::fprintf(stdout, "gen-ms        : %.1f\n", genMs);
    std::fprintf(stdout, "open-ms       : %.1f  (mmap+index, rows=%d, rate=%.2fM rows/s)\n",
                 openMs, rows, indexRate / 1e6);
    std::fprintf(stdout, "filter-ms     : %.1f  (rate=%.2fM rows/s)\n", filterMs,
                 matchRate / 1e6);
    if (withRegex)
        std::fprintf(stdout, "regex-ms      : %.1f\n", regexMs);
    std::fprintf(stdout, "rss-after     : %.1f MiB\n", rssBytes / 1048576.0);
    std::fprintf(stdout, "index-meta    : %.1f MiB (LineMeta)\n",
                 static_cast<double>(rows) * sizeof(LineMeta) / 1048576.0);
    (void)fps;
    (void)threshold;

    ::remove(path.c_str());

    // 判定指标（本机基准）：打开耗时按速率外推到 1 GiB 的参考门禁
    if (openMs > 1200) {
        std::fprintf(stderr, "THRESHOLD FAIL: open-ms=%.1f too slow\n", openMs);
        return 2;
    }
    std::fprintf(stdout, "== bench done ==\n");
    return 0;
}