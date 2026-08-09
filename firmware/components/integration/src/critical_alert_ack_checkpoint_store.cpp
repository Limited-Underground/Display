#include "opengauge/critical_alert_ack_checkpoint_store.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace opengauge::integration {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'A', 'S', '0'}};
constexpr std::size_t kHeaderBytes = 24;
constexpr std::size_t kCheckpointOffset = kHeaderBytes;
constexpr std::size_t kTailReservedOffset =
    kCheckpointOffset + kCriticalAlertAckCheckpointBytes;
constexpr std::size_t kCrcOffset = kCriticalAlertAckStoredCheckpointBytes - 4;

void write_u16(std::uint8_t* output, std::uint16_t value) {
    output[0] = static_cast<std::uint8_t>(value & 0xFFU);
    output[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

void write_u64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

std::uint16_t read_u16(const std::uint8_t* input) {
    return static_cast<std::uint16_t>(input[0]) |
           (static_cast<std::uint16_t>(input[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(input[index]) << (index * 8U);
    }
    return value;
}

std::uint64_t read_u64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(input[index]) << (index * 8U);
    }
    return value;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -(static_cast<std::int32_t>(crc & 1U)));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

bool all_zero(const std::uint8_t* data, std::size_t size) {
    return std::all_of(
        data, data + size, [](std::uint8_t byte) { return byte == 0; });
}

struct StoredCheckpoint {
    std::uint64_t generation{0};
    std::array<std::uint8_t, kCriticalAlertAckCheckpointBytes> checkpoint{};
};

bool encode_record(
    const StoredCheckpoint& value,
    std::array<std::uint8_t, kCriticalAlertAckStoredCheckpointBytes>& output) {
    if (value.generation == 0) {
        return false;
    }
    std::array<std::uint8_t, kCriticalAlertAckStoredCheckpointBytes> candidate{};
    std::copy(kMagic.begin(), kMagic.end(), candidate.begin());
    candidate[4] = kCriticalAlertAckStoredCheckpointVersion;
    candidate[5] = static_cast<std::uint8_t>(kHeaderBytes);
    write_u16(candidate.data() + 6,
              static_cast<std::uint16_t>(value.checkpoint.size()));
    write_u64(candidate.data() + 8, value.generation);
    std::copy(value.checkpoint.begin(), value.checkpoint.end(),
              candidate.begin() + kCheckpointOffset);
    write_u32(candidate.data() + kCrcOffset,
              crc32(candidate.data(), kCrcOffset));
    output = candidate;
    return true;
}

bool decode_record(
    const std::array<std::uint8_t, kCriticalAlertAckStoredCheckpointBytes>& input,
    StoredCheckpoint& output) {
    if (!std::equal(kMagic.begin(), kMagic.end(), input.begin()) ||
        input[4] != kCriticalAlertAckStoredCheckpointVersion ||
        input[5] != kHeaderBytes ||
        read_u16(input.data() + 6) != kCriticalAlertAckCheckpointBytes ||
        read_u64(input.data() + 8) == 0 ||
        !all_zero(input.data() + 16, 8) ||
        !all_zero(input.data() + kTailReservedOffset,
                  kCrcOffset - kTailReservedOffset) ||
        read_u32(input.data() + kCrcOffset) !=
            crc32(input.data(), kCrcOffset)) {
        return false;
    }
    StoredCheckpoint candidate{};
    candidate.generation = read_u64(input.data() + 8);
    std::copy(input.begin() + kCheckpointOffset,
              input.begin() + kTailReservedOffset,
              candidate.checkpoint.begin());
    output = candidate;
    return true;
}

struct InspectedSlot {
    CriticalAlertAckCheckpointSlotState state{
        CriticalAlertAckCheckpointSlotState::empty};
    StoredCheckpoint value{};
    std::array<std::uint8_t, kCriticalAlertAckStoredCheckpointBytes> bytes{};
};

InspectedSlot inspect_slot(
    CriticalAlertAckCheckpointStorage& storage,
    std::uint8_t slot) {
    InspectedSlot result{};
    const auto read = storage.read_slot(
        slot, result.bytes.data(), result.bytes.size());
    if (read == CriticalAlertAckCheckpointStorageError::not_found) {
        return result;
    }
    if (read != CriticalAlertAckCheckpointStorageError::none) {
        result.state = CriticalAlertAckCheckpointSlotState::io_failure;
        return result;
    }
    result.state = decode_record(result.bytes, result.value)
                       ? CriticalAlertAckCheckpointSlotState::valid
                       : CriticalAlertAckCheckpointSlotState::invalid;
    return result;
}

CriticalAlertAckCheckpointSource source_for_slot(std::uint8_t slot) {
    return slot == 0 ? CriticalAlertAckCheckpointSource::slot_a
                     : CriticalAlertAckCheckpointSource::slot_b;
}

bool generation_conflict(
    const InspectedSlot& a,
    const InspectedSlot& b) {
    return a.state == CriticalAlertAckCheckpointSlotState::valid &&
           b.state == CriticalAlertAckCheckpointSlotState::valid &&
           a.value.generation == b.value.generation && a.bytes != b.bytes;
}

}  // namespace

CriticalAlertAckCheckpointStore::CriticalAlertAckCheckpointStore(
    CriticalAlertAckCheckpointStorage& storage)
    : storage_(storage) {}

CriticalAlertAckCheckpointLoadResult
CriticalAlertAckCheckpointStore::restore(CriticalAlertAckIngress& ingress) {
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    CriticalAlertAckCheckpointLoadResult result{};
    result.slot_a = a.state;
    result.slot_b = b.state;

    if (generation_conflict(a, b)) {
        result.error =
            CriticalAlertAckCheckpointStoreError::generation_conflict;
        result.recovery_required = true;
        return result;
    }

    const InspectedSlot* selected = nullptr;
    std::uint8_t selected_slot = 0;
    if (a.state == CriticalAlertAckCheckpointSlotState::valid &&
        b.state == CriticalAlertAckCheckpointSlotState::valid) {
        if (b.value.generation > a.value.generation) {
            selected = &b;
            selected_slot = 1;
        } else {
            selected = &a;
        }
    } else if (a.state == CriticalAlertAckCheckpointSlotState::valid) {
        selected = &a;
        result.recovery_required = true;
    } else if (b.state == CriticalAlertAckCheckpointSlotState::valid) {
        selected = &b;
        selected_slot = 1;
        result.recovery_required = true;
    }

    if (selected == nullptr) {
        result.error =
            a.state == CriticalAlertAckCheckpointSlotState::io_failure ||
                    b.state == CriticalAlertAckCheckpointSlotState::io_failure
                ? CriticalAlertAckCheckpointStoreError::storage_failure
                : CriticalAlertAckCheckpointStoreError::no_checkpoint;
        result.recovery_required = true;
        return result;
    }

    result.source = source_for_slot(selected_slot);
    result.generation = selected->value.generation;
    result.error =
        a.state == CriticalAlertAckCheckpointSlotState::io_failure ||
                b.state == CriticalAlertAckCheckpointSlotState::io_failure
            ? CriticalAlertAckCheckpointStoreError::storage_failure
            : CriticalAlertAckCheckpointStoreError::none;
    result.ingress_error = ingress.import_checkpoint(
        selected->value.checkpoint.data(),
        selected->value.checkpoint.size());
    if (result.ingress_error != CriticalAlertAckIngressError::none) {
        result.error = CriticalAlertAckCheckpointStoreError::checkpoint_rejected;
        result.recovery_required = true;
        return result;
    }
    result.restored = true;
    return result;
}

CriticalAlertAckCheckpointSaveResult CriticalAlertAckCheckpointStore::save(
    CriticalAlertAckIngress& ingress,
    std::uint64_t generation) {
    if (generation == 0) {
        return {CriticalAlertAckCheckpointStoreError::invalid_generation};
    }
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    if (a.state == CriticalAlertAckCheckpointSlotState::io_failure ||
        b.state == CriticalAlertAckCheckpointSlotState::io_failure) {
        return {CriticalAlertAckCheckpointStoreError::storage_failure};
    }
    if (generation_conflict(a, b)) {
        return {CriticalAlertAckCheckpointStoreError::generation_conflict};
    }

    std::uint64_t highest_generation = 0;
    if (a.state == CriticalAlertAckCheckpointSlotState::valid) {
        highest_generation = a.value.generation;
    }
    if (b.state == CriticalAlertAckCheckpointSlotState::valid) {
        highest_generation = std::max(
            highest_generation, b.value.generation);
    }
    if (generation <= highest_generation) {
        return {CriticalAlertAckCheckpointStoreError::stale_generation};
    }

    StoredCheckpoint value{};
    value.generation = generation;
    const auto exported = ingress.export_checkpoint(value.checkpoint);
    if (exported != CriticalAlertAckIngressError::none) {
        CriticalAlertAckCheckpointSaveResult result{
            CriticalAlertAckCheckpointStoreError::checkpoint_rejected};
        result.ingress_error = exported;
        return result;
    }
    std::array<std::uint8_t, kCriticalAlertAckStoredCheckpointBytes> encoded{};
    if (!encode_record(value, encoded)) {
        return {CriticalAlertAckCheckpointStoreError::invalid_generation};
    }

    std::uint8_t target = 0;
    if (a.state != CriticalAlertAckCheckpointSlotState::valid) {
        target = 0;
    } else if (b.state != CriticalAlertAckCheckpointSlotState::valid) {
        target = 1;
    } else {
        target = a.value.generation <= b.value.generation ? 0 : 1;
    }
    if (storage_.write_slot(target, encoded.data(), encoded.size()) !=
        CriticalAlertAckCheckpointStorageError::none) {
        return {CriticalAlertAckCheckpointStoreError::storage_failure};
    }

    std::array<std::uint8_t, kCriticalAlertAckStoredCheckpointBytes> verified{};
    if (storage_.read_slot(target, verified.data(), verified.size()) !=
            CriticalAlertAckCheckpointStorageError::none ||
        verified != encoded) {
        return {CriticalAlertAckCheckpointStoreError::verification_failure};
    }
    StoredCheckpoint decoded{};
    if (!decode_record(verified, decoded) ||
        decoded.generation != value.generation ||
        decoded.checkpoint != value.checkpoint) {
        return {CriticalAlertAckCheckpointStoreError::verification_failure};
    }
    return {
        CriticalAlertAckCheckpointStoreError::none,
        source_for_slot(target),
        CriticalAlertAckIngressError::none,
        generation};
}

CriticalAlertAckCheckpointStoreError CriticalAlertAckCheckpointStore::reset() {
    const auto a = storage_.erase_slot(0);
    const auto b = storage_.erase_slot(1);
    return a == CriticalAlertAckCheckpointStorageError::none &&
                   b == CriticalAlertAckCheckpointStorageError::none
               ? CriticalAlertAckCheckpointStoreError::none
               : CriticalAlertAckCheckpointStoreError::storage_failure;
}

}  // namespace opengauge::integration
