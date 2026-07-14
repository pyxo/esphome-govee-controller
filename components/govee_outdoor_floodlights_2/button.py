import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import button
from esphome.const import (
  CONF_ID,
  CONF_OUTPUT_ID,
)

from .light import (
  GoveeOutdoorFloodlights2Output,
  govee_outdoor_floodlights_2_ns,
)

CONF_TEST = "test"

TEST_OPTIONS = {
  "cool_fade": 1,
  "warm_fade": 2,
  "mixed_fade": 3,
  "ratio_sweep": 4,
  "cool_red_fade": 5,
  "cool_green_fade": 6,
  "cool_blue_fade": 7,
  "cool_fixed_retransmit": 8,
}

GoveeOutdoorFloodlights2DiagnosticButton = govee_outdoor_floodlights_2_ns.class_(
  "GoveeOutdoorFloodlights2DiagnosticButton",
  button.Button,
  cg.Component,
)

CONFIG_SCHEMA = button.button_schema(
  GoveeOutdoorFloodlights2DiagnosticButton,
).extend(
  {
    cv.Required(CONF_OUTPUT_ID): cv.use_id(GoveeOutdoorFloodlights2Output),
    cv.Required(CONF_TEST): cv.enum(TEST_OPTIONS, lower=True),
  }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
  var = cg.new_Pvariable(config[CONF_ID])

  await cg.register_component(var, config)
  await button.register_button(var, config)

  light_output = await cg.get_variable(config[CONF_OUTPUT_ID])

  cg.add(var.set_light_output(light_output))
  cg.add(var.set_test_type(config[CONF_TEST]))
