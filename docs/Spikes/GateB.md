# Gate B

Plan step 8. Recorded on 2026-10-21. **GATE B: PASS.** The Host display does not recover after `rtcwake` suspends. Plan step 11 fixes this.

## Setup

- The Adapter runs `Pulse8Core` in a FreeRTOS task (`pulse8`, priority 6) on USB
  Serial/JTAG. The task reads with a 5 ms timeout. Writes never wait. A full TX buffer
  drops the bytes and counts them.
- `StubBus` replaces the CEC bus. It reports the TX result 50 ms after each send. A poll
  (header only, initiator equal to destination) gets a NACK. Every other frame gets
  success. With the switch "Pulse8 stub RX frames" on, it also gives the RX frame
  `0F:87:08:00:46` every 100 ms.
- The Host runs `inputattach --pulse8-cec` as the systemd unit `plumcec-inputattach`,
  with `pulse8_cec` debug 2. `cec-ctl --playback -p 1.0.0.0` claims logical address 4.
- `scripts/gate_b.sh` runs the tests on the Host. A reset of the Adapter makes the USB
  device enumerate again, so the script reads the USB device number from sysfs. A new
  number means a reset.

## Results

| Test | Pass condition | Result |
|---|---|---|
| T1 | 50 attach cycles and 50 `cec-ctl` cycles, no reset | **PASS**. USB device number did not change. |
| T2 | Attach in 1 s, version in `dmesg` | **PASS**. Slowest attach 30 ms. Version 50 of 50 times. |
| T3 | 1 h of traffic with WiFi on, no USB drop | **PASS**. 1752 sends of Image View On, 11 driver pings, 0 driver errors. |
| T3b | 10 min of stub RX with no reader, then a read | **PASS**. The Adapter dropped 124464 B and did not stall. Attach 30 ms, same USB device, RX frames at once. |
| T4 | 20 suspend and resume cycles | **PASS** for the Adapter. 20 of 20, attach 18 to 32 ms after resume, 0 failures. The Host display failed (finding 3). |
| T5 | API traffic during USB traffic, no driver error | **PASS**. 80 API switch toggles and `esphome logs`, 747 RX frames, 0 driver errors, 0 dropped bytes. |

## Findings

1. The first stub ACKed every frame, polls too. The kernel then found every logical
   address in use, and the logical address stayed "Not Allocated". An empty CEC bus
   gives no ACK to a poll, so the stub now gives a NACK.
2. The plan detects a reset with a boot counter on UART0. The USB device number gives
   the same result from the Host alone, so the test needs no Mac.
3. After T4 cycle 2, the Host gave no HDMI picture until a reboot. KWin logged
   "Pageflip timed out" (nvidia-drm) 310 times. The NVIDIA units `nvidia-suspend` and
   `nvidia-resume` are disabled. Also, `rtcwake -m mem` skips the systemd sleep hooks, so
   those units do not run even when enabled. Plan step 11 enables them, and uses
   `systemctl suspend`.
4. The first T4 run left the Host awake for only 2 to 3 s in each cycle. The network did
   not come up in that time, so the test could not be stopped over SSH. `t4` now keeps
   the Host awake for 30 s in each cycle (`PLUMCEC_AWAKE`).
5. Also, WiFi on the Host moved between two access points after each resume.

## Run it

```bash
scp scripts/gate_b.sh admin@<host>:~/plumcec/
sudo bash ~/plumcec/gate_b.sh t1                              # T1 and T2
sudo systemd-run --unit plumcec-t3 bash -c 'bash ~admin/plumcec/gate_b.sh t3 3600 > ~admin/plumcec/gateb-t3.log 2>&1'
sudo systemd-run --unit plumcec-t4 bash -c 'bash ~admin/plumcec/gate_b.sh t4 20 > ~admin/plumcec/gateb-t4.log 2>&1'   # ask the owner first
```
