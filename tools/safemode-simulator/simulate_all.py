#!/usr/bin/env python3
"""
simulate_all.py — SafeMode VHAL Simulator for RPi5 development.

HOW IT WORKS
============
safemoded is built with -DSAFEMODE_SIM_MODE (in Android.bp). In this mode it
runs a simulator thread that polls /data/local/tmp/safemode_sim.txt every 500ms.

This script writes one line to that file every --interval seconds:
    "<speed_ms> <gear_int> <fuel_ml>"

safemoded reads the line, updates its internal mCurrentData, and dispatches
the new values to all registered ISafeModeCallback clients. The exact same
code path runs in production (only the data source differs).

PRODUCTION MIGRATION
====================
1. Remove -DSAFEMODE_SIM_MODE from Android.bp cflags in:
       vendor/myoem/services/safemode/Android.bp
2. Rebuild: m safemoded
3. Push to device and restart.
safemoded will connect to the real VHAL instead of reading this file.
No other code changes needed.

USAGE
=====
  # Run all 20 steps once (5s apart)
  python3 simulate_all.py

  # Loop continuously
  python3 simulate_all.py --loop

  # Faster cycle (2s per step)
  python3 simulate_all.py --interval 2

  # Watch output in parallel terminal
  adb logcat -s safemoded SafeModeManager

  # Verify C++ side receives callbacks
  adb shell /vendor/bin/safemode_client subscribe 120

PREREQUISITES
=============
  adb root
  Build safemoded with -DSAFEMODE_SIM_MODE (already set in Android.bp)
"""

import subprocess
import time
import argparse
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_DIR   = os.path.join(SCRIPT_DIR, "data")
SIM_FILE   = "/data/local/tmp/safemode_sim.txt"

# SafeModeState thresholds (must match SafeModeState.kt)
NORMAL_KMH = 5.0
HARD_KMH   = 15.0

GEAR_NAMES = {
    "0": "UNKNOWN",
    "1": "NEUTRAL",
    "2": "REVERSE",
    "4": "PARK",
    "8": "DRIVE",
    "16": "GEAR_1",
    "32": "GEAR_2",
    "64": "GEAR_3",
}


def load_data(filename: str) -> list[str]:
    """Read a data file, skipping blank lines and # comments."""
    path = os.path.join(DATA_DIR, filename)
    values = []
    with open(path) as f:
        for line in f:
            line = line.split("#")[0].strip()
            if line:
                values.append(line)
    return values


def safe_mode_state(speed_ms: float) -> str:
    """Derive SafeModeState from speed in m/s (mirrors SafeModeState.kt logic)."""
    kmh = speed_ms * 3.6
    if kmh >= HARD_KMH:
        return "HARD_SAFE_MODE  "
    if kmh >= NORMAL_KMH:
        return "NORMAL_SAFE_MODE"
    return "NO_SAFE_MODE    "


def inject(speed: str, gear: str, fuel: str, idx: int, total: int) -> bool:
    """Write one data point to the sim file on device."""
    line = f"{speed} {gear} {fuel}"
    result = subprocess.run(
        ["adb", "shell", f"echo '{line}' > {SIM_FILE}"],
        capture_output=True, text=True
    )

    speed_f  = float(speed)
    fuel_f   = float(fuel)
    kmh      = speed_f * 3.6
    fuel_pct = fuel_f / 50000.0 * 100.0
    gear_name = GEAR_NAMES.get(gear, f"GEAR({gear})")
    state     = safe_mode_state(speed_f)

    if result.returncode == 0:
        print(f"  [{idx+1:02d}/{total}]  {kmh:5.1f} km/h  {gear_name:<8}  {fuel_pct:5.1f}%  → {state}")
        return True
    else:
        print(f"  [{idx+1:02d}/{total}]  ERROR: {result.stderr.strip() or result.stdout.strip()}")
        return False


def check_adb() -> bool:
    """Return True if adb can reach a device."""
    r = subprocess.run(["adb", "devices"], capture_output=True, text=True)
    lines = [l for l in r.stdout.splitlines() if l.strip() and "List of" not in l]
    if not lines:
        print("ERROR: No adb device found. Connect RPi5 and try again.")
        return False
    return True


def main():
    parser = argparse.ArgumentParser(
        description="SafeMode VHAL simulator — writes vehicle data to safemoded"
    )
    parser.add_argument(
        "--interval", type=float, default=5.0,
        help="Seconds between each data step (default: 5)"
    )
    parser.add_argument(
        "--loop", action="store_true",
        help="Loop through the 20 values continuously (Ctrl+C to stop)"
    )
    args = parser.parse_args()

    if not check_adb():
        sys.exit(1)

    speeds = load_data("speed_data.txt")
    gears  = load_data("gear_data.txt")
    fuels  = load_data("fuel_data.txt")

    n = len(speeds)
    if len(gears) != n or len(fuels) != n:
        print(f"ERROR: data files must all have {n} values")
        sys.exit(1)

    print("=" * 60)
    print("SafeMode Simulator (RPi5 dev mode)")
    print(f"  Steps:    {n}")
    print(f"  Interval: {args.interval}s per step")
    print(f"  Loop:     {args.loop}")
    print(f"  Sim file: {SIM_FILE}")
    print("=" * 60)
    print()
    print("  Step   Speed      Gear      Fuel%    SafeModeState")
    print("  " + "-" * 56)

    try:
        while True:
            for i in range(n):
                inject(speeds[i], gears[i], fuels[i], i, n)
                time.sleep(args.interval)
            if not args.loop:
                break
        print()
        print("Simulation complete.")
    except KeyboardInterrupt:
        print()
        print("Stopped. Resetting to PARK, 0 km/h, full tank.")
        subprocess.run(
            ["adb", "shell", f"echo '0.0 4 50000' > {SIM_FILE}"],
            capture_output=True
        )


if __name__ == "__main__":
    main()
