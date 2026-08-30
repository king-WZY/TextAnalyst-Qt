// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// testutil.h: BLL/DAL 单测公共工具（临时文件 → TextBuffer）
// =============================================================================
#pragma once

#include <cstdio>
#include <string>
#include <unistd.h>

#include "buffer/TextBuffer.h"
#include "minitest.h"

namespace testutil {

struct Ctx {
    std::string path;
    std::shared_ptr<tat::TextBuffer> buffer;
    ~Ctx() {
        if (!path.empty()) ::remove(path.c_str());
    }
};

inline Ctx makeBuffer(const std::string& content) {
    Ctx c;
    char tmpl[] = "/tmp/tat_XXXXXX";
    const int fd = mkstemp(tmpl);
    if (fd < 0) return c;
    c.path = tmpl;
    if (!content.empty()) {
        const ssize_t w = ::write(fd, content.data(), content.size());
        (void)w;
    }
    ::close(fd);
    tat::Error e;
    c.buffer = tat::TextBuffer::create(c.path, &e, nullptr, nullptr, nullptr);
    return c;
}

}  // namespace testutil