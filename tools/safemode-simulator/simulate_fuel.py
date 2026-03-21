#!/usr/bin/env python3
# Copyright (C) 2025 MyOEM
# SPDX-License-Identifier: Apache-2.0
#
# simulate_fuel.py — injects FUEL_LEVEL values into the VHAL emulator.
#
# FUEL_LEVEL is ON_CHANGE, like CURRENT_GEAR. In a real vehicle, the fuel
# sensor reports only when level changes significantly (hysteresis). We inject
# explicit steps to observe how safemoded propagates fuel changes to subscribers.
#
# Usage:
#   python3 simulate_fuel.py [--interval 2.0] [--device <serial>]

import subprocess
import time
import argparse
import os
import sys

# ── VHAL property constants ───────────────────────────────────────────────────
# FUEL_LEVEL: 0x45100004
#   - Type: FLOAT  (fuel level in millilitres)
#   - Change mode: ON_CHANGE
#   - Area: 0 (global)
PROP_FUEL = "0x45100004"
AREA_ID   = "0"

# Fuel warning thresholds (informational — not used by SafeModeService itself;
# the library or app decides what to do with fuelLevelMl)
TANK_CAPACITY_ML  = 50_000.0   # 50 litres
LOW_FUEL_ML       =  5_000.0   # 5 litres — typical low-fuel warning threshold
CRITICAL_FUEL_ML  =  2_000.0   # 2 litres — critical


def fuel_label(ml: float) -> str:
    litres = ml / 1000.0
    pct    = (ml / TANK_CAPACITY_ML) * 100.0
    if ml <= CRITICAL_FUEL_ML:
        status = "⛽ CRITICAL"
    elif ml <= LOW_FUEL_ML:
        status = "⚠️  LOW"
    else:
        status = "✅ OK"
    return f"{litres:5.1f}L ({pct:4.1f}%) {status}"


def inject_vhal_event(prop: str, area: str, value: str, device: str | None) -> bool:
    cmd = ["adb"]
    if device:
        cmd += ["-s", device]
    cmd += ["shell", "cmd", "car_service", "inject-vhal-event", prop, area, value]

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  [ERROR] {result.stderr.strip()}", file=sys.stderr)
        return False
    return True


def load_data(filepath: str) -> list[float]:
    values = []
    with open(filepath) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            try:
                values.append(float(line))
            except ValueError:
                print(f"  [WARN] Skipping: {line!r}", file=sys.stderr)
    return values


def main():
    parser = argparse.ArgumentParser(
        description="Inject FUEL_LEVEL values into AAOS VHAL emulator"
    )
    parser.add_argument("--interval", type=float, default=2.0,
                        help="Seconds between injections (default: 2.0)")
    parser.add_argument("--device",   type=str,   default=None,
                        help="ADB device serial")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    data_file  = os.path.join(script_dir, "fuel_data.txt")

    if not os.path.exists(data_file):
        print(f"ERROR: data file not found: {data_file}", file=sys.stderr)
        sys.exit(1)

    fuels = load_data(data_file)
    if not fuels:
        print("ERROR: no fuel values found in data file", file=sys.stderr)
        sys.exit(1)

    print(f"Simulating {len(fuels)} fuel levels at {args.interval}s interval")
    print(f"Property: FUEL_LEVEL ({PROP_FUEL}), Area: {AREA_ID}")
    print(f"Tank capacity: {TANK_CAPACITY_ML/1000:.0f}L | Low: {LOW_FUEL_ML/1000:.0f}L | Critical: {CRITICAL_FUEL_ML/1000:.0f}L")
    print("-" * 60)

    for i, fuel_ml in enumerate(fuels, start=1):
        label     = fuel_label(fuel_ml)
        value_str = f"{fuel_ml:.1f}"

        print(f"[{i:02d}/{len(fuels)}] {fuel_ml:8.1f} ml  →  {label}")
        inject_vhal_event(PROP_FUEL, AREA_ID, value_str, args.device)

        if i < len(fuels):
            time.sleep(args.interval)

    print("-" * 60)
    print("Fuel simulation complete.")


if __name__ == "__main__":
    main()
