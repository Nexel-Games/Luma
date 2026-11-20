#include "Luma/Core/LogBuffer.h"

#include <algorithm>

namespace Luma {

LogBuffer::LogBuffer(usize capacity) : m_capacity(std::max<usize>(capacity, 1)) {}

void LogBuffer::Write(const LogRecord& record) {
    std::lock_guard<std::mutex> lock(m_mutex);
    LogEntry e;
    e.sequence = m_nextSequence++;
    e.level = record.level;
    e.category.assign(record.category);
    e.message.assign(record.message);
    e.time = record.time;
    e.userFacing = record.userFacing;
    m_entries.push_back(std::move(e));
    while (m_entries.size() > m_capacity) m_entries.pop_front();
}

u64 LogBuffer::Fetch(u64 afterSequence, std::vector<LogEntry>& out) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_entries.empty() || m_entries.back().sequence <= afterSequence) {
        return afterSequence;
    }
    // Sequences are contiguous within the deque: jump straight to the first
    // unseen entry.
    const u64 first = m_entries.front().sequence;
    usize start = afterSequence < first
                      ? 0
                      : static_cast<usize>(afterSequence - first + 1);
    for (usize i = start; i < m_entries.size(); ++i) out.push_back(m_entries[i]);
    return m_entries.back().sequence;
}

void LogBuffer::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
}

usize LogBuffer::Size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.size();
}

u64 LogBuffer::LatestSequence() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_nextSequence - 1;
}

}  // namespace Luma
