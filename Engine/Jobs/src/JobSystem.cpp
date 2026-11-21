#include "Luma/Jobs/JobSystem.h"

#include <algorithm>
#include <chrono>
#include <exception>

#include "Luma/Core/Log.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Luma::Jobs {

struct JobState {
    std::string name;
    Priority priority = Priority::Normal;
    bool mainThread = false;
    std::function<void()> fn;

    // Unfinished dependencies + 1 guard held while Schedule wires them up.
    std::atomic<i32> pending{1};

    std::mutex mutex;                                 // guards finished + dependents
    bool finished = false;
    std::vector<std::shared_ptr<JobState>> dependents;

    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    std::string error;                                // written before `done`
};

bool JobHandle::IsDone() const { return m_state && m_state->done.load(std::memory_order_acquire); }
bool JobHandle::Failed() const { return IsDone() && m_state->failed.load(); }
std::string JobHandle::Error() const { return IsDone() ? m_state->error : std::string{}; }
const std::string& JobHandle::Name() const {
    static const std::string kNone;
    return m_state ? m_state->name : kNone;
}

namespace {

void NameCurrentThread(u32 index) {
#ifdef _WIN32
    const std::wstring name = L"Luma Worker " + std::to_wstring(index);
    SetThreadDescription(GetCurrentThread(), name.c_str());
#else
    (void)index;
#endif
}

}  // namespace

JobSystem::JobSystem(u32 workerCount) : m_mainThread(std::this_thread::get_id()) {
    if (workerCount == 0) {
        const u32 hw = std::max(1u, std::thread::hardware_concurrency());
        workerCount = std::max(1u, hw - 1);
    }
    m_workers.reserve(workerCount);
    for (u32 i = 0; i < workerCount; ++i) {
        m_workers.emplace_back([this, i] { WorkerLoop(i); });
    }
}

JobSystem::~JobSystem() {
    // Drain: everything scheduled must finish, including main-thread jobs
    // (pumped here) and jobs still waiting on dependencies.
    while (m_outstanding.load() > 0) {
        if (RunMainThreadJobs() == 0) {
            std::unique_lock lock(m_doneMutex);
            m_doneCv.wait_for(lock, std::chrono::milliseconds(1));
        }
    }
    {
        std::lock_guard lock(m_queueMutex);
        m_stop = true;
    }
    m_queueCv.notify_all();
    for (std::thread& t : m_workers) t.join();
}

JobHandle JobSystem::Schedule(JobDesc desc, std::function<void()> fn) {
    auto job = std::make_shared<JobState>();
    job->name = std::move(desc.name);
    job->priority = desc.priority;
    job->mainThread = desc.mainThread;
    job->fn = std::move(fn);
    m_outstanding.fetch_add(1);

    for (const JobHandle& dep : desc.dependencies) {
        if (!dep.m_state) continue;
        std::lock_guard lock(dep.m_state->mutex);
        if (!dep.m_state->finished) {
            dep.m_state->dependents.push_back(job);
            job->pending.fetch_add(1);
        }
    }
    // Release the scheduling guard; enqueue if nothing is pending.
    if (job->pending.fetch_sub(1) == 1) Enqueue(job);
    return JobHandle(job);
}

JobHandle JobSystem::ParallelFor(std::string name, usize count, usize grain,
                                 std::function<void(usize, usize)> body, Priority priority) {
    grain = std::max<usize>(1, grain);
    auto shared = std::make_shared<std::function<void(usize, usize)>>(std::move(body));
    JobDesc join{.name = name, .priority = priority};
    for (usize begin = 0; begin < count; begin += grain) {
        const usize end = std::min(count, begin + grain);
        join.dependencies.push_back(Schedule({.name = name, .priority = priority},
                                             [shared, begin, end] { (*shared)(begin, end); }));
    }
    return Schedule(std::move(join), [] {});
}

void JobSystem::Enqueue(const std::shared_ptr<JobState>& job) {
    if (job->mainThread) {
        std::lock_guard lock(m_mainMutex);
        m_mainQueue.push_back(job);
        m_doneCv.notify_all();  // a main thread waiting in Wait() may run it
        return;
    }
    {
        std::lock_guard lock(m_queueMutex);
        m_queues[static_cast<usize>(job->priority)].push_back(job);
    }
    m_queueCv.notify_one();
    m_doneCv.notify_all();      // helping waiters
}

std::shared_ptr<JobState> JobSystem::TryPop() {
    std::lock_guard lock(m_queueMutex);
    for (auto& q : m_queues) {
        if (!q.empty()) {
            auto job = std::move(q.front());
            q.pop_front();
            return job;
        }
    }
    return nullptr;
}

std::shared_ptr<JobState> JobSystem::TryPopMain() {
    std::lock_guard lock(m_mainMutex);
    if (m_mainQueue.empty()) return nullptr;
    auto job = std::move(m_mainQueue.front());
    m_mainQueue.pop_front();
    return job;
}

void JobSystem::Execute(const std::shared_ptr<JobState>& job) {
    try {
        if (job->fn) job->fn();
    } catch (const std::exception& e) {
        job->error = e.what();
        job->failed = true;
    } catch (...) {
        job->error = "unknown exception";
        job->failed = true;
    }
    if (job->failed) LUMA_LOG_ERROR("Jobs", "job '{}' failed: {}", job->name, job->error);
    job->fn = nullptr;  // release captures early

    std::vector<std::shared_ptr<JobState>> dependents;
    {
        std::lock_guard lock(job->mutex);
        job->finished = true;
        dependents.swap(job->dependents);
    }
    job->done.store(true, std::memory_order_release);
    for (auto& d : dependents) {
        if (d->pending.fetch_sub(1) == 1) Enqueue(d);
    }
    m_outstanding.fetch_sub(1);
    {
        std::lock_guard lock(m_doneMutex);  // pairs with waiters' predicate checks
    }
    m_doneCv.notify_all();
}

void JobSystem::WorkerLoop(u32 index) {
    NameCurrentThread(index);
    for (;;) {
        std::shared_ptr<JobState> job;
        {
            std::unique_lock lock(m_queueMutex);
            m_queueCv.wait(lock, [this] {
                if (m_stop) return true;
                for (auto& q : m_queues) {
                    if (!q.empty()) return true;
                }
                return false;
            });
            for (auto& q : m_queues) {
                if (!q.empty()) {
                    job = std::move(q.front());
                    q.pop_front();
                    break;
                }
            }
            if (!job && m_stop) return;
        }
        if (job) Execute(job);
    }
}

void JobSystem::Wait(const JobHandle& handle) {
    if (!handle.m_state) return;
    const bool onMain = IsMainThread();
    while (!handle.IsDone()) {
        std::shared_ptr<JobState> job = onMain ? TryPopMain() : nullptr;
        if (!job) job = TryPop();
        if (job) {
            Execute(job);
            continue;
        }
        std::unique_lock lock(m_doneMutex);
        m_doneCv.wait_for(lock, std::chrono::milliseconds(1),
                          [&] { return handle.IsDone(); });
    }
}

void JobSystem::WaitAll(const std::vector<JobHandle>& jobs) {
    for (const JobHandle& j : jobs) Wait(j);
}

u32 JobSystem::RunMainThreadJobs(f64 budgetMs) {
    const auto start = std::chrono::steady_clock::now();
    u32 ran = 0;
    while (auto job = TryPopMain()) {
        Execute(job);
        ++ran;
        if (budgetMs > 0.0) {
            const f64 elapsed = std::chrono::duration<f64, std::milli>(
                                    std::chrono::steady_clock::now() - start).count();
            if (elapsed >= budgetMs) break;
        }
    }
    return ran;
}

}  // namespace Luma::Jobs
