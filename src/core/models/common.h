// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// common.h: 基础类型别名、常量与 SharedSnapshot（C++17 兼容无锁快照）
// 文档：DISPLAYDESIGN.md §1.1 / §1.5（R-01）
// 线程安全：SharedSnapshot [A] 任意线程；其余为纯类型定义
// =============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

namespace tat {

using Offset = uint32_t;  // mmap 基址内字节偏移（< 4 GiB 场景）
using Length = uint32_t;  // 行字节长度，不含行结束符
using LineNo = int;       // 1-based，kInvalidLine 表示无效
using RuleId = int;
using Hash32 = uint32_t;
using Argb   = uint32_t;  // 0xAARRGGBB，与 QColor::rgb() 兼容

inline constexpr LineNo kInvalidLine = -1;
inline constexpr int    kMarkerCount = 8;
// 同时启用的规则上限：RowState::ruleRef 只有 6 bit（0..63），超出时应用被拒绝。
inline constexpr int    kMaxRules    = 64;
inline constexpr Argb   kMarkerArgb[kMarkerCount] = {
    0xFF22AA55, 0xFFAA3333, 0xFF3366CC, 0xFF9933CC,
    0xFFCC6600, 0xFF33AA33, 0xFF7F8C8D, 0xFF555555,
};

// ---- C++17 兼容的无锁发布-订阅快照（DISPLAYDESIGN §1.5 R-01）----
// std::atomic<std::shared_ptr<T>> 是 C++20 特性；本类基于 C++11 的
// std::atomic_load/store 自由函数（libstdc++ 内部全局自旋锁，无争用时
// 单次 20-40 ns 可接受），对外语义与 C++20 版一致：
//   写者 publish() 原子替换；读者 snapshot() 取得私有快照，
//   旧对象在最后一个快照析构时才释放（无 use-after-free，见 §5.4）。
// 所有层一律使用本类，禁止直接写 std::atomic<std::shared_ptr<T>>。
template <class T>
class SharedSnapshot {
public:
    SharedSnapshot() = default;
    SharedSnapshot(const SharedSnapshot&) = delete;
    SharedSnapshot& operator=(const SharedSnapshot&) = delete;

    void publish(std::shared_ptr<T> value) noexcept {
        std::atomic_store(&m_slot, std::move(value));
    }
    std::shared_ptr<T> snapshot() const noexcept {
        return std::atomic_load(&m_slot);
    }
    void clear() noexcept { publish(nullptr); }

private:
    mutable std::shared_ptr<T> m_slot;  // 仅经 atomic 自由函数访问
};

}  // namespace tat