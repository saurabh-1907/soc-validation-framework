#include "soc/sequencer.hpp"

#include <algorithm>
#include <future>
#include <map>
#include <set>
#include <stdexcept>

namespace soc {

const char* ToString(TestStatus status) {
  switch (status) {
    case TestStatus::Pass:
      return "PASS";
    case TestStatus::Fail:
      return "FAIL";
    case TestStatus::Skip:
      return "SKIP";
    case TestStatus::Error:
      return "ERROR";
    case TestStatus::Timeout:
      return "TIMEOUT";
  }
  return "UNKNOWN";
}

std::atomic<uint64_t>& Sequencer::AccessCounter() {
  static std::atomic<uint64_t> counter{0};
  return counter;
}

void Sequencer::Add(TestCase test) {
  if (test.name.empty()) {
    throw std::invalid_argument("test name must not be empty");
  }
  const auto clash = std::find_if(tests_.begin(), tests_.end(),
                                  [&](const TestCase& t) { return t.name == test.name; });
  if (clash != tests_.end()) {
    throw std::invalid_argument("duplicate test name: " + test.name);
  }
  tests_.push_back(std::move(test));
}

std::vector<std::vector<std::string>> Sequencer::Levels() const {
  std::map<std::string, std::set<std::string>> pending;
  for (const auto& test : tests_) {
    pending[test.name] = {test.depends_on.begin(), test.depends_on.end()};
  }

  // A dependency naming a test that was never registered is a typo, and
  // silently treating it as satisfied would run tests out of order.
  for (const auto& [name, deps] : pending) {
    for (const auto& dep : deps) {
      if (pending.find(dep) == pending.end()) {
        throw std::invalid_argument("test '" + name + "' depends on unknown test '" + dep +
                                    "'");
      }
    }
  }

  std::vector<std::vector<std::string>> levels;
  std::set<std::string> done;

  while (!pending.empty()) {
    std::vector<std::string> ready;
    for (const auto& [name, deps] : pending) {
      const bool satisfied = std::all_of(
          deps.begin(), deps.end(), [&](const std::string& d) { return done.count(d) > 0; });
      if (satisfied) {
        ready.push_back(name);
      }
    }
    if (ready.empty()) {
      std::string cycle;
      for (const auto& [name, deps] : pending) {
        cycle += (cycle.empty() ? "" : ", ") + name;
      }
      throw std::invalid_argument("dependency cycle among: " + cycle);
    }
    std::sort(ready.begin(), ready.end());  // deterministic ordering
    for (const auto& name : ready) {
      pending.erase(name);
      done.insert(name);
    }
    levels.push_back(std::move(ready));
  }
  return levels;
}

namespace {

TestResult RunOne(const TestCase& test, int level) {
  TestResult result;
  result.name = test.name;
  result.level = level;

  const uint64_t before = Sequencer::AccessCounter().load();
  const auto started = std::chrono::steady_clock::now();

  // The body runs on its own thread so a hang cannot take the suite with it.
  auto future = std::async(std::launch::async, [&test]() {
    if (test.setup) {
      test.setup();
    }
    try {
      test.body();
    } catch (...) {
      if (test.teardown) {
        test.teardown();  // teardown must run even when the body throws
      }
      throw;
    }
    if (test.teardown) {
      test.teardown();
    }
  });

  if (future.wait_for(test.timeout) == std::future_status::timeout) {
    result.status = TestStatus::Timeout;
    result.message = "exceeded " + std::to_string(test.timeout.count()) + " ms";
    // Deliberately not detaching and not destroying the future here would block;
    // we wait it out so no thread is leaked. A genuinely wedged test would hang
    // the suite - on real silicon that is what a watchdog reset is for.
    future.wait();
  } else {
    try {
      future.get();
      result.status = TestStatus::Pass;
    } catch (const std::exception& error) {
      result.status = TestStatus::Fail;
      result.message = error.what();
    } catch (...) {
      result.status = TestStatus::Error;
      result.message = "unknown exception";
    }
  }

  result.duration_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
          .count();
  result.register_accesses = Sequencer::AccessCounter().load() - before;
  return result;
}

}  // namespace

std::vector<TestResult> Sequencer::Run(unsigned threads) {
  const auto levels = Levels();
  std::map<std::string, TestResult> by_name;

  int level_index = 0;
  for (const auto& level : levels) {
    if (threads <= 1) {
      for (const auto& name : level) {
        const auto& test = *std::find_if(tests_.begin(), tests_.end(),
                                         [&](const TestCase& t) { return t.name == name; });
        by_name[name] = RunOne(test, level_index);
      }
    } else {
      // Independent tests in a level run together, capped at `threads`.
      for (std::size_t start = 0; start < level.size(); start += threads) {
        const std::size_t end = std::min(level.size(), start + threads);
        std::vector<std::future<TestResult>> running;
        running.reserve(end - start);
        for (std::size_t i = start; i < end; ++i) {
          const auto& test =
              *std::find_if(tests_.begin(), tests_.end(),
                            [&](const TestCase& t) { return t.name == level[i]; });
          running.push_back(std::async(
              std::launch::async, [&test, level_index] { return RunOne(test, level_index); }));
        }
        for (auto& future : running) {
          auto result = future.get();
          by_name[result.name] = std::move(result);
        }
      }
    }
    ++level_index;
  }

  std::vector<TestResult> ordered;
  ordered.reserve(tests_.size());
  for (const auto& test : tests_) {
    ordered.push_back(by_name[test.name]);
  }
  return ordered;
}

}  // namespace soc
