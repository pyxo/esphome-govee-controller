#include "govee_outdoor_floodlights_2.h"

namespace esphome {
namespace govee_outdoor_floodlights_2 {

static const char *const NUMBER_TAG = "govee_outdoor_floodlights_2.number";
static const char *const BUTTON_TAG = "govee_outdoor_floodlights_2.button";

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
  if (this->diagnostic_phase_ != GoveeFloodDiagnosticPhase::NONE) {
    this->update_diagnostic_();
    return;
  }

  if (!this->transition_active_) {
    return;
  }

  const uint32_t now = millis();

  uint32_t frame_interval = RGB_FRAME_INTERVAL_MS;

  if (
    this->transition_mode_ == GoveeFloodTransitionMode::WHITE ||
    this->transition_mode_ == GoveeFloodTransitionMode::RGB_TO_WHITE_FADE_WHITE_IN ||
    this->transition_mode_ == GoveeFloodTransitionMode::WHITE_TO_RGB_FADE_WHITE_OUT
  ) {
    frame_interval = WHITE_FRAME_INTERVAL_MS;
  }

  if (now - this->last_frame_ms_ < frame_interval) {
    return;
  }

  this->last_frame_ms_ = now;

  if (this->phase_duration_ms_ == 0) {
    this->phase_duration_ms_ = 1;
  }

  const uint32_t elapsed = now - this->transition_start_ms_;

  float progress = static_cast<float>(elapsed) / static_cast<float>(this->phase_duration_ms_);

  if (progress >= 1.0f) {
    progress = 1.0f;
  }

  progress = this->ease_(progress);

  if (
    this->transition_mode_ == GoveeFloodTransitionMode::WHITE ||
    this->transition_mode_ == GoveeFloodTransitionMode::RGB_TO_WHITE_FADE_WHITE_IN ||
    this->transition_mode_ == GoveeFloodTransitionMode::WHITE_TO_RGB_FADE_WHITE_OUT
  ) {
    this->current_values_ = this->interpolate_white_values_(
      this->start_values_,
      this->target_values_,
      progress
    );
  } else {
    this->current_values_ = this->interpolate_rgb_values_(
      this->start_values_,
      this->target_values_,
      progress
    );
  }

  this->apply_values_(this->current_values_);

  if (progress >= 1.0f) {
    this->current_values_ = this->target_values_;
    this->apply_values_(this->current_values_);
    this->finish_current_phase_();
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
  ESP_LOGCONFIG(TAG, "  White low cutoff: %u", WHITE_LOW_CUTOFF);
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

uint8_t GoveeOutdoorFloodlights2Output::to_white_u8_(float value) {
  uint8_t out = this->to_u8_(value);

  if (out > 0 && out < WHITE_LOW_CUTOFF) {
    return 0;
  }

  return out;
}

float GoveeOutdoorFloodlights2Output::clamp_(float value, float min_value, float max_value) {
  if (value < min_value) {
    return min_value;
  }

  if (value > max_value) {
    return max_value;
  }

  return value;
}

float GoveeOutdoorFloodlights2Output::ease_(float progress) {
  progress = this->clamp_(progress, 0.0f, 1.0f);

  // Smoothstep easing: smoother start and stop than a linear fade.
  return progress * progress * (3.0f - (2.0f * progress));
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

GoveeFloodOutputValues GoveeOutdoorFloodlights2Output::zero_values_() {
  GoveeFloodOutputValues values;
  return values;
}

float GoveeOutdoorFloodlights2Output::total_rgb_(const GoveeFloodOutputValues &values) {
  float total = values.red + values.green + values.blue;

  return this->clamp_(total, 0.0f, 1.0f);
}

float GoveeOutdoorFloodlights2Output::total_white_(const GoveeFloodOutputValues &values) {
  float total = values.cool_white + values.warm_white;

  return this->clamp_(total, 0.0f, 1.0f);
}

GoveeFloodOutputMode GoveeOutdoorFloodlights2Output::output_mode_(const GoveeFloodOutputValues &values) {
  const float rgb_total = this->total_rgb_(values);
  const float white_total = this->total_white_(values);

  if (rgb_total <= 0.001f && white_total <= 0.001f) {
    return GoveeFloodOutputMode::OFF;
  }

  if (white_total > rgb_total) {
    return GoveeFloodOutputMode::WHITE;
  }

  return GoveeFloodOutputMode::RGB;
}

GoveeFloodOutputValues GoveeOutdoorFloodlights2Output::interpolate_rgb_values_(
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

GoveeFloodOutputValues GoveeOutdoorFloodlights2Output::interpolate_white_values_(
  const GoveeFloodOutputValues &from,
  const GoveeFloodOutputValues &to,
  float progress
) {
  GoveeFloodOutputValues result;

  const float from_total = this->total_white_(from);
  const float to_total = this->total_white_(to);
  const float total = from_total + ((to_total - from_total) * progress);

  float from_cool_ratio = 0.0f;
  float to_cool_ratio = 0.0f;

  if (from_total > 0.001f) {
    from_cool_ratio = from.cool_white / from_total;
  } else if (to_total > 0.001f) {
    from_cool_ratio = to.cool_white / to_total;
  }

  if (to_total > 0.001f) {
    to_cool_ratio = to.cool_white / to_total;
  } else if (from_total > 0.001f) {
    to_cool_ratio = from.cool_white / from_total;
  }

  from_cool_ratio = this->clamp_(from_cool_ratio, 0.0f, 1.0f);
  to_cool_ratio = this->clamp_(to_cool_ratio, 0.0f, 1.0f);

  const float cool_ratio = from_cool_ratio + ((to_cool_ratio - from_cool_ratio) * progress);
  const float warm_ratio = 1.0f - cool_ratio;

  result.red = 0.0f;
  result.green = 0.0f;
  result.blue = 0.0f;
  result.cool_white = total * cool_ratio;
  result.warm_white = total * warm_ratio;

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

    warm_ratio = this->clamp_(warm_ratio, 0.0f, 1.0f);

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
  const uint8_t cool_white = this->to_white_u8_(values.cool_white);
  const uint8_t warm_white = this->to_white_u8_(values.warm_white);

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

void GoveeOutdoorFloodlights2Output::begin_phase_(
  GoveeFloodTransitionMode mode,
  const GoveeFloodOutputValues &from,
  const GoveeFloodOutputValues &to,
  uint32_t duration_ms
) {
  if (duration_ms < 1) {
    duration_ms = 1;
  }

  this->transition_mode_ = mode;
  this->start_values_ = from;
  this->target_values_ = to;
  this->phase_duration_ms_ = duration_ms;
  this->transition_start_ms_ = millis();
  this->last_frame_ms_ = 0;
  this->transition_active_ = true;
}

void GoveeOutdoorFloodlights2Output::finish_current_phase_() {
  const auto finished_mode = this->transition_mode_;

  if (finished_mode == GoveeFloodTransitionMode::RGB_TO_WHITE_FADE_RGB_OUT) {
    this->begin_phase_(
      GoveeFloodTransitionMode::RGB_TO_WHITE_FADE_WHITE_IN,
      this->zero_values_(),
      this->pending_values_,
      this->next_phase_duration_ms_
    );
    return;
  }

  if (finished_mode == GoveeFloodTransitionMode::WHITE_TO_RGB_FADE_WHITE_OUT) {
    this->begin_phase_(
      GoveeFloodTransitionMode::WHITE_TO_RGB_FADE_RGB_IN,
      this->zero_values_(),
      this->pending_values_,
      this->next_phase_duration_ms_
    );
    return;
  }

  this->transition_active_ = false;
  this->transition_mode_ = GoveeFloodTransitionMode::NONE;
}

void GoveeOutdoorFloodlights2Output::write_state(light::LightState *state) {
  if (this->diagnostic_phase_ != GoveeFloodDiagnosticPhase::NONE) {
    ESP_LOGI(TAG, "Diagnostic cancelled by light command");
    this->diagnostic_phase_ = GoveeFloodDiagnosticPhase::NONE;
    this->diagnostic_test_ = GoveeFloodDiagnosticTest::NONE;
  }

  this->pending_values_ = this->values_from_light_state_(state);

  if (this->transition_ms_ == 0) {
    this->current_values_ = this->pending_values_;
    this->apply_values_(this->current_values_);
    this->transition_active_ = false;
    this->transition_mode_ = GoveeFloodTransitionMode::NONE;
    return;
  }

  const auto current_mode = this->output_mode_(this->current_values_);
  const auto target_mode = this->output_mode_(this->pending_values_);

  const uint32_t first_phase_duration = this->transition_ms_ / 2;
  uint32_t second_phase_duration = this->transition_ms_ - first_phase_duration;

  if (second_phase_duration < 1) {
    second_phase_duration = 1;
  }

  this->next_phase_duration_ms_ = second_phase_duration;

  if (current_mode == GoveeFloodOutputMode::RGB && target_mode == GoveeFloodOutputMode::WHITE) {
    this->begin_phase_(
      GoveeFloodTransitionMode::RGB_TO_WHITE_FADE_RGB_OUT,
      this->current_values_,
      this->zero_values_(),
      first_phase_duration
    );
    return;
  }

  if (current_mode == GoveeFloodOutputMode::WHITE && target_mode == GoveeFloodOutputMode::RGB) {
    this->begin_phase_(
      GoveeFloodTransitionMode::WHITE_TO_RGB_FADE_WHITE_OUT,
      this->current_values_,
      this->zero_values_(),
      first_phase_duration
    );
    return;
  }

  if (current_mode == GoveeFloodOutputMode::WHITE || target_mode == GoveeFloodOutputMode::WHITE) {
    this->begin_phase_(
      GoveeFloodTransitionMode::WHITE,
      this->current_values_,
      this->pending_values_,
      this->transition_ms_
    );
    return;
  }

  this->begin_phase_(
    GoveeFloodTransitionMode::RGB,
    this->current_values_,
    this->pending_values_,
    this->transition_ms_
  );
}

void GoveeOutdoorFloodlights2Output::diagnostic_set_white_raw_(uint8_t cool_white, uint8_t warm_white) {
  this->transition_active_ = false;
  this->transition_mode_ = GoveeFloodTransitionMode::NONE;

  this->current_values_ = this->zero_values_();
  this->current_values_.cool_white = static_cast<float>(cool_white) / 255.0f;
  this->current_values_.warm_white = static_cast<float>(warm_white) / 255.0f;

  this->clear_();

  for (uint16_t i = 0; i < this->flood_count_; i++) {
    const uint16_t base = i * 3;

    // RGB pixel off.
    this->set_pixel_rgb_(base + 0, 0, 0, 0);

    // White pixels use exact raw values.
    // This bypasses the normal white low cutoff so we can test low-level behavior.
    this->set_pixel_rgb_(base + 1, cool_white, cool_white, cool_white);
    this->set_pixel_rgb_(base + 2, warm_white, warm_white, warm_white);
  }

  this->show_();
}

void GoveeOutdoorFloodlights2Output::diagnostic_apply_fade_step_(uint8_t value) {
  switch (this->diagnostic_test_) {
    case GoveeFloodDiagnosticTest::COOL_FADE:
      this->diagnostic_set_white_raw_(value, 0);
      if (value % 25 == 0) {
        ESP_LOGI(TAG, "Cool white fade value: %u", value);
      }
      break;

    case GoveeFloodDiagnosticTest::WARM_FADE:
      this->diagnostic_set_white_raw_(0, value);
      if (value % 25 == 0) {
        ESP_LOGI(TAG, "Warm white fade value: %u", value);
      }
      break;

    case GoveeFloodDiagnosticTest::MIXED_FADE: {
      // Approximate 188.5 mireds using the 153-371 mired range.
      static constexpr float COOL_RATIO = 0.837f;
      static constexpr float WARM_RATIO = 0.163f;

      const uint8_t cool = static_cast<uint8_t>((value * COOL_RATIO) + 0.5f);
      const uint8_t warm = static_cast<uint8_t>((value * WARM_RATIO) + 0.5f);

      this->diagnostic_set_white_raw_(cool, warm);
      if (value % 25 == 0) {
        ESP_LOGI(TAG, "Mixed white fade value: total=%u cool=%u warm=%u", value, cool, warm);
      }
      break;
    }

    case GoveeFloodDiagnosticTest::NONE:
    case GoveeFloodDiagnosticTest::RATIO_SWEEP:
    default:
      break;
  }
}

void GoveeOutdoorFloodlights2Output::diagnostic_apply_ratio_step_(uint8_t step) {
  static constexpr uint8_t TOTAL = 160;

  if (step > 100) {
    step = 100;
  }

  const float warm_ratio = static_cast<float>(step) / 100.0f;
  const float cool_ratio = 1.0f - warm_ratio;

  const uint8_t cool = static_cast<uint8_t>((TOTAL * cool_ratio) + 0.5f);
  const uint8_t warm = static_cast<uint8_t>((TOTAL * warm_ratio) + 0.5f);

  this->diagnostic_set_white_raw_(cool, warm);
  if (step % 10 == 0) {
    ESP_LOGI(TAG, "Ratio sweep: step=%u cool=%u warm=%u", step, cool, warm);
  }
}

void GoveeOutdoorFloodlights2Output::update_diagnostic_() {
  const uint32_t now = millis();

  if (this->diagnostic_phase_ == GoveeFloodDiagnosticPhase::HOLD) {
    if (now - this->diagnostic_phase_start_ms_ < DIAGNOSTIC_HOLD_MS) {
      return;
    }

    if (this->diagnostic_test_ == GoveeFloodDiagnosticTest::RATIO_SWEEP) {
      this->diagnostic_phase_ = GoveeFloodDiagnosticPhase::RATIO_REVERSE;
      this->diagnostic_step_ = 100;
    } else {
      this->diagnostic_phase_ = GoveeFloodDiagnosticPhase::FADE_DOWN;
      this->diagnostic_step_ = 255;
    }

    this->diagnostic_last_frame_ms_ = 0;
    return;
  }

  uint32_t frame_interval = DIAGNOSTIC_FADE_INTERVAL_MS;
  if (
    this->diagnostic_phase_ == GoveeFloodDiagnosticPhase::RATIO_FORWARD ||
    this->diagnostic_phase_ == GoveeFloodDiagnosticPhase::RATIO_REVERSE
  ) {
    frame_interval = DIAGNOSTIC_RATIO_INTERVAL_MS;
  }

  if (
    this->diagnostic_last_frame_ms_ != 0 &&
    now - this->diagnostic_last_frame_ms_ < frame_interval
  ) {
    return;
  }

  this->diagnostic_last_frame_ms_ = now;

  switch (this->diagnostic_phase_) {
    case GoveeFloodDiagnosticPhase::FADE_UP:
      this->diagnostic_apply_fade_step_(static_cast<uint8_t>(this->diagnostic_step_));

      if (this->diagnostic_step_ >= 255) {
        this->diagnostic_phase_ = GoveeFloodDiagnosticPhase::HOLD;
        this->diagnostic_phase_start_ms_ = now;
      } else {
        this->diagnostic_step_++;
      }
      break;

    case GoveeFloodDiagnosticPhase::FADE_DOWN:
      this->diagnostic_apply_fade_step_(static_cast<uint8_t>(this->diagnostic_step_));

      if (this->diagnostic_step_ == 0) {
        this->finish_diagnostic_();
      } else {
        this->diagnostic_step_--;
      }
      break;

    case GoveeFloodDiagnosticPhase::RATIO_FORWARD:
      this->diagnostic_apply_ratio_step_(static_cast<uint8_t>(this->diagnostic_step_));

      if (this->diagnostic_step_ >= 100) {
        this->diagnostic_phase_ = GoveeFloodDiagnosticPhase::HOLD;
        this->diagnostic_phase_start_ms_ = now;
      } else {
        this->diagnostic_step_++;
      }
      break;

    case GoveeFloodDiagnosticPhase::RATIO_REVERSE:
      this->diagnostic_apply_ratio_step_(static_cast<uint8_t>(this->diagnostic_step_));

      if (this->diagnostic_step_ == 0) {
        this->finish_diagnostic_();
      } else {
        this->diagnostic_step_--;
      }
      break;

    case GoveeFloodDiagnosticPhase::NONE:
    case GoveeFloodDiagnosticPhase::HOLD:
    default:
      break;
  }
}

void GoveeOutdoorFloodlights2Output::finish_diagnostic_() {
  const auto finished_test = this->diagnostic_test_;

  this->diagnostic_set_white_raw_(0, 0);
  this->diagnostic_test_ = GoveeFloodDiagnosticTest::NONE;
  this->diagnostic_phase_ = GoveeFloodDiagnosticPhase::NONE;
  this->diagnostic_step_ = 0;

  switch (finished_test) {
    case GoveeFloodDiagnosticTest::COOL_FADE:
      ESP_LOGI(TAG, "Finished diagnostic: cool white only fade");
      break;

    case GoveeFloodDiagnosticTest::WARM_FADE:
      ESP_LOGI(TAG, "Finished diagnostic: warm white only fade");
      break;

    case GoveeFloodDiagnosticTest::MIXED_FADE:
      ESP_LOGI(TAG, "Finished diagnostic: mixed white fade");
      break;

    case GoveeFloodDiagnosticTest::RATIO_SWEEP:
      ESP_LOGI(TAG, "Finished diagnostic: white ratio sweep at fixed brightness");
      break;

    case GoveeFloodDiagnosticTest::NONE:
    default:
      break;
  }
}

void GoveeOutdoorFloodlights2Output::run_diagnostic_test(GoveeFloodDiagnosticTest test_type) {
  if (test_type == GoveeFloodDiagnosticTest::NONE) {
    ESP_LOGW(TAG, "No diagnostic test selected");
    return;
  }

  this->transition_active_ = false;
  this->transition_mode_ = GoveeFloodTransitionMode::NONE;
  this->diagnostic_test_ = test_type;
  this->diagnostic_step_ = 0;
  this->diagnostic_phase_start_ms_ = millis();
  this->diagnostic_last_frame_ms_ = 0;
  this->diagnostic_set_white_raw_(0, 0);

  switch (test_type) {
    case GoveeFloodDiagnosticTest::COOL_FADE:
      ESP_LOGI(TAG, "Starting diagnostic: cool white only fade");
      this->diagnostic_phase_ = GoveeFloodDiagnosticPhase::FADE_UP;
      break;

    case GoveeFloodDiagnosticTest::WARM_FADE:
      ESP_LOGI(TAG, "Starting diagnostic: warm white only fade");
      this->diagnostic_phase_ = GoveeFloodDiagnosticPhase::FADE_UP;
      break;

    case GoveeFloodDiagnosticTest::MIXED_FADE:
      ESP_LOGI(TAG, "Starting diagnostic: mixed white fade");
      this->diagnostic_phase_ = GoveeFloodDiagnosticPhase::FADE_UP;
      break;

    case GoveeFloodDiagnosticTest::RATIO_SWEEP:
      ESP_LOGI(TAG, "Starting diagnostic: white ratio sweep at fixed brightness");
      this->diagnostic_phase_ = GoveeFloodDiagnosticPhase::RATIO_FORWARD;
      break;

    case GoveeFloodDiagnosticTest::NONE:
    default:
      this->diagnostic_phase_ = GoveeFloodDiagnosticPhase::NONE;
      break;
  }
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

void GoveeOutdoorFloodlights2DiagnosticButton::setup() {
  if (this->light_output_ == nullptr) {
    ESP_LOGE(BUTTON_TAG, "No Govee flood light output was configured");
    this->mark_failed();
    return;
  }
}

void GoveeOutdoorFloodlights2DiagnosticButton::dump_config() {
  ESP_LOGCONFIG(BUTTON_TAG, "Govee Outdoor Floodlights 2 Diagnostic Button");
  ESP_LOGCONFIG(BUTTON_TAG, "  Test type: %u", static_cast<uint8_t>(this->test_type_));
}

void GoveeOutdoorFloodlights2DiagnosticButton::press_action() {
  if (this->light_output_ == nullptr) {
    ESP_LOGW(BUTTON_TAG, "No light output configured");
    return;
  }

  this->light_output_->run_diagnostic_test(this->test_type_);
}

}  // namespace govee_outdoor_floodlights_2
}  // namespace esphome
