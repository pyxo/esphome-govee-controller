import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_ID, CONF_PIN

CODEOWNERS = ["@pyxo"]

MULTI_CONF = True

CONF_RELAY_PIN = "relay_pin"
CONF_POWER_OFF_DELAY = "power_off_delay"

govee_tree_lights_ns = cg.esphome_ns.namespace("govee_tree_lights")
GoveeTreeLights = govee_tree_lights_ns.class_("GoveeTreeLights", cg.Component)

CONFIG_SCHEMA = cv.Schema(
  {
    cv.GenerateID(): cv.declare_id(GoveeTreeLights),
    cv.Required(CONF_PIN): pins.gpio_output_pin_schema,
    cv.Optional(CONF_RELAY_PIN): pins.gpio_output_pin_schema,
    cv.Optional(CONF_POWER_OFF_DELAY, default="30s"): cv.positive_time_period_milliseconds,
  }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
  var = cg.new_Pvariable(config[CONF_ID])
  await cg.register_component(var, config)

  pin = await cg.gpio_pin_expression(config[CONF_PIN])
  cg.add(var.set_pin(pin))

  if CONF_RELAY_PIN in config:
    relay_pin = await cg.gpio_pin_expression(config[CONF_RELAY_PIN])
    cg.add(var.set_relay_pin(relay_pin))

  cg.add(var.set_power_off_delay_ms(config[CONF_POWER_OFF_DELAY].total_milliseconds))