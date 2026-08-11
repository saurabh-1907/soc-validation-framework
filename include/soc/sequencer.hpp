// Dependency-aware parallel test sequencer.
//
// Tests declare what they depend on; the sequencer topologically sorts them into
// levels and runs each level concurrently. That is where the cycle-time win
// comes from: a bring-up suite is mostly independent tests that were written
// sequentially out of habit, and the dependency graph is usually shallow and
// wide.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace soc {

enum class TestStatus { Pass, Fail, Skip, Error, Timeout };

[[nodiscard]] const char* ToString(TestStatus status);

struct TestResult {
  std::string name;
  TestStatus status{TestStatus::Skip};
  double duration_ms{0.0};
  std::string message;
  uint64_t register_accesses{0};
  int level{0};  // topological depth; tests at the same level run together
};

struct TestCase {
  std::string name;
  std::vector<std::string> depends_on;
  std::function<void()> body;  // throws to fail
  std::chrono::milliseconds timeout{std::chrono::seconds(30)};
  std::function<void()> setup;
  std::function<void()> teardown;
};

class Sequencer {
 public:
  void Add(TestCase test);

  // Returns results in declaration order. threads <= 1 runs serially, which is
  // what the benchmark compares against.
  std::vector<TestResult> Run(unsigned threads);

  // Topological order as levels of independent tests. Exposed so the ordering
  // can be tested directly rather than inferred from timing.
  [[nodiscard]] std::vector<std::vector<std::string>> Levels() const;

  [[nodiscard]] std::size_t size() const noexcept { return tests_.size(); }

  // Counter tests can bump so a result records how much bus traffic it caused.
  static std::atomic<uint64_t>& AccessCounter();

 private:
  std::vector<TestCase> tests_;
};

}  // namespace soc
