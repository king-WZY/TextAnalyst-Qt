// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// TextBuffer.cpp: 文本快照实现
// 构造流程见 DISPLAYDESIGN.md §2.5.2（mmap → detectFast → 索引 → 发布）
// =============================================================================

#include "buffer/TextBuffer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "buffer/EncodingDetector.h"
#include "buffer/LineIndexer.h"
#include "buffer/MemoryMappedFile.h"

namespace tat {

std::shared_ptr<TextBuffer> TextBuffer::create(const std::string& path,
                                               Error* err, EncodingInfo* enc,
                                               const TokenSnapshot* tok,
                                               Progress* progress) {
    Error e;
    MemoryMappedFile mm = MemoryMappedFile::open(path, MemoryMapFlags::ReadOnly, &e);
    if (e.hasError()) {
        *err = e;
        return nullptr;
    }

    auto tb = std::make_shared<TextBuffer>();
    tb->m_map = std::make_shared<MemoryMappedFile>(std::move(mm));
    tb->m_base = tb->m_map->data();
    tb->m_size = tb->m_map->size();
    tb->m_path = path;
    tb->m_mtimeNs = tb->m_map->mtimeNs();

    // 空文件：合法结果，0 行（R-06）
    if (tb->m_size == 0) {
        if (enc) *enc = EncodingInfo{};
        *err = Error::none();
        return tb;
    }

    // 索引构建阶段只跑 L1+L2（R-04，§2.3.3）
    EncodingInfo det = EncodingDetector::detectFast(
        tb->m_base, EncodingDetector::recommendSampleSize(tb->m_size));
    if (enc) *enc = det;
    tb->m_encoding = det;

    IndexStats stats;
    Error e2 = LineIndexer::build(tb->m_base, tb->m_size, det, &tb->m_lines,
                                  &stats, tok, progress);
    if (e2.hasError()) {
        *err = e2;
        return nullptr;
    }
    *err = Error::none();
    return tb;
}

const LineMeta* TextBuffer::meta(int i) const noexcept {
    if (i < 0 || i >= static_cast<int>(m_lines.size())) return nullptr;
    return &m_lines[static_cast<size_t>(i)];
}

std::string_view TextBuffer::textAt(int i) const noexcept {
    if (i < 0 || i >= static_cast<int>(m_lines.size())) return {};
    const LineMeta& lm = m_lines[static_cast<size_t>(i)];
    // 注意：LineMeta.offset 是相对"跳过 BOM 后"的基址（LineIndexer 约定），
    // 读取时必须补上 bomLen（DISPLAYDESIGN §2.4.2 / §2.5.3）。
    return {m_base + m_encoding.bomLen + lm.offset, lm.length};
}

std::string TextBuffer::toUtf8(int i) const {
    auto sv = textAt(i);
    if (sv.empty() && m_encoding.isUtf8ByteStream())
        return std::string();  // 空行（UTF-8 直通）
    if (m_encoding.isUtf8ByteStream())
        return std::string(sv.data(), sv.size());
    std::string out;
    bool replaced = false;
    EncodingDetector::convert(sv.data(), sv.size(), m_encoding.encoding,
                              Encoding::Utf8, &out, &replaced);
    return out;
}

TextBuffer::MemoryFootprint TextBuffer::footprint() const {
    MemoryFootprint fp;
    fp.mapped = m_size;
    fp.indexMeta = m_lines.size() * sizeof(LineMeta);
    FILE* f = std::fopen("/proc/self/smaps_rollup", "re");
    if (f) {
        char line[256];
        while (std::fgets(line, sizeof(line), f)) {
            if (std::strncmp(line, "Rss:", 4) == 0) {
                fp.resident = static_cast<size_t>(
                                  std::strtoull(line + 4, nullptr, 10)) * 1024;
                break;
            }
        }
        std::fclose(f);
    }
    return fp;
}

}  // namespace tat