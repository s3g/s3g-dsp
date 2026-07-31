#pragma once

#include <clap/ext/state.h>

#include <cstddef>
#include <cstdint>

namespace s3g::clap_state {

inline bool writeAll(
    const clap_ostream_t* stream, const void* source, size_t byteCount)
{
    if (!stream || !stream->write || (!source && byteCount > 0u)) return false;
    const auto* bytes = static_cast<const uint8_t*>(source);
    size_t written = 0u;
    while (written < byteCount) {
        const int64_t amount = stream->write(
            stream, bytes + written, byteCount - written);
        if (amount <= 0
            || static_cast<uint64_t>(amount) > byteCount - written) {
            return false;
        }
        written += static_cast<size_t>(amount);
    }
    return true;
}

inline bool readAll(
    const clap_istream_t* stream, void* destination, size_t byteCount)
{
    if (!stream || !stream->read || (!destination && byteCount > 0u)) return false;
    auto* bytes = static_cast<uint8_t*>(destination);
    size_t read = 0u;
    while (read < byteCount) {
        const int64_t amount = stream->read(
            stream, bytes + read, byteCount - read);
        if (amount <= 0
            || static_cast<uint64_t>(amount) > byteCount - read) {
            return false;
        }
        read += static_cast<size_t>(amount);
    }
    return true;
}

} // namespace s3g::clap_state
