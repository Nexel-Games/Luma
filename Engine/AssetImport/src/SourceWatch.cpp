#include "Luma/AssetImport/SourceWatch.h"

#include <chrono>
#include <system_error>

#include "Luma/AssetImport/ImportMetadata.h"
#include "Luma/Core/Log.h"
#include "Luma/Jobs/JobSystem.h"
#include "Luma/Tasks/TaskManager.h"

namespace Luma::Import {
namespace {

struct Stamp {
    bool exists = false;
    u64 size = 0;
    i64 mtime = 0;
};

Stamp StatFile(const std::filesystem::path& file) {
    Stamp stamp;
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error) || error) return stamp;
    stamp.size = static_cast<u64>(std::filesystem::file_size(file, error));
    if (error) return stamp;
    const auto time = std::filesystem::last_write_time(file, error);
    if (error) return stamp;
    stamp.mtime = static_cast<i64>(time.time_since_epoch().count());
    stamp.exists = true;
    return stamp;
}

std::string Key(const std::filesystem::path& file) {
    return file.lexically_normal().generic_string();
}

struct HashMemo {
    u64 size = 0;
    i64 mtime = 0;
    std::string hash;
};

std::mutex& MemoMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, HashMemo>& Memo() {
    static std::unordered_map<std::string, HashMemo> memo;
    return memo;
}

}  // namespace

std::string HashFileCached(const std::filesystem::path& file, SourceCheckStats* stats) {
    const Stamp stamp = StatFile(file);
    if (!stamp.exists) return {};
    const std::string key = Key(file);
    {
        std::lock_guard lock(MemoMutex());
        const auto it = Memo().find(key);
        if (it != Memo().end() && it->second.size == stamp.size && it->second.mtime == stamp.mtime) {
            return it->second.hash;
        }
    }
    if (stats != nullptr) {
        stats->hashed = true;
        if (stats->beforeHash) stats->beforeHash(file, stamp.size);
    }
    std::string hash = HashFile(file);
    if (!hash.empty()) {
        std::lock_guard lock(MemoMutex());
        Memo()[key] = HashMemo{stamp.size, stamp.mtime, hash};
    }
    return hash;
}

SourceState CheckSource(const std::filesystem::path& assetFile, SourceCheckStats* stats) {
    const std::optional<ImportMetadata> meta = ReadMetadata(assetFile);
    if (!meta || meta->sourceFile.empty()) return SourceState::NotImported;
    const std::filesystem::path source = meta->sourceFile;
    const Stamp stamp = StatFile(source);
    if (!stamp.exists) return SourceState::Missing;
    // Cheapest first: a different size is a different file.
    if (meta->sourceSize >= 0 && static_cast<u64>(meta->sourceSize) != stamp.size) {
        return SourceState::Changed;
    }
    if (stamp.mtime == meta->sourceMtime) return SourceState::UpToDate;
    // The timestamp moved (copied, touched, re-saved): only the bytes can tell.
    if (meta->sourceHash.empty()) return SourceState::Changed;
    return HashFileCached(source, stats) == meta->sourceHash ? SourceState::UpToDate : SourceState::Changed;
}

SourceWatch::SourceWatch(Jobs::JobSystem* jobs, Tasks::TaskManager* tasks) : m_jobs(jobs), m_tasks(tasks) {}

SourceWatch::~SourceWatch() {
    std::unique_lock lock(m_mutex);
    m_queue.clear();
    m_idle.wait(lock, [this] { return m_running == 0; });
}

f64 SourceWatch::Now() const {
    using namespace std::chrono;
    return duration<f64>(steady_clock::now().time_since_epoch()).count();
}

SourceState SourceWatch::Query(const std::filesystem::path& assetFile) {
    const std::string key = Key(assetFile);
    std::lock_guard lock(m_mutex);
    Entry& entry = m_entries[key];
    const bool stale = entry.checkedAt < 0.0 || Now() - entry.checkedAt > RecheckSeconds();
    if (stale && !entry.queued && !entry.running) {
        entry.queued = true;
        m_queue.push_back(key);
    }
    return entry.state;
}

void SourceWatch::Invalidate(const std::filesystem::path& assetFile) {
    std::lock_guard lock(m_mutex);
    const auto it = m_entries.find(Key(assetFile));
    if (it == m_entries.end()) return;
    it->second.state = SourceState::Unknown;
    it->second.checkedAt = -1.0;
    ++it->second.generation;
}

void SourceWatch::InvalidateAll() {
    std::lock_guard lock(m_mutex);
    for (auto& [key, entry] : m_entries) {
        entry.state = SourceState::Unknown;
        entry.checkedAt = -1.0;
        ++entry.generation;
    }
}

void SourceWatch::Update() {
    std::vector<std::pair<std::string, u64>> start;
    {
        std::lock_guard lock(m_mutex);
        start.reserve(m_queue.size());
        for (const std::string& key : m_queue) {
            Entry& entry = m_entries[key];
            entry.queued = false;
            entry.running = true;
            ++m_running;
            start.emplace_back(key, entry.generation);
        }
        m_queue.clear();
    }
    for (auto& [key, generation] : start) {
        if (m_jobs != nullptr) {
            Jobs::JobDesc desc;
            desc.name = "Check import source";
            desc.priority = Jobs::Priority::Background;
            m_jobs->Schedule(std::move(desc), [this, key, generation] { RunCheck(key, generation); });
        } else {
            RunCheck(key, generation);
        }
    }
}

void SourceWatch::RunCheck(const std::string& key, u64 generation) {
    SourceState state = SourceState::Unknown;
    try {
        Tasks::TaskToken token;
        SourceCheckStats stats;
        stats.beforeHash = [&](const std::filesystem::path& source, u64 bytes) {
            if (m_tasks == nullptr || bytes < kLargeSourceBytes) return;
            Tasks::TaskDesc desc;
            desc.title = "Checking " + source.filename().string() + " for changes";
            desc.category = "Import";
            token = m_tasks->Begin(std::move(desc));
        };
        state = CheckSource(std::filesystem::path(key), &stats);
        if (token.IsValid()) token.Complete();
    } catch (const std::exception& e) {
        LUMA_LOG_WARN("Import", "Source check for '{}' failed: {}", key, e.what());
    }
    std::lock_guard lock(m_mutex);
    Entry& entry = m_entries[key];
    if (entry.generation == generation) {
        entry.state = state;
        entry.checkedAt = Now();
    }
    entry.running = false;
    --m_running;
    m_idle.notify_all();
}

void SourceWatch::WaitIdle() {
    for (;;) {
        {
            std::unique_lock lock(m_mutex);
            m_idle.wait(lock, [this] { return m_running == 0; });
            if (m_queue.empty()) return;
        }
        Update();
    }
}

}  // namespace Luma::Import
