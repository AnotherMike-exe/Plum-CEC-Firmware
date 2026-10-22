# SPDX-License-Identifier: MIT
"""Plan step 4 (GATE A): test the Adapter USB Serial/JTAG echo from the Host.

Uses only the standard library, because the Host has no pyserial.

    python3 usb_echo_spike.py listen PORT SECONDS
    python3 usb_echo_spike.py echo PORT COUNT
    python3 usb_echo_spike.py openclose PORT COUNT
"""

import os
import select
import sys
import termios
import time
import tty


def open_port(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    tty.setraw(fd)
    return fd


def read_for(fd, seconds, want=None):
    """Read until `want` bytes arrive or `seconds` pass. Return the bytes."""
    data = b""
    end = time.monotonic() + seconds
    while time.monotonic() < end and (want is None or len(data) < want):
        ready, _, _ = select.select([fd], [], [], max(0.0, end - time.monotonic()))
        if ready:
            try:
                data += os.read(fd, 4096)
            except BlockingIOError:
                pass
    return data


def listen(path, seconds):
    fd = open_port(path)
    data = read_for(fd, seconds)
    os.close(fd)
    print(f"listen: {len(data)} bytes in {seconds} s")
    if data:
        print(data.hex(" "))
        print(repr(data))
    return 0 if not data else 1


def echo(path, count):
    fd = open_port(path)
    stray = read_for(fd, 0.5)
    if stray:
        print(f"stray bytes before the test: {len(stray)}: {stray!r}")
    fails, worst = 0, 0.0
    for i in range(count):
        # 32 bytes that change with each round, all values 0 to 255 over the run.
        pattern = bytes((i * 7 + k) & 0xFF for k in range(32))
        start = time.monotonic()
        os.write(fd, pattern)
        got = read_for(fd, 1.0, want=len(pattern))
        took = time.monotonic() - start
        worst = max(worst, took)
        if got != pattern:
            fails += 1
            if fails <= 5:
                print(f"round {i}: sent {pattern.hex()} got {got.hex()}")
    os.close(fd)
    print(f"echo: {count - fails}/{count} rounds match, worst round trip {worst * 1000:.1f} ms")
    return 0 if fails == 0 else 1


def openclose(path, count):
    extra = 0
    for _ in range(count):
        fd = open_port(path)
        extra += len(read_for(fd, 0.05))
        termios.tcflush(fd, termios.TCIOFLUSH)
        os.close(fd)
        time.sleep(0.05)
    still_there = os.path.exists(path)
    print(f"openclose: {count} cycles, {extra} unexpected bytes, port present after: {still_there}")
    return 0 if extra == 0 and still_there else 1


def main(argv):
    mode, path, n = argv[1], argv[2], argv[3]
    if mode == "listen":
        return listen(path, float(n))
    if mode == "echo":
        return echo(path, int(n))
    if mode == "openclose":
        return openclose(path, int(n))
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
