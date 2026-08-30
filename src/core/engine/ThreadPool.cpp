// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// ThreadPool.cpp: SimpleThreadPool（std::thread 实现）
// =============================================================================

#include "engine/ThreadPool.h"

namespace tat {

SimpleThreadPool::SimpleThreadPool(int maxThreads) {
    const int n = maxThreads > 0 ? maxThreads : idealThreadCount();
    workers_.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

SimpleThreadPool::~SimpleThreadPool() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
}

int SimpleThreadPool::submit(std::function<void()> task) {
    if (!task) return -1;
    int id;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (stop_) return -1;
        id = nextId_++;
        tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
    return id;
}

bool SimpleThreadPool::waitForDone(int timeoutMs) {
    std::unique_lock<std::mutex> lk(mtx_);
    auto pred = [this] { return stop_ || (tasks_.empty() && active_ == 0); };
    if (timeoutMs < 0) {
        cv_.wait(lk, pred);
        return true;
    }
    return cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), pred);
}

int SimpleThreadPool::activeTaskCount() {
    std::lock_guard<std::mutex> lk(mtx_);
    return static_cast<int>(tasks_.size() + active_);
}

void SimpleThreadPool::workerLoop() {
    for (;;) {
        std::function<void()> t;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty()) return;
            t = std::move(tasks_.front());
            tasks_.pop_front();
            ++active_;
        }
        // 约定：BLL/DAL 任务不抛异常（ERROR 码返回模型）；
        // 防御性捕获，避免单个任务异常杀死 worker 线程。
        try {
            t();
        } catch (...) {
            // 静默吞掉：任务级失败应由任务内 Error 传递
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            --active_;
        }
        cv_.notify_all();
    }
}

}  // namespace tat