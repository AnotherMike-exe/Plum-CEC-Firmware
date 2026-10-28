# SPDX-License-Identifier: MIT
"""The USB link to the Host on the S3 USB OTG port: TinyUSB CDC and HID with remote wakeup (plan step 15)."""

import esphome.codegen as cg
from esphome.components import esp32
from esphome.components.esp32 import (
    VARIANT_ESP32S3,
    add_idf_component,
    add_idf_sdkconfig_option,
)
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32"]

host_usb_ns = cg.esphome_ns.namespace("host_usb")
HostUsb = host_usb_ns.class_("HostUsb", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema({cv.GenerateID(): cv.declare_id(HostUsb)}).extend(cv.COMPONENT_SCHEMA),
    esp32.only_on_variant(supported=[VARIANT_ESP32S3]),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    add_idf_component(name="espressif/esp_tinyusb", ref="2.2.1")
    add_idf_sdkconfig_option("CONFIG_TINYUSB_CDC_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_TINYUSB_CDC_COUNT", 1)
    add_idf_sdkconfig_option("CONFIG_TINYUSB_HID_COUNT", 1)
    add_idf_sdkconfig_option("CONFIG_TINYUSB_SUSPEND_CALLBACK", True)
    add_idf_sdkconfig_option("CONFIG_TINYUSB_RESUME_CALLBACK", True)
