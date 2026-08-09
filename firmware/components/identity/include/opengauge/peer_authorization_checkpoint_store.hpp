#pragma once

#include <cstddef>
#include <cstdint>

#include "opengauge/peer_authorization.hpp"

namespace opengauge::identity {

inline constexpr std::uint8_t kPeerAuthorizationStoredCheckpointVersion = 0;
inline constexpr std::size_t kPeerAuthorizationStoredCheckpointBytes = 288;

enum class PeerAuthorizationCheckpointStorageError : std::uint8_t {
    none = 0,
    not_found,
    invalid_argument,
    io_failure,
};

class PeerAuthorizationCheckpointStorage {
public:
    virtual ~PeerAuthorizationCheckpointStorage() = default;
    [[nodiscard]] virtual PeerAuthorizationCheckpointStorageError read_slot(
        std::uint8_t slot, std::uint8_t* output, std::size_t size) = 0;
    [[nodiscard]] virtual PeerAuthorizationCheckpointStorageError write_slot(
        std::uint8_t slot, const std::uint8_t* data, std::size_t size) = 0;
    [[nodiscard]] virtual PeerAuthorizationCheckpointStorageError erase_slot(
        std::uint8_t slot) = 0;
};

enum class PeerAuthorizationCheckpointSlotState : std::uint8_t {
    empty = 0,
    valid,
    invalid,
    io_failure,
};

enum class PeerAuthorizationCheckpointStoreError : std::uint8_t {
    none = 0,
    no_checkpoint,
    invalid_generation,
    generation_exhausted,
    stale_generation,
    generation_conflict,
    storage_failure,
    verification_failure,
    checkpoint_rejected,
};

enum class PeerAuthorizationCheckpointSource : std::uint8_t {
    none = 0,
    slot_a,
    slot_b,
};

struct PeerAuthorizationCheckpointLoadResult {
    PeerAuthorizationCheckpointStoreError error{
        PeerAuthorizationCheckpointStoreError::no_checkpoint};
    PeerAuthorizationCheckpointSource source{
        PeerAuthorizationCheckpointSource::none};
    PeerAuthorizationCheckpointSlotState slot_a{
        PeerAuthorizationCheckpointSlotState::empty};
    PeerAuthorizationCheckpointSlotState slot_b{
        PeerAuthorizationCheckpointSlotState::empty};
    PeerAuthorizationError registry_error{PeerAuthorizationError::none};
    std::uint64_t generation{0};
    bool recovery_required{false};
    bool restored{false};
};

struct PeerAuthorizationCheckpointSaveResult {
    PeerAuthorizationCheckpointStoreError error{
        PeerAuthorizationCheckpointStoreError::storage_failure};
    PeerAuthorizationCheckpointSource written_slot{
        PeerAuthorizationCheckpointSource::none};
    PeerAuthorizationError registry_error{PeerAuthorizationError::none};
    std::uint64_t generation{0};
    bool commit_uncertain{false};

    [[nodiscard]] constexpr bool saved() const {
        return error == PeerAuthorizationCheckpointStoreError::none;
    }
};

class PeerAuthorizationCheckpointStore {
public:
    explicit PeerAuthorizationCheckpointStore(
        PeerAuthorizationCheckpointStorage& storage);

    [[nodiscard]] PeerAuthorizationCheckpointSaveResult save(
        PeerAuthorizationRegistry& registry, std::uint64_t generation);
    [[nodiscard]] PeerAuthorizationCheckpointSaveResult save_next(
        PeerAuthorizationRegistry& registry);
    [[nodiscard]] PeerAuthorizationCheckpointLoadResult restore(
        PeerAuthorizationRegistry& registry);
    [[nodiscard]] PeerAuthorizationCheckpointStoreError reset();

private:
    PeerAuthorizationCheckpointStorage& storage_;
};

}  // namespace opengauge::identity
