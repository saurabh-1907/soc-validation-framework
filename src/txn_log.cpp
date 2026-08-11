#include "soc/txn_log.hpp"

#include <chrono>
#include <fstream>
#include <stdexcept>

namespace soc {
namespace {

uint64_t NowNs() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

const char* OpName(Op op) {
  switch (op) {
    case Op::Read32:
      return "read32";
    case Op::Write32:
      return "write32";
    case Op::ReadBlock:
      return "read_block";
    case Op::WriteBlock:
      return "write_block";
  }
  return "unknown";
}

}  // namespace

void TransactionLog::Record(Op op, uint64_t address, uint32_t value, const std::string& test) {
  const std::lock_guard<std::mutex> lock(mutex_);
  records_.push_back({NowNs(), op, address, value, test});
}

std::vector<Transaction> TransactionLog::Snapshot() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return records_;
}

std::size_t TransactionLog::size() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return records_.size();
}

void TransactionLog::Clear() {
  const std::lock_guard<std::mutex> lock(mutex_);
  records_.clear();
}

void TransactionLog::WriteCsv(const std::string& path) const {
  const auto records = Snapshot();
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("cannot write transaction log: " + path);
  }
  out << "timestamp_ns,op,address,value,test\n";
  for (const auto& record : records) {
    out << record.timestamp_ns << ',' << OpName(record.op) << ",0x" << std::hex
        << record.address << ",0x" << record.value << std::dec << ',' << record.test << '\n';
  }
}

uint32_t LoggedTransport::Read32(uint64_t address) {
  const uint32_t value = inner_->Read32(address);
  log_->Record(Op::Read32, address, value, test_);
  return value;
}

void LoggedTransport::Write32(uint64_t address, uint32_t value) {
  inner_->Write32(address, value);
  log_->Record(Op::Write32, address, value, test_);
}

void LoggedTransport::ReadBlock(uint64_t address, uint32_t* out, std::size_t count) {
  inner_->ReadBlock(address, out, count);
  log_->Record(Op::ReadBlock, address, static_cast<uint32_t>(count), test_);
}

void LoggedTransport::WriteBlock(uint64_t address, const uint32_t* in, std::size_t count) {
  inner_->WriteBlock(address, in, count);
  log_->Record(Op::WriteBlock, address, static_cast<uint32_t>(count), test_);
}

std::string LoggedTransport::name() const {
  return inner_->name() + "+log";
}

}  // namespace soc
