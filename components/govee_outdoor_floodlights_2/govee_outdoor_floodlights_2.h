#pragma once

#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/light/light_traits.h"
#include "esphome/components/light/light_color_values.h"

#include "driver/gpio.h"
#include "driver/rmt_tx.h"

namespace esphome {
namespace govee_outdoor_floodlights_2 {

class GoveeOutdoorFloodlights2Output : public light::LightOutput, public Component {
 public:
  void setup() override;
  void dump_config() override;

  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;

  void set_pin(uint8_t pin) {
    this->pin_ = pin;
  }

  void set_flood_count(uint16_t flood_count) {
    this->flood_count_ = flood_count;
  }

 protected:
  uint8_t pin_{16};
  uint16_t flood_count_{1};
  uint16_t pixel_count_{3};

  rmt_channel_handle_t rmt_channel_{nullptr};
  rmt_encoder_handle_t rmt_encoder_{nullptr};

  std::vector<uint8_t> pixel_data_;

  // Fixed color temperature range.
  // ESPHome uses mireds internally.
  // 6500 K = 153.8 mireds
  // 2700 K = 370.4 mireds
  static constexpr float COLD_WHITE_MIRED = 153.8f;
  static constexpr float WARM_WHITE_MIRED = 370.4f;

  // Fixed raw LED settings.
  static constexpr uint32_t RMT_RESOLUTION_HZ = 10000000;
  static constexpr uint16_t RMT_MEM_BLOCK_SYMBOLS = 64;

  uint8_t to_u8_(float value);

  void clear_();
  void show_();

  void set_pixel_rgb_(uint16_t pixel, uint8_t red, uint8_t green, uint8_t blue);
};

}  // namespace govee_outdoor_floodlights_2
}  // namespace esphome