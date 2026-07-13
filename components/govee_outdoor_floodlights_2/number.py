import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import number
from esphome.const import (
  CONF_ID,
  CONF_OUTPUT_ID,
  DEVICE_CLASS_DURATION,
  ENTITY_CATEGORY_CONFIG,
  UNIT_MILLISECOND,
)

from .light import (
  GoveeOutdoorFloodlights2Output,
  govee_outdoor_floodlights_2_ns,
)

GoveeOutdoorFloodlights2TransitionNumber = govee_outdoor_floodlights_2_ns.class_(
  "GoveeOutdoorFloodlights2TransitionNumber",
  number.Number,
  cg.Component,
)

CONFIG_SCHEMA = number.number_schema(
  GoveeOutdoorFloodlights2TransitionNumber,
  device_class=DEVICE_CLASS_DURATION,
  entity_category=ENTITY_CATEGORY_CONFIG,
  unit_of_measurement=UNIT_MILLISECOND,
).extend(
  {
    cv.Required(CONF_OUTPUT_ID): cv.use_id(GoveeOutdoorFloodlights2Output),
  }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
  var = cg.new_Pvariable(config[CONF_ID])

  await cg.register_component(var, config)
  await number.register_number(
    var,
    config,
    min_value=0,
    max_value=5000,
    step=100,
  )

  light_output = await cg.get_variable(config[CONF_OUTPUT_ID])
  cg.add(var.set_light_output(light_output))