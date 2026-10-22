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

## HDMI

**Source**:
The HDMI device on the input side of the Adapter. It is often the Host, but it can be a
different device, for example a console, when a charger powers the Adapter.
_Avoid_: input device, player

**Passthrough**:
The HDMI path through the Adapter, as plain copper from the plug to the receptacle. It
works with or without power.
_Avoid_: bypass, loop-through

**Tap**:
A low-speed HDMI line that the Adapter connects to through isolation: CEC, SCL or SDA.
_Avoid_: sniff line, probe

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

**Physical address**:
The address of an HDMI input on the TV (for example `1.0.0.0`). The Adapter reads it
from the EDID when the Source reads the EDID, and shares it with the Source.
_Avoid_: port number, PA

**Logical address**:
The CEC bus address (0 to 15) that a device claims by polling. 15 is Unregistered.
_Avoid_: LA, CEC ID

**Proxy**:
The role of the Adapter when the Source has no CEC. The Adapter claims a logical address
and acts as the CEC device for the Source.
_Avoid_: host mode, active mode

**Guest**:
The role of the Adapter when the Source has its own CEC. The Adapter claims no logical
address, and sends as Unregistered or as the Source.
_Avoid_: listen mode, passive mode
