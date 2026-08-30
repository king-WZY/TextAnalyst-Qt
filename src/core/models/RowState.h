// =============================================================================
// RowState.h: 每行最终渲染状态（1 字节：状态 2 bit + ruleRef 6 bit）
// 文档：DISPLAYDESIGN.md §1.5 / §3.2.2 / §3.3
// 线程安全：值类型
// =============================================================================
#pragma once

#include <cstdint>

#include "models/common.h"

namespace tat {

// 渲染状态，2 bit。见 DISPLAYDESIGN §3.2.2 的状态决策表。
enum class ResultState : uint8_t {
    Normal      = 0,  // 未被任何规则命中
    Hidden      = 1,  // 被排除，不绘制（行槽保留，行号仍显示）
    Dimmed      = 2,  // 非白名单，降低对比度绘制
    Highlighted = 3,  // 命中高亮
};

// 每行最终结果，1 字节：
//   bit 0..1 : ResultState
//   bit 2..7 : ruleColorRef —— Highlighted 时是 RuleSet.rules 的索引（0..63）；
//              Dimmed/Normal/Hidden 时为 0。
// 颜色不在每行存放，按 ruleRef 查 RuleSet.rules（§3.3，省 400 MB/亿行）。
struct alignas(1) RowState {
    uint8_t raw = 0;

    ResultState state() const noexcept { return static_cast<ResultState>(raw & 0x3); }
    int  ruleRef() const noexcept { return (raw >> 2) & 0x3F; }
    bool isHidden() const noexcept { return state() == ResultState::Hidden; }
    bool isNormal() const noexcept { return state() == ResultState::Normal; }
    bool hasColor() const noexcept {
        return state() == ResultState::Highlighted || state() == ResultState::Dimmed;
    }

    static uint8_t make(ResultState s, int ruleRef) noexcept {
        return static_cast<uint8_t>((static_cast<uint8_t>(s) & 0x3) |
                                    ((static_cast<uint8_t>(ruleRef) & 0x3F) << 2));
    }
};
static_assert(sizeof(RowState) == 1, "one byte per line");

}  // namespace tat