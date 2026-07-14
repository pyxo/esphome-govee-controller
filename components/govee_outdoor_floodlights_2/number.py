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

CONF_MIN_VALUE = "min_value"
CONF_MAX_VALUE = "max_value"
CONF_STEP = "step"
CONF_INITIAL_VALUE = "initial_value"

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
    cv.Optional(CONF_MIN_VALUE, default="0ms"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_MAX_VALUE, default="5000ms"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_STEP, default="100ms"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_INITIAL_VALUE, default="1000ms"): cv.positive_time_period_milliseconds,
  }
).extend(cv.COMPONENT_SCHEMA)


def validate_transition_number(config):
  min_value = config[CONF_MIN_VALUE].total_milliseconds
  max_value = config[CONF_MAX_VALUE].total_milliseconds
  step = config[CONF_STEP].total_milliseconds
  initial_value = config[CONF_INITIAL_VALUE].total_milliseconds

  if max_value < min_value:
    raise cv.Invalid("max_value must be greater than or equal to min_value")

  if step < 1:
    raise cv.Invalid("step must be at least 1ms")

  if initial_value < min_value or initial_value > max_value:
    raise cv.Invalid("initial_value must be between min_value and max_value")

  return config


CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, validate_transition_number)


async def to_code(config):
  var = cg.new_Pvariable(config[CONF_ID])

  min_value = config[CONF_MIN_VALUE].total_milliseconds
  max_value = config[CONF_MAX_VALUE].total_milliseconds
  step = config[CONF_STEP].total_milliseconds
  initial_value = config[CONF_INITIAL_VALUE].total_milliseconds

  await cg.register_component(var, config)
  await number.register_number(
    var,
    config,
    min_value=min_value,
    max_value=max_value,
    step=step,
  )

  light_output = await cg.get_variable(config[CONF_OUTPUT_ID])

  cg.add(var.set_light_output(light_output))
  cg.add(var.set_min_value_ms(min_value))
  cg.add(var.set_max_value_ms(max_value))
  cg.add(var.set_step_ms(step))
  cg.add(var.set_initial_value_ms(initial_value))