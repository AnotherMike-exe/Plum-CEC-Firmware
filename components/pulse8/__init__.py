# SPDX-License-Identifier: MIT
"""Pulse-Eight USB-CEC emulation on the C3 USB Serial/JTAG peripheral."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

pulse8_ns = cg.esphome_ns.namespace("pulse8")
Pulse8Component = pulse8_ns.class_("Pulse8Component", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Pulse8Component),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
