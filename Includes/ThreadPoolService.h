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

	ThreadPool<ProcessRunGuardResult>& Pool() { return *pool_; }

private:
	ThreadPoolService() :pool_(std::make_unique<ThreadPool<ProcessRunGuardResult>>()) {}

	~ThreadPoolService() = default;


	ThreadPoolService(const ThreadPoolService&) = delete;
	ThreadPoolService& operator=(const ThreadPoolService&) = delete;

private:
	std::unique_ptr<ThreadPool<ProcessRunGuardResult>> pool_;
};