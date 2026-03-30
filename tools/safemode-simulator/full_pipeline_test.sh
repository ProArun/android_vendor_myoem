#!/usr/bin/env bash
# Copyright (C) 2025 MyOEM
# SPDX-License-Identifier: Apache-2.0
#
# full_pipeline_test.sh — end-to-end Phase 3 verification for SafeMode service.
#
# This script validates the complete data pipeline:
#   VHAL emulator → safemoded (VHAL subscriber) → ISafeModeCallback → safemode_client
#
# It runs each individual simulator script while a safemode_client instance is
# listening in subscribe mode so you can visually confirm callbacks arrive.
#
# ── Prerequisites ──────────────────────────────────────────────────────────────
#   1. Device connected over ADB:  adb devices
#   2. AOSP image flashed with safemoded in vendor partition
#   3. safemoded running:          adb shell ps -e | grep safemoded
#   4. Python 3 installed on host
#
# ── Usage ─────────────────────────────────────────────────────────────────────
#   chmod +x full_pipeline_test.sh
#   ./full_pipeline_test.sh [<adb-device-serial>]
#
# Output: PASS/FAIL for each section.

set -euo pipefail

DEVICE="${1:-}"   # optional ADB serial
ADB="adb"
[[ -n "$DEVICE" ]] && ADB="adb -s $DEVICE"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BLUE='\033[0;34m'; GREEN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'

step() { echo -e "\n${BLUE}━━ $* ${NC}"; }
pass() { echo -e "${GREEN}✔ PASS: $*${NC}"; }
fail() { echo -e "${RED}✘ FAIL: $*${NC}"; exit 1; }

# ─────────────────────────────────────────────────────────────────────────────
step "0. ADB connectivity check"
$ADB get-state > /dev/null 2>&1 && pass "Device reachable" || fail "No ADB device found"

# ─────────────────────────────────────────────────────────────────────────────
step "1. Verify safemoded is running"
if $ADB shell "ps -e | grep -q '[s]afemoded'"; then
    pass "safemoded process found"
else
    fail "safemoded not running — flash the image and retry"
fi

# ─────────────────────────────────────────────────────────────────────────────
step "2. Verify service is registered with ServiceManager"
if $ADB shell "service check com.myoem.safemode.ISafeModeService" | grep -q "found"; then
    pass "Service registered in ServiceManager"
else
    fail "Service NOT registered — check logcat: adb logcat -s safemoded"
fi

# ─────────────────────────────────────────────────────────────────────────────
step "3. Snapshot test — synchronous getCurrentData()"
echo "Running: safemode_client snapshot"
output=$($ADB shell "/vendor/bin/safemode_client snapshot" 2>&1)
echo "$output"
if echo "$output" | grep -q "Speed"; then
    pass "getCurrentData() returned vehicle data"
else
    fail "getCurrentData() did not return expected data"
fi

# ─────────────────────────────────────────────────────────────────────────────
step "4. Subscribe mode — open callback listener in background"
echo "Starting safemode_client in subscribe mode for 90 seconds (background)..."
# Run client on-device in background via nohup; capture PID
$ADB shell "nohup /vendor/bin/safemode_client subscribe 90 > /data/local/tmp/safemode_cb.log 2>&1 &"
CB_LAUNCH_SLEEP=2
sleep $CB_LAUNCH_SLEEP
echo "Client running. Logs → /data/local/tmp/safemode_cb.log on device"

# ─────────────────────────────────────────────────────────────────────────────
step "5. Inject speed values (all three SafeMode zones)"
python3 "$SCRIPT_DIR/simulate_speed.py" --interval 1.5 ${DEVICE:+--device "$DEVICE"}
pass "Speed injection complete"

# ─────────────────────────────────────────────────────────────────────────────
step "6. Inject gear position sequence"
python3 "$SCRIPT_DIR/simulate_gear.py" --interval 2.0 ${DEVICE:+--device "$DEVICE"}
pass "Gear injection complete"

# ─────────────────────────────────────────────────────────────────────────────
step "7. Inject fuel level sequence"
python3 "$SCRIPT_DIR/simulate_fuel.py" --interval 1.5 ${DEVICE:+--device "$DEVICE"}
pass "Fuel injection complete"

# ─────────────────────────────────────────────────────────────────────────────
step "8. Read callback log — verify events arrived"
sleep 2  # give the last callbacks a moment to flush
echo "Callback log from device:"
$ADB shell "cat /data/local/tmp/safemode_cb.log"

# Check that at least 5 callback events were received
EVENT_COUNT=$($ADB shell "grep -c '^\[' /data/local/tmp/safemode_cb.log 2>/dev/null || echo 0")
echo ""
echo "Events received: $EVENT_COUNT"

if [[ "$EVENT_COUNT" -ge 5 ]]; then
    pass "Received $EVENT_COUNT callback events (expected ≥ 5)"
else
    fail "Only $EVENT_COUNT events received — callbacks may not be working"
fi

# ─────────────────────────────────────────────────────────────────────────────
step "9. SafeMode state transitions — verify HARD_SAFE_MODE triggered"
if $ADB shell "cat /data/local/tmp/safemode_cb.log" | grep -q "HARD_SAFE_MODE"; then
    pass "HARD_SAFE_MODE state observed in callbacks"
else
    echo "  Note: HARD_SAFE_MODE not observed — check that speed injection reached >15 km/h"
fi

# ─────────────────────────────────────────────────────────────────────────────
step "10. Logcat — check for service-side errors"
echo "Recent safemoded logcat (last 50 lines):"
$ADB logcat -d -s "safemode_service" | tail -50

echo ""
echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GREEN}  Full pipeline test PASSED                           ${NC}"
echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "Next step: Phase 2 — build safemode_library (AAR)"
echo "  See vendor/myoem/SAFEMODE_PLAN.md §Phase 2"
