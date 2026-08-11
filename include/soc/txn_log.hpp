// Transaction log: every bus access, timestamped.
//
// This is the artefact that makes a post-silicon failure debuggable. A test that
// fails on the bench and passes in emulation is a diff between two of these
// files - same sequence of reads and writes, or not. Without it the only
// evidence is "it failed", which on real silicon is close to useless.
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "soc/transport.hpp"

namespace soc {

enum class Op { Read32, Write32, ReadBlock, WriteBlock };

struct Transaction {
  uint64_t timestamp_ns;
  Op op;
  uint64_t address;
  uint32_t value;
  std::string test;  // which test issued it
};

class TransactionLog {
 public:
  void Record(Op op, uint64_t address, uint32_t value, const std::string& test);
  [[nodiscard]] std::vector<Transaction> Snapshot() const;
  [[nodiscard]] std::size_t size() const;
  void Clear();
  // CSV so the pandas analysis script can read it without a parser.
  void WriteCsv(const std::string& path) const;

 private:
  mutable std::mutex mutex_;
  std::vector<Transaction> records_;
};

// Decorator: adds logging to any transport without the transport knowing.
// Keeps the protocol implementations free of observability concerns, and means
// a new transport gets logging for nothing.
class LoggedTransport final : public ITransport {
 public:
  LoggedTransport(ITransport& inner, TransactionLog& log) noexcept
      : inner_(&inner), log_(&log) {}

  void set_test(std::string test) { test_ = std::move(test); }

  [[nodiscard]] uint32_t Read32(uint64_t address) override;
  void Write32(uint64_t address, uint32_t value) override;
  void ReadBlock(uint64_t address, uint32_t* out, std::size_t count) override;
  void WriteBlock(uint64_t address, const uint32_t* in, std::size_t count) override;
  [[nodiscard]] std::string name() const override;

 private:
  ITransport* inner_;
  TransactionLog* log_;
  std::string test_{"<none>"};
};

}  // namespace soc
