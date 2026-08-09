#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>

#include "opengauge/normalized_signal.hpp"

namespace opengauge::telemetry {

inline constexpr std::size_t kTelemetryCacheCapacity = 16;

enum class CacheError : std::uint8_t {
    none = 0,
    invalid_signal,
    invalid_stale_threshold,
    capacity_full,
    out_of_order,
    conflicting_timestamp,
    not_found,
    freshness_error,
    cursor_epoch_mismatch,
    invalid_cursor,
    insufficient_output_capacity,
};

enum class CacheWriteDisposition : std::uint8_t {
    none = 0,
    inserted,
    updated,
    unchanged,
};

struct CacheCursor {
    std::uint32_t epoch{1};
    std::uint64_t generation{0};
};

struct CacheWriteResult {
    CacheError error{CacheError::invalid_signal};
    SignalModelError signal_error{SignalModelError::none};
    CacheWriteDisposition disposition{CacheWriteDisposition::none};
    CacheCursor cursor{};

    [[nodiscard]] constexpr bool accepted() const {
        return error == CacheError::none;
    }
};

struct CachedSignalSnapshot {
    NormalizedSignal signal{};
    SignalQuality effective_quality{SignalQuality::unknown};
    std::uint64_t age_ms{0};
    std::uint64_t generation{0};
};

struct CacheReadResult {
    CacheError error{CacheError::not_found};
    SignalModelError signal_error{SignalModelError::none};
    CachedSignalSnapshot snapshot{};

    [[nodiscard]] constexpr bool found() const {
        return error == CacheError::none;
    }
};

struct CacheChangesResult {
    CacheError error{CacheError::invalid_cursor};
    SignalModelError signal_error{SignalModelError::none};
    std::size_t snapshot_count{0};
    CacheCursor next_cursor{};

    [[nodiscard]] constexpr bool collected() const {
        return error == CacheError::none;
    }
};

class TelemetryCache {
public:
    [[nodiscard]] CacheWriteResult upsert(
        const NormalizedSignal& signal,
        std::uint64_t stale_after_ms);

    [[nodiscard]] CacheReadResult read(
        std::string_view signal_id,
        std::uint64_t now_ms) const;

    // Returns the latest state for every signal changed after `cursor`. This is
    // a bounded state-sync cursor, not an event log. The method also materializes
    // fresh-to-stale transitions at `now_ms` so polling subscribers observe them.
    [[nodiscard]] CacheChangesResult collect_changes(
        CacheCursor cursor,
        std::uint64_t now_ms,
        CachedSignalSnapshot* output,
        std::size_t output_capacity);

    // Clears all state and changes the cursor epoch. A subscriber holding an
    // older cursor receives cursor_epoch_mismatch and must start a full sync.
    void clear();

    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] CacheCursor current_cursor() const;

private:
    struct Entry {
        NormalizedSignal signal{};
        std::uint64_t stale_after_ms{0};
        std::uint64_t generation{0};
        SignalQuality last_effective_quality{SignalQuality::unknown};
        bool occupied{false};
    };

    [[nodiscard]] std::size_t find_index(std::string_view signal_id) const;

    mutable std::mutex mutex_{};
    std::array<Entry, kTelemetryCacheCapacity> entries_{};
    std::size_t size_{0};
    std::uint32_t epoch_{1};
    std::uint64_t generation_{0};
};

}  // namespace opengauge::telemetry
