#!/usr/bin/env python3
# Copyright (C) 2025 MyOEM
# SPDX-License-Identifier: Apache-2.0
#
# simulate_gear.py — injects CURRENT_GEAR values into the VHAL emulator.
#
# How it works:
#   Reads gear integer codes from gear_data.txt and injects them via:
#       adb shell cmd car_service inject-vhal-event 0x11400400 0 <value>
#
#   CURRENT_GEAR is ON_CHANGE — the VHAL only publishes an event when the gear
#   actually changes. Injecting the same value twice will NOT fire a second
#   VHAL event (the emulator deduplicates unchanged values).
#
# Usage:
#   python3 simulate_gear.py [--interval 3.0] [--device <serial>]

import subprocess
import time
import argparse
import os
import sys

# ── VHAL property constants ───────────────────────────────────────────────────
# CURRENT_GEAR: 0x11400400
#   - Type: INT32
#   - Change mode: ON_CHANGE (event fires only when value differs from last)
#   - Area: 0 (global)
PROP_GEAR = "0x11400400"
AREA_ID   = "0"

# VehicleGear enum — maps integer code to human-readable name.
# These values come from android/hardware/automotive/vehicle/VehicleGear.aidl
GEAR_NAMES = {
    1:    "NEUTRAL",
    2:    "REVERSE",
    4:    "PARK",
    8:    "DRIVE",
    16:   "GEAR_1",
    32:   "GEAR_2",
    64:   "GEAR_3",
    128:  "GEAR_4",
    256:  "GEAR_5",
    512:  "GEAR_6",
    1024: "GEAR_7",
    2048: "GEAR_8",
    4096: "GEAR_9",
}


def gear_name(code: int) -> str:
    return GEAR_NAMES.get(code, f"UNKNOWN(0x{code:04x})")


def inject_vhal_event(prop: str, area: str, value: str, device: str | None) -> bool:
    """
    Inject an integer VHAL property value via CarService's shell command.
    INT32 properties use the same inject-vhal-event command as FLOAT;
    CarService reads the property type from the VHAL config and casts accordingly.
    """
    cmd = ["adb"]
    if device:
        cmd += ["-s", device]
    cmd += ["shell", "cmd", "car_service", "inject-vhal-event", prop, area, value]

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  [ERROR] {result.stderr.strip()}", file=sys.stderr)
        return False
    return True


def load_data(filepath: str) -> list[int]:
    values = []
    with open(filepath) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            try:
                values.append(int(float(line)))  # allow "4.0" as well as "4"
            except ValueError:
                print(f"  [WARN] Skipping non-numeric line: {line!r}", file=sys.stderr)
    return values


def main():
    parser = argparse.ArgumentParser(
        description="Inject CURRENT_GEAR values into AAOS VHAL emulator"
    )
    parser.add_argument("--interval", type=float, default=3.0,
                        help="Seconds between injections (default: 3.0)")
    parser.add_argument("--device",   type=str,   default=None,
                        help="ADB device serial")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    data_file  = os.path.join(script_dir, "gear_data.txt")

    if not os.path.exists(data_file):
        print(f"ERROR: data file not found: {data_file}", file=sys.stderr)
        sys.exit(1)

    gears = load_data(data_file)
    if not gears:
        print("ERROR: no gear values found in data file", file=sys.stderr)
        sys.exit(1)

    print(f"Simulating {len(gears)} gear positions at {args.interval}s interval")
    print(f"Property: CURRENT_GEAR ({PROP_GEAR}), Area: {AREA_ID}")
    print("Note: ON_CHANGE — duplicate consecutive values won't trigger VHAL events")
    print("-" * 60)

    prev_gear = None
    for i, gear_code in enumerate(gears, start=1):
        name = gear_name(gear_code)
        changed = (gear_code != prev_gear)
        change_note = "" if changed else " (same as previous — no VHAL event expected)"

        print(f"[{i:02d}/{len(gears)}] Gear: {gear_code:5d}  ({name}){change_note}")
        inject_vhal_event(PROP_GEAR, AREA_ID, str(gear_code), args.device)

        prev_gear = gear_code
        if i < len(gears):
            time.sleep(args.interval)

    print("-" * 60)
    print("Gear simulation complete.")


if __name__ == "__main__":
    main()
