# Timing baseline

Plan step 6. Recorded on 2026-10-21. **TV frames: PASS. Own TX: FAIL** (expected, fixed
in plan step 9b).

## Method

GPIO4 is jumpered to GPIO10 (the CEC line). The `cec_probe` component reads GPIO4 with
an RMT RX channel at 1 µs resolution. The RMT timestamps each level in hardware, so the
result does not depend on the CPU or on interrupt latency.

- One RMT symbol is one bit: the low level, then the high level up to the next falling edge.
- 5 ms of idle ends a frame. A start bit is low for 3.7 ms, so it stays inside one frame.
- The probe decodes the header of each frame. Bits of a frame with initiator 4 (the
  Adapter) count as own TX. An ACK bit stretched to a 0 belongs to the follower.
- Every 10 s, the probe logs a histogram in 50 µs bins for each owner and class.
- `scripts/check_timing.py` checks each bin against the HDMI 1.4b CEC windows. A bin
  passes only when all of it is inside the window.

Traffic: TV and soundbar frames, and the Adapter sending Active Source and Image View On
6 times each, plus its replies to the TV.

## Results

Range is the lowest and highest bin start, in µs.

| Class | Window (µs) | TV and soundbar: n, range | Adapter: n, range, out of window |
|---|---|---|---|
| Start bit low | 3500 to 3900 | 361, 3700 | 18, 3700 to 4600, **4** |
| Start bit period | 4300 to 4700 | 361, 4500 to 4550 | 18, 4500 to 5400, **8** |
| Data 0 low | 1300 to 1700 | 2383, 1500 | 402, 1500 to 2450, **6** |
| Data 1 low | 400 to 800 | 1163, 600 | 111, 600 to 800, **1** |
| ACK 0 low | 1300 to 1700 | 63, 1500 to 1550 | 43, 1450 to 1500, 0 |
| ACK 1 low | 400 to 800 | 300, 600 | 45, 600 to 700, 0 |
| Bit period | 2050 to 2750 | 3579, 2400 to 2500 | 552, 2400 to 3000, **3** |

No frame was malformed.

## Findings

1. The TV and the soundbar are exact. They are the reference, and `scripts/testdata/timing_good.txt` is from this run.
2. The probe and the check work: the TV frames pass, and the Adapter frames fail.
3. The Adapter ACKs (driven from the receive interrupt and the gptimer) are in the window.
4. The Adapter TX stretches some levels by 0.4 to 0.9 ms. 8 of 18 start bits have a period out of window.
   About 1 % of the data bits are out of window. The TX bit-bangs from `loop()` with
   busy waits, and a task of higher priority (WiFi) can preempt it. This explains the
   send retries in plan step 5.

## Decision

The baseline is recorded. Plan step 9b replaces the `loop()` TX with a gptimer state
machine. Its exit gate is `check_timing.py --owner own` with no failure.

## Run it

```bash
esphome logs PlumCec.yaml --device <adapter IP> > probe.log   # some minutes of traffic
python3 scripts/check_timing.py probe.log --owner other       # TV frames
python3 scripts/check_timing.py probe.log --owner own         # Adapter frames
```

## After plan step 9b

Recorded on 2026-10-21. **Own TX: PASS.** A gptimer drives the TX edges (step 9b).

Traffic: 1 h of Give Device Power Status to the TV, one each 2 s, with WiFi and the API on.

| Check | Result |
|---|---|
| Sends | 1802 of 1802 succeeded. 1800 on the first attempt, 2 on the second |
| `check_timing.py --owner own` | PASS. 1802 start bits, 32490 data bits, 5796 ACK bits, all in the windows |
| Read-back differences | 0 |
| Resets | 0 |
| Own frames marked as own | 1802 of 1802 |

Before step 9b, 8 of 18 start periods were out of window, and sends needed 2 to 6 attempts.
