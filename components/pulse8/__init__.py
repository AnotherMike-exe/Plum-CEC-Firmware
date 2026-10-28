# SPDX-License-Identifier: MIT
"""Pulse-Eight USB-CEC emulation. With host_usb_id it runs on TinyUSB CDC (ESP32-S3), else on USB Serial/JTAG."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CONF_HOST_USB_ID = "host_usb_id"

pulse8_ns = cg.esphome_ns.namespace("pulse8")
Pulse8Component = pulse8_ns.class_("Pulse8Component", cg.Component)
HostUsb = cg.esphome_ns.namespace("host_usb").class_("HostUsb", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Pulse8Component),
        # The TinyUSB link to the Host (S3). Without it, Pulse-Eight runs on USB Serial/JTAG.
        cv.Optional(CONF_HOST_USB_ID): cv.use_id(HostUsb),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if CONF_HOST_USB_ID in config:
        cg.add_define("USE_PULSE8_HOST_USB")
        host_usb = await cg.get_variable(config[CONF_HOST_USB_ID])
        cg.add(var.set_host_usb(host_usb))
