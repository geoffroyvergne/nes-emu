#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace nes {

// Minimal binary serialization helpers used for save states. Every
// saveState()/loadState() pair across the core (Cpu6502, Ppu2C02, Apu2A03,
// Mapper, Cartridge, Bus) reads/writes in the same fixed order, so the
// overall save-state format is just the concatenation of each component's
// fields - no versioning/schema beyond "both sides agree on the order",
// which is enforced by construction since it's all in-tree code.
class StateWriter {
public:
    template <typename T>
    void write(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "StateWriter::write requires a trivially copyable type");
        const auto* p = reinterpret_cast<const uint8_t*>(&value);
        buffer_.insert(buffer_.end(), p, p + sizeof(T));
    }

    template <typename T, size_t N>
    void writeArray(const std::array<T, N>& arr) {
        static_assert(std::is_trivially_copyable_v<T>, "StateWriter::writeArray requires a trivially copyable type");
        const auto* p = reinterpret_cast<const uint8_t*>(arr.data());
        buffer_.insert(buffer_.end(), p, p + arr.size() * sizeof(T));
    }

    // For buffers whose size is already known/fixed by construction (e.g.
    // Cartridge's PRG RAM) rather than needing its own length prefix.
    void writeBytes(const uint8_t* data, size_t n) { buffer_.insert(buffer_.end(), data, data + n); }

    const std::vector<uint8_t>& buffer() const { return buffer_; }

private:
    std::vector<uint8_t> buffer_;
};

// Throws std::runtime_error if a read would run past the end of the buffer
// (e.g. a save state from an incompatible build) rather than reading
// garbage or overflowing.
class StateReader {
public:
    explicit StateReader(const std::vector<uint8_t>& buffer) : buffer_(buffer) {}

    template <typename T>
    T read() {
        static_assert(std::is_trivially_copyable_v<T>, "StateReader::read requires a trivially copyable type");
        T value{};
        readBytes(reinterpret_cast<uint8_t*>(&value), sizeof(T));
        return value;
    }

    template <typename T, size_t N>
    void readArray(std::array<T, N>& arr) {
        static_assert(std::is_trivially_copyable_v<T>, "StateReader::readArray requires a trivially copyable type");
        readBytes(reinterpret_cast<uint8_t*>(arr.data()), arr.size() * sizeof(T));
    }

    // Counterpart to StateWriter::writeBytes - `out` must already be sized
    // to exactly what was written (the format has no length prefixes).
    void readBytes(uint8_t* out, size_t n) {
        if (pos_ + n > buffer_.size()) {
            throw std::runtime_error("StateReader: save state is truncated or incompatible");
        }
        std::memcpy(out, buffer_.data() + pos_, n);
        pos_ += n;
    }

private:
    const std::vector<uint8_t>& buffer_;
    size_t pos_ = 0;
};

} // namespace nes
