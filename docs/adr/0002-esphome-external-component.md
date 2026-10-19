# ESPHome external component, not pure ESP-IDF

The firmware is an ESPHome configuration with our own external components, so that other
people can reuse and change the CEC and Pulse-Eight parts in their own ESPHome devices.
We did consider pure ESP-IDF, which gives full control of task priorities and no
dependency on the ESPHome release cycle. Home Assistant connects through the ESPHome
native API. MQTT is an option in the YAML for users without Home Assistant.

## Consequences

- The Pulse-Eight protocol code is plain C++ with no ESPHome dependency, so it has host
  unit tests and can move to another platform.
- CEC TX runs in its own FreeRTOS task inside the component, so a send does not block
  the ESPHome main loop.
- The first work is a spike: an external component must own USB Serial/JTAG while the
  logs stay on UART0. If the spike fails, we change to the ESP32-S3 (separate USB-OTG
  peripheral) and stay on ESPHome.
- The YAML pins a stable ESPHome release, not `dev`, because the people who reuse the
  component use stable releases. Our copy of `hdmi_cec` changes to compile on it.
