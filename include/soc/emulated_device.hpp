// Host-side behavioural model of the sample register map.
//
// Not a stub that echoes writes back. It honours the things that actually break
// bring-up code: reset values, read-only bits that silently ignore writes, W1C
// bits that clear on a written 1, and side effects with latency (writing GO
// starts a transfer that only reports DONE some ticks later). A test that
// passes against this has exercised the same semantics it will meet on silicon.
//
// Time advances one tick per bus access, so a test that polls a status bit
// makes progress exactly as it would against hardware, with no wall-clock sleep.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "soc/transport.hpp"

namespace soc {

class EmulatedDevice final : public ITransport {
 public:
  EmulatedDevice();

  [[nodiscard]] uint32_t Read32(uint64_t address) override;
  void Write32(uint64_t address, uint32_t value) override;
  void ReadBlock(uint64_t address, uint32_t* out, std::size_t count) override;
  void WriteBlock(uint64_t address, const uint32_t* in, std::size_t count) override;
  [[nodiscard]] std::string name() const override { return "emulated"; }

  void Reset();
  [[nodiscard]] uint64_t ticks() const noexcept { return ticks_; }
  // How many ticks a DMA transfer takes before DONE appears.
  void set_transfer_latency(uint64_t ticks) noexcept { latency_ = ticks; }

 private:
  struct RegPolicy {
    uint32_t reset;
    uint32_t ro_mask;   // bits a write must not change
    uint32_t w1c_mask;  // bits cleared by writing a 1
  };

  void Tick();
  [[nodiscard]] const RegPolicy& PolicyFor(uint64_t address) const;

  std::unordered_map<uint64_t, uint32_t> storage_;
  std::unordered_map<uint64_t, RegPolicy> policy_;
  uint64_t ticks_{0};
  uint64_t latency_{8};
  uint64_t dma_done_at_{0};
  bool dma_running_{false};
};

}  // namespace soc
