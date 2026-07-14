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

enum class GoveeFloodOutputMode {
  OFF,
  RGB,
  WHITE,
};

enum class GoveeFloodTransitionMode {
  NONE,
  RGB,
  WHITE,
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

  void set_transition_ms(uint32_t transition_ms) {
    this->transition_ms_ = transition_ms;
    ESP_LOGD(TAG, "Transition time set to %u ms", this->transition_ms_);
  }

  uint32_t get_transition_ms() const {
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
  //
  // Real values:
  // 6500 K = 153.846 mireds
  // 2700 K = 370.370 mireds
  //
  // Slightly widened to avoid endpoint rounding warnings from HA/ESPHome.
  static constexpr float COLD_WHITE_MIRED = 153.0f;
  static constexpr float WARM_WHITE_MIRED = 371.0f;

  // Fixed raw LED settings.
  static constexpr uint32_t RMT_RESOLUTION_HZ = 10000000;
  static constexpr uint16_t RMT_MEM_BLOCK_SYMBOLS = 64;

  // HA slider-controlled transition time.
  uint32_t transition_ms_{1000};

  // Hardcoded frame intervals while we find the best values.
  static constexpr uint32_t RGB_FRAME_INTERVAL_MS = 20;
  static constexpr uint32_t WHITE_FRAME_INTERVAL_MS = 20;

  bool transition_active_{false};
  GoveeFloodTransitionMode transition_mode_{GoveeFloodTransitionMode::NONE};

  uint32_t transition_start_ms_{0};
  uint32_t phase_duration_ms_{0};
  uint32_t last_frame_ms_{0};

  GoveeFloodOutputValues current_values_;
  GoveeFloodOutputValues start_values_;
  GoveeFloodOutputValues target_values_;
  GoveeFloodOutputValues pending_values_;

  uint8_t to_u8_(float value);

  float clamp_(float value, float min_value, float max_value);
  float ease_(float progress);

  void clear_();
  void show_();

  void set_pixel_rgb_(uint16_t pixel, uint8_t red, uint8_t green, uint8_t blue);
  void apply_values_(const GoveeFloodOutputValues &values);

  GoveeFloodOutputValues values_from_light_state_(light::LightState *state);

  GoveeFloodOutputMode output_mode_(const GoveeFloodOutputValues &values);

  float total_rgb_(const GoveeFloodOutputValues &values);
  float total_white_(const GoveeFloodOutputValues &values);

  GoveeFloodOutputValues interpolate_rgb_values_(
    const GoveeFloodOutputValues &from,
    const GoveeFloodOutputValues &to,
    float progress
  );

  GoveeFloodOutputValues interpolate_white_values_(
    const GoveeFloodOutputValues &from,
    const GoveeFloodOutputValues &to,
    float progress
  );

  void begin_phase_(
    GoveeFloodTransitionMode mode,
    const GoveeFloodOutputValues &from,
    const GoveeFloodOutputValues &to,
    uint32_t duration_ms
  );

  void finish_current_phase_();
};

class GoveeOutdoorFloodlights2TransitionNumber : public number::Number, public Component {
 public:
  void setup() override;
  void dump_config() override;

  void set_light_output(GoveeOutdoorFloodlights2Output *light_output) {
    this->light_output_ = light_output;
  }

  void set_min_value_ms(uint32_t min_value_ms) {
    this->min_value_ms_ = min_value_ms;
  }

  void set_max_value_ms(uint32_t max_value_ms) {
    this->max_value_ms_ = max_value_ms;
  }

  void set_step_ms(uint32_t step_ms) {
    if (step_ms < 1) {
      step_ms = 1;
    }

    this->step_ms_ = step_ms;
  }

  void set_initial_value_ms(uint32_t initial_value_ms) {
    this->initial_value_ms_ = initial_value_ms;
  }

 protected:
  void control(float value) override;

  GoveeOutdoorFloodlights2Output *light_output_{nullptr};

  uint32_t min_value_ms_{0};
  uint32_t max_value_ms_{5000};
  uint32_t step_ms_{100};
  uint32_t initial_value_ms_{1000};
};

}  // namespace govee_outdoor_floodlights_2
}  // namespace esphome
