import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import CONF_OUTPUT_ID

from . import govee_tree_lights_ns, GoveeTreeLights

DEPENDENCIES = ["govee_tree_lights"]

CONF_GOVEE_TREE_LIGHTS_ID = "govee_tree_lights_id"
CONF_SPOT = "spot"

GoveeTreeLightsLightOutput = govee_tree_lights_ns.class_(
  "GoveeTreeLightsLightOutput",
  light.LightOutput,
  cg.Component,
)

SPOT_VALUES = {
  "1": 0,
  "2": 1,
  "3": 2,
}

CONFIG_SCHEMA = light.RGB_LIGHT_SCHEMA.extend(
  {
    cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(GoveeTreeLightsLightOutput),
    cv.Required(CONF_GOVEE_TREE_LIGHTS_ID): cv.use_id(GoveeTreeLights),
    cv.Required(CONF_SPOT): cv.enum(SPOT_VALUES, lower=True),
  }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
  var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
  await cg.register_component(var, config)
  await light.register_light(var, config)

  parent = await cg.get_variable(config[CONF_GOVEE_TREE_LIGHTS_ID])
  cg.add(var.set_parent(parent))
  cg.add(var.set_spot_index(config[CONF_SPOT]))