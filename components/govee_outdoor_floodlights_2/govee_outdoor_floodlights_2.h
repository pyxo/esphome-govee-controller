#pragma once

#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/light/light_traits.h"
#include "esphome/components/light/light_color_values.h"
#include "esphome/components/number/number.h"

#include "driver/gpio.h"
#include "driver/rmt_tx.h"

namespace esphome {
namespace govee_outdoor_floodlights_2 {

struct GoveeFloodOutputValues {
  float red{0.0f};
  float green{0.0f};
  float blue{0.0f};
  float cool_white{0.0f};
  float warm_white{0.0f};
};

class GoveeOutdoorFloodlights2Output : public light::LightOutput, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;

  void set_pin(uint8_t pin) {
    this->pin_ = pin;
  }

  void set_flood_count(uint16_t flood_count) {
    this->flood_count_ = flood_count;
  }

  void set_transition_ms(uint16_t transition_ms) {
    if (transition_ms > 5000) {
      transition_ms = 5000;
    }

    this->transition_ms_ = transition_ms;
    ESP_LOGD(TAG, "Transition time set to %u ms", this->transition_ms_);
  }

  uint16_t get_transition_ms() const {
    return this->transition_ms_;
  }

 protected:
  static constexpr const char *TAG = "govee_outdoor_floodlights_2";

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

  // Component-level transition settings.
  uint16_t transition_ms_{1000};
  // The Govee flood white pixels flash if they are updated too quickly
  // during color temperature transitions. Keep transitions slow and stable.
  static constexpr uint32_t FRAME_INTERVAL_MS = 100;

  bool transition_active_{false};
  uint32_t transition_start_ms_{0};
  uint32_t last_frame_ms_{0};

  GoveeFloodOutputValues current_values_;
  GoveeFloodOutputValues start_values_;
  GoveeFloodOutputValues target_values_;

  uint8_t to_u8_(float value);

  void clear_();
  void show_();

  void set_pixel_rgb_(uint16_t pixel, uint8_t red, uint8_t green, uint8_t blue);
  void apply_values_(const GoveeFloodOutputValues &values);
  GoveeFloodOutputValues values_from_light_state_(light::LightState *state);
  GoveeFloodOutputValues interpolate_values_(const GoveeFloodOutputValues &from, const GoveeFloodOutputValues &to, float progress);
};

class GoveeOutdoorFloodlights2TransitionNumber : public number::Number, public Component {
 public:
  void setup() override;
  void dump_config() override;

  void set_light_output(GoveeOutdoorFloodlights2Output *light_output) {
    this->light_output_ = light_output;
  }

 protected:
  void control(float value) override;

  GoveeOutdoorFloodlights2Output *light_output_{nullptr};
};

}  // namespace govee_outdoor_floodlights_2
}  // namespace esphome