// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// FilterRule.h: 过滤规则与规则快照（值语义，可拷贝）
// 文档：DISPLAYDESIGN.md §1.6（R-10：匹配逻辑归 FilterEngine，不在此处）
// 线程安全：RuleSet 在 Apply 期间不可变，多 worker 共享零锁
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "models/common.h"

namespace tat {

enum class FilterAction : uint8_t { Include = 0, Exclude = 1 };

enum class MatchMode : uint8_t { Substring = 0, Regex = 1 };

// 文本匹配的锚定方式（Filter 下拉：Matches text / Contains / Starts with / Ends with）
enum class FilterMatch : uint8_t {
    Contains   = 0,  // 子串包含（默认，等于历史 Substring 语义）
    Exact      = 1,  // 整行相等（Matches text）
    StartsWith = 2,
    EndsWith   = 3,
};

struct FilterRule {
    int          id         = -1;  // FilterEngine 分配，删除后不复用
    // 默认 Include（着色）——原版 TAT 语义：Excluding 是显式勾选项。
    // R-26 修复：曾默认 Exclude，导致双击行弹窗预填（默认构造规则）
    // 时 Excluding 复选框被勾上，添加的规则默认隐藏匹配行。
    FilterAction action     = FilterAction::Include;
    MatchMode    mode       = MatchMode::Substring;
    FilterMatch  matchType  = FilterMatch::Contains;
    std::string  pattern;             // 原始输入，序列化用
    std::string  description;         // 备注（面板 Description 输入框，仅展示/序列化）
    // Regex 模式的编译产物（不透明字节，由注入的 CompileFn 产出，见 DISPLAYDESIGN §4.1）。
    // v1.0 实际存放 pattern 字符串作为缓存键（Qt 6 无序列化 API，见 §13.1 L1）。
    std::optional<std::string> compiled;
    Argb         foreground  = 0xFF000000;      // #000000
    Argb         background  = 0xFFD7D7D7;      // 浅灰（原版 TAT 默认背景）
    bool         caseSensitive = false;
    bool         isEnabled     = true;
    bool         wholeWord     = false;          // 词边界；仅 Contains 生效
    int          matchCount    = 0;              // 运行态，不序列化
    int          rank          = 0;              // Dock 显示顺序
};

// 规则快照：Apply 期间只读，多 worker 共享，零锁。
// 由 FilterEngine 在提交任务时构造并 shared_ptr 发布（§5.4）。
// 匹配逻辑由 FilterEngine::classifyLine + 注入的 MatchFn 完成（R-10）。
struct RuleSet {
    std::vector<FilterRule>  rules;       // 已过滤 isEnabled
    std::vector<int>         ruleIds;     // 与 rules 同序的 id
    std::vector<std::string> compiled;    // 每条规则对应的编译产物
    bool   hasInclude = false;
    bool   hasExclude = false;
    bool   anyRule    = false;
    uint64_t fingerprint = 0;             // 规则集指纹，缓存失效判断
    int      generation = 0;              // 与 Token 对齐的世代号
};

}  // namespace tat