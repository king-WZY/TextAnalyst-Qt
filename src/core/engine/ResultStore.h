// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// ResultStore.h: 发布-订阅式双缓冲（C++17 兼容，无 use-after-free）
// 文档：DISPLAYDESIGN.md §5.4（R-01：SharedSnapshot 替代 std::atomic<shared_ptr>）
// 线程安全：publish [W]；snapshot [A]；旧对象由引用计数自然释放
// =============================================================================
#pragma once

#include <memory>

#include "engine/FilterResult.h"
#include "models/common.h"  // SharedSnapshot<T>

namespace tat {

class ResultStore {
public:
    // 写者：发布新结果。旧结果在最后一个快照析构时释放（可能发生在任意线程）。
    void publish(std::shared_ptr<const FilterResult> result) noexcept {
        m_current.publish(std::move(result));
    }

    // 读者：取得当前快照；调用方持有期间结果不会被释放。
    std::shared_ptr<const FilterResult> snapshot() const noexcept {
        return m_current.snapshot();
    }

    // 读者：检查是否需要重绘（generation 变化）。
    int currentGeneration() const noexcept {
        auto r = snapshot();
        return r ? r->generation : -1;
    }

    // 写者：清空（文件关闭时）。
    void clear() noexcept { m_current.clear(); }

private:
    SharedSnapshot<const FilterResult> m_current;
};

}  // namespace tat