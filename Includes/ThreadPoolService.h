#pragma once

#include "ThreadPool.h"
#include "memory"
#include "ProcessRunGuard.h"

class ThreadPoolService {
public:
    static ThreadPoolService& Instance() {
        static ThreadPoolService instance;
        return instance;
    }

    ThreadPoolService(const ThreadPoolService&) = delete;
    ThreadPoolService& operator=(const ThreadPoolService&) = delete;

    void Enqueue(std::function<void()> wrapper) {
        pool_->Enqueue(std::move(wrapper));
    }

    ThreadPool& Pool() { return *pool_; }

private:
    ThreadPoolService() {
        size_t threads = std::thread::hardware_concurrency();
        if (threads == 0) threads = 4;
        pool_ = std::make_unique<ThreadPool>(threads);
    }

    ~ThreadPoolService() = default;

    std::unique_ptr<ThreadPool> pool_;
};