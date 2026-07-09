#include "govee_tree_lights.h"

namespace esphome {
namespace govee_tree_lights {

static const char *const TAG = "govee_tree_lights";

void GoveeTreeLights::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Govee Tree Lights output");

  this->clear_frame_();
  this->setup_relay_();
  this->init_rmt_();
  this->rebuild_frame_from_current_words_();
}

void GoveeTreeLights::loop() {
  this->update_transitions_();
  this->update_relay_();

  const uint32_t now = millis();

  if (now - this->last_send_ms_ >= 20) {
    this->last_send_ms_ = now;

    if (this->relay_pin_ == nullptr || this->relay_is_on_) {
      this->send_frame_();
    }
  }
}

void GoveeTreeLights::dump_config() {
  ESP_LOGCONFIG(TAG, "Govee Tree Lights:");
  LOG_PIN("  Data Pin: ", this->data_pin_);
  LOG_PIN("  Relay Pin: ", this->relay_pin_);
  ESP_LOGCONFIG(TAG, "  Transition Time: %u ms", this->transition_time_ms_);
  ESP_LOGCONFIG(TAG, "  Power Off Delay: %u ms", this->power_off_delay_ms_);

  if (this->setup_error_ != nullptr) {
    ESP_LOGE(TAG, "  Setup Error: %s", this->setup_error_);
  }
}

void GoveeTreeLights::setup_relay_() {
  if (this->relay_pin_ == nullptr) {
    return;
  }

  this->relay_pin_->setup();
  this->set_relay_state_(false);
}

void GoveeTreeLights::set_relay_state_(bool state) {
  if (this->relay_pin_ == nullptr) {
    return;
  }

  if (this->relay_is_on_ == state) {
    return;
  }

  this->relay_is_on_ = state;
  this->relay_pin_->digital_write(state);

  if (state) {
    ESP_LOGD(TAG, "Relay turned on");
  } else {
    ESP_LOGD(TAG, "Relay turned off");
  }
}

bool GoveeTreeLights::words_active_(const uint16_t words[10]) {
  for (int i = 0; i < 10; i++) {
    if (words[i] != 0x0000) {
      return true;
    }
  }

  return false;
}

bool GoveeTreeLights::any_current_output_active_() {
  for (int spot = 0; spot < 3; spot++) {
    if (this->words_active_(this->current_words_[spot])) {
      return true;
    }
  }

  return false;
}

bool GoveeTreeLights::any_target_output_active_() {
  for (int spot = 0; spot < 3; spot++) {
    if (this->words_active_(this->target_words_[spot])) {
      return true;
    }
  }

  return false;
}

bool GoveeTreeLights::any_transition_active_() {
  for (int spot = 0; spot < 3; spot++) {
    if (this->transition_active_[spot]) {
      return true;
    }
  }

  return false;
}

void GoveeTreeLights::update_relay_() {
  if (this->relay_pin_ == nullptr) {
    return;
  }

  const bool output_active =
    this->any_current_output_active_() ||
    this->any_target_output_active_() ||
    this->any_transition_active_();

  if (output_active) {
    this->relay_off_pending_ = false;
    this->set_relay_state_(true);
    return;
  }

  if (!this->relay_is_on_) {
    this->relay_off_pending_ = false;
    return;
  }

  const uint32_t now = millis();

  if (!this->relay_off_pending_) {
    this->relay_off_pending_ = true;
    this->relay_off_at_ms_ = now + this->power_off_delay_ms_;
    ESP_LOGD(TAG, "Relay off scheduled in %u ms", this->power_off_delay_ms_);
    return;
  }

  if (static_cast<int32_t>(now - this->relay_off_at_ms_) >= 0) {
    this->relay_off_pending_ = false;
    this->set_relay_state_(false);
  }
}

void GoveeTreeLights::init_rmt_() {
  this->setup_error_ = nullptr;
  std::memset(this->setup_error_buffer_, 0x00, sizeof(this->setup_error_buffer_));

  if (this->data_pin_ == nullptr) {
    this->setup_error_ = "Data pin not configured";
    ESP_LOGE(TAG, "%s", this->setup_error_);
    this->mark_failed();
    return;
  }

  rmt_tx_channel_config_t tx_channel_config = {
    .gpio_num = static_cast<gpio_num_t>(this->data_pin_->get_pin()),
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = RMT_RESOLUTION_HZ,
    .mem_block_symbols = 64,
    .trans_queue_depth = 4,
  };

  esp_err_t err = rmt_new_tx_channel(&tx_channel_config, &this->tx_channel_);
  if (err != ESP_OK) {
    snprintf(
      this->setup_error_buffer_,
      sizeof(this->setup_error_buffer_),
      "rmt_new_tx_channel failed on GPIO%d: %s",
      this->data_pin_->get_pin(),
      esp_err_to_name(err)
    );

    this->setup_error_ = this->setup_error_buffer_;
    ESP_LOGE(TAG, "%s", this->setup_error_);
    this->tx_channel_ = nullptr;
    this->copy_encoder_ = nullptr;
    this->mark_failed();
    return;
  }

  rmt_copy_encoder_config_t copy_encoder_config = {};
  err = rmt_new_copy_encoder(&copy_encoder_config, &this->copy_encoder_);
  if (err != ESP_OK) {
    snprintf(
      this->setup_error_buffer_,
      sizeof(this->setup_error_buffer_),
      "rmt_new_copy_encoder failed on GPIO%d: %s",
      this->data_pin_->get_pin(),
      esp_err_to_name(err)
    );

    this->setup_error_ = this->setup_error_buffer_;
    ESP_LOGE(TAG, "%s", this->setup_error_);
    this->copy_encoder_ = nullptr;
    this->mark_failed();
    return;
  }

  err = rmt_enable(this->tx_channel_);
  if (err != ESP_OK) {
    snprintf(
      this->setup_error_buffer_,
      sizeof(this->setup_error_buffer_),
      "rmt_enable failed on GPIO%d: %s",
      this->data_pin_->get_pin(),
      esp_err_to_name(err)
    );

    this->setup_error_ = this->setup_error_buffer_;
    ESP_LOGE(TAG, "%s", this->setup_error_);
    this->mark_failed();
    return;
  }

  ESP_LOGCONFIG(TAG, "RMT TX channel ready on GPIO%d", this->data_pin_->get_pin());
}

float GoveeTreeLights::clamp_(float value) {
  if (value < 0.0f) {
    return 0.0f;
  }

  if (value > 1.0f) {
    return 1.0f;
  }

  return value;
}

uint16_t GoveeTreeLights::level_from_float_(float value) {
  value = this->clamp_(value);

  if (value <= 0.0001f) {
    return 0x0000;
  }

  const float min_nonzero = 0x0831;
  const float max_value = 0xCCC9;
  return static_cast<uint16_t>(std::round(min_nonzero + value * (max_value - min_nonzero)));
}

uint16_t GoveeTreeLights::scale_word_(uint16_t max_value, float value) {
  value = this->clamp_(value);

  if (value <= 0.0001f) {
    return 0x0000;
  }

  return static_cast<uint16_t>(std::round(static_cast<float>(max_value) * value));
}

void GoveeTreeLights::set_word_(int word_index, uint16_t value) {
  int byte_index = word_index * 2;

  if (byte_index < 0 || byte_index + 1 >= GOVEE_FRAME_BYTES) {
    return;
  }

  this->frame_[byte_index] = static_cast<uint8_t>((value >> 8) & 0xFF);
  this->frame_[byte_index + 1] = static_cast<uint8_t>(value & 0xFF);
}

void GoveeTreeLights::clear_frame_() {
  std::memset(this->frame_, 0x00, sizeof(this->frame_));

  this->set_word_(30, 0x9CE7);
  this->set_word_(31, 0x39FF);
}

void GoveeTreeLights::build_rgb_words_(uint16_t words[10], uint16_t r, uint16_t g, uint16_t b) {
  if (g > 0) {
    words[0] = g;
    words[1] = g;
  }

  if (r > 0) {
    words[3] = r;
    words[4] = r;
  }

  if (b > 0) {
    words[8] = b;
    words[9] = b;
  }
}

void GoveeTreeLights::build_cold_white_words_(uint16_t words[10], float level) {
  words[0] = this->scale_word_(0x3078, level);
  words[1] = this->scale_word_(0x3078, level);

  words[5] = this->scale_word_(0x85C6, level);
  words[6] = this->scale_word_(0x85C6, level);

  words[8] = this->scale_word_(0x168C, level);
  words[9] = this->scale_word_(0x168C, level);
}

void GoveeTreeLights::build_warm_white_words_(uint16_t words[10], float level) {
  words[0] = this->scale_word_(0x26A9, level);
  words[1] = this->scale_word_(0x26A9, level);

  words[3] = this->scale_word_(0x918B, level);
  words[4] = this->scale_word_(0x918B, level);

  words[5] = this->scale_word_(0x1494, level);
  words[6] = this->scale_word_(0x1494, level);
}

void GoveeTreeLights::build_blended_white_words_(uint16_t words[10], float cold_white, float warm_white) {
  cold_white = this->clamp_(cold_white);
  warm_white = this->clamp_(warm_white);

  if (cold_white <= 0.0001f && warm_white <= 0.0001f) {
    return;
  }

  auto blend = [&](uint16_t cold_max, uint16_t warm_max) -> uint16_t {
    float mixed = (static_cast<float>(cold_max) * cold_white) +
                  (static_cast<float>(warm_max) * warm_white);
    return static_cast<uint16_t>(std::round(std::min(mixed, 65535.0f)));
  };

  words[0] = blend(0x3078, 0x26A9);
  words[1] = blend(0x3078, 0x26A9);

  words[3] = blend(0x0000, 0x918B);
  words[4] = blend(0x0000, 0x918B);

  words[5] = blend(0x85C6, 0x1494);
  words[6] = blend(0x85C6, 0x1494);

  words[8] = blend(0x168C, 0x0000);
  words[9] = blend(0x168C, 0x0000);
}

void GoveeTreeLights::build_target_words_(
  uint16_t words[10],
  float r,
  float g,
  float b,
  float cold_white,
  float warm_white
) {
  for (int i = 0; i < 10; i++) {
    words[i] = 0x0000;
  }

  r = this->clamp_(r);
  g = this->clamp_(g);
  b = this->clamp_(b);
  cold_white = this->clamp_(cold_white);
  warm_white = this->clamp_(warm_white);

  const float max_rgb = std::max({r, g, b});
  const float max_white = std::max(cold_white, warm_white);
  const float max_delta = std::max({std::fabs(r - g), std::fabs(r - b), std::fabs(g - b)});

  if (max_rgb <= 0.0001f && max_white <= 0.0001f) {
    return;
  }

  if (max_rgb > 0.0001f && max_delta >= 0.035f) {
    this->build_rgb_words_(
      words,
      this->level_from_float_(r),
      this->level_from_float_(g),
      this->level_from_float_(b)
    );
    return;
  }

  if (max_white > 0.0001f) {
    this->build_blended_white_words_(words, cold_white, warm_white);
    return;
  }

  this->build_cold_white_words_(words, max_rgb);
}

void GoveeTreeLights::apply_words_to_frame_(int spot_index, const uint16_t words[10]) {
  int base = this->spot_base_word_(spot_index);

  for (int i = 0; i < 10; i++) {
    this->set_word_(base + i, words[i]);
  }
}

void GoveeTreeLights::rebuild_frame_from_current_words_() {
  this->clear_frame_();

  for (int spot = 0; spot < 3; spot++) {
    this->apply_words_to_frame_(spot, this->current_words_[spot]);
  }
}

void GoveeTreeLights::update_transitions_() {
  const uint32_t now = millis();
  bool changed = false;

  for (int spot = 0; spot < 3; spot++) {
    if (!this->transition_active_[spot]) {
      continue;
    }

    const uint32_t elapsed = now - this->transition_start_ms_[spot];
    const uint32_t duration = this->transition_duration_ms_[spot];

    float progress = 1.0f;

    if (duration > 0 && elapsed < duration) {
      progress = static_cast<float>(elapsed) / static_cast<float>(duration);
    }

    if (progress >= 1.0f) {
      progress = 1.0f;
      this->transition_active_[spot] = false;
    }

    for (int i = 0; i < 10; i++) {
      const float start = static_cast<float>(this->start_words_[spot][i]);
      const float target = static_cast<float>(this->target_words_[spot][i]);
      this->current_words_[spot][i] = static_cast<uint16_t>(std::round(start + ((target - start) * progress)));
    }

    changed = true;
  }

  if (changed) {
    this->rebuild_frame_from_current_words_();
  }
}

void GoveeTreeLights::set_spot_rgbww(
  int spot_index,
  float r,
  float g,
  float b,
  float cold_white,
  float warm_white
) {
  if (spot_index < 0 || spot_index > 2) {
    return;
  }

  this->update_transitions_();

  uint16_t new_target_words[10]{};
  this->build_target_words_(new_target_words, r, g, b, cold_white, warm_white);

  if (this->words_active_(new_target_words)) {
    this->relay_off_pending_ = false;
    this->set_relay_state_(true);
  }

  for (int i = 0; i < 10; i++) {
    this->start_words_[spot_index][i] = this->current_words_[spot_index][i];
    this->target_words_[spot_index][i] = new_target_words[i];
  }

  if (this->transition_time_ms_ == 0) {
    for (int i = 0; i < 10; i++) {
      this->current_words_[spot_index][i] = this->target_words_[spot_index][i];
    }

    this->transition_active_[spot_index] = false;
    this->rebuild_frame_from_current_words_();
    return;
  }

  this->transition_start_ms_[spot_index] = millis();
  this->transition_duration_ms_[spot_index] = this->transition_time_ms_;
  this->transition_active_[spot_index] = true;
}

void GoveeTreeLights::build_rmt_symbols_from_frame_() {
  int symbol_index = 0;

  for (int byte_index = 0; byte_index < GOVEE_FRAME_BYTES; byte_index++) {
    uint8_t value = this->frame_[byte_index];

    for (int bit = 7; bit >= 0; bit--) {
      bool is_one = (value >> bit) & 0x01;

      if (is_one) {
        this->symbols_[symbol_index].level0 = 1;
        this->symbols_[symbol_index].duration0 = T1H_TICKS;
        this->symbols_[symbol_index].level1 = 0;
        this->symbols_[symbol_index].duration1 = T1L_TICKS;
      } else {
        this->symbols_[symbol_index].level0 = 1;
        this->symbols_[symbol_index].duration0 = T0H_TICKS;
        this->symbols_[symbol_index].level1 = 0;
        this->symbols_[symbol_index].duration1 = T0L_TICKS;
      }

      symbol_index++;
    }
  }

  this->symbols_[symbol_index].level0 = 0;
  this->symbols_[symbol_index].duration0 = RESET_TICKS;
  this->symbols_[symbol_index].level1 = 0;
  this->symbols_[symbol_index].duration1 = RESET_TICKS;
}

void GoveeTreeLights::send_frame_() {
  if (this->tx_channel_ == nullptr || this->copy_encoder_ == nullptr) {
    return;
  }

  this->build_rmt_symbols_from_frame_();

  rmt_transmit_config_t tx_config = {
    .loop_count = 0,
  };

  esp_err_t err = rmt_transmit(
    this->tx_channel_,
    this->copy_encoder_,
    this->symbols_,
    sizeof(this->symbols_),
    &tx_config
  );

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "RMT transmit failed: %s", esp_err_to_name(err));
    return;
  }

  err = rmt_tx_wait_all_done(this->tx_channel_, portMAX_DELAY);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "RMT wait failed: %s", esp_err_to_name(err));
  }
}

light::LightTraits GoveeTreeLightsLightOutput::get_traits() {
  auto traits = light::LightTraits();

  traits.set_supported_color_modes({
    light::ColorMode::RGB,
    light::ColorMode::COLD_WARM_WHITE,
  });

  traits.set_min_mireds(153);
  traits.set_max_mireds(500);

  return traits;
}

void GoveeTreeLightsLightOutput::write_state(light::LightState *state) {
  if (this->parent_ == nullptr) {
    return;
  }

  const auto color_mode = state->current_values.get_color_mode();

  float red = 0.0f;
  float green = 0.0f;
  float blue = 0.0f;
  float cold_white = 0.0f;
  float warm_white = 0.0f;

  if (color_mode == light::ColorMode::RGB) {
    state->current_values_as_rgb(&red, &green, &blue);

    this->parent_->set_spot_rgbww(
      this->spot_index_,
      red,
      green,
      blue,
      0.0f,
      0.0f
    );

    return;
  }

  if (color_mode == light::ColorMode::COLD_WARM_WHITE) {
    state->current_values_as_cwww(&cold_white, &warm_white, false);

    this->parent_->set_spot_rgbww(
      this->spot_index_,
      0.0f,
      0.0f,
      0.0f,
      cold_white,
      warm_white
    );

    return;
  }

  this->parent_->set_spot_rgbww(
    this->spot_index_,
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    0.0f
  );
}

}  // namespace govee_tree_lights
}  // namespace esphome