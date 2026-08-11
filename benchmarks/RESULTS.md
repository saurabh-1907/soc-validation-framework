# Benchmark results

Measured on this machine by `soc_benchmark`, which writes this file from its own timings. No figure here is hand-written.

Reproduce with:

```bash
./scripts/build.sh && ./build/soc_benchmark
```

- Hardware threads available: 12
- Suite size: 26 tests (2 gating, 24 independent)
- Register-read iterations: 20000

## 1. Serial vs dependency-parallel execution

| | wall time |
|---|---:|
| serial (`threads=1`) | 109.25 ms |
| parallel (`threads=12`) | 19.08 ms |
| **speed-up** | **5.72x** |

The suite is two gating tests followed by 24 independent ones. The sequencer topologically sorts them into two levels and runs the wide level concurrently; the gating level cannot be parallelised, which is why the speed-up is below the thread count.

## 2. Naive per-field reads vs one coalesced read

Reading the four fields of `DMA_STATUS`, over the JTAG cost model (40 bit-times per access at 10 MHz TCK).

| | naive | coalesced | |
|---|---:|---:|---:|
| bus transactions | 80000 | 20000 | **4.00x fewer** |
| simulated bus time | 320.00 ms | 80.00 ms | **4.00x faster** |
| host wall time | 1.48 ms | 0.58 ms | |

### Reading these numbers honestly

The **bus transaction count** is the real result. It is exact, it is a property of the code rather than of this laptop, and on a physical JTAG link it is what determines runtime.

**Simulated bus time** is that count multiplied by the protocol cost model in `include/soc/transports.hpp`. It is arithmetic on the model, not a measurement of real hardware - treat it as an estimate of what the saving would be on a real adapter.

**Host wall time** is measured but nearly meaningless here: against an in-process emulated device a register read is a hash lookup, so the numbers reflect the emulator's cost, not a bus. It is reported only so the gap between it and the bus figure is visible.

The claim this supports is "reduced bus transactions by 4.00x for multi-field register reads", not a wall-clock speed-up on silicon that was never measured.
