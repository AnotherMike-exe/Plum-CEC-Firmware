from esphome import automation, pins
import esphome.codegen as cg
from esphome.components.esp32 import include_builtin_idf_component
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_AUTOMATION_ID,
    CONF_DATA,
    CONF_ID,
    CONF_ON_MESSAGE,
    CONF_PIN,
    CONF_SOURCE,
    CONF_THEN,
    PLATFORM_BK72XX,
    PLATFORM_ESP32,
    PLATFORM_ESP8266,
    PLATFORM_LN882X,
    PLATFORM_RP2,
    PLATFORM_RTL87XX,
)
from esphome.core import CORE
from esphome.types import ConfigType

CODEOWNERS = ["@JosVanEijndhoven"]
DOMAIN = "hdmi_cec"

MAX_LOGICAL_ADDRESS = 15
MAX_PAYLOAD_LENGTH = 15
CONF_PROMISCUOUS_MODE = "promiscuous_mode"
CONF_MONITOR_MODE = "monitor_mode"
CONF_DESTINATION = "destination"
CONF_OPCODE = "opcode"

hdmi_cec_ns = cg.esphome_ns.namespace("hdmi_cec")
HDMICEC = hdmi_cec_ns.class_("HDMICEC", cg.Component)
SendAction = hdmi_cec_ns.class_("SendAction", automation.Action)
_PAYLOAD_SPAN = cg.std_span.template(cg.uint8.operator("const"))
_PAYLOAD_VECTOR = cg.std_vector.template(cg.uint8)
_MESSAGE_ARGS = [
    (cg.uint8, "source"),
    (cg.uint8, "destination"),
    (_PAYLOAD_SPAN, "data"),
]

_request_message_callback_slot = cg.slot_counter("HDMI_CEC_MESSAGE_CALLBACK_COUNT")

logical_address = cv.int_range(min=0, max=MAX_LOGICAL_ADDRESS)
payload = cv.All(cv.ensure_list(cv.hex_uint8_t), cv.Length(max=MAX_PAYLOAD_LENGTH))
payload_prefix = cv.All(payload, cv.Length(min=1))


def _validate_message_filter(config: ConfigType) -> ConfigType:
    # The opcode is the first payload byte: fold it into the data prefix
    if (opcode := config.pop(CONF_OPCODE, None)) is not None:
        data = config.setdefault(CONF_DATA, [opcode])
        if data[0] != opcode:
            raise cv.Invalid(
                f"opcode 0x{opcode:02X} differs from the first data byte 0x{data[0]:02X}"
            )
    return config


def _message_filter(conf: ConfigType) -> str | None:
    """The C++ condition an on_message automation runs under, or None for every frame."""
    checks = []
    if (source := conf.get(CONF_SOURCE)) is not None:
        checks.append(f"source == {source}")
    if (destination := conf.get(CONF_DESTINATION)) is not None:
        checks.append(f"destination == {destination}")
    if data := conf.get(CONF_DATA):
        checks.append(f"data.size() >= {len(data)}")
        checks.extend(f"data[{i}] == 0x{byte:02X}" for i, byte in enumerate(data))
    return " && ".join(checks) or None


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(HDMICEC),
            cv.Required(CONF_PIN): pins.internal_gpio_input_pullup_pin_schema,
            cv.Required(CONF_ADDRESS): logical_address,
            cv.Optional(CONF_PROMISCUOUS_MODE, default=False): cv.boolean,
            cv.Optional(CONF_MONITOR_MODE, default=False): cv.boolean,
            cv.Optional(CONF_ON_MESSAGE): automation.validate_automation(
                {
                    cv.Optional(CONF_SOURCE): logical_address,
                    cv.Optional(CONF_DESTINATION): logical_address,
                    cv.Optional(CONF_OPCODE): cv.hex_uint8_t,
                    cv.Optional(CONF_DATA): payload_prefix,
                },
                _validate_message_filter,
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on(
        [
            PLATFORM_ESP32,
            PLATFORM_ESP8266,
            PLATFORM_RP2,
            PLATFORM_BK72XX,
            PLATFORM_RTL87XX,
            PLATFORM_LN882X,
        ]
    ),
)


async def to_code(config: ConfigType) -> None:
    if CORE.is_esp8266:
        from esphome.components.esp8266.const import use_timer1

        # The acknowledge bit is released from hardware timer 1
        use_timer1(DOMAIN)
    if CORE.is_esp32:
        # The acknowledge bit is released from a general purpose timer, left out of the build by default
        include_builtin_idf_component("esp_driver_gptimer")
    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    var = cg.new_Pvariable(config[CONF_ID], pin, config[CONF_ADDRESS])
    await cg.register_component(var, config)
    if config[CONF_PROMISCUOUS_MODE]:
        cg.add(var.set_promiscuous_mode(True))
    if config[CONF_MONITOR_MODE]:
        cg.add(var.set_monitor_mode(True))

    for conf in config.get(CONF_ON_MESSAGE, []):
        _request_message_callback_slot()
        # Local change for ESPHome 2026.9.1: build_callback_automation() has no when= argument
        # there, so the filter goes into a captureless lambda (see UPSTREAM.md)
        templ = cg.TemplateArguments(*(arg[0] for arg in _MESSAGE_ARGS))
        trigger = cg.new_Pvariable(conf[CONF_AUTOMATION_ID], templ)
        actions = await automation.build_action_list(conf[CONF_THEN], templ, _MESSAGE_ARGS)
        cg.add(trigger.add_actions(actions))
        condition = _message_filter(conf) or "true"
        cg.add(
            var.add_on_message_callback(
                cg.RawExpression(
                    "[](uint8_t source, uint8_t destination, std::span<const uint8_t> data) {"
                    f" if ({condition}) {trigger}->trigger(source, destination, data); }}"
                )
            )
        )


# Local change for ESPHome 2026.9.1, which has no register_apply_action (see UPSTREAM.md)
@automation.register_action(
    "hdmi_cec.send",
    SendAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(HDMICEC),
            cv.Required(CONF_DESTINATION): cv.templatable(logical_address),
            cv.Required(CONF_DATA): cv.templatable(payload),
        }
    ),
    synchronous=True,
)
async def hdmi_cec_send_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    destination = await cg.templatable(config[CONF_DESTINATION], args, cg.uint8)
    cg.add(var.set_destination(destination))
    data = await cg.templatable(config[CONF_DATA], args, _PAYLOAD_VECTOR)
    cg.add(var.set_data(data))
    return var
