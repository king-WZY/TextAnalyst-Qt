// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// ThreadPool.h: 线程池抽象（BLL 零 Qt 依赖的关键接口）
// 文档：DISPLAYDESIGN.md §4.2（ThreadPool 抽象 / QThreadPoolAdapter 在 UI 层）
// 线程安全：submit/waitForDone [A]；v1.0 限制单一提交者（主线程）语义
// =============================================================================
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace tat {

class ThreadPool {
public:
    virtual ~ThreadPool() = default;

    // 提交任务（非阻塞，立即返回），返回单调递增的任务 ID。
    virtual int submit(std::function<void()> task) = 0;

    // 等待已提交任务全部完成。timeoutMs < 0 无限等待；返回 true 全部完成。
    // 注意：v1.0 假设单一提交者，waitForDone 期间不提交新任务。
    virtual bool waitForDone(int timeoutMs) = 0;

    // 当前活跃（执行中+排队）任务数，近似值。
    virtual int activeTaskCount() = 0;

    // 默认线程数（std::thread::hardware_concurrency，0 表示未知）
    static int idealThreadCount() noexcept {
        const unsigned n = std::thread::hardware_concurrency();
        return n > 0 ? static_cast<int>(n) : 1;
    }
};

// 纯 std::thread 实现（core 内可用，供 CLI/单测/无 Qt 环境）。
// UI 主程序使用 QThreadPoolAdapter（src/ui 或 src/controller 提供）。
class SimpleThreadPool : public ThreadPool {
public:
    explicit SimpleThreadPool(int maxThreads = 0);
    ~SimpleThreadPool() override;

    int  submit(std::function<void()> task) override;
    bool waitForDone(int timeoutMs) override;
    int  activeTaskCount() override;

private:
    void workerLoop();

    std::vector<std::thread>          workers_;
    std::deque<std::function<void()>> tasks_;
    std::mutex                        mtx_;
    std::condition_variable           cv_;
    size_t                            active_ = 0;
    bool                              stop_   = false;
    int                               nextId_ = 0;
};

}  // namespace tat