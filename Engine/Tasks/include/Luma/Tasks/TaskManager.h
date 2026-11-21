#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Luma/Core/Types.h"

// Luma Task Manager — the user-visible side of background work.
//
// Long operations (shader compilation, imports, pipeline initialisation...)
// call Begin() and get a TaskToken they update from any thread: counts or a
// fraction, a status line, completion, failure. The editor's Background Tasks
// UI reads thread-safe Snapshot()s each frame and can request cancellation;
// the worker polls CancelRequested() and stops. Work distribution itself is
// the Job System's job (Luma::Jobs) — the two are deliberately separate.

namespace Luma::Tasks {

using TaskId = u64;
enum class TaskState : u8 { Running, Completed, Failed, Cancelled };

struct TaskDesc {
    std::string title;          // "Compiling Shaders"
    std::string unit;           // "shaders" → "144 / 200 shaders"
    u64 total = 0;              // 0 = no item count (fraction or indeterminate)
    bool cancellable = false;   // only when the worker polls CancelRequested()
    TaskId parent = 0;          // sub-step of another task (rolled up in the UI)
    std::string category;       // optional grouping ("Shaders", "Import")
};

struct TaskSnapshot {
    TaskId id = 0;
    TaskId parent = 0;
    std::string title, unit, status, error, category;
    u64 done = 0, total = 0;
    f32 fraction = -1.0f;       // explicit fraction when total == 0 (-1 = none)
    bool cancellable = false;
    bool cancelRequested = false;
    TaskState state = TaskState::Running;
    f64 elapsedSeconds = 0.0;

    f32 Fraction() const;                       // 0..1
    bool Indeterminate() const { return total == 0 && fraction < 0.0f; }
    std::string CountText() const;              // "144 / 200 shaders", "" without a total
    bool Finished() const { return state != TaskState::Running; }
};

class TaskManager;
struct TaskManagerShared;

// Move-only handle the worker uses to report. Thread-safe. Destroying a
// still-running token completes its task (cancelled if a cancel was asked).
class TaskToken {
public:
    TaskToken() = default;
    TaskToken(TaskToken&&) noexcept;
    TaskToken& operator=(TaskToken&&) noexcept;
    TaskToken(const TaskToken&) = delete;
    TaskToken& operator=(const TaskToken&) = delete;
    ~TaskToken();

    TaskId Id() const { return m_id; }
    bool IsValid() const { return m_shared != nullptr; }
    void Advance(u64 count = 1);
    void SetDone(u64 done);
    void SetTotal(u64 total);
    void SetFraction(f32 fraction);
    void SetStatus(std::string status);
    bool CancelRequested() const;
    // Ends the task: Completed, or Cancelled if a cancel had been requested.
    void Complete();
    void Fail(std::string error);

private:
    friend class TaskManager;
    TaskToken(std::shared_ptr<TaskManagerShared> s, TaskId id) : m_shared(std::move(s)), m_id(id) {}
    std::shared_ptr<TaskManagerShared> m_shared;
    TaskId m_id = 0;
};

class TaskManager {
public:
    TaskManager();
    ~TaskManager();

    TaskToken Begin(TaskDesc desc);

    // All tasks in creation order (running + recently finished + failed).
    std::vector<TaskSnapshot> Snapshot() const;
    u32 RunningCount() const;            // running top-level tasks
    f32 AggregateFraction() const;       // mean over running top-level determinate tasks (0 if none)

    bool RequestCancel(TaskId id);       // false when not cancellable / not running
    void Dismiss(TaskId id);             // removes a finished task now (failed ones stay until this)

    // Successfully finished / cancelled tasks are pruned after `seconds`.
    void SetLinger(f64 seconds);
    // Ages finished tasks by `dt` and prunes expired ones (call once a frame).
    void Update(f64 dt);

private:
    std::shared_ptr<TaskManagerShared> m_shared;
};

}  // namespace Luma::Tasks
