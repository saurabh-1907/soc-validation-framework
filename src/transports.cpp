#include "soc/transports.hpp"

#include <stdexcept>

#include "soc/emulated_device.hpp"

namespace soc {

std::unique_ptr<ITransport> MakeTransport(const std::string& kind) {
  auto device = std::make_unique<EmulatedDevice>();
  if (kind == "emulated") {
    return device;
  }
  if (kind == "jtag") {
    return std::make_unique<JtagTransport>(std::move(device));
  }
  if (kind == "i2c") {
    return std::make_unique<I2cTransport>(std::move(device));
  }
  if (kind == "spi") {
    return std::make_unique<SpiTransport>(std::move(device));
  }
  throw std::invalid_argument("unknown transport: " + kind +
                              " (expected emulated|jtag|i2c|spi)");
}

}  // namespace soc
