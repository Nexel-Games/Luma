#pragma once

#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "Luma/Core/Log.h"
#include "Luma/Core/Types.h"

// In-memory log history: an ILogSink that keeps the most recent `capacity`
// records (owned copies) for UIs such as the editor's Output Log. Every
// record gets a monotonically increasing sequence number so readers can pull
// only what is new (Fetch) instead of re-copying the whole history each
// frame. Thread-safe: Write may be called from any thread.

namespace Luma {

struct LogEntry {
    u64 sequence = 0;
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string message;
    std::chrono::system_clock::time_point time;
    bool userFacing = false;  // see LogRecord::userFacing
};

class LogBuffer final : public ILogSink {
public:
    explicit LogBuffer(usize capacity = 5000);

    void Write(const LogRecord& record) override;

    // Appends to `out` every retained entry with sequence > `afterSequence`
    // (oldest first) and returns the newest sequence seen (or
    // `afterSequence` when nothing is new). Entries evicted before the reader
    // caught up are simply skipped.
    u64 Fetch(u64 afterSequence, std::vector<LogEntry>& out) const;

    // Drops every retained entry (sequence numbers keep increasing).
    void Clear();

    usize Capacity() const { return m_capacity; }
    usize Size() const;
    u64 LatestSequence() const;

private:
    mutable std::mutex m_mutex;
    std::deque<LogEntry> m_entries;
    usize m_capacity;
    u64 m_nextSequence = 1;
};

}  // namespace Luma
