#pragma once

#include <cstddef>
#include <cstdint>

namespace opengauge::storage {

enum class KeyValueBlobBackendError : std::uint8_t {
    none = 0,
    not_found,
    invalid_argument,
    io_failure,
};

// Backend writes and erases stage one mutation. commit() makes that mutation
// durable. One adapter instance must exclusively own its backend transaction.
class KeyValueBlobBackend {
public:
    virtual ~KeyValueBlobBackend() = default;

    [[nodiscard]] virtual KeyValueBlobBackendError read_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t& actual_size) = 0;
    [[nodiscard]] virtual KeyValueBlobBackendError write_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        const std::uint8_t* data,
        std::size_t size) = 0;
    [[nodiscard]] virtual KeyValueBlobBackendError erase_key(
        const char* partition_label,
        const char* namespace_name,
        const char* key) = 0;
    [[nodiscard]] virtual KeyValueBlobBackendError commit(
        const char* partition_label,
        const char* namespace_name) = 0;
};

}  // namespace opengauge::storage
