#!/usr/bin/env python3
# Copyright (C) 2025 MyOEM
# SPDX-License-Identifier: Apache-2.0
#
# simulate_speed.py — injects PERF_VEHICLE_SPEED values into the VHAL emulator.
#
# How it works:
#   1. Reads speed values (m/s) from speed_data.txt, one per line.
#   2. For each value, runs:
#        adb shell cmd car_service inject-vhal-event <propId> <areaId> <value>
#      which tells CarService's VHAL emulator to publish a property update.
#   3. The VHAL emulator dispatches it to all subscribers — including safemoded.
#   4. safemoded's onVhalEvent() fires, mCurrentData.speedMs is updated,
#      and all registered ISafeModeCallback clients receive onVehicleDataChanged().
#
# Usage:
#   python3 simulate_speed.py [--interval 2.0] [--device <serial>]
#
# Arguments:
#   --interval   Seconds between injections (default: 2.0)
#   --device     ADB device serial (default: uses first connected device)
#
# Prerequisites:
#   - ADB connected to RPi5 running AAOS
#   - safemoded running: adb shell ps -e | grep safemoded
#   - safemode_client running in subscribe mode (optional, for live verification):
#       adb shell /vendor/bin/safemode_client subscribe 60

import subprocess
import time
import argparse
import os
import sys

# ── VHAL property constants ───────────────────────────────────────────────────
# PERF_VEHICLE_SPEED: 0x11600207
#   - Type: FLOAT  (speed in m/s)
#   - Change mode: CONTINUOUS  (always publishing at a fixed rate)
#   - Area: 0 (global — not per-seat or per-wheel)
PROP_SPEED = "0x11600207"
AREA_ID    = "0"

# ── SafeMode threshold display (mirrors SafeModeManager.kt logic) ─────────────
def get_state_label(speed_ms: float) -> str:
    kmh = speed_ms * 3.6
    if kmh >= 15.0:
        return "HARD_SAFE_MODE   🔴"
    elif kmh >= 5.0:
        return "NORMAL_SAFE_MODE 🟡"
    else:
        return "NO_SAFE_MODE     🟢"


def inject_vhal_event(prop: str, area: str, value: str, device: str | None) -> bool:
    """
    Runs: adb [-s <device>] shell cmd car_service inject-vhal-event <prop> <area> <value>

    inject-vhal-event is an official AAOS debugging tool exposed through
    CarService's shell command interface. It bypasses the physical hardware
    and directly notifies the VHAL property store — identical to a real sensor
    update from the vehicle's perspective.

    Returns True on success, False on error.
    """
    cmd = ["adb"]
    if device:
        cmd += ["-s", device]
    cmd += ["shell", "cmd", "car_service", "inject-vhal-event", prop, area, value]

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  [ERROR] adb returned {result.returncode}: {result.stderr.strip()}", file=sys.stderr)
        return False
    return True


def load_data(filepath: str) -> list[float]:
    """
    Read the data file, skipping blank lines and lines starting with '#'.
    Returns a list of float values.
    """
    values = []
    with open(filepath) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            try:
                values.append(float(line))
            except ValueError:
                print(f"  [WARN] Skipping non-numeric line: {line!r}", file=sys.stderr)
    return values


def main():
    parser = argparse.ArgumentParser(
        description="Inject PERF_VEHICLE_SPEED values into AAOS VHAL emulator"
    )
    parser.add_argument(
        "--interval", type=float, default=2.0,
        help="Seconds between injections (default: 2.0)"
    )
    parser.add_argument(
        "--device", type=str, default=None,
        help="ADB device serial (default: first connected device)"
    )
    args = parser.parse_args()

    # Locate data file relative to this script
    script_dir = os.path.dirname(os.path.abspath(__file__))
    data_file  = os.path.join(script_dir, "speed_data.txt")

    if not os.path.exists(data_file):
        print(f"ERROR: data file not found: {data_file}", file=sys.stderr)
        sys.exit(1)

    speeds = load_data(data_file)
    if not speeds:
        print("ERROR: no speed values found in data file", file=sys.stderr)
        sys.exit(1)

    print(f"Simulating {len(speeds)} speed values at {args.interval}s interval")
    print(f"Property: PERF_VEHICLE_SPEED ({PROP_SPEED}), Area: {AREA_ID}")
    print("-" * 60)

    for i, speed_ms in enumerate(speeds, start=1):
        kmh   = speed_ms * 3.6
        label = get_state_label(speed_ms)
        value_str = f"{speed_ms:.4f}"

        print(f"[{i:02d}/{len(speeds)}] Injecting {speed_ms:6.2f} m/s = {kmh:6.2f} km/h  →  {label}")
        ok = inject_vhal_event(PROP_SPEED, AREA_ID, value_str, args.device)
        if not ok:
            print("         Injection failed — check adb connection and that safemoded is running")

        if i < len(speeds):
            time.sleep(args.interval)

    print("-" * 60)
    print("Speed simulation complete.")
    print("Check safemode_client output or logcat for service reactions:")
    print("  adb logcat -s safemode_service safemode_client")


if __name__ == "__main__":
    main()
