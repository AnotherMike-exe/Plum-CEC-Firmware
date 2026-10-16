# Plum-CEC-Firmware

> ESP32-C3 firmware that makes a DIY HDMI-CEC adapter look like a Pulse-Eight dongle to Linux.

GPUs have no CEC hardware, so a PC cannot turn a TV on or off, or select an input. This
firmware drives the HDMI CEC line from one GPIO. It speaks the Pulse-Eight serial
protocol over USB, so the Linux `pulse8-cec` driver gives the PC `/dev/cec0`. It also
takes commands over WiFi (MQTT and HTTP) for Home Assistant. Both paths share one CEC
bus.

The hardware is in a separate repo: **[Plum-CEC-Hardware](https://github.com/AnotherMike-exe/Plum-CEC-Hardware)**.

---

## Usage

The firmware is a scaffold. The components are stubs, and no CEC function works yet.

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Working when: the monitor shows the `plum_cec` start-up log on UART0.

After the Pulse-Eight endpoint works (milestone M2), attach it on the PC:

```bash
sudo inputattach --pulse8-cec --baud <rate> /dev/ttyUSB0
cec-ctl -d /dev/cec0 --playback -p 1.0.0.0
```

Full setup is in [docs/DEV-SETUP.md](docs/DEV-SETUP.md).

## Details

Commands, pins and the MQTT topics are in
[docs/QUICK-REFERENCE.md](docs/QUICK-REFERENCE.md).

Known limitations:

- The adapter does not use sleep modes while USB is connected. USB Serial/JTAG stops in
  light sleep.
- The native USB device enumerates as Espressif `303a:1001`. Match udev rules on the
  serial number.
- Wiring and power limits are in the hardware repo.

- [Architecture](docs/ARCHITECTURE.md): the design and the decisions behind it
- [Dev setup](docs/DEV-SETUP.md): from a clone to a flashed board
- [Quick reference](docs/QUICK-REFERENCE.md): commands, pins and topics

## Attributions

Built on [ESP-IDF](https://github.com/espressif/esp-idf). The USB protocol follows the
Linux [`pulse8-cec`](https://git.zx2c4.com/linux-dev/tree/drivers/media/cec/usb/pulse8/pulse8-cec.c)
driver.

Prior art: [esphome-native-hdmi-cec](https://github.com/Palakis/esphome-native-hdmi-cec),
[pico-cec](https://github.com/gkoh/pico-cec) and the
[SteamOS CEC Toolkit](https://github.com/Twsts/steamos-cec-toolkit).

Licensed under [MIT](LICENSE).

---

**Repository**: https://github.com/AnotherMike-exe/Plum-CEC-Firmware · **Hardware**: https://github.com/AnotherMike-exe/Plum-CEC-Hardware · **Maintainer**: Plum Solutions
