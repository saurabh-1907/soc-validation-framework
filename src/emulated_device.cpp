#include "soc/emulated_device.hpp"

#include <string>

namespace soc {
namespace {

constexpr uint64_t kDmaBase = 0x40000000ull;
constexpr uint64_t kDmaCtrl = kDmaBase + 0x00;
constexpr uint64_t kDmaStatus = kDmaBase + 0x04;
constexpr uint64_t kDmaIrq = kDmaBase + 0x08;
constexpr uint64_t kDmaXferLen = kDmaBase + 0x14;

constexpr uint64_t kPcieBase = 0x50000000ull;
constexpr uint64_t kPcieLinkStatus = kPcieBase + 0x00;
constexpr uint64_t kPcieLinkCtrl = kPcieBase + 0x04;
constexpr uint64_t kPcieErrStatus = kPcieBase + 0x08;

constexpr uint32_t kCtrlGo = 1u << 0;
constexpr uint32_t kStatusIdle = 1u << 0;
constexpr uint32_t kStatusBusy = 1u << 1;
constexpr uint32_t kStatusDone = 1u << 2;
constexpr uint32_t kIrqDone = 1u << 0;

}  // namespace

EmulatedDevice::EmulatedDevice() {
  // Policy table mirrors regmaps/soc.yaml. A real deployment would generate
  // this from the same YAML; it is written out here so the model can be read
  // side by side with the databook.
  policy_ = {
      {kDmaCtrl, {0x00000000u, 0x00000000u, 0x00000000u}},
      {kDmaStatus, {0x00000001u, 0xFFFFFFFFu, 0x00000000u}},  // wholly read-only
      {kDmaIrq, {0x00000000u, 0x00000000u, 0x00000007u}},     // three W1C bits
      {kDmaBase + 0x0C, {0x00000000u, 0x00000000u, 0x00000000u}},
      {kDmaBase + 0x10, {0x00000000u, 0x00000000u, 0x00000000u}},
      {kDmaXferLen, {0x00000000u, 0x00000000u, 0x00000000u}},
      {kPcieLinkStatus, {0x00000000u, 0xFFFFFFFFu, 0x00000000u}},
      {kPcieLinkCtrl, {0x00000000u, 0x00000000u, 0x00000000u}},
      {kPcieErrStatus, {0x00000000u, 0x00000000u, 0x00000007u}},
  };
  Reset();
}

void EmulatedDevice::Reset() {
  for (const auto& [address, policy] : policy_) {
    storage_[address] = policy.reset;
  }
  ticks_ = 0;
  dma_running_ = false;
  dma_done_at_ = 0;
}

const EmulatedDevice::RegPolicy& EmulatedDevice::PolicyFor(uint64_t address) const {
  const auto it = policy_.find(address);
  if (it == policy_.end()) {
    throw TransportError("no register decoded at address", address);
  }
  return it->second;
}

void EmulatedDevice::Tick() {
  ++ticks_;
  if (dma_running_ && ticks_ >= dma_done_at_) {
    // Transfer completes: BUSY drops, DONE and IDLE rise, beats are reported,
    // and the completion interrupt latches in the W1C status register.
    const uint32_t beats = (storage_[kDmaXferLen] & 0x00FFFFFFu) / 4u;
    storage_[kDmaStatus] = kStatusIdle | kStatusDone | ((beats & 0xFFFFu) << 8);
    storage_[kDmaIrq] |= kIrqDone;
    dma_running_ = false;
  }
}

uint32_t EmulatedDevice::Read32(uint64_t address) {
  const auto& policy = PolicyFor(address);
  (void)policy;
  Tick();
  return storage_[address];
}

void EmulatedDevice::Write32(uint64_t address, uint32_t value) {
  const auto& policy = PolicyFor(address);
  Tick();

  uint32_t current = storage_[address];

  // W1C first: a written 1 clears, a written 0 leaves the bit alone.
  if (policy.w1c_mask != 0) {
    current &= ~(value & policy.w1c_mask);
  }
  // Read-only bits keep their current value regardless of what was written.
  const uint32_t writable = ~(policy.ro_mask | policy.w1c_mask);
  current = (current & ~writable) | (value & writable);
  storage_[address] = current;

  // Side effects.
  if (address == kDmaCtrl && (value & kCtrlGo) != 0 && !dma_running_) {
    dma_running_ = true;
    dma_done_at_ = ticks_ + latency_;
    storage_[kDmaStatus] = kStatusBusy;  // BUSY only: not idle, not done
  }
  if (address == kPcieLinkCtrl && (value & 0x1u) != 0) {
    const uint32_t target_speed = (value >> 1) & 0x7u;
    storage_[kPcieLinkStatus] = 0x1u                   // LINK_UP
                                | (target_speed << 1)  // SPEED
                                | (16u << 4)           // WIDTH = x16
                                | (0x10u << 10);       // LTSSM = L0
  }
}

void EmulatedDevice::ReadBlock(uint64_t address, uint32_t* out, std::size_t count) {
  if (out == nullptr && count != 0) {
    throw TransportError("ReadBlock called with a null buffer", address);
  }
  for (std::size_t i = 0; i < count; ++i) {
    out[i] = Read32(address + i * 4);
  }
}

void EmulatedDevice::WriteBlock(uint64_t address, const uint32_t* in, std::size_t count) {
  if (in == nullptr && count != 0) {
    throw TransportError("WriteBlock called with a null buffer", address);
  }
  for (std::size_t i = 0; i < count; ++i) {
    Write32(address + i * 4, in[i]);
  }
}

}  // namespace soc
