#!/usr/bin/env python3
"""A DMA bring-up sequence, written the way a validation engineer would.

    PYTHONPATH=build python3 python/examples/dma_bringup.py

Note what is NOT here: no masks, no shifts, no sleep loops. The register layer
comes from the YAML databook and the emulated device advances one tick per bus
access, so polling makes progress without wall-clock waiting.
"""

import sys

import soc_val


def main() -> int:
    device = soc_val.EmulatedDevice()
    device.set_transfer_latency(6)

    log = soc_val.TransactionLog()
    bus = soc_val.LoggedTransport(device, log)

    seq = soc_val.Sequencer()

    def check_reset_values():
        status = soc_val.DmaStatus()
        status.raw = bus.read32(soc_val.DmaStatus.address)
        assert status.idle == 1, f"IDLE should be set out of reset, got {status.raw:#x}"
        assert status.busy == 0, "BUSY must be clear out of reset"

    def start_transfer():
        ctrl = soc_val.DmaCtrl()
        ctrl.go = 1
        ctrl.burst_len = 8
        ctrl.channel = 2
        bus.write32(soc_val.DmaCtrl.address, ctrl.raw)

    def wait_for_done():
        # One read per poll, decoded locally - four field reads would be four
        # bus transactions and the register could change between them.
        for _ in range(50):
            status = soc_val.DmaStatus()
            status.raw = bus.read32(soc_val.DmaStatus.address)
            if status.done:
                assert status.busy == 0, "DONE and BUSY must not both be set"
                return
        raise AssertionError("transfer never reported DONE")

    seq.add("reset_values", [], check_reset_values)
    seq.add("start_transfer", ["reset_values"], start_transfer)
    seq.add("wait_for_done", ["start_transfer"], wait_for_done)

    print(f"levels: {seq.levels()}")
    results = seq.run(threads=4)

    failed = 0
    for r in results:
        print(f"  {r.name:<20} {str(r.status).split('.')[-1]:<8} {r.duration_ms:6.2f} ms")
        if r.status != soc_val.TestStatus.PASS:
            failed += 1
            if r.message:
                print(f"      {r.message}")

    log.write_csv("artifacts/dma_bringup.csv")
    print(f"\n{len(log)} bus transactions logged -> artifacts/dma_bringup.csv")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
