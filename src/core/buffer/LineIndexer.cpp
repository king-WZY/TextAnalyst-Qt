// =============================================================================
// LineIndexer.cpp: 换行符扫描索引
// 实现要点（DISPLAYDESIGN.md §2.4.2）：
//   - 用 memchr 跳转扫描（glibc 2.35+ 为 AVX2 向量化），避免逐字节循环
//   - 行尾识别顺序：\r\n → \n → \r（Windows 日志在 Ubuntu 下常见）
//   - 文件末尾无换行也产生最后一行；纯 "\n" 文件产生 1 个空行（R-06）
//   - 进度/取消：每 64 KiB 检查一次
// =============================================================================

#include "buffer/LineIndexer.h"

#include <cstring>
#include <new>

#include <chrono>

namespace tat {

namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t kCancelCheckBytes = 64 * 1024;

}  // namespace

int LineIndexer::estimateRows(const char* data, size_t size) {
    const size_t sample = size < (1u << 20) ? size : (1u << 20);
    if (sample == 0) return 1;
    size_t count = 0;
    const char* p = data;
    const char* end = data + sample;
    while ((p = static_cast<const char*>(memchr(p, '\n', end - p))) != nullptr) {
        ++count;
        ++p;
    }
    if (count == 0) return 1;
    // 按密度外推（整数乘法可能溢出 64 位？64 位安全）
    size_t est = (size * count + sample - 1) / sample;
    return static_cast<int>(est < 0x7FFFFFFF ? est : 0x7FFFFFFF);
}

Error LineIndexer::build(const char* data, size_t size, const EncodingInfo& enc,
                         std::vector<LineMeta>* out, IndexStats* stats,
                         const TokenSnapshot* tok, Progress* progress) {
    const auto t0 = Clock::now();
    out->clear();
    stats->rowCount = 0;
    stats->maxLength = 0;
    stats->totalBytes = 0;
    stats->hitOffsetLimit = false;

    const char* base = data + enc.bomLen;   // 偏移基准：跳过 BOM
    const size_t n = size - enc.bomLen;
    if (n == 0) {
        stats->elapsedMs = 0;
        return Error::none();  // 全 BOM 文件：0 行
    }

    // 预留容量（按预估行数；bad_alloc → OutOfMemory）
    const int est = estimateRows(data, size);
    try {
        out->reserve(static_cast<size_t>(est));
    } catch (const std::bad_alloc&) {
        stats->elapsedMs = 0;
        return {ErrCode::OutOfMemory, "LineIndexer::build", "reserve failed"};
    }

    size_t lfCount = 0, crlfCount = 0, crCount = 0;
    size_t maxLen = 0, totalBytes = 0;

    // 主循环：memchr 定位行结束符
    const char* it = base;
    const char* end = base + n;
    size_t bytesProcessed = 0;

    while (it < end) {
        const char* lf = static_cast<const char*>(memchr(it, '\n', end - it));
        // 关键：\r 的搜索范围限定在 [it, lf)（或到 end）。日志文件通常无 \r，
        // 若改为 memchr(it, '\r', end - it) 则每次迭代全段扫描剩余内容，
        // 退化为 O(行数 × 剩余字节) = O(n²)（曾导致 24 MiB 索引耗时数分钟）。
        const char* scanEnd = lf ? lf : end;
        const char* cr = static_cast<const char*>(memchr(it, '\r', scanEnd - it));

        const char* eol = nullptr;
        const char* nextStart = end;
        size_t len = 0;
        LineEnd kind = LineEnd::LF;

        if (cr) {
            if (lf && cr + 1 == lf) {      // \r\n（\r 恰为 \n 前一字节）
                eol = lf; nextStart = lf + 1;
                len = static_cast<size_t>(cr - it);
                kind = LineEnd::CRLF; ++crlfCount;
            } else {                       // 独立 \r（macOS classic 风格）
                eol = cr; nextStart = cr + 1;
                len = static_cast<size_t>(cr - it);
                kind = LineEnd::CR; ++crCount;
            }
        } else if (lf) {
            eol = lf; nextStart = lf + 1;
            len = static_cast<size_t>(lf - it);
            kind = LineEnd::LF; ++lfCount;
        } else {                           // 尾行：无行结束符
            eol = end; nextStart = end;
            len = static_cast<size_t>(end - it);
            kind = LineEnd::LastNoEol;
        }
        (void)eol;

        const uint64_t off = static_cast<uint64_t>(it - base);
        if (off > 0xFFFFFFFFull - 4096) {
            // 超过 uint32_t 偏移上限 → 触发分段降级（DISPLAYDESIGN §2.4.4）
            stats->hitOffsetLimit = true;
            stats->dominantEnd = kind;
            stats->elapsedMs = std::chrono::duration<double, std::milli>(
                                   Clock::now() - t0).count();
            return {ErrCode::UnsupportedFormat, "LineIndexer::build",
                    "file exceeds 4 GiB offset limit"};
        }

        out->push_back(LineMeta{static_cast<Offset>(off),
                                static_cast<Length>(len)});
        if (len > maxLen) maxLen = len;
        totalBytes += len;

        bytesProcessed += static_cast<size_t>(nextStart - it);
        it = nextStart;

        // 进度与取消：每 64 KiB 检查一次
        if (bytesProcessed >= kCancelCheckBytes) {
            bytesProcessed = 0;
            if (tok && !tok->valid())
                return {ErrCode::Cancelled, "LineIndexer::build",
                        "cancelled"};
            if (progress) {
                progress->current = static_cast<size_t>(it - base);
                progress->total = n;
                progress->percent = n > 0 ? 100.0 * (it - base) / n : 100.0;
            }
        }
    }

    stats->rowCount = static_cast<int>(out->size());
    stats->maxLength = maxLen;
    stats->totalBytes = totalBytes;
    const size_t total = lfCount + crlfCount + crCount;
    stats->dominantEnd = (crlfCount > lfCount && crlfCount > crCount)
                             ? LineEnd::CRLF
                             : (crCount > lfCount && crCount > crlfCount)
                                   ? LineEnd::CR
                                   : LineEnd::LF;
    (void)total;
    stats->elapsedMs = std::chrono::duration<double, std::milli>(
                           Clock::now() - t0).count();
    return Error::none();
}

}  // namespace tat