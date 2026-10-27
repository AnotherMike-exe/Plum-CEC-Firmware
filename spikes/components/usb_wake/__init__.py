# SPDX-License-Identifier: MIT
"""USB wake spike: a TinyUSB CDC and HID keyboard device with remote wakeup (plan step 14)."""

import esphome.codegen as cg
from esphome.components import esp32
from esphome.components.esp32 import (
    VARIANT_ESP32S3,
    add_idf_component,
    add_idf_sdkconfig_option,
)
import esphome.config_validation as cv
from esphome.const import CONF_ID

usb_wake_ns = cg.esphome_ns.namespace("usb_wake")
UsbWake = usb_wake_ns.class_("UsbWake", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema({cv.GenerateID(): cv.declare_id(UsbWake)}).extend(cv.COMPONENT_SCHEMA),
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
