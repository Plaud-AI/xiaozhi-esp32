/**
 * @file micro_wake_word.cc
 * @brief Implementation of Micro Wake Word detection
 */

#include "micro_wake_word.h"
#include <cmath>
#include <algorithm>

namespace micro_wake_word {

static const char *const TAG = "MicroWakeWord";
static const size_t SAMPLE_RATE_HZ = AUDIO_SAMPLE_FREQUENCY;

static const char *micro_wake_word_state_to_string(State state) {
  switch (state) {
    case State::IDLE:
      return "IDLE";
    case State::DETECTING_WAKE_WORD:
      return "DETECTING_WAKE_WORD";
    case State::DETECTED:
      return "DETECTED";
    default:
      return "UNKNOWN";
  }
}

MicroWakeWord::MicroWakeWord() {
  MWW_LOGI(TAG, "MicroWakeWord constructor");
}

MicroWakeWord::~MicroWakeWord() {
  deallocate_buffers_();
  unload_models_();
}

bool MicroWakeWord::Initialize() {
  MWW_LOGI(TAG, "Initializing MicroWakeWord...");

  // Only register operations once
  if (!ops_registered_) {
    MWW_LOGI(TAG, "Registering TFLite streaming operations...");
    if (!this->register_streaming_ops_(this->streaming_op_resolver_)) {
      MWW_LOGE(TAG, "Failed to register streaming operations");
      return false;
    }
    ops_registered_ = true;
    MWW_LOGI(TAG, "Successfully registered all TFLite streaming operations");
  } else {
    MWW_LOGI(TAG, "TFLite operations already registered, skipping...");
  }

  // Configure audio frontend - MUST match the parameters used during model training!
  this->frontend_config_.window.size_ms = FEATURE_DURATION_MS;
  this->frontend_config_.window.step_size_ms = this->features_step_size_;
  this->frontend_config_.filterbank.num_channels = PREPROCESSOR_FEATURE_SIZE;
  this->frontend_config_.filterbank.lower_band_limit = FILTERBANK_LOWER_BAND_LIMIT;
  this->frontend_config_.filterbank.upper_band_limit = FILTERBANK_UPPER_BAND_LIMIT;
  
  // Noise reduction settings
  this->frontend_config_.noise_reduction.smoothing_bits = NOISE_REDUCTION_SMOOTHING_BITS;
  this->frontend_config_.noise_reduction.even_smoothing = NOISE_REDUCTION_EVEN_SMOOTHING;
  this->frontend_config_.noise_reduction.odd_smoothing = NOISE_REDUCTION_ODD_SMOOTHING;
  this->frontend_config_.noise_reduction.min_signal_remaining = NOISE_REDUCTION_MIN_SIGNAL_REMAINING;
  
  // PCAN gain control settings
  this->frontend_config_.pcan_gain_control.enable_pcan = PCAN_GAIN_CONTROL_ENABLE_PCAN;
  this->frontend_config_.pcan_gain_control.strength = PCAN_GAIN_CONTROL_STRENGTH;
  this->frontend_config_.pcan_gain_control.offset = PCAN_GAIN_CONTROL_OFFSET;
  this->frontend_config_.pcan_gain_control.gain_bits = PCAN_GAIN_CONTROL_GAIN_BITS;
  
  // Log scale settings
  this->frontend_config_.log_scale.enable_log = LOG_SCALE_ENABLE_LOG;
  this->frontend_config_.log_scale.scale_shift = LOG_SCALE_SCALE_SHIFT;
  
  MWW_LOGI(TAG, "Frontend Configuration:");
  MWW_LOGI(TAG, "   - Filterbank: %.1f - %.1f Hz, %d channels",
           FILTERBANK_LOWER_BAND_LIMIT, FILTERBANK_UPPER_BAND_LIMIT, PREPROCESSOR_FEATURE_SIZE);
  MWW_LOGI(TAG, "   - Noise Reduction: min_signal=%.2f", 
           NOISE_REDUCTION_MIN_SIGNAL_REMAINING);
  MWW_LOGI(TAG, "   - PCAN Gain Control: strength=%.2f, offset=%.1f", 
           PCAN_GAIN_CONTROL_STRENGTH, PCAN_GAIN_CONTROL_OFFSET);
  MWW_LOGI(TAG, "   - Log Scale: shift=%d", LOG_SCALE_SCALE_SHIFT);

  MWW_LOGI(TAG, "Micro Wake Word initialized");
  return true;
}

void MicroWakeWord::Feed(const std::vector<int16_t> &data) {
  static uint32_t feed_count = 0;
  feed_count++;
  
  if (state_ != State::DETECTING_WAKE_WORD) {
    if (feed_count % 100 == 0) {
      MWW_LOGI(TAG, "Feed #%lu: Not in detecting state (current state: %d)", 
               (unsigned long)feed_count, (int)state_);
    }
    return;
  }

  if (feed_count % 1000 == 0) {
    // Calculate audio level to check if we're receiving valid data
    int32_t sum = 0;
    int16_t max_val = 0;
    for (size_t i = 0; i < std::min(data.size(), (size_t)100); i++) {
      sum += abs(data[i]);
      if (abs(data[i]) > max_val) max_val = abs(data[i]);
    }
    int16_t avg = data.size() > 0 ? sum / std::min(data.size(), (size_t)100) : 0;
    
    MWW_LOGD(TAG, "Feed #%lu: Received %u samples, ring buffer: %u, avg_level: %d, max: %d", 
             (unsigned long)feed_count, (unsigned)data.size(), (unsigned)ring_buffer_available_, avg, max_val);
  }

  // Store data for wake word recording (keep ~2 seconds)
  wake_word_pcm_.insert(wake_word_pcm_.end(), data.begin(), data.end());
  while (wake_word_pcm_.size() > SAMPLE_RATE_HZ * 2) {
    wake_word_pcm_.erase(wake_word_pcm_.begin(), 
                         wake_word_pcm_.begin() + (wake_word_pcm_.size() - SAMPLE_RATE_HZ * 2));
  }

  // Write audio data to ring buffer
  size_t written = write_to_ring_buffer_(data.data(), data.size());
  if (written < data.size()) {
    MWW_LOGW(TAG, "Ring buffer partial write: %zu/%zu samples", written, data.size());
  }

  // Process audio and detect wake words
  while (has_enough_samples_()) {
    update_model_probabilities_();
    if (detect_wake_words_()) {
      MWW_LOGI(TAG, "Wake Word '%s' Detected!", detected_wake_word_.c_str());
      detected_ = true;
      set_state_(State::DETECTED);
      if (detection_callback_) {
        MWW_LOGI(TAG, "Calling detection callback...");
        detection_callback_(detected_wake_word_);
      } else {
        MWW_LOGW(TAG, "Detection callback is not set!");
      }
      break;
    }
  }
}

void MicroWakeWord::OnWakeWordDetected(std::function<void(const std::string &)> callback) {
  detection_callback_ = callback;
}

void MicroWakeWord::Start() {
  MWW_LOGI(TAG, "Starting MicroWakeWord detection");
  MWW_LOGI(TAG, "  - Wake word models: %u", (unsigned int)wake_word_models_.size());
  MWW_LOGI(TAG, "  - Sample rate: %u Hz", (unsigned int)AUDIO_SAMPLE_FREQUENCY);
  MWW_LOGI(TAG, "  - Feature duration: %d ms", FEATURE_DURATION_MS);
  MWW_LOGI(TAG, "  - Ring buffer: %u ms (%u samples, %.1f KB)", 
           RING_BUFFER_DURATION_MS, (unsigned int)RING_BUFFER_SIZE, 
           (RING_BUFFER_SIZE * sizeof(int16_t)) / 1024.0f);

  if (state_ != State::IDLE) {
    MWW_LOGW(TAG, "Wake word is already running (state: %d)", (int)state_);
    return;
  }

  if (wake_word_models_.empty()) {
    MWW_LOGE(TAG, "No wake word models configured!");
    return;
  }

  MWW_LOGI(TAG, "Loading models and allocating buffers...");
  if (!load_models_()) {
    MWW_LOGE(TAG, "Failed to load models");
    return;
  }
  
  if (!allocate_buffers_()) {
    MWW_LOGE(TAG, "Failed to allocate buffers");
    return;
  }

  reset_states_();
  set_state_(State::DETECTING_WAKE_WORD);
  MWW_LOGI(TAG, "MicroWakeWord detection started successfully");
}

void MicroWakeWord::Stop() {
  MWW_LOGI(TAG, "Stopping MicroWakeWord detection");

  if (state_ == State::IDLE) {
    MWW_LOGW(TAG, "Wake word is already stopped");
    return;
  }

  set_state_(State::IDLE);
  unload_models_();
  deallocate_buffers_();
}

size_t MicroWakeWord::GetFeedSize() const {
  // Return chunk size for feeding audio data (480 samples = 30ms at 16kHz)
  return 480;
}

const std::string &MicroWakeWord::GetLastDetectedWakeWord() const {
  return detected_wake_word_;
}

void MicroWakeWord::add_wake_word_model(const uint8_t *model_start, float probability_cutoff,
                                        size_t sliding_window_average_size, const std::string &wake_word,
                                        size_t tensor_arena_size) {
  this->wake_word_models_.push_back(
      make_unique<WakeWordModel>(model_start, probability_cutoff, sliding_window_average_size, wake_word,
                                 tensor_arena_size));
  MWW_LOGI(TAG, "Added wake word model: '%s' (threshold: %.3f, window: %u, arena: %u bytes)", 
           wake_word.c_str(), probability_cutoff, (unsigned int)sliding_window_average_size, 
           (unsigned int)tensor_arena_size);
}

void MicroWakeWord::set_state_(State state) {
  MWW_LOGD(TAG, "State changed from %s to %s", micro_wake_word_state_to_string(this->state_),
           micro_wake_word_state_to_string(state));
  this->state_ = state;
}

bool MicroWakeWord::has_enough_samples_() {
  return ring_buffer_available_ >= this->new_samples_to_get_();
}

size_t MicroWakeWord::read_from_ring_buffer_(int16_t *buffer, size_t samples) {
  size_t samples_to_read = std::min(samples, ring_buffer_available_);
  
  for (size_t i = 0; i < samples_to_read; i++) {
    buffer[i] = ring_buffer_[ring_buffer_read_pos_];
    ring_buffer_read_pos_ = (ring_buffer_read_pos_ + 1) % RING_BUFFER_SIZE;
  }
  
  ring_buffer_available_ -= samples_to_read;
  return samples_to_read;
}

size_t MicroWakeWord::write_to_ring_buffer_(const int16_t *buffer, size_t samples) {
  size_t samples_written = 0;
  
  for (size_t i = 0; i < samples; i++) {
    if (ring_buffer_available_ >= RING_BUFFER_SIZE) {
      MWW_LOGW(TAG, "Ring buffer overflow, dropping oldest sample");
      ring_buffer_read_pos_ = (ring_buffer_read_pos_ + 1) % RING_BUFFER_SIZE;
      ring_buffer_available_--;
    }
    
    ring_buffer_[ring_buffer_write_pos_] = buffer[i];
    ring_buffer_write_pos_ = (ring_buffer_write_pos_ + 1) % RING_BUFFER_SIZE;
    ring_buffer_available_++;
    samples_written++;
  }
  
  return samples_written;
}

bool MicroWakeWord::allocate_buffers_() {
  ExternalRAMAllocator<int16_t> audio_samples_allocator(ExternalRAMAllocator<int16_t>::ALLOW_FAILURE);

  // Allocate preprocessor buffer (prefer internal RAM for cache coherency)
  if (this->preprocessor_audio_buffer_ == nullptr) {
    size_t buffer_size = this->new_samples_to_get_();
    // Use internal allocator - on platforms without PSRAM, this is just malloc
    this->preprocessor_audio_buffer_ = static_cast<int16_t*>(malloc(buffer_size * sizeof(int16_t)));
    
    if (this->preprocessor_audio_buffer_ == nullptr) {
      MWW_LOGE(TAG, "Could not allocate preprocessor buffer (%u bytes)", 
               (unsigned int)(buffer_size * sizeof(int16_t)));
      return false;
    }
    MWW_LOGI(TAG, "Preprocessor buffer (%u bytes) allocated", 
             (unsigned int)(buffer_size * sizeof(int16_t)));
  }

  if (this->ring_buffer_ == nullptr) {
    this->ring_buffer_ = audio_samples_allocator.allocate(RING_BUFFER_SIZE);
    if (this->ring_buffer_ == nullptr) {
      MWW_LOGE(TAG, "Could not allocate the ring buffer.");
      return false;
    }
    this->ring_buffer_size_ = RING_BUFFER_SIZE;
    ring_buffer_read_pos_ = 0;
    ring_buffer_write_pos_ = 0;
    ring_buffer_available_ = 0;
    
    MWW_LOGI(TAG, "Ring buffer (%u bytes) allocated", 
             (unsigned int)(RING_BUFFER_SIZE * sizeof(int16_t)));
  }

  MWW_LOGI(TAG, "Buffers allocated successfully");
  return true;
}

void MicroWakeWord::deallocate_buffers_() {
  ExternalRAMAllocator<int16_t> audio_samples_allocator(ExternalRAMAllocator<int16_t>::ALLOW_FAILURE);
  
  if (this->preprocessor_audio_buffer_ != nullptr) {
    free(this->preprocessor_audio_buffer_);
    this->preprocessor_audio_buffer_ = nullptr;
  }

  if (this->ring_buffer_ != nullptr) {
    audio_samples_allocator.deallocate(this->ring_buffer_, this->ring_buffer_size_);
    this->ring_buffer_ = nullptr;
    this->ring_buffer_size_ = 0;
  }
  
  ring_buffer_read_pos_ = 0;
  ring_buffer_write_pos_ = 0;
  ring_buffer_available_ = 0;
}

bool MicroWakeWord::load_models_() {
  MWW_LOGI(TAG, "Loading %u wake word models...", (unsigned int)wake_word_models_.size());
  
  // Setup preprocessor feature generator
  MWW_LOGI(TAG, "Initializing audio frontend (sample rate: %d Hz)...", AUDIO_SAMPLE_FREQUENCY);
  if (!FrontendPopulateState(&this->frontend_config_, &this->frontend_state_, AUDIO_SAMPLE_FREQUENCY)) {
    MWW_LOGE(TAG, "Failed to populate frontend state");
    FrontendFreeStateContents(&this->frontend_state_);
    return false;
  }
  MWW_LOGI(TAG, "Audio frontend initialized");

  // Setup streaming models
  int model_idx = 0;
  for (auto &model : this->wake_word_models_) {
    model_idx++;
    MWW_LOGI(TAG, "Loading model #%d: '%s'...", model_idx, model->get_wake_word().c_str());
    if (!model->load_model(this->streaming_op_resolver_)) {
      MWW_LOGE(TAG, "Failed to initialize wake word model '%s'", model->get_wake_word().c_str());
      return false;
    }
    model->log_model_config();
    MWW_LOGI(TAG, "Model '%s' loaded successfully", model->get_wake_word().c_str());
  }

  MWW_LOGI(TAG, "All %d models loaded successfully", model_idx);
  return true;
}

void MicroWakeWord::unload_models_() {
  FrontendFreeStateContents(&this->frontend_state_);

  for (auto &model : this->wake_word_models_) {
    model->unload_model();
  }
  
  MWW_LOGI(TAG, "Models unloaded");
}

void MicroWakeWord::update_model_probabilities_() {
  int8_t audio_features[PREPROCESSOR_FEATURE_SIZE];

  if (!this->generate_features_for_window_(audio_features)) {
    return;
  }

  // Increase the counter since the last positive detection
  this->ignore_windows_ = std::min<int16_t>(this->ignore_windows_ + 1, 0);

  for (size_t i = 0; i < this->wake_word_models_.size(); i++) {
    auto &model = this->wake_word_models_[i];
    // Perform inference
    model->perform_streaming_inference(audio_features);
    
    float prob = model->get_sliding_window_average();
    
    // Only log when close to threshold (indicates potential wake word)
    if (prob > 0.30f) {
      MWW_LOGI(TAG, "Model '%s': probability %.3f (threshold: %.3f)", 
               model->get_wake_word().c_str(), prob, model->get_probability_cutoff());
    }
  }
}

bool MicroWakeWord::detect_wake_words_() {
  // Verify we have processed samples since the last positive detection
  if (this->ignore_windows_ < 0) {
    return false;
  }

  for (size_t i = 0; i < this->wake_word_models_.size(); i++) {
    auto &model = this->wake_word_models_[i];
    if (model->determine_detected()) {
      this->detected_wake_word_ = model->get_wake_word();
      float prob = model->get_sliding_window_average();
      MWW_LOGI(TAG, "Model '%s' detected! (probability: %.3f, threshold: %.3f)", 
               this->detected_wake_word_.c_str(), prob, model->get_probability_cutoff());
      return true;
    }
  }

  return false;
}

bool MicroWakeWord::generate_features_for_window_(int8_t features[PREPROCESSOR_FEATURE_SIZE]) {
  // ESPHome implementation - directly ported for compatibility
  if (!this->has_enough_samples_()) {
    return false;
  }

  size_t samples_read = this->read_from_ring_buffer_(this->preprocessor_audio_buffer_, this->new_samples_to_get_());

  if (samples_read < this->new_samples_to_get_()) {
    MWW_LOGW(TAG, "Partial read of data: got %u samples, needed %u", 
             (unsigned)samples_read, (unsigned)this->new_samples_to_get_());
    return false;
  }

  // Google Audio Frontend Processing
  size_t num_samples_read = 0;
  struct FrontendOutput frontend_output =
      FrontendProcessSamples(&this->frontend_state_, this->preprocessor_audio_buffer_, this->new_samples_to_get_(),
                             &num_samples_read);

  // Feature Scaling (exact copy from ESPHome)
  // These scaling values match the TFLite audio frontend int8 output.
  for (size_t i = 0; i < frontend_output.size; ++i) {
    // The feature pipeline outputs 16-bit signed integers in roughly a 0 to 670
    // range. In training, these are then arbitrarily divided by 25.6 to get
    // float values in the rough range of 0.0 to 26.0. This scaling is performed
    // for historical reasons, to match up with the output of other feature
    // generators.
    // The process is then further complicated when we quantize the model. This
    // means we have to scale the 0.0 to 26.0 real values to the -128 (INT8_MIN)
    // to 127 (INT8_MAX) signed integer numbers.
    // All this means that to get matching values from our integer feature
    // output into the tensor input, we have to perform:
    // input = (((feature / 25.6) / 26.0) * 256) - 128
    // To simplify this and perform it in 32-bit integer math, we rearrange to:
    // input = (feature * 256) / (25.6 * 26.0) - 128
    constexpr int32_t value_scale = 256;
    constexpr int32_t value_div = 666;  // 666 = 25.6 * 26.0 after rounding
    int32_t value = ((frontend_output.values[i] * value_scale) + (value_div / 2)) / value_div;

    value += INT8_MIN;  // Adds a -128; i.e., subtracts 128
    features[i] = static_cast<int8_t>(std::clamp<int32_t>(value, INT8_MIN, INT8_MAX));
  }

  return true;
}

void MicroWakeWord::reset_states_() {
  MWW_LOGI(TAG, "Resetting buffers and probabilities");
  
  ring_buffer_read_pos_ = 0;
  ring_buffer_write_pos_ = 0;
  ring_buffer_available_ = 0;
  
  this->ignore_windows_ = -MIN_SLICES_BEFORE_DETECTION;
  
  for (auto &model : this->wake_word_models_) {
    model->reset_probabilities();
  }
  
  wake_word_pcm_.clear();
  detected_ = false;
  detected_wake_word_.clear();
}

bool MicroWakeWord::register_streaming_ops_(tflite::MicroMutableOpResolver<20> &op_resolver) {
  if (op_resolver.AddCallOnce() != kTfLiteOk)
    return false;
  if (op_resolver.AddVarHandle() != kTfLiteOk)
    return false;
  if (op_resolver.AddReshape() != kTfLiteOk)
    return false;
  if (op_resolver.AddReadVariable() != kTfLiteOk)
    return false;
  if (op_resolver.AddStridedSlice() != kTfLiteOk)
    return false;
  if (op_resolver.AddConcatenation() != kTfLiteOk)
    return false;
  if (op_resolver.AddAssignVariable() != kTfLiteOk)
    return false;
  if (op_resolver.AddConv2D() != kTfLiteOk)
    return false;
  if (op_resolver.AddMul() != kTfLiteOk)
    return false;
  if (op_resolver.AddAdd() != kTfLiteOk)
    return false;
  if (op_resolver.AddMean() != kTfLiteOk)
    return false;
  if (op_resolver.AddFullyConnected() != kTfLiteOk)
    return false;
  if (op_resolver.AddLogistic() != kTfLiteOk)
    return false;
  if (op_resolver.AddQuantize() != kTfLiteOk)
    return false;
  if (op_resolver.AddDepthwiseConv2D() != kTfLiteOk)
    return false;
  if (op_resolver.AddAveragePool2D() != kTfLiteOk)
    return false;
  if (op_resolver.AddMaxPool2D() != kTfLiteOk)
    return false;
  if (op_resolver.AddPad() != kTfLiteOk)
    return false;
  if (op_resolver.AddPack() != kTfLiteOk)
    return false;
  if (op_resolver.AddSplitV() != kTfLiteOk)
    return false;

  MWW_LOGI(TAG, "Successfully registered all TFLite streaming operations");
  return true;
}

}  // namespace micro_wake_word

