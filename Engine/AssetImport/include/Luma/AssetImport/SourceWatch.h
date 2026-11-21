#pragma once

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Luma/Core/Types.h"

namespace Luma::Jobs {
class JobSystem;
class JobHandle;
}  // namespace Luma::Jobs
namespace Luma::Tasks {
class TaskManager;
}

// "Has this imported asset's source file changed?" without ever blocking a
// frame. The Content Browser asks every frame for every visible tile, so a
// query is a map lookup; the actual check (read the .meta, stat the source,
// hash only if size/mtime moved) runs on the Job System.
namespace Luma::Import {

enum class SourceState : u8 {
    Unknown,      // not checked yet (a check is queued)
    NotImported,  // no .meta, or the .meta names no source file
    UpToDate,
    Changed,
    Missing,      // the source file is gone; reimport is impossible
};

struct SourceCheckStats {
    bool hashed = false;  // the source's bytes were read (cache miss)
    // Called just before a source is hashed (cache miss), with its size.
    std::function<void(const std::filesystem::path& source, u64 bytes)> beforeHash;
};

// Synchronous check of one asset (reads its .meta). Cheap unless the source's
// size or mtime differ from the .meta, and even then each source is hashed
// once per (size, mtime) for the whole process.
SourceState CheckSource(const std::filesystem::path& assetFile, SourceCheckStats* stats = nullptr);

// FNV-1a 64 of the file, memoised by (path, size, mtime). Thread-safe.
std::string HashFileCached(const std::filesystem::path& file, SourceCheckStats* stats = nullptr);

class SourceWatch {
public:
    // Without a JobSystem, checks run inside Update() on the calling thread.
    explicit SourceWatch(Jobs::JobSystem* jobs = nullptr, Tasks::TaskManager* tasks = nullptr);
    ~SourceWatch();  // waits for checks in flight
    SourceWatch(const SourceWatch&) = delete;
    SourceWatch& operator=(const SourceWatch&) = delete;

    // Never touches the disk. Unknown on first sight (and a check is queued);
    // known results are re-checked after RecheckSeconds() in the background.
    SourceState Query(const std::filesystem::path& assetFile);
    bool IsChanged(const std::filesystem::path& assetFile) { return Query(assetFile) == SourceState::Changed; }

    // Forget results (after a scan/import/reimport) so they are checked again.
    void Invalidate(const std::filesystem::path& assetFile);
    void InvalidateAll();

    // Starts queued checks. Call once per frame on the main thread.
    void Update();
    // Blocks until no check is queued or running (tests, shutdown).
    void WaitIdle();

    static constexpr f64 RecheckSeconds() { return 5.0; }
    // Sources at least this large get a Task Manager entry while hashing.
    static constexpr u64 kLargeSourceBytes = 16ull * 1024 * 1024;

private:
    struct Entry {
        SourceState state = SourceState::Unknown;
        f64 checkedAt = -1.0;  // seconds on the watch clock; <0 = never
        bool queued = false;
        bool running = false;
        u64 generation = 0;  // bumped by Invalidate; stale results are dropped
    };
    void RunCheck(const std::string& key, u64 generation);
    f64 Now() const;

    Jobs::JobSystem* m_jobs;
    Tasks::TaskManager* m_tasks;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, Entry> m_entries;
    std::vector<std::string> m_queue;
    u32 m_running = 0;
    std::condition_variable m_idle;
};

}  // namespace Luma::Import
