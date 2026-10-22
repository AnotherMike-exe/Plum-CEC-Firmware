#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Gate B tests (plan step 8). Run on the Host as root:
#   sudo bash gate_b.sh t1            # T1 and T2: 50 attach cycles, 50 cec-ctl cycles
#   sudo bash gate_b.sh t3 3600       # T3: pings and traffic for N seconds
#   sudo bash gate_b.sh attach        # attach and configure, for T3b and T5
#   sudo bash gate_b.sh detach
#   sudo bash gate_b.sh t4 20         # T4: N suspend and resume cycles (ask the owner first)
# A reset of the Adapter makes the USB device enumerate again, so a new USB device number
# means a reset.

# shellcheck disable=SC2016  # wait_for evaluates its argument later, so single quotes are correct
set -u

PORT=${PLUMCEC_PORT:-/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_58:CF:79:D8:71:70-if00}
CEC=/dev/cec0
UNIT=plumcec-inputattach
AWAKE=${PLUMCEC_AWAKE:-30} # seconds awake in each T4 cycle, so the network comes up and SSH can stop the test
FAILS=0
ATTACH_MS=0

log() { printf '%s %s\n' "$(date +%T.%3N)" "$*"; }
fail() { log "FAIL: $*"; FAILS=$((FAILS + 1)); }

usb_devnum() {
    local tty
    tty=$(basename "$(readlink -f "$PORT")")
    cat "/sys/class/tty/${tty}/device/../devnum" 2>/dev/null || echo none
}

now_ms() { date +%s%3N; }

wait_for() { # wait_for <test expression> <timeout ms>
    local end=$(($(now_ms) + $2))
    while ! eval "$1"; do
        (($(now_ms) > end)) && return 1
        sleep 0.01
    done
}

detach() {
    systemctl stop "$UNIT" 2>/dev/null
    systemctl reset-failed "$UNIT" 2>/dev/null
    pkill -x inputattach 2>/dev/null
    wait_for '[ ! -e $CEC ]' 3000 || fail "$CEC still present after detach"
}

attach() { # sets ATTACH_MS
    local start
    start=$(now_ms)
    systemd-run --quiet --unit "$UNIT" /usr/local/bin/inputattach --pulse8-cec "$PORT"
    if ! wait_for '[ -e $CEC ]' 3000; then
        fail "no $CEC after 3 s"
        ATTACH_MS=-1
        return
    fi
    ATTACH_MS=$(($(now_ms) - start))
}

configure() {
    cec-ctl -d "$CEC" --playback -p 1.0.0.0 >/dev/null || fail "cec-ctl configure"
    wait_for 'cec-ctl -d $CEC | grep -q "Logical Address  *: 4"' 3000 || fail "logical address 4 not claimed"
}

kernel_errors() { # count driver error lines since the given dmesg line count
    dmesg | tail -n +"$1" | grep -E "cec0|pulse8" | grep -ciE "timed out|failed|error"
}

t1() {
    local dev0 versions0 lines0 max=0
    detach
    dev0=$(usb_devnum)
    versions0=$(dmesg | grep -c "Firmware version")
    lines0=$(($(dmesg | wc -l) + 1))
    log "T1: 50 attach and detach cycles (USB device $dev0)"
    for i in $(seq 1 50); do
        attach
        ((ATTACH_MS > max)) && max=$ATTACH_MS
        ((ATTACH_MS > 1000)) && fail "attach $i took $ATTACH_MS ms"
        detach
    done
    local versions=$(($(dmesg | grep -c "Firmware version") - versions0))
    log "T2: slowest attach $max ms, version reported $versions of 50 times"
    ((versions == 50)) || fail "version reported $versions times"
    attach
    configure
    log "T1: 50 cec-ctl open and close cycles"
    for i in $(seq 1 50); do
        cec-ctl -d "$CEC" >/dev/null || fail "cec-ctl open $i"
    done
    [ "$(usb_devnum)" = "$dev0" ] || fail "the Adapter reset (USB device $dev0 -> $(usb_devnum))"
    local errors
    errors=$(kernel_errors "$lines0")
    ((errors == 0)) || fail "$errors driver error lines"
    log "T1/T2 done, $FAILS failures"
}

t3() {
    local seconds=${1:-3600} dev0 lines0 end sent=0
    dev0=$(usb_devnum)
    lines0=$(($(dmesg | wc -l) + 1))
    [ -e "$CEC" ] || { attach; configure; }
    log "T3: $seconds s of traffic (USB device $dev0)"
    end=$(($(date +%s) + seconds))
    while (($(date +%s) < end)); do
        cec-ctl -d "$CEC" --to 0 --image-view-on >/dev/null 2>&1 || fail "send $sent"
        sent=$((sent + 1))
        if ((sent % 30 == 0)); then
            [ -e "$CEC" ] || fail "$CEC gone"
            [ "$(usb_devnum)" = "$dev0" ] || { fail "the Adapter reset"; dev0=$(usb_devnum); }
            log "T3: $sent sends, $(kernel_errors "$lines0") driver error lines, $FAILS failures"
        fi
        sleep 2
    done
    local errors pings
    errors=$(kernel_errors "$lines0")
    pings=$(dmesg | tail -n +"$lines0" | grep -c "transmit PING")
    ((errors == 0)) || fail "$errors driver error lines"
    log "T3 done: $sent sends, $pings driver pings, $FAILS failures"
}

t4() {
    local cycles=${1:-20}
    log "T4: $cycles suspend and resume cycles"
    for i in $(seq 1 "$cycles"); do
        [ -e "$CEC" ] || { attach; configure; }
        rtcwake -m mem -s 20 >/dev/null || fail "rtcwake $i"
        detach
        wait_for '[ -e $PORT ]' 5000 || fail "cycle $i: no USB port after resume"
        attach
        configure
        log "T4 cycle $i: attach $ATTACH_MS ms after resume, $FAILS failures"
        sleep "$AWAKE"
    done
    log "T4 done, $FAILS failures"
}

case "${1:-}" in
    t1) t1 ;;
    t3) t3 "${2:-3600}" ;;
    t4) t4 "${2:-20}" ;;
    attach) attach; configure; log "attach $ATTACH_MS ms" ;;
    detach) detach ;;
    *) sed -n '3,9p' "$0"; exit 2 ;;
esac
log "RESULT: $FAILS failures"
exit $((FAILS > 0))
