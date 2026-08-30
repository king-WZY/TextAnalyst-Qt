// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// Task.h: 任务令牌（世代号取消）与进度报告
// 文档：DISPLAYDESIGN.md §1.5 / §3.2.4 / §5.6
// 线程安全：Token [A]；TokenSnapshot 任务私有；Progress [A] 值类型
// =============================================================================
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace tat {

// 单调递增的世代号。取消 = 自增；过期任务观察到世代变化后自行退出。
class Token {
public:
    void cancel() noexcept { gen_.fetch_add(1, std::memory_order_release); }
    bool valid(int myGen) const noexcept {
        return gen_.load(std::memory_order_acquire) == myGen;
    }
    int current() const noexcept { return gen_.load(std::memory_order_relaxed); }

private:
    std::atomic<int> gen_{0};
};

// 任务私有世代快照，避免多次 load。
struct TokenSnapshot {
    Token* token = nullptr;
    int    myGen = 0;

    bool valid() const noexcept { return token && token->valid(myGen); }
};

struct Progress {
    double   percent    = 0.0;  // 0.0 - 100.0
    size_t   current    = 0;
    size_t   total      = 0;
    uint64_t bytesDone  = 0;
    uint64_t totalBytes = 0;
    uint64_t elapsedMs  = 0;

    bool isFinished() const noexcept { return total > 0 && current >= total; }
};

}  // namespace tat