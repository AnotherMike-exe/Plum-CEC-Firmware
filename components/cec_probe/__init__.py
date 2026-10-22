# SPDX-License-Identifier: MIT
"""CEC timing probe: an RMT RX channel on a pin jumpered to the CEC line (plan step 6)."""

from esphome import pins
import esphome.codegen as cg
from esphome.components.esp32 import include_builtin_idf_component
import esphome.config_validation as cv
from esphome.const import CONF_ADDRESS, CONF_ID, CONF_PIN

DEPENDENCIES = ["esp32"]

cec_probe_ns = cg.esphome_ns.namespace("cec_probe")
CecProbe = cec_probe_ns.class_("CecProbe", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CecProbe),
        cv.Required(CONF_PIN): pins.internal_gpio_input_pin_number,
        # The logical address of this device. Bits that it drives count as own TX.
        cv.Required(CONF_ADDRESS): cv.int_range(min=0, max=15),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    include_builtin_idf_component("esp_driver_rmt")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_pin(config[CONF_PIN]))
    cg.add(var.set_address(config[CONF_ADDRESS]))
