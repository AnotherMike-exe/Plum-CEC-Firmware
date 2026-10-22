# SPDX-License-Identifier: MIT
"""Check the cec_probe histograms in an Adapter log against the CEC timing windows.

    python3 scripts/check_timing.py LOG [--owner own|other|all]

Exit 0 when every sample is inside its window, 1 when one is outside, 2 when the log
has no cec_probe histogram line.
"""

import argparse
import re
import sys

BIN_US = 50

# HDMI 1.4b CEC: (minimum, maximum) in microseconds
WINDOWS = {
    "start_low": (3500, 3900),
    "start_period": (4300, 4700),
    "data0_low": (1300, 1700),
    "data1_low": (400, 800),
    "ack0_low": (1300, 1700),
    "ack1_low": (400, 800),
    "bit_period": (2050, 2750),
}

LINE = re.compile(r"cec_probe[^\]]*\]: (own|other) (\w+) n=(\d+)(.*)$")


def parse(lines):
    """Yield (owner, bit_class, bin_start_us, count, overflow) for each bin in the log."""
    for line in lines:
        match = LINE.search(line.rstrip())
        if not match:
            continue
        owner, bit_class, _, rest = match.groups()
        for token in rest.split():
            if token == "truncated":
                yield owner, bit_class, None, 0, True
                continue
            start, count = token.split(":")
            overflow = start.endswith("+")
            yield owner, bit_class, int(start.rstrip("+")), int(count), overflow


def bin_in_window(bit_class, start, overflow):
    low, high = WINDOWS[bit_class]
    return not overflow and start >= low and start + BIN_US - 1 <= high


def check(lines, owner_filter="all"):
    """Return (totals, failures). A failure is (owner, class, bin start, count)."""
    totals = {}
    failures = []
    for owner, bit_class, start, count, overflow in parse(lines):
        if owner_filter != "all" and owner != owner_filter:
            continue
        if bit_class not in WINDOWS:
            failures.append((owner, bit_class, start, count))
            continue
        totals[(owner, bit_class)] = totals.get((owner, bit_class), 0) + count
        if start is None or not bin_in_window(bit_class, start, overflow):
            failures.append((owner, bit_class, start, count))
    return totals, failures


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("log")
    parser.add_argument("--owner", choices=["own", "other", "all"], default="all")
    args = parser.parse_args(argv)
    with open(args.log, encoding="utf-8", errors="replace") as log:
        totals, failures = check(log, args.owner)
    if not totals and not failures:
        print("no cec_probe histogram lines found")
        return 2
    for (owner, bit_class), total in sorted(totals.items()):
        low, high = WINDOWS[bit_class]
        print(f"{owner:5} {bit_class:12} n={total:<6} window {low}-{high} us")
    for owner, bit_class, start, count in failures:
        if start is None:
            print(f"FAIL {owner} {bit_class}: the log line is truncated")
        elif bit_class not in WINDOWS:
            print(f"FAIL {owner} {bit_class}: unknown class")
        else:
            low, high = WINDOWS[bit_class]
            print(f"FAIL {owner} {bit_class}: {count} in {start}-{start + BIN_US - 1} us, window {low}-{high} us")
    print("FAIL" if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
