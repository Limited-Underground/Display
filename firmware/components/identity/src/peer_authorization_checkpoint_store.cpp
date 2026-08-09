#include "opengauge/peer_authorization_checkpoint_store.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace opengauge::identity {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'P', 'S', '0'}};
constexpr std::size_t kHeaderBytes = 24;
constexpr std::size_t kCheckpointOffset = kHeaderBytes;
constexpr std::size_t kTailOffset =
    kCheckpointOffset + kPeerAuthorizationCheckpointBytes;
constexpr std::size_t kCrcOffset = kPeerAuthorizationStoredCheckpointBytes - 4;

void write_u16(std::uint8_t* output, std::uint16_t value) {
    output[0] = static_cast<std::uint8_t>(value);
    output[1] = static_cast<std::uint8_t>(value >> 8U);
}

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index)
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
}

void write_u64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index)
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
}

std::uint16_t read_u16(const std::uint8_t* input) {
    return static_cast<std::uint16_t>(input[0]) |
           (static_cast<std::uint16_t>(input[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index)
        value |= static_cast<std::uint32_t>(input[index]) << (index * 8U);
    return value;
}

std::uint64_t read_u64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index)
        value |= static_cast<std::uint64_t>(input[index]) << (index * 8U);
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
    return std::all_of(data, data + size,
                       [](std::uint8_t value) { return value == 0; });
}

struct StoredCheckpoint {
    std::uint64_t generation{0};
    std::array<std::uint8_t, kPeerAuthorizationCheckpointBytes> checkpoint{};
};

bool encode_record(
    const StoredCheckpoint& value,
    std::array<std::uint8_t, kPeerAuthorizationStoredCheckpointBytes>& output) {
    if (value.generation == 0) return false;
    std::array<std::uint8_t, kPeerAuthorizationStoredCheckpointBytes> candidate{};
    std::copy(kMagic.begin(), kMagic.end(), candidate.begin());
    candidate[4] = kPeerAuthorizationStoredCheckpointVersion;
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
    const std::array<std::uint8_t, kPeerAuthorizationStoredCheckpointBytes>& input,
    StoredCheckpoint& output) {
    if (!std::equal(kMagic.begin(), kMagic.end(), input.begin()) ||
        input[4] != kPeerAuthorizationStoredCheckpointVersion ||
        input[5] != kHeaderBytes ||
        read_u16(input.data() + 6) != kPeerAuthorizationCheckpointBytes ||
        read_u64(input.data() + 8) == 0 ||
        !all_zero(input.data() + 16, 8) ||
        !all_zero(input.data() + kTailOffset, kCrcOffset - kTailOffset) ||
        read_u32(input.data() + kCrcOffset) !=
            crc32(input.data(), kCrcOffset)) {
        return false;
    }
    StoredCheckpoint candidate{};
    candidate.generation = read_u64(input.data() + 8);
    std::copy(input.begin() + kCheckpointOffset,
              input.begin() + kTailOffset, candidate.checkpoint.begin());
    output = candidate;
    return true;
}

struct InspectedSlot {
    PeerAuthorizationCheckpointSlotState state{
        PeerAuthorizationCheckpointSlotState::empty};
    StoredCheckpoint value{};
    std::array<std::uint8_t, kPeerAuthorizationStoredCheckpointBytes> bytes{};
};

InspectedSlot inspect_slot(
    PeerAuthorizationCheckpointStorage& storage, std::uint8_t slot) {
    InspectedSlot result{};
    const auto read =
        storage.read_slot(slot, result.bytes.data(), result.bytes.size());
    if (read == PeerAuthorizationCheckpointStorageError::not_found)
        return result;
    if (read != PeerAuthorizationCheckpointStorageError::none) {
        result.state = PeerAuthorizationCheckpointSlotState::io_failure;
        return result;
    }
    result.state = decode_record(result.bytes, result.value)
                       ? PeerAuthorizationCheckpointSlotState::valid
                       : PeerAuthorizationCheckpointSlotState::invalid;
    return result;
}

bool generation_conflict(const InspectedSlot& a, const InspectedSlot& b) {
    return a.state == PeerAuthorizationCheckpointSlotState::valid &&
           b.state == PeerAuthorizationCheckpointSlotState::valid &&
           a.value.generation == b.value.generation && a.bytes != b.bytes;
}

PeerAuthorizationCheckpointSource source_for(std::uint8_t slot) {
    return slot == 0 ? PeerAuthorizationCheckpointSource::slot_a
                     : PeerAuthorizationCheckpointSource::slot_b;
}

std::uint64_t highest_generation(
    const InspectedSlot& a, const InspectedSlot& b) {
    std::uint64_t highest = 0;
    if (a.state == PeerAuthorizationCheckpointSlotState::valid)
        highest = a.value.generation;
    if (b.state == PeerAuthorizationCheckpointSlotState::valid)
        highest = std::max(highest, b.value.generation);
    return highest;
}

}  // namespace

PeerAuthorizationCheckpointStore::PeerAuthorizationCheckpointStore(
    PeerAuthorizationCheckpointStorage& storage)
    : storage_(storage) {}

PeerAuthorizationCheckpointSaveResult PeerAuthorizationCheckpointStore::save(
    PeerAuthorizationRegistry& registry, std::uint64_t generation) {
    if (generation == 0)
        return {PeerAuthorizationCheckpointStoreError::invalid_generation};
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    if (a.state == PeerAuthorizationCheckpointSlotState::io_failure ||
        b.state == PeerAuthorizationCheckpointSlotState::io_failure)
        return {PeerAuthorizationCheckpointStoreError::storage_failure};
    if (generation_conflict(a, b))
        return {PeerAuthorizationCheckpointStoreError::generation_conflict};
    if (generation <= highest_generation(a, b))
        return {PeerAuthorizationCheckpointStoreError::stale_generation};

    StoredCheckpoint value{};
    value.generation = generation;
    const auto exported = registry.export_checkpoint(value.checkpoint);
    if (exported != PeerAuthorizationError::none) {
        PeerAuthorizationCheckpointSaveResult result{
            PeerAuthorizationCheckpointStoreError::checkpoint_rejected};
        result.registry_error = exported;
        return result;
    }
    std::array<std::uint8_t, kPeerAuthorizationStoredCheckpointBytes> encoded{};
    if (!encode_record(value, encoded))
        return {PeerAuthorizationCheckpointStoreError::invalid_generation};

    std::uint8_t target = 0;
    if (a.state != PeerAuthorizationCheckpointSlotState::valid)
        target = 0;
    else if (b.state != PeerAuthorizationCheckpointSlotState::valid)
        target = 1;
    else
        target = a.value.generation <= b.value.generation ? 0 : 1;

    const auto write = storage_.write_slot(
        target, encoded.data(), encoded.size());
    if (write != PeerAuthorizationCheckpointStorageError::none) {
        PeerAuthorizationCheckpointSaveResult result{
            PeerAuthorizationCheckpointStoreError::storage_failure};
        result.written_slot = source_for(target);
        result.generation = generation;
        result.commit_uncertain = true;
        return result;
    }
    std::array<std::uint8_t, kPeerAuthorizationStoredCheckpointBytes> verified{};
    if (storage_.read_slot(target, verified.data(), verified.size()) !=
            PeerAuthorizationCheckpointStorageError::none ||
        verified != encoded) {
        PeerAuthorizationCheckpointSaveResult result{
            PeerAuthorizationCheckpointStoreError::verification_failure};
        result.written_slot = source_for(target);
        result.generation = generation;
        result.commit_uncertain = true;
        return result;
    }
    StoredCheckpoint decoded{};
    if (!decode_record(verified, decoded) ||
        decoded.generation != value.generation ||
        decoded.checkpoint != value.checkpoint) {
        PeerAuthorizationCheckpointSaveResult result{
            PeerAuthorizationCheckpointStoreError::verification_failure};
        result.written_slot = source_for(target);
        result.generation = generation;
        result.commit_uncertain = true;
        return result;
    }
    PeerAuthorizationCheckpointSaveResult result{
        PeerAuthorizationCheckpointStoreError::none};
    result.written_slot = source_for(target);
    result.generation = generation;
    return result;
}

PeerAuthorizationCheckpointSaveResult
PeerAuthorizationCheckpointStore::save_next(
    PeerAuthorizationRegistry& registry) {
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    if (a.state == PeerAuthorizationCheckpointSlotState::io_failure ||
        b.state == PeerAuthorizationCheckpointSlotState::io_failure)
        return {PeerAuthorizationCheckpointStoreError::storage_failure};
    if (generation_conflict(a, b))
        return {PeerAuthorizationCheckpointStoreError::generation_conflict};
    const auto highest = highest_generation(a, b);
    if (highest == std::numeric_limits<std::uint64_t>::max())
        return {PeerAuthorizationCheckpointStoreError::generation_exhausted};
    return save(registry, highest + 1);
}

PeerAuthorizationCheckpointLoadResult
PeerAuthorizationCheckpointStore::restore(
    PeerAuthorizationRegistry& registry) {
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    PeerAuthorizationCheckpointLoadResult result{};
    result.slot_a = a.state;
    result.slot_b = b.state;
    if (generation_conflict(a, b)) {
        result.error = PeerAuthorizationCheckpointStoreError::generation_conflict;
        result.recovery_required = true;
        return result;
    }

    const InspectedSlot* selected = nullptr;
    std::uint8_t selected_slot = 0;
    if (a.state == PeerAuthorizationCheckpointSlotState::valid &&
        b.state == PeerAuthorizationCheckpointSlotState::valid) {
        if (b.value.generation > a.value.generation) {
            selected = &b;
            selected_slot = 1;
        } else {
            selected = &a;
        }
    } else if (a.state == PeerAuthorizationCheckpointSlotState::valid) {
        selected = &a;
        result.recovery_required = true;
    } else if (b.state == PeerAuthorizationCheckpointSlotState::valid) {
        selected = &b;
        selected_slot = 1;
        result.recovery_required = true;
    }
    if (selected == nullptr) {
        result.error =
            a.state == PeerAuthorizationCheckpointSlotState::io_failure ||
                    b.state == PeerAuthorizationCheckpointSlotState::io_failure
                ? PeerAuthorizationCheckpointStoreError::storage_failure
                : PeerAuthorizationCheckpointStoreError::no_checkpoint;
        result.recovery_required = true;
        return result;
    }

    result.source = source_for(selected_slot);
    result.generation = selected->value.generation;
    result.error =
        a.state == PeerAuthorizationCheckpointSlotState::io_failure ||
                b.state == PeerAuthorizationCheckpointSlotState::io_failure
            ? PeerAuthorizationCheckpointStoreError::storage_failure
            : PeerAuthorizationCheckpointStoreError::none;
    result.registry_error = registry.import_checkpoint(
        selected->value.checkpoint.data(), selected->value.checkpoint.size());
    if (result.registry_error != PeerAuthorizationError::none) {
        result.error = PeerAuthorizationCheckpointStoreError::checkpoint_rejected;
        result.recovery_required = true;
        return result;
    }
    result.restored = true;
    return result;
}

PeerAuthorizationCheckpointStoreError
PeerAuthorizationCheckpointStore::reset() {
    const auto a = storage_.erase_slot(0);
    const auto b = storage_.erase_slot(1);
    return a == PeerAuthorizationCheckpointStorageError::none &&
                   b == PeerAuthorizationCheckpointStorageError::none
               ? PeerAuthorizationCheckpointStoreError::none
               : PeerAuthorizationCheckpointStoreError::storage_failure;
}

}  // namespace opengauge::identity
