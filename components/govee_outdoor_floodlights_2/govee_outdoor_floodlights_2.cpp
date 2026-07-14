#include "govee_outdoor_floodlights_2.h"

namespace esphome {
namespace govee_outdoor_floodlights_2 {

static const char *const NUMBER_TAG = "govee_outdoor_floodlights_2.number";

void GoveeOutdoorFloodlights2Output::setup() {
  this->pixel_count_ = this->flood_count_ * 3;
  this->pixel_data_.resize(this->pixel_count_ * 3, 0);

  gpio_reset_pin(static_cast<gpio_num_t>(this->pin_));

  rmt_tx_channel_config_t channel_config = {};
  channel_config.gpio_num = static_cast<gpio_num_t>(this->pin_);
  channel_config.clk_src = RMT_CLK_SRC_DEFAULT;
  channel_config.resolution_hz = RMT_RESOLUTION_HZ;
  channel_config.mem_block_symbols = RMT_MEM_BLOCK_SYMBOLS;
  channel_config.trans_queue_depth = 4;
  channel_config.flags.invert_out = false;
  channel_config.flags.with_dma = false;

  esp_err_t err = rmt_new_tx_channel(&channel_config, &this->rmt_channel_);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  rmt_bytes_encoder_config_t encoder_config = {};

  // WS2812-style timing at 10 MHz RMT resolution.
  // 1 tick = 100 ns.
  //
  // 0 bit: high 300 ns, low 900 ns
  // 1 bit: high 900 ns, low 300 ns
  encoder_config.bit0.level0 = 1;
  encoder_config.bit0.duration0 = 3;
  encoder_config.bit0.level1 = 0;
  encoder_config.bit0.duration1 = 9;

  encoder_config.bit1.level0 = 1;
  encoder_config.bit1.duration0 = 9;
  encoder_config.bit1.level1 = 0;
  encoder_config.bit1.duration1 = 3;

  encoder_config.flags.msb_first = 1;

  err = rmt_new_bytes_encoder(&encoder_config, &this->rmt_encoder_);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create RMT bytes encoder: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  err = rmt_enable(this->rmt_channel_);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable RMT channel: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  this->clear_();
  this->show_();
}

void GoveeOutdoorFloodlights2Output::loop() {
  if (!this->transition_active_) {
    return;
  }

  const uint32_t now = millis();

  uint32_t frame_interval = RGB_FRAME_INTERVAL_MS;

  if (this->transition_mode_ == GoveeFloodTransitionMode::WHITE_SAFE) {
    frame_interval = WHITE_FRAME_INTERVAL_MS;
  }

  if (now - this->last_frame_ms_ < frame_interval) {
    return;
  }

  this->last_frame_ms_ = now;

  if (this->transition_ms_ == 0) {
    this->current_values_ = this->target_values_;
    this->apply_values_(this->current_values_);
    this->transition_active_ = false;
    this->transition_mode_ = GoveeFloodTransitionMode::NONE;
    return;
  }

  const uint32_t elapsed = now - this->transition_start_ms_;

  float progress = static_cast<float>(elapsed) / static_cast<float>(this->transition_ms_);

  if (progress >= 1.0f) {
    progress = 1.0f;
  }

  this->current_values_ = this->interpolate_values_(
    this->start_values_,
    this->target_values_,
    progress
  );

  this->apply_values_(this->current_values_);

  if (progress >= 1.0f) {
    this->current_values_ = this->target_values_;
    this->apply_values_(this->current_values_);
    this->transition_active_ = false;
    this->transition_mode_ = GoveeFloodTransitionMode::NONE;
  }
}

void GoveeOutdoorFloodlights2Output::dump_config() {
  ESP_LOGCONFIG(TAG, "Govee Outdoor Floodlights 2");
  ESP_LOGCONFIG(TAG, "  Pin: GPIO%u", this->pin_);
  ESP_LOGCONFIG(TAG, "  Flood count: %u", this->flood_count_);
  ESP_LOGCONFIG(TAG, "  Physical pixels: %u", this->pixel_count_);
  ESP_LOGCONFIG(TAG, "  Layout per flood: RGB, Cool White, Warm White");
  ESP_LOGCONFIG(TAG, "  RGB order: BRG");
  ESP_LOGCONFIG(TAG, "  Cold white: 6500 K / %.1f mireds", COLD_WHITE_MIRED);
  ESP_LOGCONFIG(TAG, "  Warm white: 2700 K / %.1f mireds", WARM_WHITE_MIRED);
  ESP_LOGCONFIG(TAG, "  Transition time: %u ms", this->transition_ms_);
  ESP_LOGCONFIG(TAG, "  RGB frame interval: %u ms", RGB_FRAME_INTERVAL_MS);
  ESP_LOGCONFIG(TAG, "  White frame interval: %u ms", WHITE_FRAME_INTERVAL_MS);
  ESP_LOGCONFIG(TAG, "  RMT resolution: %u Hz", RMT_RESOLUTION_HZ);
  ESP_LOGCONFIG(TAG, "  RMT mem block symbols: %u", RMT_MEM_BLOCK_SYMBOLS);
}

light::LightTraits GoveeOutdoorFloodlights2Output::get_traits() {
  auto traits = light::LightTraits();

  traits.set_supported_color_modes({
    light::ColorMode::RGB,
    light::ColorMode::COLOR_TEMPERATURE,
  });

  traits.set_min_mireds(COLD_WHITE_MIRED);
  traits.set_max_mireds(WARM_WHITE_MIRED);

  return traits;
}

uint8_t GoveeOutdoorFloodlights2Output::to_u8_(float value) {
  if (value <= 0.0f) {
    return 0;
  }

  if (value >= 1.0f) {
    return 255;
  }

  return static_cast<uint8_t>(value * 255.0f + 0.5f);
}

void GoveeOutdoorFloodlights2Output::clear_() {
  for (size_t i = 0; i < this->pixel_data_.size(); i++) {
    this->pixel_data_[i] = 0;
  }
}

void GoveeOutdoorFloodlights2Output::set_pixel_rgb_(uint16_t pixel, uint8_t red, uint8_t green, uint8_t blue) {
  if (pixel >= this->pixel_count_) {
    return;
  }

  const size_t offset = pixel * 3;

  // Fixed BRG byte order.
  // This is the known-good order for this Govee flood light.
  this->pixel_data_[offset + 0] = blue;
  this->pixel_data_[offset + 1] = red;
  this->pixel_data_[offset + 2] = green;
}

void GoveeOutdoorFloodlights2Output::show_() {
  if (this->rmt_channel_ == nullptr || this->rmt_encoder_ == nullptr) {
    return;
  }

  esp_err_t wait_err = rmt_tx_wait_all_done(this->rmt_channel_, 0);

  if (wait_err == ESP_ERR_TIMEOUT) {
    return;
  }

  if (wait_err != ESP_OK) {
    ESP_LOGW(TAG, "RMT wait failed: %s", esp_err_to_name(wait_err));
    return;
  }

  rmt_transmit_config_t transmit_config = {};
  transmit_config.loop_count = 0;

  esp_err_t err = rmt_transmit(
    this->rmt_channel_,
    this->rmt_encoder_,
    this->pixel_data_.data(),
    this->pixel_data_.size(),
    &transmit_config
  );

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "RMT transmit failed: %s", esp_err_to_name(err));
    return;
  }

  rmt_tx_wait_all_done(this->rmt_channel_, pdMS_TO_TICKS(20));

  // Extra latch/reset gap for the Govee pixels.
  delayMicroseconds(300);
}

GoveeFloodOutputValues GoveeOutdoorFloodlights2Output::interpolate_values_(
  const GoveeFloodOutputValues &from,
  const GoveeFloodOutputValues &to,
  float progress
) {
  GoveeFloodOutputValues result;

  result.red = from.red + ((to.red - from.red) * progress);
  result.green = from.green + ((to.green - from.green) * progress);
  result.blue = from.blue + ((to.blue - from.blue) * progress);
  result.cool_white = from.cool_white + ((to.cool_white - from.cool_white) * progress);
  result.warm_white = from.warm_white + ((to.warm_white - from.warm_white) * progress);

  return result;
}

float GoveeOutdoorFloodlights2Output::total_white_(const GoveeFloodOutputValues &values) {
  float total = values.cool_white + values.warm_white;

  if (total < 0.0f) {
    total = 0.0f;
  }

  if (total > 1.0f) {
    total = 1.0f;
  }

  return total;
}

GoveeFloodOutputValues GoveeOutdoorFloodlights2Output::remap_current_to_target_white_ratio_(
  const GoveeFloodOutputValues &current,
  const GoveeFloodOutputValues &target
) {
  GoveeFloodOutputValues result;

  // Preserve RGB from the current output so RGB can fade out if switching
  // from color mode to white mode.
  result.red = current.red;
  result.green = current.green;
  result.blue = current.blue;

  const float current_white_total = this->total_white_(current);
  const float target_white_total = this->total_white_(target);

  if (target_white_total <= 0.0f || current_white_total <= 0.0f) {
    result.cool_white = 0.0f;
    result.warm_white = 0.0f;
    return result;
  }

  // Snap to the target color-temperature ratio immediately, but keep the
  // current total white brightness. This avoids repeated CW/WW crossmix frames,
  // which are what made the Govee white pixels flash.
  const float target_cool_ratio = target.cool_white / target_white_total;
  const float target_warm_ratio = target.warm_white / target_white_total;

  result.cool_white = current_white_total * target_cool_ratio;
  result.warm_white = current_white_total * target_warm_ratio;

  return result;
}

GoveeFloodOutputValues GoveeOutdoorFloodlights2Output::values_from_light_state_(light::LightState *state) {
  auto values = state->current_values;

  const float master = values.get_state() * values.get_brightness();
  const auto color_mode = values.get_color_mode();

  GoveeFloodOutputValues output;

  if (master <= 0.0f) {
    return output;
  }

  if (color_mode == light::ColorMode::RGB) {
    const float color_brightness = values.get_color_brightness();

    output.red = values.get_red() * color_brightness * master;
    output.green = values.get_green() * color_brightness * master;
    output.blue = values.get_blue() * color_brightness * master;

    return output;
  }

  if (color_mode == light::ColorMode::COLOR_TEMPERATURE) {
    float color_temperature = values.get_color_temperature();

    if (color_temperature < COLD_WHITE_MIRED) {
      color_temperature = COLD_WHITE_MIRED;
    }

    if (color_temperature > WARM_WHITE_MIRED) {
      color_temperature = WARM_WHITE_MIRED;
    }

    const float range = WARM_WHITE_MIRED - COLD_WHITE_MIRED;

    float warm_ratio = 0.0f;

    if (range > 0.0f) {
      warm_ratio = (color_temperature - COLD_WHITE_MIRED) / range;
    }

    if (warm_ratio < 0.0f) {
      warm_ratio = 0.0f;
    }

    if (warm_ratio > 1.0f) {
      warm_ratio = 1.0f;
    }

    const float cold_ratio = 1.0f - warm_ratio;

    output.cool_white = cold_ratio * master;
    output.warm_white = warm_ratio * master;
  }

  return output;
}

void GoveeOutdoorFloodlights2Output::apply_values_(const GoveeFloodOutputValues &values) {
  const uint8_t rgb_red = this->to_u8_(values.red);
  const uint8_t rgb_green = this->to_u8_(values.green);
  const uint8_t rgb_blue = this->to_u8_(values.blue);
  const uint8_t cool_white = this->to_u8_(values.cool_white);
  const uint8_t warm_white = this->to_u8_(values.warm_white);

  this->clear_();

  for (uint16_t i = 0; i < this->flood_count_; i++) {
    const uint16_t base = i * 3;

    // Physical pixel 0 in each flood: RGB.
    this->set_pixel_rgb_(base + 0, rgb_red, rgb_green, rgb_blue);

    // Physical pixel 1 in each flood: cool white.
    this->set_pixel_rgb_(base + 1, cool_white, cool_white, cool_white);

    // Physical pixel 2 in each flood: warm white.
    this->set_pixel_rgb_(base + 2, warm_white, warm_white, warm_white);
  }

  this->show_();
}

void GoveeOutdoorFloodlights2Output::write_state(light::LightState *state) {
  const auto color_mode = state->current_values.get_color_mode();

  this->target_values_ = this->values_from_light_state_(state);

  if (this->transition_ms_ == 0) {
    this->current_values_ = this->target_values_;
    this->apply_values_(this->current_values_);
    this->transition_active_ = false;
    this->transition_mode_ = GoveeFloodTransitionMode::NONE;
    return;
  }

  if (color_mode == light::ColorMode::COLOR_TEMPERATURE) {
    // Safe white transition:
    // - Snap CW/WW ratio to the target color temperature immediately.
    // - Fade only the total white brightness.
    // - Also fade RGB down if switching from RGB to white.
    this->start_values_ = this->remap_current_to_target_white_ratio_(
      this->current_values_,
      this->target_values_
    );

    this->transition_mode_ = GoveeFloodTransitionMode::WHITE_SAFE;
  } else {
    // RGB transitions can be smooth and frequent.
    this->start_values_ = this->current_values_;
    this->transition_mode_ = GoveeFloodTransitionMode::RGB;
  }

  this->transition_start_ms_ = millis();
  this->last_frame_ms_ = 0;
  this->transition_active_ = true;
}

void GoveeOutdoorFloodlights2TransitionNumber::setup() {
  if (this->light_output_ == nullptr) {
    ESP_LOGE(NUMBER_TAG, "No Govee flood light output was configured");
    this->mark_failed();
    return;
  }

  uint32_t initial_value = this->initial_value_ms_;

  if (initial_value < this->min_value_ms_) {
    initial_value = this->min_value_ms_;
  }

  if (initial_value > this->max_value_ms_) {
    initial_value = this->max_value_ms_;
  }

  this->light_output_->set_transition_ms(initial_value);
  this->publish_state(initial_value);
}

void GoveeOutdoorFloodlights2TransitionNumber::dump_config() {
  ESP_LOGCONFIG(NUMBER_TAG, "Govee Outdoor Floodlights 2 Transition Number");
  ESP_LOGCONFIG(NUMBER_TAG, "  Min value: %u ms", this->min_value_ms_);
  ESP_LOGCONFIG(NUMBER_TAG, "  Max value: %u ms", this->max_value_ms_);
  ESP_LOGCONFIG(NUMBER_TAG, "  Step: %u ms", this->step_ms_);
  ESP_LOGCONFIG(NUMBER_TAG, "  Initial value: %u ms", this->initial_value_ms_);
}

void GoveeOutdoorFloodlights2TransitionNumber::control(float value) {
  if (this->light_output_ == nullptr) {
    return;
  }

  uint32_t transition_ms = static_cast<uint32_t>(value + 0.5f);

  if (transition_ms < this->min_value_ms_) {
    transition_ms = this->min_value_ms_;
  }

  if (transition_ms > this->max_value_ms_) {
    transition_ms = this->max_value_ms_;
  }

  if (this->step_ms_ > 1) {
    transition_ms = static_cast<uint32_t>(
      (transition_ms / this->step_ms_) * this->step_ms_
    );
  }

  this->light_output_->set_transition_ms(transition_ms);
  this->publish_state(transition_ms);
}

}  // namespace govee_outdoor_floodlights_2
}  // namespace esphome