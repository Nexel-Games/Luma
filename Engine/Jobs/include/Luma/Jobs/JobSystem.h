#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Luma/Core/Types.h"

// Luma Job System — distributes CPU work across worker threads.
//
// A job is a function with a name, a priority, optional dependencies (other
// jobs that must finish first) and an optional main-thread affinity (work that
// must run on the thread that owns the window / GPU objects; it runs when that
// thread calls RunMainThreadJobs). Waiting is cooperative: a thread that waits
// executes other ready jobs instead of blocking, so jobs may wait on jobs they
// spawn without deadlocking the pool.
//
// Used for shader compilation, texture processing, mesh generation, asset
// loading and other background preparation. Progress reporting for the user is
// a separate system (Luma::Tasks::TaskManager); jobs report into it.

namespace Luma::Jobs {

enum class Priority : u8 { High = 0, Normal = 1, Low = 2, Background = 3 };
inline constexpr usize kPriorityCount = 4;

struct JobState;

// Shared handle to a scheduled job. Cheap to copy; outlives the job.
class JobHandle {
public:
    JobHandle() = default;
    bool IsValid() const { return m_state != nullptr; }
    bool IsDone() const;
    // True when the job threw; Error() holds the message.
    bool Failed() const;
    std::string Error() const;
    const std::string& Name() const;

private:
    friend class JobSystem;
    explicit JobHandle(std::shared_ptr<JobState> s) : m_state(std::move(s)) {}
    std::shared_ptr<JobState> m_state;
};

struct JobDesc {
    std::string name = "Job";
    Priority priority = Priority::Normal;
    std::vector<JobHandle> dependencies;  // all must finish (or fail) first
    bool mainThread = false;              // run only inside RunMainThreadJobs
};

class JobSystem {
public:
    // workerCount 0 = hardware threads - 1 (at least 1). The constructing
    // thread is the "main thread" for main-thread jobs and helping waits.
    explicit JobSystem(u32 workerCount = 0);
    // Finishes every scheduled job (pumping main-thread jobs), then joins.
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    JobHandle Schedule(JobDesc desc, std::function<void()> fn);

    // Splits [0, count) into chunks of `grain` items run in parallel. The
    // returned handle finishes when every chunk has.
    JobHandle ParallelFor(std::string name, usize count, usize grain,
                          std::function<void(usize begin, usize end)> body,
                          Priority priority = Priority::Normal);

    // Returns once `job` finished, executing other ready jobs meanwhile (and
    // main-thread jobs when called on the main thread).
    void Wait(const JobHandle& job);
    void WaitAll(const std::vector<JobHandle>& jobs);

    // Runs ready main-thread jobs; call once per frame from the main thread.
    // Stops early after `budgetMs` (0 = no budget). Returns how many ran.
    u32 RunMainThreadJobs(f64 budgetMs = 0.0);

    u32 WorkerCount() const { return static_cast<u32>(m_workers.size()); }
    bool IsMainThread() const { return std::this_thread::get_id() == m_mainThread; }
    // Jobs scheduled and not yet finished (diagnostics / tests).
    u64 Outstanding() const { return m_outstanding.load(); }

private:
    void Enqueue(const std::shared_ptr<JobState>& job);
    std::shared_ptr<JobState> TryPop();          // highest priority worker job
    std::shared_ptr<JobState> TryPopMain();
    void Execute(const std::shared_ptr<JobState>& job);
    void WorkerLoop(u32 index);

    std::vector<std::thread> m_workers;
    std::thread::id m_mainThread;

    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<std::shared_ptr<JobState>> m_queues[kPriorityCount];
    bool m_stop = false;

    std::mutex m_mainMutex;
    std::deque<std::shared_ptr<JobState>> m_mainQueue;

    std::mutex m_doneMutex;  // waiters sleep here between completions
    std::condition_variable m_doneCv;

    std::atomic<u64> m_outstanding{0};
};

}  // namespace Luma::Jobs
