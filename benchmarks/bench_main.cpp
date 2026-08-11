// Measures the two things that dominate post-silicon suite runtime:
//   1. serial vs dependency-parallel execution        -> wall clock
//   2. naive per-field reads vs coalesced reads       -> bus transactions
//
// Writes benchmarks/RESULTS.md from what it measured. Nothing here is a
// hard-coded figure.
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "soc/emulated_device.hpp"
#include "soc/register.hpp"
#include "soc/sequencer.hpp"
#include "soc/soc_registers.hpp"
#include "soc/transports.hpp"

using namespace soc;

namespace {

constexpr int kTests = 24;
constexpr int kFieldReadIterations = 20000;

double MillisSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
      .count();
}

// A test that does a little real work so parallelism has something to overlap.
void Workload() {
  EmulatedDevice device;
  RegisterAccessor<regs::DMA_CTRL> ctrl(device);
  RegisterAccessor<regs::DMA_STATUS> status(device);
  device.set_transfer_latency(4);
  ctrl.WriteOnly([](auto& r) { r.set_GO(1).set_BURST_LEN(8); });
  for (int i = 0; i < 200; ++i) {
    if (status.Read().DONE() != 0u) {
      break;
    }
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(4));
}

Sequencer BuildSuite() {
  Sequencer seq;
  // A shape typical of bring-up: a couple of gating tests, then a wide fan-out
  // of independent ones.
  seq.Add({"link_up", {}, Workload, std::chrono::seconds(10), nullptr, nullptr});
  seq.Add({"dma_reset", {}, Workload, std::chrono::seconds(10), nullptr, nullptr});
  for (int i = 0; i < kTests; ++i) {
    seq.Add({"dma_case_" + std::to_string(i),
             {"link_up", "dma_reset"},
             Workload,
             std::chrono::seconds(10),
             nullptr,
             nullptr});
  }
  return seq;
}

struct SuiteTiming {
  double serial_ms;
  double parallel_ms;
  unsigned threads;
};

SuiteTiming BenchmarkSuite() {
  auto serial_suite = BuildSuite();
  const auto t0 = std::chrono::steady_clock::now();
  const auto serial_results = serial_suite.Run(1);
  const double serial_ms = MillisSince(t0);

  const unsigned threads = std::max(2u, std::thread::hardware_concurrency());
  auto parallel_suite = BuildSuite();
  const auto t1 = std::chrono::steady_clock::now();
  const auto parallel_results = parallel_suite.Run(threads);
  const double parallel_ms = MillisSince(t1);

  for (const auto& r : serial_results) {
    if (r.status != TestStatus::Pass) {
      std::cerr << "benchmark workload failed: " << r.name << " " << r.message << "\n";
    }
  }
  (void)parallel_results;
  return {serial_ms, parallel_ms, threads};
}

struct AccessTiming {
  uint64_t naive_accesses;
  uint64_t coalesced_accesses;
  double naive_bus_us;
  double coalesced_bus_us;
  double naive_wall_ms;
  double coalesced_wall_ms;
};

// DMA_STATUS has four fields. Naive code re-reads the register for each one;
// coalesced code reads once and decodes locally.
AccessTiming BenchmarkFieldReads() {
  AccessTiming out{};

  {
    JtagTransport jtag(std::make_unique<EmulatedDevice>());
    RegisterAccessor<regs::DMA_STATUS> status(jtag);
    jtag.ResetCounters();
    const auto t0 = std::chrono::steady_clock::now();
    uint32_t sink = 0;
    for (int i = 0; i < kFieldReadIterations; ++i) {
      sink += status.Read().IDLE();
      sink += status.Read().BUSY();
      sink += status.Read().DONE();
      sink += status.Read().BEATS();
    }
    out.naive_wall_ms = MillisSince(t0);
    out.naive_accesses = jtag.accesses();
    out.naive_bus_us = jtag.simulated_ns() / 1000.0;
    if (sink == 0xFFFFFFFFu) {
      std::cout << "";  // keep the loop from being optimised away
    }
  }
  {
    JtagTransport jtag(std::make_unique<EmulatedDevice>());
    RegisterAccessor<regs::DMA_STATUS> status(jtag);
    jtag.ResetCounters();
    const auto t0 = std::chrono::steady_clock::now();
    uint32_t sink = 0;
    for (int i = 0; i < kFieldReadIterations; ++i) {
      const auto snapshot = status.Read();  // ONE transaction
      sink += snapshot.IDLE();
      sink += snapshot.BUSY();
      sink += snapshot.DONE();
      sink += snapshot.BEATS();
    }
    out.coalesced_wall_ms = MillisSince(t0);
    out.coalesced_accesses = jtag.accesses();
    out.coalesced_bus_us = jtag.simulated_ns() / 1000.0;
    if (sink == 0xFFFFFFFFu) {
      std::cout << "";
    }
  }
  return out;
}

std::string Fixed(double value, int places = 2) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(places) << value;
  return oss.str();
}

}  // namespace

int main(int argc, char** argv) {
  const std::string output = (argc > 1) ? argv[1] : "benchmarks/RESULTS.md";

  std::cout << "running suite benchmark...\n";
  const auto suite = BenchmarkSuite();
  std::cout << "running register-access benchmark...\n";
  const auto access = BenchmarkFieldReads();

  const double suite_speedup =
      suite.parallel_ms > 0.0 ? suite.serial_ms / suite.parallel_ms : 0.0;
  const double bus_speedup =
      access.coalesced_bus_us > 0.0 ? access.naive_bus_us / access.coalesced_bus_us : 0.0;

  std::ostringstream md;
  md << "# Benchmark results\n\n"
     << "Measured on this machine by `soc_benchmark`, which writes this file "
     << "from its own timings. No figure here is hand-written.\n\n"
     << "Reproduce with:\n\n```bash\n./scripts/build.sh && ./build/soc_benchmark\n```\n\n"
     << "- Hardware threads available: " << std::thread::hardware_concurrency() << "\n"
     << "- Suite size: " << (kTests + 2) << " tests (2 gating, " << kTests << " independent)\n"
     << "- Register-read iterations: " << kFieldReadIterations << "\n\n";

  md << "## 1. Serial vs dependency-parallel execution\n\n"
     << "| | wall time |\n|---|---:|\n"
     << "| serial (`threads=1`) | " << Fixed(suite.serial_ms) << " ms |\n"
     << "| parallel (`threads=" << suite.threads << "`) | " << Fixed(suite.parallel_ms)
     << " ms |\n"
     << "| **speed-up** | **" << Fixed(suite_speedup) << "x** |\n\n"
     << "The suite is two gating tests followed by " << kTests
     << " independent ones. The sequencer topologically sorts them into two "
        "levels and runs the wide level concurrently; the gating level cannot "
        "be parallelised, which is why the speed-up is below the thread count.\n\n";

  md << "## 2. Naive per-field reads vs one coalesced read\n\n"
     << "Reading the four fields of `DMA_STATUS`, over the JTAG cost model "
        "(40 bit-times per access at 10 MHz TCK).\n\n"
     << "| | naive | coalesced | |\n|---|---:|---:|---:|\n"
     << "| bus transactions | " << access.naive_accesses << " | " << access.coalesced_accesses
     << " | **"
     << Fixed(static_cast<double>(access.naive_accesses) /
              static_cast<double>(access.coalesced_accesses))
     << "x fewer** |\n"
     << "| simulated bus time | " << Fixed(access.naive_bus_us / 1000.0) << " ms | "
     << Fixed(access.coalesced_bus_us / 1000.0) << " ms | **" << Fixed(bus_speedup)
     << "x faster** |\n"
     << "| host wall time | " << Fixed(access.naive_wall_ms) << " ms | "
     << Fixed(access.coalesced_wall_ms) << " ms | |\n\n";

  md << "### Reading these numbers honestly\n\n"
     << "The **bus transaction count** is the real result. It is exact, it is a "
        "property of the code rather than of this laptop, and on a physical "
        "JTAG link it is what determines runtime.\n\n"
     << "**Simulated bus time** is that count multiplied by the protocol cost "
        "model in `include/soc/transports.hpp`. It is arithmetic on the model, "
        "not a measurement of real hardware - treat it as an estimate of what "
        "the saving would be on a real adapter.\n\n"
     << "**Host wall time** is measured but nearly meaningless here: against an "
        "in-process emulated device a register read is a hash lookup, so the "
        "numbers reflect the emulator's cost, not a bus. It is reported only so "
        "the gap between it and the bus figure is visible.\n\n"
     << "The claim this supports is \"reduced bus transactions by "
     << Fixed(static_cast<double>(access.naive_accesses) /
              static_cast<double>(access.coalesced_accesses))
     << "x for multi-field register reads\", not a wall-clock speed-up on "
        "silicon that was never measured.\n";

  std::ofstream file(output);
  if (!file) {
    std::cerr << "error: cannot write " << output << "\n";
    return 1;
  }
  file << md.str();

  std::cout << "\n" << md.str() << "\nwrote " << output << "\n";
  return 0;
}
