// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// TatSerializer.h: .tat 过滤器文件读写（与原版 TAT 格式互通）
// 文档：ARCHITECTURE.md §7.2；DISPLAYDESIGN.md §6.2
// 格式：QXmlStreamWriter 格式化输出；QXmlStreamReader 流式解析（不加载 DOM）
// 线程安全：write/read 因文件 < 1 MB 而同步执行（ARCH-UB §8 v1.0.1 豁免）
// =============================================================================
#pragma once

#include <QString>
#include <vector>

#include "core/models/Error.h"
#include "core/models/FilterRule.h"

namespace tat {

class TatSerializer {
public:
    // 写入：先写 *.tmp，成功后 rename() 原子替换，同时保留 *.bak（上一版）。
    static Error write(const QString& path, const std::vector<FilterRule>& rules);

    // 读取：流式解析；非法 XML/字段错误返回 XmlError。
    // 空文件视为"无规则"（合法）。解析遇到未知属性时忽略（向前兼容）。
    static Error read(const QString& path, std::vector<FilterRule>* out);

private:
    static QString colorToHex(Argb argb);
    static bool    hexToColor(const QString& hex, Argb* out);
    static QString actToStr(FilterAction a);
    static QString modeToStr(MatchMode m);
    static QString matchToStr(FilterMatch m);
    static bool    strToMatch(const QString& s, FilterMatch* out);
    static bool    strToAct(const QString& s, FilterAction* out);
    static bool    strToMode(const QString& s, MatchMode* out);
};

}  // namespace tat