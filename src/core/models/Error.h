// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// Error.h: 错误模型（不使用 C++ 异常，统一返回值）
// 文档：DISPLAYDESIGN.md §1.4
// 线程安全：[A] 任意线程（不可变值类型）
// =============================================================================
#pragma once

#include <string>

namespace tat {

enum class ErrCode : int {
    Ok                 = 0,
    InvalidArgument    = -1,
    FileNotFound       = -2,
    PermissionDenied   = -3,
    MapFailed          = -4,
    OutOfMemory        = -5,
    EncodingUnknown    = -6,
    RegexError         = -7,
    XmlError           = -8,
    Cancelled          = -9,
    Timeout            = -10,
    IoError            = -11,
    UnsupportedFormat  = -12,
};

// 不可变、可拷贝、可跨线程传递。
struct Error {
    ErrCode     code    = ErrCode::Ok;
    std::string op;       // 出错操作名，如 "mmap"、"compileRegex"
    std::string message;  // 人类可读描述，用于日志；UI 层走 i18n

    bool ok() const noexcept { return code == ErrCode::Ok; }
    bool hasError() const noexcept { return code != ErrCode::Ok; }
    static Error none() noexcept { return {}; }
};

}  // namespace tat