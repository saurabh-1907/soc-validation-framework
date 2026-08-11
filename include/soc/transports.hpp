// Protocol transports.
//
// Each one forwards to a backing device (the emulated model in CI, a real
// adapter in the lab) and accounts for the protocol's own cost: a 32-bit
// register read is not one "operation", it is a specific number of bit-times on
// a specific clock. That accounting is what makes the coalescing benchmark
// meaningful - the win from folding six field reads into one transaction is a
// property of the link, not of the host CPU, so measuring host wall-clock alone
// would understate it by orders of magnitude.
//
// Simulated bus time is accumulated, never slept. Tests stay fast and the
// numbers stay deterministic.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "soc/transport.hpp"

namespace soc {

// Bits moved on the wire for one 32-bit register access, and the wire clock.
struct ProtocolCost {
  uint32_t bits_per_access;
  double clock_hz;
  [[nodiscard]] double ns_per_access() const {
    return (static_cast<double>(bits_per_access) / clock_hz) * 1e9;
  }
};

class ProtocolTransport : public ITransport {
 public:
  ProtocolTransport(std::unique_ptr<ITransport> backing, ProtocolCost cost, std::string label)
      : backing_(std::move(backing)), cost_(cost), label_(std::move(label)) {
    if (!backing_) {
      throw std::invalid_argument("transport requires a backing device");
    }
  }

  [[nodiscard]] uint32_t Read32(uint64_t address) override {
    simulated_ns_ += cost_.ns_per_access();
    ++accesses_;
    return backing_->Read32(address);
  }

  void Write32(uint64_t address, uint32_t value) override {
    simulated_ns_ += cost_.ns_per_access();
    ++accesses_;
    backing_->Write32(address, value);
  }

  // A burst amortises the protocol's fixed overhead across all words, which is
  // exactly why block access exists on real adapters.
  void ReadBlock(uint64_t address, uint32_t* out, std::size_t count) override {
    simulated_ns_ +=
        cost_.ns_per_access() + 0.25 * cost_.ns_per_access() * static_cast<double>(count);
    ++accesses_;
    backing_->ReadBlock(address, out, count);
  }

  void WriteBlock(uint64_t address, const uint32_t* in, std::size_t count) override {
    simulated_ns_ +=
        cost_.ns_per_access() + 0.25 * cost_.ns_per_access() * static_cast<double>(count);
    ++accesses_;
    backing_->WriteBlock(address, in, count);
  }

  [[nodiscard]] std::string name() const override { return label_; }
  [[nodiscard]] double simulated_ns() const noexcept { return simulated_ns_; }
  [[nodiscard]] uint64_t accesses() const noexcept { return accesses_; }
  void ResetCounters() noexcept {
    simulated_ns_ = 0.0;
    accesses_ = 0;
  }

 private:
  std::unique_ptr<ITransport> backing_;
  ProtocolCost cost_;
  std::string label_;
  double simulated_ns_{0.0};
  uint64_t accesses_{0};
};

// IR shift + DR shift per access, 10 MHz TCK.
class JtagTransport final : public ProtocolTransport {
 public:
  explicit JtagTransport(std::unique_ptr<ITransport> backing)
      : ProtocolTransport(std::move(backing), {40, 10e6}, "jtag") {}
};

// Address byte + 4 data bytes, 9 bits each with ACK, 400 kHz fast mode.
class I2cTransport final : public ProtocolTransport {
 public:
  explicit I2cTransport(std::unique_ptr<ITransport> backing)
      : ProtocolTransport(std::move(backing), {45, 400e3}, "i2c") {}
};

// 8-bit command + 32-bit address + 32-bit data, 25 MHz SCLK.
class SpiTransport final : public ProtocolTransport {
 public:
  explicit SpiTransport(std::unique_ptr<ITransport> backing)
      : ProtocolTransport(std::move(backing), {72, 25e6}, "spi") {}
};

// Runtime selection: the same test binary runs against emulation in CI or a
// real adapter in the lab, chosen by a config string.
std::unique_ptr<ITransport> MakeTransport(const std::string& kind);

}  // namespace soc
