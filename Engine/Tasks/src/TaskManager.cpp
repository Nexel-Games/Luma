#include "Luma/Tasks/TaskManager.h"

#include <algorithm>
#include <chrono>
#include <mutex>

namespace Luma::Tasks {

namespace {
using Clock = std::chrono::steady_clock;
}

struct TaskRecord {
    TaskSnapshot snap;
    Clock::time_point start;
    Clock::time_point end;
    f64 age = 0.0;  // seconds since finished (Update-driven)
};

struct TaskManagerShared {
    mutable std::mutex mutex;
    std::vector<TaskRecord> tasks;  // creation order
    TaskId nextId = 1;
    f64 linger = 3.0;

    TaskRecord* Find(TaskId id) {
        for (auto& t : tasks) {
            if (t.snap.id == id) return &t;
        }
        return nullptr;
    }
    void Finish(TaskRecord& t, TaskState state) {
        if (t.snap.state != TaskState::Running) return;
        t.snap.state = state;
        t.end = Clock::now();
    }
};

f32 TaskSnapshot::Fraction() const {
    if (total > 0) {
        return std::clamp(static_cast<f32>(done) / static_cast<f32>(total), 0.0f, 1.0f);
    }
    return fraction >= 0.0f ? std::clamp(fraction, 0.0f, 1.0f) : 0.0f;
}

std::string TaskSnapshot::CountText() const {
    if (total == 0) return {};
    std::string s = std::to_string(done) + " / " + std::to_string(total);
    if (!unit.empty()) s += " " + unit;
    return s;
}

// --- TaskToken ------------------------------------------------------------

TaskToken::TaskToken(TaskToken&& o) noexcept : m_shared(std::move(o.m_shared)), m_id(o.m_id) {
    o.m_id = 0;
}

TaskToken& TaskToken::operator=(TaskToken&& o) noexcept {
    if (this != &o) {
        if (m_shared) Complete();
        m_shared = std::move(o.m_shared);
        m_id = o.m_id;
        o.m_id = 0;
    }
    return *this;
}

TaskToken::~TaskToken() {
    if (m_shared) Complete();
}

#define LUMA_TASK_LOCKED(body)                                    \
    if (!m_shared) return;                                        \
    std::lock_guard lock(m_shared->mutex);                        \
    TaskRecord* t = m_shared->Find(m_id);                         \
    if (!t || t->snap.state != TaskState::Running) return;        \
    body

void TaskToken::Advance(u64 count) { LUMA_TASK_LOCKED(t->snap.done += count;) }
void TaskToken::SetDone(u64 done) { LUMA_TASK_LOCKED(t->snap.done = done;) }
void TaskToken::SetTotal(u64 total) { LUMA_TASK_LOCKED(t->snap.total = total;) }
void TaskToken::SetFraction(f32 fraction) { LUMA_TASK_LOCKED(t->snap.fraction = fraction;) }
void TaskToken::SetStatus(std::string status) { LUMA_TASK_LOCKED(t->snap.status = std::move(status);) }

#undef LUMA_TASK_LOCKED

bool TaskToken::CancelRequested() const {
    if (!m_shared) return false;
    std::lock_guard lock(m_shared->mutex);
    const TaskRecord* t = m_shared->Find(m_id);
    return t && t->snap.cancelRequested;
}

void TaskToken::Complete() {
    if (!m_shared) return;
    {
        std::lock_guard lock(m_shared->mutex);
        if (TaskRecord* t = m_shared->Find(m_id)) {
            m_shared->Finish(*t, t->snap.cancelRequested ? TaskState::Cancelled
                                                         : TaskState::Completed);
            if (t->snap.state == TaskState::Completed && t->snap.total > 0) {
                t->snap.done = t->snap.total;
            }
        }
    }
    m_shared.reset();
}

void TaskToken::Fail(std::string error) {
    if (!m_shared) return;
    {
        std::lock_guard lock(m_shared->mutex);
        if (TaskRecord* t = m_shared->Find(m_id)) {
            t->snap.error = std::move(error);
            m_shared->Finish(*t, TaskState::Failed);
        }
    }
    m_shared.reset();
}

// --- TaskManager -----------------------------------------------------------

TaskManager::TaskManager() : m_shared(std::make_shared<TaskManagerShared>()) {}
TaskManager::~TaskManager() = default;

TaskToken TaskManager::Begin(TaskDesc desc) {
    std::lock_guard lock(m_shared->mutex);
    TaskRecord rec;
    rec.snap.id = m_shared->nextId++;
    rec.snap.parent = desc.parent;
    rec.snap.title = std::move(desc.title);
    rec.snap.unit = std::move(desc.unit);
    rec.snap.total = desc.total;
    rec.snap.cancellable = desc.cancellable;
    rec.snap.category = std::move(desc.category);
    rec.start = Clock::now();
    const TaskId id = rec.snap.id;
    m_shared->tasks.push_back(std::move(rec));
    return TaskToken(m_shared, id);
}

std::vector<TaskSnapshot> TaskManager::Snapshot() const {
    std::lock_guard lock(m_shared->mutex);
    std::vector<TaskSnapshot> out;
    out.reserve(m_shared->tasks.size());
    const auto now = Clock::now();
    for (const TaskRecord& t : m_shared->tasks) {
        TaskSnapshot s = t.snap;
        s.elapsedSeconds = std::chrono::duration<f64>(
                               (t.snap.state == TaskState::Running ? now : t.end) - t.start)
                               .count();
        out.push_back(std::move(s));
    }
    return out;
}

u32 TaskManager::RunningCount() const {
    std::lock_guard lock(m_shared->mutex);
    u32 n = 0;
    for (const TaskRecord& t : m_shared->tasks) {
        if (t.snap.parent == 0 && t.snap.state == TaskState::Running) ++n;
    }
    return n;
}

f32 TaskManager::AggregateFraction() const {
    std::lock_guard lock(m_shared->mutex);
    f32 sum = 0.0f;
    u32 n = 0;
    for (const TaskRecord& t : m_shared->tasks) {
        if (t.snap.parent != 0 || t.snap.state != TaskState::Running || t.snap.Indeterminate()) {
            continue;
        }
        sum += t.snap.Fraction();
        ++n;
    }
    return n ? sum / static_cast<f32>(n) : 0.0f;
}

bool TaskManager::RequestCancel(TaskId id) {
    std::lock_guard lock(m_shared->mutex);
    TaskRecord* t = m_shared->Find(id);
    if (!t || !t->snap.cancellable || t->snap.state != TaskState::Running) return false;
    t->snap.cancelRequested = true;
    // Sub-tasks of a cancelled operation are cancelled with it.
    for (TaskRecord& child : m_shared->tasks) {
        if (child.snap.parent == id && child.snap.state == TaskState::Running) {
            child.snap.cancelRequested = true;
        }
    }
    return true;
}

void TaskManager::Dismiss(TaskId id) {
    std::lock_guard lock(m_shared->mutex);
    auto& v = m_shared->tasks;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [id](const TaskRecord& t) {
                               return (t.snap.id == id || t.snap.parent == id) &&
                                      t.snap.state != TaskState::Running;
                           }),
            v.end());
}

void TaskManager::SetLinger(f64 seconds) {
    std::lock_guard lock(m_shared->mutex);
    m_shared->linger = seconds;
}

void TaskManager::Update(f64 dt) {
    std::lock_guard lock(m_shared->mutex);
    for (TaskRecord& t : m_shared->tasks) {
        if (t.snap.state != TaskState::Running) t.age += dt;
    }
    auto& v = m_shared->tasks;
    const f64 linger = m_shared->linger;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [linger](const TaskRecord& t) {
                               // Failures stay until dismissed.
                               return t.snap.state != TaskState::Running &&
                                      t.snap.state != TaskState::Failed && t.age >= linger;
                           }),
            v.end());
}

}  // namespace Luma::Tasks
