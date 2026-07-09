#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esphome/components/light/light_output.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/log.h"

#include "driver/rmt_tx.h"

namespace esphome {
namespace govee_tree_lights {

class GoveeTreeLights : public Component {
 public:
  void set_pin(InternalGPIOPin *pin) { this->data_pin_ = pin; }
  void set_relay_pin(InternalGPIOPin *pin) { this->relay_pin_ = pin; }
  void set_transition_time_ms(uint32_t transition_time_ms) { this->transition_time_ms_ = transition_time_ms; }
  void set_power_off_delay_ms(uint32_t power_off_delay_ms) { this->power_off_delay_ms_ = power_off_delay_ms; }

  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_spot_rgbww(int spot_index, float r, float g, float b, float cold_white, float warm_white);

 protected:
  static constexpr uint32_t RMT_RESOLUTION_HZ = 10000000;

  static constexpr int GOVEE_FRAME_BYTES = 64;
  static constexpr int GOVEE_FRAME_BITS = GOVEE_FRAME_BYTES * 8;
  static constexpr int GOVEE_SYMBOLS = GOVEE_FRAME_BITS + 1;

  static constexpr uint16_t T0H_TICKS = 4;
  static constexpr uint16_t T0L_TICKS = 9;
  static constexpr uint16_t T1H_TICKS = 8;
  static constexpr uint16_t T1L_TICKS = 5;

  static constexpr uint16_t RESET_TICKS = 3000;

  InternalGPIOPin *data_pin_{nullptr};
  InternalGPIOPin *relay_pin_{nullptr};

  bool relay_is_on_{false};
  bool relay_off_pending_{false};
  uint32_t relay_off_at_ms_{0};
  uint32_t power_off_delay_ms_{30000};

  rmt_channel_handle_t tx_channel_{nullptr};
  rmt_encoder_handle_t copy_encoder_{nullptr};

  const char *setup_error_{nullptr};
  char setup_error_buffer_[128]{};

  rmt_symbol_word_t symbols_[GOVEE_SYMBOLS];
  uint8_t frame_[GOVEE_FRAME_BYTES];

  uint32_t last_send_ms_{0};
  uint32_t transition_time_ms_{1000};

  uint16_t current_words_[3][10]{};
  uint16_t start_words_[3][10]{};
  uint16_t target_words_[3][10]{};

  uint32_t transition_start_ms_[3]{};
  uint32_t transition_duration_ms_[3]{};
  bool transition_active_[3]{false, false, false};

  void setup_relay_();
  void set_relay_state_(bool state);
  void update_relay_();

  bool words_active_(const uint16_t words[10]);
  bool any_current_output_active_();
  bool any_target_output_active_();
  bool any_transition_active_();

  void init_rmt_();

  void clear_frame_();
  void rebuild_frame_from_current_words_();
  void apply_words_to_frame_(int spot_index, const uint16_t words[10]);
  void update_transitions_();

  void build_target_words_(uint16_t words[10], float r, float g, float b, float cold_white, float warm_white);
  void build_rgb_words_(uint16_t words[10], uint16_t r, uint16_t g, uint16_t b);
  void build_cold_white_words_(uint16_t words[10], float level);
  void build_warm_white_words_(uint16_t words[10], float level);
  void build_blended_white_words_(uint16_t words[10], float cold_white, float warm_white);

  void set_word_(int word_index, uint16_t value);

  void build_rmt_symbols_from_frame_();
  void send_frame_();

  int spot_base_word_(int spot_index) { return spot_index * 10; }

  uint16_t level_from_float_(float value);
  uint16_t scale_word_(uint16_t max_value, float value);
  float clamp_(float value);
};

class GoveeTreeLightsLightOutput : public light::LightOutput, public Component {
 public:
  void set_parent(GoveeTreeLights *parent) { this->parent_ = parent; }

  void set_spot_index(int spot_index) { this->spot_index_ = spot_index; }
  void set_spot(int spot_index) { this->spot_index_ = spot_index; }

  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;

 protected:
  GoveeTreeLights *parent_{nullptr};
  int spot_index_{0};
};

}  // namespace govee_tree_lights
}  // namespace esphome