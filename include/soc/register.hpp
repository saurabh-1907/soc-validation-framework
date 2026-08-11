// Typed register access built on top of a transport.
//
// The central performance idea: a register value is a plain 32-bit word held in
// a value type, and every field accessor operates on that word. So reading six
// fields costs ONE bus transaction, not six. On a JTAG link where a single
// 32-bit read is tens of microseconds, that difference dominates suite runtime -
// see benchmarks/RESULTS.md for the measured effect.
#pragma once

#include <cstdint>
#include <utility>

#include "soc/transport.hpp"

namespace soc {

enum class Access { RW, RO, W1C };

template <typename Reg>
class RegisterAccessor {
 public:
  explicit RegisterAccessor(ITransport& transport) noexcept : transport_(&transport) {}

  // One transaction. All field getters then read the cached word.
  [[nodiscard]] Reg Read() const {
    Reg reg;
    reg.raw = transport_->Read32(Reg::kAddress);
    return reg;
  }

  void Write(const Reg& reg) {
    static_assert(Reg::kAccess != Access::RO,
                  "this register is read-only in the databook; writing it is a bug");
    transport_->Write32(Reg::kAddress, reg.raw);
  }

  // Read-modify-write.
  //
  // NOT atomic with respect to the device. Between the Read and the Write the
  // hardware may update bits on its own - a W1C status bit set by an interrupt,
  // a counter advancing - and this will write back the stale value, silently
  // undoing it. That race is real on every SoC. The mitigations, in order of
  // preference: use a dedicated set/clear register if the block provides one;
  // hold a lock that also excludes the interrupt handler; or narrow the window
  // and re-verify. Never RMW a register that contains W1C bits you are not
  // deliberately clearing - see WriteOnly() below.
  template <typename Fn>
  Reg Modify(Fn&& mutate) {
    Reg reg = Read();
    std::forward<Fn>(mutate)(reg);
    Write(reg);
    return reg;
  }

  // Write a freshly reset value without reading first. This is the correct way
  // to touch a register containing W1C bits: a read-modify-write would read the
  // currently-set status bits and write them straight back, clearing whichever
  // ones happened to be set - an interrupt lost to a helper function that
  // looked like it only changed one unrelated field.
  template <typename Fn>
  Reg WriteOnly(Fn&& mutate) {
    Reg reg;  // starts at kReset, not at whatever the device currently holds
    std::forward<Fn>(mutate)(reg);
    Write(reg);
    return reg;
  }

  [[nodiscard]] static constexpr uint64_t address() noexcept { return Reg::kAddress; }
  [[nodiscard]] static constexpr const char* name() noexcept { return Reg::kName; }

 private:
  ITransport* transport_;
};

template <typename Reg>
[[nodiscard]] RegisterAccessor<Reg> Access_(ITransport& transport) noexcept {
  return RegisterAccessor<Reg>(transport);
}

}  // namespace soc
