// pybind11 bindings: the register layer, transports and sequencer in Python.
//
// The point is that a validation engineer writes a bring-up sequence in a few
// lines without recompiling anything, while the bus access itself stays in C++.
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>

#include "soc/emulated_device.hpp"
#include "soc/register.hpp"
#include "soc/sequencer.hpp"
#include "soc/soc_registers.hpp"
#include "soc/transports.hpp"
#include "soc/txn_log.hpp"

namespace py = pybind11;
using namespace soc;

PYBIND11_MODULE(soc_val, m) {
  m.doc() = "SoC post-silicon validation framework";

  py::register_exception<TransportError>(m, "TransportError");

  py::class_<ITransport>(m, "Transport")
      .def("read32", &ITransport::Read32, py::arg("address"))
      .def("write32", &ITransport::Write32, py::arg("address"), py::arg("value"))
      .def_property_readonly("name", &ITransport::name);

  py::class_<EmulatedDevice, ITransport>(m, "EmulatedDevice")
      .def(py::init<>())
      .def("reset", &EmulatedDevice::Reset)
      .def("set_transfer_latency", &EmulatedDevice::set_transfer_latency)
      .def_property_readonly("ticks", &EmulatedDevice::ticks);

  m.def("make_transport", &MakeTransport, py::arg("kind"),
        "Build a transport by name: emulated | jtag | i2c | spi");

  // Registers are exposed as light wrappers so Python reads a whole register
  // once and decodes fields locally - the same coalescing rule as in C++.
  py::class_<regs::DMA_CTRL>(m, "DmaCtrl")
      .def(py::init<>())
      .def_readwrite("raw", &regs::DMA_CTRL::raw)
      .def_property("go", &regs::DMA_CTRL::GO,
                    [](regs::DMA_CTRL& r, uint32_t v) { r.set_GO(v); })
      .def_property("burst_len", &regs::DMA_CTRL::BURST_LEN,
                    [](regs::DMA_CTRL& r, uint32_t v) { r.set_BURST_LEN(v); })
      .def_property("channel", &regs::DMA_CTRL::CHANNEL,
                    [](regs::DMA_CTRL& r, uint32_t v) { r.set_CHANNEL(v); })
      .def_property_readonly_static("address",
                                    [](py::object) { return regs::DMA_CTRL::kAddress; });

  py::class_<regs::DMA_STATUS>(m, "DmaStatus")
      .def(py::init<>())
      .def_readwrite("raw", &regs::DMA_STATUS::raw)
      // Read-only in the databook, so read-only here too. The binding must not
      // hand Python a setter the C++ API deliberately withholds.
      .def_property_readonly("idle", &regs::DMA_STATUS::IDLE)
      .def_property_readonly("busy", &regs::DMA_STATUS::BUSY)
      .def_property_readonly("done", &regs::DMA_STATUS::DONE)
      .def_property_readonly("beats", &regs::DMA_STATUS::BEATS)
      .def_property_readonly_static("address",
                                    [](py::object) { return regs::DMA_STATUS::kAddress; });

  py::enum_<TestStatus>(m, "TestStatus")
      .value("PASS", TestStatus::Pass)
      .value("FAIL", TestStatus::Fail)
      .value("SKIP", TestStatus::Skip)
      .value("ERROR", TestStatus::Error)
      .value("TIMEOUT", TestStatus::Timeout);

  py::class_<TestResult>(m, "TestResult")
      .def_readonly("name", &TestResult::name)
      .def_readonly("status", &TestResult::status)
      .def_readonly("duration_ms", &TestResult::duration_ms)
      .def_readonly("message", &TestResult::message)
      .def_readonly("level", &TestResult::level)
      .def("__repr__", [](const TestResult& r) {
        return "<TestResult " + r.name + " " + ToString(r.status) + ">";
      });

  py::class_<Sequencer>(m, "Sequencer")
      .def(py::init<>())
      .def(
          "add",
          [](Sequencer& self, const std::string& name, std::vector<std::string> deps,
             std::function<void()> body, int timeout_ms) {
            TestCase test;
            test.name = name;
            test.depends_on = std::move(deps);
            test.body = std::move(body);
            test.timeout = std::chrono::milliseconds(timeout_ms);
            self.Add(std::move(test));
          },
          py::arg("name"), py::arg("depends_on") = std::vector<std::string>{},
          py::arg("body") = std::function<void()>{}, py::arg("timeout_ms") = 30000)
      .def("levels", &Sequencer::Levels)
      // The GIL must be released or parallel tests serialise on it and the
      // whole point of the thread pool is lost.
      .def("run", &Sequencer::Run, py::arg("threads") = 1,
           py::call_guard<py::gil_scoped_release>())
      .def("__len__", &Sequencer::size);

  py::class_<TransactionLog>(m, "TransactionLog")
      .def(py::init<>())
      .def("write_csv", &TransactionLog::WriteCsv, py::arg("path"))
      .def("clear", &TransactionLog::Clear)
      .def("__len__", &TransactionLog::size);

  py::class_<LoggedTransport, ITransport>(m, "LoggedTransport")
      .def(py::init<ITransport&, TransactionLog&>(), py::keep_alive<1, 2>(),
           py::keep_alive<1, 3>())
      .def("set_test", &LoggedTransport::set_test);
}
