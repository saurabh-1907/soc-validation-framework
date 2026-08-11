// Bus transport abstraction.
//
// Error policy: every operation either succeeds or throws TransportError. There
// is no status code a caller can forget to check and no sentinel value that
// could be mistaken for data - on a real bus 0xFFFFFFFF is both a plausible
// register value and the classic "nothing responded" pattern, so returning it
// to signal failure would make a dead device indistinguishable from a live one.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace soc {

class TransportError : public std::runtime_error {
 public:
  TransportError(std::string what, uint64_t address)
      : std::runtime_error(std::move(what)), address_(address) {}
  [[nodiscard]] uint64_t address() const noexcept { return address_; }

 private:
  uint64_t address_;
};

class ITransport {
 public:
  ITransport() = default;
  virtual ~ITransport() = default;
  ITransport(const ITransport&) = delete;
  ITransport& operator=(const ITransport&) = delete;
  ITransport(ITransport&&) = delete;
  ITransport& operator=(ITransport&&) = delete;

  [[nodiscard]] virtual uint32_t Read32(uint64_t address) = 0;
  virtual void Write32(uint64_t address, uint32_t value) = 0;

  // Block helpers exist so a burst is one protocol operation rather than N.
  // Pointer + count rather than a container: this is C++17, and a bus API is
  // one of the few places where a raw non-owning pointer is the honest type.
  // Ownership stays with the caller; these never allocate or free.
  virtual void ReadBlock(uint64_t address, uint32_t* out, std::size_t count) = 0;
  virtual void WriteBlock(uint64_t address, const uint32_t* in, std::size_t count) = 0;

  [[nodiscard]] virtual std::string name() const = 0;
};

// Owning RAII wrapper for a host-side mapped register window.
//
// Deliberately manages its own buffer with new[]/delete[] rather than using a
// std::vector: the rule of five below is the point of the exercise. Copy is
// deleted because two objects owning one allocation is the bug this class
// exists to prevent; move transfers ownership and leaves the source empty but
// destructible, which is what makes it safe to return one from a factory.
class MappedRegion {
 public:
  MappedRegion() noexcept = default;

  MappedRegion(ITransport& transport, uint64_t base, std::size_t words)
      : data_(new uint32_t[words]()), words_(words), base_(base) {
    try {
      transport.ReadBlock(base, data_, words_);
    } catch (...) {
      delete[] data_;  // no leak if the fill throws part way through
      data_ = nullptr;
      words_ = 0;
      throw;
    }
  }

  ~MappedRegion() { delete[] data_; }

  MappedRegion(const MappedRegion&) = delete;
  MappedRegion& operator=(const MappedRegion&) = delete;

  MappedRegion(MappedRegion&& other) noexcept
      : data_(std::exchange(other.data_, nullptr)),
        words_(std::exchange(other.words_, 0)),
        base_(std::exchange(other.base_, 0)) {}

  MappedRegion& operator=(MappedRegion&& other) noexcept {
    if (this != &other) {  // self-move must not free our own buffer
      delete[] data_;
      data_ = std::exchange(other.data_, nullptr);
      words_ = std::exchange(other.words_, 0);
      base_ = std::exchange(other.base_, 0);
    }
    return *this;
  }

  [[nodiscard]] std::size_t size() const noexcept { return words_; }
  [[nodiscard]] uint64_t base() const noexcept { return base_; }
  [[nodiscard]] bool empty() const noexcept { return words_ == 0; }

  [[nodiscard]] uint32_t at(std::size_t index) const {
    if (index >= words_) {
      throw std::out_of_range("MappedRegion index out of range");
    }
    return data_[index];
  }

  // Non-owning view. Valid only while this object is alive - the whole point
  // of the class is that the buffer has exactly one owner.
  [[nodiscard]] const uint32_t* data() const noexcept { return data_; }

 private:
  uint32_t* data_{nullptr};  // owning
  std::size_t words_{0};
  uint64_t base_{0};
};

// Owns a transport for the lifetime of a test session.
class DeviceHandle {
 public:
  explicit DeviceHandle(std::unique_ptr<ITransport> transport)
      : transport_(std::move(transport)) {
    if (!transport_) {
      throw std::invalid_argument("DeviceHandle requires a transport");
    }
  }

  [[nodiscard]] ITransport& transport() noexcept { return *transport_; }
  [[nodiscard]] MappedRegion Map(uint64_t base, std::size_t words) {
    return MappedRegion(*transport_, base, words);
  }

 private:
  std::unique_ptr<ITransport> transport_;
};

}  // namespace soc
