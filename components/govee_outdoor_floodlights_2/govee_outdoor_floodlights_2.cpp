#include "govee_outdoor_floodlights_2.h"

namespace esphome {
namespace govee_outdoor_floodlights_2 {

static const char *const TAG = "govee_outdoor_floodlights_2";

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

void GoveeOutdoorFloodlights2Output::dump_config() {
  ESP_LOGCONFIG(TAG, "Govee Outdoor Floodlights 2");
  ESP_LOGCONFIG(TAG, "  Pin: GPIO%u", this->pin_);
  ESP_LOGCONFIG(TAG, "  Flood count: %u", this->flood_count_);
  ESP_LOGCONFIG(TAG, "  Physical pixels: %u", this->pixel_count_);
  ESP_LOGCONFIG(TAG, "  Layout per flood: RGB, Cool White, Warm White");
  ESP_LOGCONFIG(TAG, "  RGB order: GRB");
  ESP_LOGCONFIG(TAG, "  Cold white: 6500 K / %.1f mireds", COLD_WHITE_MIRED);
  ESP_LOGCONFIG(TAG, "  Warm white: 2700 K / %.1f mireds", WARM_WHITE_MIRED);
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

  // Fixed GRB byte order.
  this->pixel_data_[offset + 0] = green;
  this->pixel_data_[offset + 1] = red;
  this->pixel_data_[offset + 2] = blue;
}

void GoveeOutdoorFloodlights2Output::show_() {
  if (this->rmt_channel_ == nullptr || this->rmt_encoder_ == nullptr) {
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

  rmt_tx_wait_all_done(this->rmt_channel_, 100);
}

void GoveeOutdoorFloodlights2Output::write_state(light::LightState *state) {
  auto values = state->current_values;

  const float master = values.get_state() * values.get_brightness();
  const auto color_mode = values.get_color_mode();

  uint8_t rgb_red = 0;
  uint8_t rgb_green = 0;
  uint8_t rgb_blue = 0;
  uint8_t cool_white = 0;
  uint8_t warm_white = 0;

  if (master <= 0.0f) {
    // Leave everything off.
  } else if (color_mode == light::ColorMode::RGB) {
    const float color_brightness = values.get_color_brightness();

    rgb_red = this->to_u8_(values.get_red() * color_brightness * master);
    rgb_green = this->to_u8_(values.get_green() * color_brightness * master);
    rgb_blue = this->to_u8_(values.get_blue() * color_brightness * master);
  } else if (color_mode == light::ColorMode::COLOR_TEMPERATURE) {
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

    cool_white = this->to_u8_(cold_ratio * master);
    warm_white = this->to_u8_(warm_ratio * master);
  }

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

}  // namespace govee_outdoor_floodlights_2
}  // namespace esphome