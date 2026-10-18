/*
 * GlideMix -- playback engine
 */
#include "MixtapeWorkers.h"

#include <algorithm>

namespace mixtape {
WorkerPool::WorkerPool(size_t threads)
{
    if (threads == 0) {
        const size_t cores = std::max<unsigned>(1, std::thread::hardware_concurrency());
        threads = std::min<size_t>(6, cores > 1 ? cores - 1 : 0);
    }
    for (size_t i = 0; i < threads; ++i) {
        mThreads.emplace_back([this] { Work(); });
    }
}

WorkerPool::~WorkerPool()
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mQuit = true;
    }
    mWake.notify_all();
    for (auto& thread : mThreads) {
        thread.join();
    }
}

void WorkerPool::Drain()
{
    for (;;) {
        const size_t index = mNext.fetch_add(1);
        if (index >= mCount) {
            return;
        }
        (*mJob)(index);
        if (mFinished.fetch_add(1) + 1 == mCount) {
            std::lock_guard<std::mutex> lock(mMutex);
            mDone.notify_all();
        }
    }
}

void WorkerPool::Run(size_t count, const std::function<void(size_t)>& job)
{
    if (count == 0) {
        return;
    }
    if (mThreads.empty() || count == 1) {
        for (size_t i = 0; i < count; ++i) {
            job(i);
        }
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mJob = &job;
        mCount = count;
        mNext = 0;
        mFinished = 0;
        ++mGeneration;
    }
    mWake.notify_all();
    Drain();
    std::unique_lock<std::mutex> lock(mMutex);
    mDone.wait(lock, [this] { return mFinished.load() >= mCount; });
    mJob = nullptr;
    mCount = 0;
}

void WorkerPool::Work()
{
    size_t seen = 0;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mWake.wait(lock, [&] { return mQuit || (mGeneration != seen && mJob); });
            if (mQuit) {
                return;
            }
            seen = mGeneration;
        }
        Drain();
    }
}
}
