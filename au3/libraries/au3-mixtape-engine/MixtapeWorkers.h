/*
 * GlideMix -- playback engine
 *
 * A few threads that run the render thread's per-track jobs in parallel. The
 * calling thread works too and Run() returns once every job is done, so a
 * block never goes out half rendered.
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace mixtape {
class WorkerPool
{
public:
    //! 0 = one fewer than the CPU cores, at most 6.
    explicit WorkerPool(size_t threads = 0);
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    //! Runs job(0) .. job(count - 1), each once, on the workers and this thread.
    void Run(size_t count, const std::function<void(size_t)>& job);

    size_t Threads() const { return mThreads.size(); }

private:
    void Work();
    void Drain();

    std::vector<std::thread> mThreads;
    std::mutex mMutex;
    std::condition_variable mWake;
    std::condition_variable mDone;
    const std::function<void(size_t)>* mJob = nullptr;
    size_t mCount = 0;
    std::atomic<size_t> mNext{ 0 };
    std::atomic<size_t> mFinished{ 0 };
    size_t mGeneration = 0;
    bool mQuit = false;
};
}
