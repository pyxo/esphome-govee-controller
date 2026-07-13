import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import light
from esphome.const import CONF_OUTPUT_ID

CONF_PIN = "pin"
CONF_FLOOD_COUNT = "flood_count"

govee_outdoor_floodlights_2_ns = cg.esphome_ns.namespace("govee_outdoor_floodlights_2")

GoveeOutdoorFloodlights2Output = govee_outdoor_floodlights_2_ns.class_(
  "GoveeOutdoorFloodlights2Output",
  light.LightOutput,
  cg.Component,
)


def validate_gpio_pin(value):
  value = cv.string(value).upper().replace("GPIO", "").strip()
  pin = cv.int_range(min=0, max=48)(value)
  return pin


CONFIG_SCHEMA = light.RGB_LIGHT_SCHEMA.extend(
  {
    cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(GoveeOutdoorFloodlights2Output),
    cv.Required(CONF_PIN): validate_gpio_pin,
    cv.Required(CONF_FLOOD_COUNT): cv.positive_int,
  }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
  var = cg.new_Pvariable(config[CONF_OUTPUT_ID])

  await cg.register_component(var, config)
  await light.register_light(var, config)

  cg.add(var.set_pin(config[CONF_PIN]))
  cg.add(var.set_flood_count(config[CONF_FLOOD_COUNT]))