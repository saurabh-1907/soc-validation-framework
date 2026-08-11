#!/usr/bin/env python3
"""Analyse transaction logs: slowest tests, retry rates, register hot spots.

    python3 python/analyze_logs.py artifacts/*.csv

Register access hot spots are the actionable output. A register read thousands
of times in one suite is almost always a poll loop that could be one blocking
wait, or a set of fields being read individually instead of coalesced - which is
exactly the inefficiency benchmarks/RESULTS.md quantifies.
"""

from __future__ import annotations

import argparse
import glob
import sys

import pandas as pd

# Address -> name, from regmaps/soc.yaml. Kept small deliberately; a real
# deployment would generate this from the same YAML the C++ layer comes from.
KNOWN = {
    0x40000000: "DMA.CTRL",
    0x40000004: "DMA.STATUS",
    0x40000008: "DMA.IRQ_STATUS",
    0x4000000C: "DMA.SRC_ADDR",
    0x40000010: "DMA.DST_ADDR",
    0x40000014: "DMA.XFER_LEN",
    0x50000000: "PCIE.LINK_STATUS",
    0x50000004: "PCIE.LINK_CTRL",
    0x50000008: "PCIE.ERR_STATUS",
}


def load(paths: list[str]) -> pd.DataFrame:
    frames = []
    for pattern in paths:
        for path in sorted(glob.glob(pattern)):
            frame = pd.read_csv(path)
            frame["source"] = path
            frames.append(frame)
    if not frames:
        raise SystemExit(f"no logs matched {paths}")
    df = pd.concat(frames, ignore_index=True)
    df["address"] = df["address"].apply(lambda v: int(str(v), 16))
    df["register"] = df["address"].map(KNOWN).fillna(
        df["address"].apply(lambda a: f"0x{a:08X}")
    )
    return df


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("logs", nargs="+", help="CSV transaction logs (globs allowed)")
    ap.add_argument("--top", type=int, default=10)
    args = ap.parse_args()

    df = load(args.logs)
    print(f"{len(df):,} transactions across {df['source'].nunique()} log(s)\n")

    # --- slowest tests, by elapsed bus time between first and last access ----
    span = df.groupby("test")["timestamp_ns"].agg(["min", "max", "count"])
    span["duration_ms"] = (span["max"] - span["min"]) / 1e6
    slowest = span.sort_values("duration_ms", ascending=False).head(args.top)
    print(f"slowest {len(slowest)} tests")
    print(f"  {'test':<28} {'ms':>9} {'accesses':>9}")
    for name, row in slowest.iterrows():
        print(f"  {name:<28} {row['duration_ms']:9.2f} {int(row['count']):9,}")

    # --- register hot spots --------------------------------------------------
    hot = df.groupby("register").size().sort_values(ascending=False).head(args.top)
    total = len(df)
    print(f"\nregister access hot spots")
    print(f"  {'register':<20} {'accesses':>9} {'share':>7}")
    for register, count in hot.items():
        print(f"  {register:<20} {count:9,} {count / total:6.1%}")

    # --- polling: repeated reads of one register inside one test -------------
    polls = (
        df[df["op"] == "read32"]
        .groupby(["test", "register"])
        .size()
        .reset_index(name="reads")
    )
    heavy = polls[polls["reads"] > 10].sort_values("reads", ascending=False)
    print(f"\npolling loops (>10 reads of one register in one test)")
    if heavy.empty:
        print("  none")
    else:
        for _, row in heavy.head(args.top).iterrows():
            print(f"  {row['test']:<24} {row['register']:<20} {int(row['reads']):6,} reads")
        print(
            "\n  Each of these is a candidate for an interrupt-driven wait or a "
            "coalesced read."
        )

    # --- read/write mix ------------------------------------------------------
    print("\noperation mix")
    for op, count in df["op"].value_counts().items():
        print(f"  {op:<14} {count:9,} {count / total:6.1%}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
