#include "opengauge/telemetry_cache.hpp"

#include <limits>

namespace opengauge::telemetry {
namespace {

bool signal_ids_equal(const SignalId& left, const SignalId& right) {
    if (left.length != right.length) {
        return false;
    }
    for (std::size_t index = 0; index < left.length; ++index) {
        if (left.bytes[index] != right.bytes[index]) {
            return false;
        }
    }
    return true;
}

bool sources_equal(const SignalSource& left, const SignalSource& right) {
    return left.protocol == right.protocol &&
           left.bus_index == right.bus_index &&
           left.source_address == right.source_address &&
           left.parameter_group_number == right.parameter_group_number &&
           left.parameter_number == right.parameter_number &&
           left.source_address_present == right.source_address_present &&
           left.parameter_group_present == right.parameter_group_present &&
           left.parameter_present == right.parameter_present;
}

bool signals_equal(
    const NormalizedSignal& left,
    const NormalizedSignal& right) {
    return signal_ids_equal(left.id, right.id) &&
           left.value.type == right.value.type &&
           left.value.raw_value == right.value.raw_value &&
           left.value.present == right.value.present &&
           left.unit == right.unit && left.quality == right.quality &&
           sources_equal(left.source, right.source) &&
           left.received_at_ms == right.received_at_ms &&
           left.sampled_at_ms == right.sampled_at_ms &&
           left.sample_time_present == right.sample_time_present;
}

CacheCursor cursor(std::uint32_t epoch, std::uint64_t generation) {
    return {epoch, generation};
}

}  // namespace

std::size_t TelemetryCache::find_index(std::string_view signal_id) const {
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        if (entries_[index].occupied &&
            signal_id_equals(entries_[index].signal.id, signal_id)) {
            return index;
        }
    }
    return entries_.size();
}

CacheWriteResult TelemetryCache::upsert(
    const NormalizedSignal& signal,
    std::uint64_t stale_after_ms) {
    const auto validation = validate_normalized_signal(signal);
    if (validation != SignalModelError::none) {
        return {
            CacheError::invalid_signal,
            validation,
            CacheWriteDisposition::none,
            current_cursor()};
    }
    if (stale_after_ms == 0) {
        return {
            CacheError::invalid_stale_threshold,
            SignalModelError::invalid_stale_threshold,
            CacheWriteDisposition::none,
            current_cursor()};
    }

    const std::string_view id(signal.id.bytes.data(), signal.id.length);
    std::lock_guard<std::mutex> lock(mutex_);
    auto index = find_index(id);
    if (index != entries_.size()) {
        auto& existing = entries_[index];
        if (signal.received_at_ms < existing.signal.received_at_ms) {
            return {
                CacheError::out_of_order,
                SignalModelError::none,
                CacheWriteDisposition::none,
                cursor(epoch_, generation_)};
        }
        if (signal.received_at_ms == existing.signal.received_at_ms) {
            if (signals_equal(signal, existing.signal) &&
                stale_after_ms == existing.stale_after_ms) {
                return {
                    CacheError::none,
                    SignalModelError::none,
                    CacheWriteDisposition::unchanged,
                    cursor(epoch_, generation_)};
            }
            return {
                CacheError::conflicting_timestamp,
                SignalModelError::none,
                CacheWriteDisposition::none,
                cursor(epoch_, generation_)};
        }

        ++generation_;
        existing.signal = signal;
        existing.stale_after_ms = stale_after_ms;
        existing.generation = generation_;
        existing.last_effective_quality = signal.quality;
        return {
            CacheError::none,
            SignalModelError::none,
            CacheWriteDisposition::updated,
            cursor(epoch_, generation_)};
    }

    if (size_ == entries_.size()) {
        return {
            CacheError::capacity_full,
            SignalModelError::none,
            CacheWriteDisposition::none,
            cursor(epoch_, generation_)};
    }
    for (index = 0; index < entries_.size(); ++index) {
        if (!entries_[index].occupied) {
            break;
        }
    }

    ++generation_;
    entries_[index] = {
        signal,
        stale_after_ms,
        generation_,
        signal.quality,
        true};
    ++size_;
    return {
        CacheError::none,
        SignalModelError::none,
        CacheWriteDisposition::inserted,
        cursor(epoch_, generation_)};
}

CacheReadResult TelemetryCache::read(
    std::string_view signal_id,
    std::uint64_t now_ms) const {
    SignalId validated_id{};
    const auto id_error = make_signal_id(signal_id, validated_id);
    if (id_error != SignalModelError::none) {
        return {CacheError::invalid_signal, id_error, {}};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto index = find_index(signal_id);
    if (index == entries_.size()) {
        return {CacheError::not_found, SignalModelError::none, {}};
    }

    const auto& entry = entries_[index];
    const auto freshness = evaluate_signal_freshness(
        entry.signal,
        now_ms,
        entry.stale_after_ms);
    if (!freshness.evaluated()) {
        return {CacheError::freshness_error, freshness.error, {}};
    }
    return {
        CacheError::none,
        SignalModelError::none,
        {
            entry.signal,
            freshness.effective_quality,
            freshness.age_ms,
            entry.generation}};
}

CacheChangesResult TelemetryCache::collect_changes(
    CacheCursor requested,
    std::uint64_t now_ms,
    CachedSignalSnapshot* output,
    std::size_t output_capacity) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto current = cursor(epoch_, generation_);
    if (requested.epoch != epoch_) {
        return {
            CacheError::cursor_epoch_mismatch,
            SignalModelError::none,
            0,
            current};
    }
    if (requested.generation > generation_) {
        return {
            CacheError::invalid_cursor,
            SignalModelError::none,
            0,
            current};
    }

    std::array<SignalFreshnessResult, kTelemetryCacheCapacity> freshness{};
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        if (!entries_[index].occupied) {
            continue;
        }
        freshness[index] = evaluate_signal_freshness(
            entries_[index].signal,
            now_ms,
            entries_[index].stale_after_ms);
        if (!freshness[index].evaluated()) {
            return {
                CacheError::freshness_error,
                freshness[index].error,
                0,
                current};
        }
    }

    for (std::size_t index = 0; index < entries_.size(); ++index) {
        auto& entry = entries_[index];
        if (!entry.occupied ||
            freshness[index].effective_quality ==
                entry.last_effective_quality) {
            continue;
        }
        ++generation_;
        entry.generation = generation_;
        entry.last_effective_quality =
            freshness[index].effective_quality;
    }

    std::size_t change_count = 0;
    for (const auto& entry : entries_) {
        if (entry.occupied && entry.generation > requested.generation) {
            ++change_count;
        }
    }
    const auto next = cursor(epoch_, generation_);
    if (change_count > output_capacity ||
        (change_count != 0 && output == nullptr)) {
        return {
            CacheError::insufficient_output_capacity,
            SignalModelError::none,
            0,
            next};
    }

    std::size_t output_index = 0;
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        const auto& entry = entries_[index];
        if (!entry.occupied ||
            entry.generation <= requested.generation) {
            continue;
        }
        output[output_index] = {
            entry.signal,
            freshness[index].effective_quality,
            freshness[index].age_ms,
            entry.generation};
        ++output_index;
    }
    return {
        CacheError::none,
        SignalModelError::none,
        output_index,
        next};
}

void TelemetryCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_ = {};
    size_ = 0;
    generation_ = 0;
    if (epoch_ == std::numeric_limits<std::uint32_t>::max()) {
        epoch_ = 1;
    } else {
        ++epoch_;
    }
}

std::size_t TelemetryCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

CacheCursor TelemetryCache::current_cursor() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cursor(epoch_, generation_);
}

}  // namespace opengauge::telemetry
