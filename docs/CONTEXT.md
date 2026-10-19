# Plum-CEC

A DIY HDMI-CEC adapter. It lets a PC and Home Assistant control a TV over the CEC bus,
so a gaming PC wakes and sleeps the TV like a console.

## Devices

**Adapter**:
The Plum-CEC device. It connects to the CEC bus, to the Host over USB, and to the
network over WiFi.
_Avoid_: dongle, bridge, board (except for the bare PCB)

**Host**:
The PC that the Adapter connects to over USB. Its kernel driver treats the Adapter as a
Pulse-Eight USB-CEC adapter.
_Avoid_: computer, client

**TV**:
The display at the other end of the HDMI cable. It is logical address 0 on the CEC bus.
_Avoid_: sink, display, monitor

## Front-ends

**USB front-end**:
The path from the Host to the CEC bus, through the Pulse-Eight protocol.
_Avoid_: serial path, CDC path

**Network front-end**:
The path from Home Assistant (or any MQTT client) to the CEC bus.
_Avoid_: WiFi path, MQTT path

**Attached**:
The state in which the Host kernel controls the Adapter through the USB front-end.
The opposite state is **Detached** (Host asleep, off, or not running the driver).
_Avoid_: connected, online

## Identity

**Identity**:
The logical address, physical address, device type and OSD name with which the Adapter
sends frames on the CEC bus.
_Avoid_: address (alone), profile

**Kernel identity**:
The Identity that the Host kernel sets while Attached.

**Default identity**:
The Identity that the Adapter uses while Detached. It starts as the last Kernel
identity, and the Network front-end can change it.
_Avoid_: fallback identity, stored identity
