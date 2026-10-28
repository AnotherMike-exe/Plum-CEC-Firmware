# SPDX-License-Identifier: MIT
"""Passive DDC sniffer: logs the EDID that the Host reads at address 0x50 (plan step 14 task 4)."""

from esphome import pins
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32"]

CONF_SDA_PIN = "sda_pin"
CONF_SCL_PIN = "scl_pin"

ddc_sniffer_ns = cg.esphome_ns.namespace("ddc_sniffer")
DdcSniffer = ddc_sniffer_ns.class_("DdcSniffer", cg.Component)

# The pins are inputs with no pull-up. The isolation module gives the pull-ups.
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(DdcSniffer),
        cv.Required(CONF_SDA_PIN): pins.internal_gpio_input_pin_schema,
        cv.Required(CONF_SCL_PIN): pins.internal_gpio_input_pin_schema,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    sda = await cg.gpio_pin_expression(config[CONF_SDA_PIN])
    cg.add(var.set_sda_pin(sda))
    scl = await cg.gpio_pin_expression(config[CONF_SCL_PIN])
    cg.add(var.set_scl_pin(scl))
