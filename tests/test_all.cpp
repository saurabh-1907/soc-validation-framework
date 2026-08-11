// GoogleTest suite: generated register layer, emulated device semantics,
// sequencer ordering, and the RAII types.
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "soc/emulated_device.hpp"
#include "soc/register.hpp"
#include "soc/sequencer.hpp"
#include "soc/soc_registers.hpp"
#include "soc/transports.hpp"
#include "soc/txn_log.hpp"

using namespace soc;

// ============================================================ generated layer

TEST(Codegen, AddressesComeFromBasePlusOffset) {
  EXPECT_EQ(regs::DMA_CTRL::kAddress, 0x40000000ull);
  EXPECT_EQ(regs::DMA_STATUS::kAddress, 0x40000004ull);
  EXPECT_EQ(regs::PCIE_LINK_STATUS::kAddress, 0x50000000ull);
}

TEST(Codegen, ResetValuesMatchTheDatabook) {
  EXPECT_EQ(regs::DMA_CTRL{}.raw, 0x00000000u);
  EXPECT_EQ(regs::DMA_STATUS{}.raw, 0x00000001u);  // IDLE out of reset
  EXPECT_EQ(regs::DMA_STATUS{}.IDLE(), 1u);
}

TEST(Codegen, MaskAndShiftAreDerivedFromTheBitRange) {
  EXPECT_EQ(regs::DMA_CTRL::kBURST_LENMask, 0x00000FF0u);
  EXPECT_EQ(regs::DMA_CTRL::kBURST_LENShift, 4u);
  EXPECT_EQ(regs::DMA_CTRL::kBURST_LENWidth, 8u);
}

TEST(Codegen, FieldSettersDoNotDisturbNeighbours) {
  regs::DMA_CTRL ctrl;
  ctrl.set_BURST_LEN(0xFF).set_CHANNEL(0x3).set_GO(1);

  EXPECT_EQ(ctrl.BURST_LEN(), 0xFFu);
  EXPECT_EQ(ctrl.CHANNEL(), 0x3u);
  EXPECT_EQ(ctrl.GO(), 1u);
  EXPECT_EQ(ctrl.ABORT(), 0u);
}

TEST(Codegen, SetterTruncatesToTheFieldWidth) {
  regs::DMA_CTRL ctrl;
  ctrl.set_MODE(0xFFFF);  // MODE is 2 bits
  EXPECT_EQ(ctrl.MODE(), 0x3u);
  EXPECT_EQ(ctrl.raw & ~regs::DMA_CTRL::kMODEMask, 0u) << "bled outside the field";
}

TEST(Codegen, EnumeratedValuesAreGenerated) {
  EXPECT_EQ(static_cast<uint32_t>(regs::DMA_CTRL::MODE_e::kSCATTER), 1u);
  EXPECT_EQ(static_cast<uint32_t>(regs::PCIE_LINK_STATUS::SPEED_e::kGEN4), 4u);
}

// A read-only register offers no setter, so `status.set_DONE(1)` does not
// compile. That is checked here with a type trait rather than a comment.
template <typename T, typename = void>
struct HasDoneSetter : std::false_type {};
template <typename T>
struct HasDoneSetter<T, std::void_t<decltype(std::declval<T&>().set_DONE(1u))>>
    : std::true_type {};

TEST(Codegen, ReadOnlyFieldsHaveNoSetter) {
  static_assert(!HasDoneSetter<regs::DMA_STATUS>::value,
                "DONE is RO and must not have a setter");
  EXPECT_FALSE(HasDoneSetter<regs::DMA_STATUS>::value);
}

// W1C offers clear_X() and no set_X(): writing 0 to "leave it alone" is the
// mistake the API shape is designed to prevent.
template <typename T, typename = void>
struct HasDoneIrqSetter : std::false_type {};
template <typename T>
struct HasDoneIrqSetter<T, std::void_t<decltype(std::declval<T&>().set_DONE_IRQ(1u))>>
    : std::true_type {};

TEST(Codegen, WriteOneToClearFieldsOfferOnlyClear) {
  static_assert(!HasDoneIrqSetter<regs::DMA_IRQ_STATUS>::value,
                "W1C fields must not expose a plain setter");
  regs::DMA_IRQ_STATUS irq;
  irq.clear_DONE_IRQ();
  EXPECT_EQ(irq.raw, 0x1u) << "clear_ writes a 1 to the bit";
}

// ============================================================ emulated device

class EmulatedTest : public ::testing::Test {
 protected:
  EmulatedDevice device;
};

TEST_F(EmulatedTest, RegistersComeUpAtTheirResetValue) {
  EXPECT_EQ(device.Read32(regs::DMA_STATUS::kAddress), 0x00000001u);
  EXPECT_EQ(device.Read32(regs::DMA_CTRL::kAddress), 0x00000000u);
}

TEST_F(EmulatedTest, UndecodedAddressThrowsRatherThanReturningGarbage) {
  EXPECT_THROW((void)device.Read32(0xDEADBEEFull), TransportError);
}

TEST_F(EmulatedTest, WritesToAReadOnlyRegisterAreIgnored) {
  device.Write32(regs::DMA_STATUS::kAddress, 0xFFFFFFFFu);
  EXPECT_EQ(device.Read32(regs::DMA_STATUS::kAddress), 0x00000001u);
}

TEST_F(EmulatedTest, WriteOneClearsAndWriteZeroDoesNot) {
  // Latch two interrupts by running a transfer to completion.
  device.set_transfer_latency(1);
  device.Write32(regs::DMA_CTRL::kAddress, 0x1u);
  for (int i = 0; i < 4; ++i) {
    (void)device.Read32(regs::DMA_STATUS::kAddress);
  }
  ASSERT_EQ(device.Read32(regs::DMA_IRQ_STATUS::kAddress) & 0x1u, 0x1u);

  device.Write32(regs::DMA_IRQ_STATUS::kAddress, 0x0u);  // writing 0 changes nothing
  EXPECT_EQ(device.Read32(regs::DMA_IRQ_STATUS::kAddress) & 0x1u, 0x1u);

  device.Write32(regs::DMA_IRQ_STATUS::kAddress, 0x1u);  // writing 1 clears
  EXPECT_EQ(device.Read32(regs::DMA_IRQ_STATUS::kAddress) & 0x1u, 0x0u);
}

TEST_F(EmulatedTest, GoStartsATransferThatCompletesAfterLatency) {
  device.set_transfer_latency(5);
  RegisterAccessor<regs::DMA_XFER_LEN> len(device);
  RegisterAccessor<regs::DMA_CTRL> ctrl(device);
  RegisterAccessor<regs::DMA_STATUS> status(device);

  len.WriteOnly([](auto& r) { r.set_BYTES(64); });
  ctrl.WriteOnly([](auto& r) { r.set_GO(1).set_BURST_LEN(8); });

  EXPECT_EQ(status.Read().BUSY(), 1u) << "should be busy immediately after GO";
  EXPECT_EQ(status.Read().DONE(), 0u) << "must not complete instantly";

  regs::DMA_STATUS observed;
  for (int i = 0; i < 20 && observed.DONE() == 0; ++i) {
    observed = status.Read();
  }
  EXPECT_EQ(observed.DONE(), 1u);
  EXPECT_EQ(observed.BUSY(), 0u);
  EXPECT_EQ(observed.BEATS(), 16u) << "64 bytes / 4 bytes per beat";
}

TEST_F(EmulatedTest, PcieRetrainBringsTheLinkUpAtTheRequestedSpeed) {
  RegisterAccessor<regs::PCIE_LINK_CTRL> ctrl(device);
  RegisterAccessor<regs::PCIE_LINK_STATUS> status(device);

  EXPECT_EQ(status.Read().LINK_UP(), 0u);
  ctrl.WriteOnly([](auto& r) {
    r.set_RETRAIN(1).set_TARGET_SPEED(
        static_cast<uint32_t>(regs::PCIE_LINK_CTRL::TARGET_SPEED_e::kGEN3));
  });

  const auto link = status.Read();
  EXPECT_EQ(link.LINK_UP(), 1u);
  EXPECT_EQ(link.SPEED(), 3u);
  EXPECT_EQ(link.WIDTH(), 16u);
}

// ================================================== read-modify-write hazards

TEST_F(EmulatedTest, ModifyIsOneReadPlusOneWrite) {
  auto backing = std::make_unique<EmulatedDevice>();
  auto* raw = backing.get();
  (void)raw;
  JtagTransport jtag(std::move(backing));
  RegisterAccessor<regs::DMA_CTRL> ctrl(jtag);

  jtag.ResetCounters();
  ctrl.Modify([](auto& r) { r.set_BURST_LEN(4).set_CHANNEL(1).set_MODE(2); });
  EXPECT_EQ(jtag.accesses(), 2u) << "three fields, still one read and one write";
}

TEST_F(EmulatedTest, CoalescedReadCostsOneTransactionForManyFields) {
  auto backing = std::make_unique<EmulatedDevice>();
  JtagTransport jtag(std::move(backing));
  RegisterAccessor<regs::DMA_STATUS> status(jtag);

  jtag.ResetCounters();
  const auto snapshot = status.Read();
  (void)snapshot.IDLE();
  (void)snapshot.BUSY();
  (void)snapshot.DONE();
  (void)snapshot.BEATS();
  EXPECT_EQ(jtag.accesses(), 1u) << "four fields must not cost four reads";
}

// This is the race the README describes: a status bit set by the device between
// the read and the write of a read-modify-write is written straight back and
// lost. Reproduced deterministically here so the hazard is demonstrable rather
// than merely asserted.
TEST_F(EmulatedTest, ReadModifyWriteOnAW1cRegisterLosesAnInterrupt) {
  device.set_transfer_latency(1);
  device.Write32(regs::DMA_CTRL::kAddress, 0x1u);
  for (int i = 0; i < 4; ++i) {
    (void)device.Read32(regs::DMA_STATUS::kAddress);
  }
  ASSERT_EQ(device.Read32(regs::DMA_IRQ_STATUS::kAddress) & 0x1u, 1u);

  RegisterAccessor<regs::DMA_IRQ_STATUS> irq(device);
  // Intending only to clear ERR_IRQ, this reads DONE_IRQ=1 and writes it back,
  // which on a W1C register clears it. The interrupt is silently lost.
  irq.Modify([](auto& r) { r.clear_ERR_IRQ(); });
  EXPECT_EQ(device.Read32(regs::DMA_IRQ_STATUS::kAddress) & 0x1u, 0u)
      << "DONE_IRQ was destroyed by the read-modify-write";

  // WriteOnly starts from the reset value, so untouched W1C bits are written as
  // 0 and survive.
  device.Reset();
  device.set_transfer_latency(1);
  device.Write32(regs::DMA_CTRL::kAddress, 0x1u);
  for (int i = 0; i < 4; ++i) {
    (void)device.Read32(regs::DMA_STATUS::kAddress);
  }
  ASSERT_EQ(device.Read32(regs::DMA_IRQ_STATUS::kAddress) & 0x1u, 1u);
  irq.WriteOnly([](auto& r) { r.clear_ERR_IRQ(); });
  EXPECT_EQ(device.Read32(regs::DMA_IRQ_STATUS::kAddress) & 0x1u, 1u)
      << "DONE_IRQ must survive a write aimed at a different bit";
}

// ==================================================================== RAII

TEST(MappedRegion, MoveTransfersOwnershipAndEmptiesTheSource) {
  EmulatedDevice device;
  MappedRegion region(device, regs::DMA_CTRL::kAddress, 4);
  ASSERT_EQ(region.size(), 4u);

  MappedRegion moved(std::move(region));
  EXPECT_EQ(moved.size(), 4u);
  EXPECT_TRUE(region.empty());  // NOLINT(bugprone-use-after-move) - checking it
}

TEST(MappedRegion, MoveAssignmentIsSelfSafe) {
  EmulatedDevice device;
  MappedRegion region(device, regs::DMA_CTRL::kAddress, 2);
  MappedRegion* alias = &region;
  region = std::move(*alias);  // self-move must not free its own buffer
  EXPECT_EQ(region.size(), 2u);
}

TEST(MappedRegion, OutOfRangeAccessThrows) {
  EmulatedDevice device;
  MappedRegion region(device, regs::DMA_CTRL::kAddress, 2);
  EXPECT_THROW((void)region.at(9), std::out_of_range);
}

TEST(DeviceHandle, RejectsANullTransport) {
  EXPECT_THROW(DeviceHandle(nullptr), std::invalid_argument);
}

// =============================================================== sequencer

namespace {
TestCase MakeTest(
    std::string name, std::vector<std::string> deps, std::function<void()> body = [] {}) {
  TestCase test;
  test.name = std::move(name);
  test.depends_on = std::move(deps);
  test.body = std::move(body);
  return test;
}
}  // namespace

TEST(Sequencer, IndependentTestsShareALevel) {
  Sequencer seq;
  seq.Add(MakeTest("a", {}));
  seq.Add(MakeTest("b", {}));
  seq.Add(MakeTest("c", {"a", "b"}));

  const auto levels = seq.Levels();
  ASSERT_EQ(levels.size(), 2u);
  EXPECT_EQ(levels[0], (std::vector<std::string>{"a", "b"}));
  EXPECT_EQ(levels[1], (std::vector<std::string>{"c"}));
}

TEST(Sequencer, ChainedDependenciesProduceOneLevelEach) {
  Sequencer seq;
  seq.Add(MakeTest("third", {"second"}));
  seq.Add(MakeTest("first", {}));
  seq.Add(MakeTest("second", {"first"}));

  const auto levels = seq.Levels();
  ASSERT_EQ(levels.size(), 3u);
  EXPECT_EQ(levels[0][0], "first");
  EXPECT_EQ(levels[2][0], "third");
}

TEST(Sequencer, CycleIsRejected) {
  Sequencer seq;
  seq.Add(MakeTest("a", {"b"}));
  seq.Add(MakeTest("b", {"a"}));
  EXPECT_THROW((void)seq.Levels(), std::invalid_argument);
}

TEST(Sequencer, UnknownDependencyIsRejected) {
  Sequencer seq;
  seq.Add(MakeTest("a", {"does_not_exist"}));
  EXPECT_THROW((void)seq.Levels(), std::invalid_argument);
}

TEST(Sequencer, DuplicateNameIsRejected) {
  Sequencer seq;
  seq.Add(MakeTest("a", {}));
  EXPECT_THROW(seq.Add(MakeTest("a", {})), std::invalid_argument);
}

TEST(Sequencer, AThrowingBodyIsRecordedAsFailWithItsMessage) {
  Sequencer seq;
  seq.Add(MakeTest("boom", {}, [] { throw std::runtime_error("register mismatch"); }));

  const auto results = seq.Run(1);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, TestStatus::Fail);
  EXPECT_NE(results[0].message.find("register mismatch"), std::string::npos);
}

TEST(Sequencer, TeardownRunsEvenWhenTheBodyFails) {
  bool torn_down = false;
  Sequencer seq;
  TestCase test = MakeTest("t", {}, [] { throw std::runtime_error("x"); });
  test.teardown = [&torn_down] { torn_down = true; };
  seq.Add(std::move(test));

  (void)seq.Run(1);
  EXPECT_TRUE(torn_down);
}

TEST(Sequencer, TimeoutIsReportedRatherThanHangingTheSuite) {
  Sequencer seq;
  TestCase test =
      MakeTest("slow", {}, [] { std::this_thread::sleep_for(std::chrono::milliseconds(200)); });
  test.timeout = std::chrono::milliseconds(20);
  seq.Add(std::move(test));

  const auto results = seq.Run(1);
  EXPECT_EQ(results[0].status, TestStatus::Timeout);
}

TEST(Sequencer, ResultsComeBackInDeclarationOrder) {
  Sequencer seq;
  seq.Add(MakeTest("z", {}));
  seq.Add(MakeTest("y", {"z"}));
  const auto results = seq.Run(2);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].name, "z");
  EXPECT_EQ(results[1].name, "y");
}

// ============================================================ transaction log

TEST(TransactionLog, RecordsEveryAccessThroughTheDecorator) {
  EmulatedDevice device;
  TransactionLog log;
  LoggedTransport logged(device, log);
  logged.set_test("dma_smoke");

  RegisterAccessor<regs::DMA_CTRL> ctrl(logged);
  ctrl.Modify([](auto& r) { r.set_GO(1); });

  const auto records = log.Snapshot();
  ASSERT_EQ(records.size(), 2u);
  EXPECT_EQ(records[0].op, Op::Read32);
  EXPECT_EQ(records[1].op, Op::Write32);
  EXPECT_EQ(records[1].test, "dma_smoke");
}

TEST(Transports, FactoryRejectsAnUnknownKind) {
  EXPECT_THROW((void)MakeTransport("usb"), std::invalid_argument);
  EXPECT_NE(MakeTransport("jtag"), nullptr);
}

TEST(Transports, SlowerProtocolsCostMoreSimulatedTime) {
  JtagTransport jtag(std::make_unique<EmulatedDevice>());
  I2cTransport i2c(std::make_unique<EmulatedDevice>());

  (void)jtag.Read32(regs::DMA_CTRL::kAddress);
  (void)i2c.Read32(regs::DMA_CTRL::kAddress);
  EXPECT_GT(i2c.simulated_ns(), jtag.simulated_ns());
}
