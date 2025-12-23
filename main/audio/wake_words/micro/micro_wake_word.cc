#include "micro_wake_word.h"

#include <esp_log.h>
// ✅ Using ESPMicroSpeechFeatures library (v1.1.0) - same as ESPHome
#include "frontend.h"
#include "frontend_util.h"
#include <opus_encoder.h>
#include <nvs_flash.h>
#include <cmath>
#include <algorithm>

// OPUS frame duration (defined in audio_service.h)
#ifndef OPUS_FRAME_DURATION_MS
#define OPUS_FRAME_DURATION_MS 60
#endif

// NVS namespace and key for wake word model states
#define MWW_NVS_NAMESPACE "micro_ww"
#define MWW_NVS_KEY "model_states"

namespace micro_wake_word {

static const char *const TAG = "MicroWakeWord";

// Note: RING_BUFFER_SIZE is now defined in preprocessor_settings.h (ESPHome-aligned: 120ms = 1920 samples)
static const size_t SAMPLE_RATE_HZ = AUDIO_SAMPLE_FREQUENCY;  // 16 kHz

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
  ESP_LOGI(TAG, "MicroWakeWord constructor");
}

MicroWakeWord::~MicroWakeWord() {
  deallocate_buffers_();
  unload_models_();
}

bool MicroWakeWord::Initialize(AudioCodec *codec, srmodel_list_t *models_list) {
  codec_ = codec;
  
  ESP_LOGI(TAG, "Initializing MicroWakeWord...");

  // Only register operations once
  if (!ops_registered_) {
    ESP_LOGI(TAG, "Registering TFLite streaming operations...");
    if (!this->register_streaming_ops_(this->streaming_op_resolver_)) {
      ESP_LOGE(TAG, "Failed to register streaming operations");
      return false;
    }
    ops_registered_ = true;
    ESP_LOGI(TAG, "✅ Successfully registered all TFLite streaming operations");
  } else {
    ESP_LOGI(TAG, "TFLite operations already registered, skipping...");
  }

  ESP_LOGI(TAG, "Micro Wake Word initialized");

  // ========================================================================
  // Configure audio frontend - MUST match the parameters used during model training!
  // These are the standard microWakeWord training parameters from ESPHome official
  // All parameters are now aligned with ESPHome's preprocessor_settings.h
  // ========================================================================
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
  
  ESP_LOGI(TAG, "🎛️  Frontend Configuration (ESPHome-aligned):");
  ESP_LOGI(TAG, "   - Filterbank: %.1f - %.1f Hz, %d channels",
           FILTERBANK_LOWER_BAND_LIMIT, FILTERBANK_UPPER_BAND_LIMIT, PREPROCESSOR_FEATURE_SIZE);
  ESP_LOGI(TAG, "   - Noise Reduction: min_signal=%.2f (ESPHome official)", 
           NOISE_REDUCTION_MIN_SIGNAL_REMAINING);
  ESP_LOGI(TAG, "   - PCAN Gain Control: strength=%.2f, offset=%.1f", 
           PCAN_GAIN_CONTROL_STRENGTH, PCAN_GAIN_CONTROL_OFFSET);
  ESP_LOGI(TAG, "   - Log Scale: shift=%d", LOG_SCALE_SCALE_SHIFT);

  return true;
}

void MicroWakeWord::Feed(const std::vector<int16_t> &data) {
  static uint32_t feed_count = 0;
  feed_count++;
  
  if (state_ != State::DETECTING_WAKE_WORD) {
    if (feed_count % 100 == 0) {
      ESP_LOGI(TAG, "Feed #%lu: Not in detecting state (current state: %d)", feed_count, (int)state_);
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
    
    ESP_LOGD(TAG, "Feed #%lu: Received %u samples, ring buffer: %u, avg_level: %d, max: %d", 
             feed_count, (unsigned)data.size(), (unsigned)ring_buffer_available_, avg, max_val);
  }

  // Store data for wake word recording
  wake_word_pcm_.insert(wake_word_pcm_.end(), data.begin(), data.end());
  // Keep about 2 seconds of data
  while (wake_word_pcm_.size() > SAMPLE_RATE_HZ * 2) {
    wake_word_pcm_.erase(wake_word_pcm_.begin(), 
                         wake_word_pcm_.begin() + (wake_word_pcm_.size() - SAMPLE_RATE_HZ * 2));
  }

  // Write audio data to ring buffer
  size_t written = write_to_ring_buffer_(data.data(), data.size());
  if (written < data.size()) {
    ESP_LOGW(TAG, "Ring buffer partial write: %zu/%zu samples", written, data.size());
  }

  // Process audio and detect wake words
  int process_count = 0;
  while (has_enough_samples_()) {
    process_count++;
    update_model_probabilities_();
    if (detect_wake_words_()) {
      ESP_LOGI(TAG, "🎯 Wake Word '%s' Detected!", detected_wake_word_.c_str());
      detected_ = true;
      set_state_(State::DETECTED);
      if (detection_callback_) {
        ESP_LOGI(TAG, "Calling detection callback...");
        detection_callback_(detected_wake_word_);
      } else {
        ESP_LOGW(TAG, "Detection callback is not set!");
      }
      break;
    }
  }
  
}

void MicroWakeWord::OnWakeWordDetected(std::function<void(const std::string &)> callback) {
  detection_callback_ = callback;
}

void MicroWakeWord::Start() {
  size_t enabled_count = get_enabled_model_count();
  ESP_LOGI(TAG, "🚀 Starting MicroWakeWord detection (ESPHome-aligned)");
  ESP_LOGI(TAG, "  - Wake word models: %u total, %u enabled", 
           (unsigned int)wake_word_models_.size(), (unsigned int)enabled_count);
  ESP_LOGI(TAG, "  - Sample rate: %u Hz", (unsigned int)AUDIO_SAMPLE_FREQUENCY);
  ESP_LOGI(TAG, "  - Feature duration: %d ms", FEATURE_DURATION_MS);
  ESP_LOGI(TAG, "  - Ring buffer: %u ms (%u samples, %.1f KB)", 
           RING_BUFFER_DURATION_MS, (unsigned int)RING_BUFFER_SIZE, 
           (RING_BUFFER_SIZE * sizeof(int16_t)) / 1024.0f);

  if (state_ != State::IDLE) {
    ESP_LOGW(TAG, "Wake word is already running (state: %d)", (int)state_);
    return;
  }

  if (wake_word_models_.empty()) {
    ESP_LOGE(TAG, "❌ No wake word models configured!");
    return;
  }
  
  if (enabled_count == 0) {
    ESP_LOGW(TAG, "⚠️ No wake word models enabled! Detection will not work.");
  }

  ESP_LOGI(TAG, "Loading models and allocating buffers...");
  if (!load_models_()) {
    ESP_LOGE(TAG, "❌ Failed to load models");
    return;
  }
  
  if (!allocate_buffers_()) {
    ESP_LOGE(TAG, "❌ Failed to allocate buffers");
    return;
  }

  reset_states_();
  set_state_(State::DETECTING_WAKE_WORD);
  ESP_LOGI(TAG, "✅ MicroWakeWord detection started successfully");
}

void MicroWakeWord::Stop() {
  ESP_LOGI(TAG, "Stopping MicroWakeWord detection");

  if (state_ == State::IDLE) {
    ESP_LOGW(TAG, "Wake word is already stopped");
    return;
  }

  set_state_(State::IDLE);
  unload_models_();
  deallocate_buffers_();
}

size_t MicroWakeWord::GetFeedSize() {
  // Return chunk size for feeding audio data (480 samples = 30ms at 16kHz)
  return 480;
}

void MicroWakeWord::EncodeWakeWordData() {
  ESP_LOGI(TAG, "Encoding wake word data to OPUS (Async)");
  
  if (wake_word_pcm_.empty()) {
    ESP_LOGW(TAG, "No wake word PCM data to encode");
    return;
  }

  const size_t stack_size = 4096 * 7;
  if (wake_word_encode_task_stack_ == nullptr) {
    wake_word_encode_task_stack_ = (StackType_t*)heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM);
  }
  if (wake_word_encode_task_buffer_ == nullptr) {
    wake_word_encode_task_buffer_ = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }

  if (!wake_word_encode_task_stack_ || !wake_word_encode_task_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate task resources for encoding");
    return;
  }

  {
      std::lock_guard<std::mutex> lock(wake_word_mutex_);
      wake_word_opus_.clear();
  }

  wake_word_encode_task_ = xTaskCreateStatic([](void* arg) {
    auto* this_ = static_cast<MicroWakeWord*>(arg);
    
    std::vector<int16_t> pcm_data;
    pcm_data = std::move(this_->wake_word_pcm_);
    
    auto start_time = esp_timer_get_time();
    auto encoder = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
    
    int packets = 0;
    if (encoder) {
        encoder->SetComplexity(0);
        encoder->Encode(std::move(pcm_data), [this_, &packets](std::vector<uint8_t>&& opus) {
            std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
            this_->wake_word_opus_.emplace_back(std::move(opus));
            this_->wake_word_cv_.notify_all();
            packets++;
        });
    }
    
    // Push empty packet as sentinel
    {
        std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
        this_->wake_word_opus_.emplace_back(std::vector<uint8_t>());
        this_->wake_word_cv_.notify_all();
    }
    
    auto end_time = esp_timer_get_time();
    ESP_LOGI(TAG, "Encoded %d wake word packets in %ld ms", packets, (long)((end_time - start_time) / 1000));

    vTaskDelete(NULL);
  }, "mw_encode", stack_size, this, 2, wake_word_encode_task_stack_, wake_word_encode_task_buffer_);
}

bool MicroWakeWord::GetWakeWordOpus(std::vector<uint8_t> &opus) {
  std::unique_lock<std::mutex> lock(wake_word_mutex_);
  wake_word_cv_.wait(lock, [this]() {
    return !wake_word_opus_.empty();
  });
  
  opus = std::move(wake_word_opus_.front());
  wake_word_opus_.pop_front();
  
  if (opus.empty()) {
      return false;
  }
  return true;
}

const std::string &MicroWakeWord::GetLastDetectedWakeWord() const {
  return detected_wake_word_;
}

void MicroWakeWord::add_wake_word_model(const uint8_t *model_start, float probability_cutoff,
                                        size_t sliding_window_average_size, const std::string &wake_word,
                                        size_t tensor_arena_size, const std::string &model_id,
                                        bool always_enabled, bool initial_enabled) {
  auto model = std::make_unique<WakeWordModel>(model_start, probability_cutoff, sliding_window_average_size, 
                                                wake_word, tensor_arena_size, model_id);
  model->set_always_enabled(always_enabled);
  model->set_enabled(always_enabled ? true : initial_enabled);
  
  ESP_LOGI(TAG, "➕ Added wake word model: '%s' (id: %s, threshold: %.3f, window: %u, arena: %u bytes)", 
           wake_word.c_str(), model->get_model_id().c_str(), probability_cutoff, 
           (unsigned int)sliding_window_average_size, (unsigned int)tensor_arena_size);
  ESP_LOGI(TAG, "   - enabled: %s, always_enabled: %s", 
           model->is_enabled() ? "true" : "false",
           model->is_always_enabled() ? "true" : "false");
  
  this->wake_word_models_.push_back(std::move(model));
}

std::vector<std::string> MicroWakeWord::get_model_ids() const {
  std::vector<std::string> ids;
  for (const auto &model : this->wake_word_models_) {
    auto *ww_model = static_cast<WakeWordModel *>(model.get());
    ids.push_back(ww_model->get_model_id());
  }
  return ids;
}

std::vector<MicroWakeWord::ModelInfo> MicroWakeWord::get_models_info() const {
  std::vector<ModelInfo> infos;
  for (const auto &model : this->wake_word_models_) {
    auto *ww_model = static_cast<WakeWordModel *>(model.get());
    ModelInfo info;
    info.model_id = ww_model->get_model_id();
    info.wake_word = ww_model->get_wake_word();
    info.enabled = ww_model->is_enabled();
    info.always_enabled = ww_model->is_always_enabled();
    info.loaded = model->is_loaded();  // 真正检查模型是否加载
    infos.push_back(info);
  }
  return infos;
}

bool MicroWakeWord::enable_model(const std::string &model_id) {
  for (auto &model : this->wake_word_models_) {
    auto *ww_model = static_cast<WakeWordModel *>(model.get());
    if (ww_model->get_model_id() == model_id) {
      if (ww_model->is_enabled()) {
        ESP_LOGW(TAG, "Model '%s' is already enabled", model_id.c_str());
        return true;
      }
      
      // 真正的动态加载：如果模型未加载，则加载它
      if (!model->is_loaded()) {
        ESP_LOGI(TAG, "🔄 Dynamically loading model '%s'...", model_id.c_str());
        if (!model->load_model(this->streaming_op_resolver_)) {
          ESP_LOGE(TAG, "❌ Failed to load model '%s'", model_id.c_str());
          return false;
        }
        model->reset_probabilities();
        ESP_LOGI(TAG, "✅ Model '%s' loaded successfully", model_id.c_str());
      }
      
      ww_model->set_enabled(true);
      ESP_LOGI(TAG, "✅ Model '%s' enabled (loaded: %s)", 
               model_id.c_str(), model->is_loaded() ? "yes" : "no");
      
      // 保存状态到 NVS
      save_model_states_to_nvs();
      return true;
    }
  }
  ESP_LOGW(TAG, "Model '%s' not found", model_id.c_str());
  return false;
}

bool MicroWakeWord::disable_model(const std::string &model_id) {
  for (auto &model : this->wake_word_models_) {
    auto *ww_model = static_cast<WakeWordModel *>(model.get());
    if (ww_model->get_model_id() == model_id) {
      if (ww_model->is_always_enabled()) {
        ESP_LOGW(TAG, "Model '%s' is always enabled, cannot disable", model_id.c_str());
        return false;
      }
      if (!ww_model->is_enabled()) {
        ESP_LOGW(TAG, "Model '%s' is already disabled", model_id.c_str());
        return true;
      }
      
      ww_model->set_enabled(false);
      
      // 真正的动态卸载：释放模型占用的 PSRAM
      if (model->is_loaded()) {
        ESP_LOGI(TAG, "🔄 Dynamically unloading model '%s' to free PSRAM...", model_id.c_str());
        model->unload_model();
        ESP_LOGI(TAG, "✅ Model '%s' unloaded, PSRAM freed", model_id.c_str());
      }
      
      ESP_LOGI(TAG, "✅ Model '%s' disabled", model_id.c_str());
      
      // 保存状态到 NVS
      save_model_states_to_nvs();
      return true;
    }
  }
  ESP_LOGW(TAG, "Model '%s' not found", model_id.c_str());
  return false;
}

bool MicroWakeWord::is_model_enabled(const std::string &model_id) const {
  for (const auto &model : this->wake_word_models_) {
    auto *ww_model = static_cast<WakeWordModel *>(model.get());
    if (ww_model->get_model_id() == model_id) {
      return ww_model->is_enabled();
    }
  }
  return false;
}

size_t MicroWakeWord::get_enabled_model_count() const {
  size_t count = 0;
  for (const auto &model : this->wake_word_models_) {
    auto *ww_model = static_cast<WakeWordModel *>(model.get());
    if (ww_model->is_enabled()) {
      count++;
    }
  }
  return count;
}

void MicroWakeWord::set_state_(State state) {
  ESP_LOGD(TAG, "State changed from %s to %s", micro_wake_word_state_to_string(this->state_),
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
      ESP_LOGW(TAG, "Ring buffer overflow, dropping oldest sample");
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

  // ⚠️ CRITICAL: Google Audio Frontend has cache coherency issues with PSRAM
  // Must allocate preprocessor_audio_buffer from internal SRAM to avoid
  // intermittent failures where Frontend outputs all zeros.
  if (this->preprocessor_audio_buffer_ == nullptr) {
    size_t buffer_size = this->new_samples_to_get_() * sizeof(int16_t);
    // Force allocation from internal SRAM using heap_caps_malloc
    this->preprocessor_audio_buffer_ = static_cast<int16_t*>(
        heap_caps_malloc(buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    
    if (this->preprocessor_audio_buffer_ == nullptr) {
      ESP_LOGE(TAG, "❌ Could not allocate preprocessor buffer (%u bytes) from SRAM", 
               (unsigned int)buffer_size);
      return false;
    }
    ESP_LOGI(TAG, "✅ Preprocessor buffer (%u bytes) allocated from internal SRAM (forced)", 
             (unsigned int)buffer_size);
  }

  if (this->ring_buffer_ == nullptr) {
    this->ring_buffer_ = audio_samples_allocator.allocate(RING_BUFFER_SIZE);
    if (this->ring_buffer_ == nullptr) {
      ESP_LOGE(TAG, "Could not allocate the ring buffer.");
      return false;
    }
    this->ring_buffer_size_ = RING_BUFFER_SIZE;
    ring_buffer_read_pos_ = 0;
    ring_buffer_write_pos_ = 0;
    ring_buffer_available_ = 0;
    
    // Check allocation location
    if (esp_ptr_external_ram(this->ring_buffer_)) {
      ESP_LOGI(TAG, "✅ Ring buffer (%u bytes) allocated from PSRAM", (unsigned int)(RING_BUFFER_SIZE * sizeof(int16_t)));
    } else {
      ESP_LOGW(TAG, "⚠️  Ring buffer (%u bytes) allocated from SRAM! This will cause memory issues.", 
               (unsigned int)(RING_BUFFER_SIZE * sizeof(int16_t)));
    }
  }

  ESP_LOGI(TAG, "Buffers allocated successfully");
  return true;
}

void MicroWakeWord::deallocate_buffers_() {
  ExternalRAMAllocator<int16_t> audio_samples_allocator(ExternalRAMAllocator<int16_t>::ALLOW_FAILURE);
  
  if (this->preprocessor_audio_buffer_ != nullptr) {
    // Preprocessor buffer was allocated with heap_caps_malloc, use free()
    free(this->preprocessor_audio_buffer_);
    this->preprocessor_audio_buffer_ = nullptr;
  }

  if (this->ring_buffer_ != nullptr) {
    audio_samples_allocator.deallocate(this->ring_buffer_, this->ring_buffer_size_);
    this->ring_buffer_ = nullptr;
    this->ring_buffer_size_ = 0;
  }

  if (wake_word_encode_task_stack_) {
    heap_caps_free(wake_word_encode_task_stack_);
    wake_word_encode_task_stack_ = nullptr;
  }
  if (wake_word_encode_task_buffer_) {
    heap_caps_free(wake_word_encode_task_buffer_);
    wake_word_encode_task_buffer_ = nullptr;
  }
  
  ring_buffer_read_pos_ = 0;
  ring_buffer_write_pos_ = 0;
  ring_buffer_available_ = 0;
}

bool MicroWakeWord::load_models_() {
  size_t total_models = wake_word_models_.size();
  size_t enabled_models = get_enabled_model_count();
  
  ESP_LOGI(TAG, "🔧 Loading wake word models (only enabled ones for PSRAM efficiency)");
  ESP_LOGI(TAG, "   Total registered: %u, Enabled: %u", 
           (unsigned int)total_models, (unsigned int)enabled_models);
  
  // Setup preprocessor feature generator
  ESP_LOGI(TAG, "Initializing audio frontend (sample rate: %d Hz)...", AUDIO_SAMPLE_FREQUENCY);
  if (!FrontendPopulateState(&this->frontend_config_, &this->frontend_state_, AUDIO_SAMPLE_FREQUENCY)) {
    ESP_LOGE(TAG, "❌ Failed to populate frontend state");
    FrontendFreeStateContents(&this->frontend_state_);
    return false;
  }
  ESP_LOGI(TAG, "✅ Audio frontend initialized");

  // 真正的动态加载：只加载启用的模型，节省 PSRAM
  int model_idx = 0;
  int loaded_count = 0;
  int skipped_count = 0;
  
  for (auto &model : this->wake_word_models_) {
    model_idx++;
    auto *ww_model = static_cast<WakeWordModel *>(model.get());
    const char* always = ww_model->is_always_enabled() ? " (always)" : "";
    
    if (ww_model->is_enabled()) {
      // 只加载启用的模型
      ESP_LOGI(TAG, "📦 Loading model #%d: '%s' [enabled%s]...", 
               model_idx, ww_model->get_wake_word().c_str(), always);
      if (!model->load_model(this->streaming_op_resolver_)) {
        ESP_LOGE(TAG, "❌ Failed to initialize wake word model '%s'", ww_model->get_wake_word().c_str());
        return false;
      }
      model->log_model_config();
      ESP_LOGI(TAG, "✅ Model '%s' loaded successfully", ww_model->get_wake_word().c_str());
      loaded_count++;
    } else {
      // 禁用的模型不加载，节省 PSRAM
      ESP_LOGI(TAG, "⏭️  Skipping model #%d: '%s' [disabled] - PSRAM saved (~26KB)", 
               model_idx, ww_model->get_wake_word().c_str());
      skipped_count++;
    }
  }

  ESP_LOGI(TAG, "═══════════════════════════════════════════════════");
  ESP_LOGI(TAG, "✅ Model loading complete:");
  ESP_LOGI(TAG, "   - Loaded: %d models (~%d KB PSRAM used)", loaded_count, loaded_count * 26);
  ESP_LOGI(TAG, "   - Skipped: %d models (~%d KB PSRAM saved)", skipped_count, skipped_count * 26);
  ESP_LOGI(TAG, "═══════════════════════════════════════════════════");
  return true;
}

void MicroWakeWord::unload_models_() {
  FrontendFreeStateContents(&this->frontend_state_);

  for (auto &model : this->wake_word_models_) {
    model->unload_model();
  }
  
  ESP_LOGI(TAG, "Models unloaded");
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
    auto *ww_model = static_cast<WakeWordModel *>(model.get());
    
    // Skip disabled or unloaded models
    if (!ww_model->is_enabled() || !model->is_loaded()) {
      continue;
    }
    
    // Perform inference
    model->perform_streaming_inference(audio_features);
    
    float prob = model->get_sliding_window_average();
    
    // 只在接近阈值时打印（prob > 0.3 表示可能正在说唤醒词）
    if (prob > 0.30) {
      ESP_LOGI(TAG, "🎯 Model '%s': probability %.3f (threshold: %.3f)", 
               ww_model->get_wake_word().c_str(), prob, model->get_probability_cutoff());
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
    auto *ww_model = static_cast<WakeWordModel *>(model.get());
    
    // Skip disabled or unloaded models
    if (!ww_model->is_enabled() || !model->is_loaded()) {
      continue;
    }
    
    if (model->determine_detected()) {
      this->detected_wake_word_ = ww_model->get_wake_word();
      float prob = model->get_sliding_window_average();
      ESP_LOGI(TAG, "🎉 Model '%s' detected! (probability: %.3f, threshold: %.3f)", 
               this->detected_wake_word_.c_str(), prob, model->get_probability_cutoff());
      return true;
    }
  }

  return false;
}

bool MicroWakeWord::generate_features_for_window_(int8_t features[PREPROCESSOR_FEATURE_SIZE]) {
  // ✅ ESPHome implementation - directly ported for compatibility
  // Ensure we have enough new audio samples in the ring buffer for a full window
  if (!this->has_enough_samples_()) {
    return false;
  }

  size_t samples_read = this->read_from_ring_buffer_(this->preprocessor_audio_buffer_, this->new_samples_to_get_());

  if (samples_read < this->new_samples_to_get_()) {
    ESP_LOGW(TAG, "Partial read of data: got %u samples, needed %u", 
             (unsigned)samples_read, (unsigned)this->new_samples_to_get_());
    return false;
  }


  // ===== ESPHome Frontend Processing =====
  size_t num_samples_read = 0;
  struct FrontendOutput frontend_output =
      FrontendProcessSamples(&this->frontend_state_, this->preprocessor_audio_buffer_, this->new_samples_to_get_(),
                             &num_samples_read);


  // ===== ESPHome Feature Scaling (exact copy) =====
  for (size_t i = 0; i < frontend_output.size; ++i) {
    // These scaling values are set to match the TFLite audio frontend int8 output.
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
  ESP_LOGI(TAG, "Resetting buffers and probabilities");
  
  ring_buffer_read_pos_ = 0;
  ring_buffer_write_pos_ = 0;
  ring_buffer_available_ = 0;
  
  this->ignore_windows_ = -MIN_SLICES_BEFORE_DETECTION;
  
  for (auto &model : this->wake_word_models_) {
    model->reset_probabilities();
  }
  
  wake_word_pcm_.clear();
  {
      std::lock_guard<std::mutex> lock(wake_word_mutex_);
      wake_word_opus_.clear();
  }
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

  ESP_LOGI(TAG, "Successfully registered all TFLite streaming operations");
  return true;
}

bool MicroWakeWord::save_model_states_to_nvs() {
  ESP_LOGI(TAG, "💾 Saving model states to NVS...");
  
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open(MWW_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "❌ Failed to open NVS: %s", esp_err_to_name(err));
    return false;
  }
  
  // 构建简单的状态字符串：model_id:enabled,model_id:enabled,...
  // 只保存非 always_enabled 的模型状态
  std::string states_str;
  int saved_count = 0;
  
  for (const auto &model : this->wake_word_models_) {
    auto *ww_model = static_cast<WakeWordModel *>(model.get());
    
    // 跳过 always_enabled 的模型（它们总是启用）
    if (ww_model->is_always_enabled()) {
      continue;
    }
    
    if (!states_str.empty()) {
      states_str += ",";
    }
    states_str += ww_model->get_model_id();
    states_str += ":";
    states_str += ww_model->is_enabled() ? "1" : "0";
    saved_count++;
  }
  
  err = nvs_set_str(nvs_handle, MWW_NVS_KEY, states_str.c_str());
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "❌ Failed to write to NVS: %s", esp_err_to_name(err));
    nvs_close(nvs_handle);
    return false;
  }
  
  err = nvs_commit(nvs_handle);
  nvs_close(nvs_handle);
  
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "❌ Failed to commit NVS: %s", esp_err_to_name(err));
    return false;
  }
  
  ESP_LOGI(TAG, "✅ Saved %d model states to NVS: %s", saved_count, states_str.c_str());
  return true;
}

bool MicroWakeWord::load_model_states_from_nvs() {
  ESP_LOGI(TAG, "📖 Loading model states from NVS...");
  
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open(MWW_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "⚠️  NVS not found (first boot?): %s", esp_err_to_name(err));
    return false;
  }
  
  // 获取字符串长度
  size_t required_size = 0;
  err = nvs_get_str(nvs_handle, MWW_NVS_KEY, nullptr, &required_size);
  if (err != ESP_OK || required_size == 0) {
    ESP_LOGW(TAG, "⚠️  No saved model states found");
    nvs_close(nvs_handle);
    return false;
  }
  
  // 读取字符串
  char* states_str = (char*)malloc(required_size);
  if (!states_str) {
    ESP_LOGE(TAG, "❌ Failed to allocate memory");
    nvs_close(nvs_handle);
    return false;
  }
  
  err = nvs_get_str(nvs_handle, MWW_NVS_KEY, states_str, &required_size);
  nvs_close(nvs_handle);
  
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "❌ Failed to read from NVS: %s", esp_err_to_name(err));
    free(states_str);
    return false;
  }
  
  ESP_LOGI(TAG, "📜 Loaded states: %s", states_str);
  
  // 解析状态字符串：model_id:enabled,model_id:enabled,...
  std::string str(states_str);
  free(states_str);
  
  int restored_count = 0;
  size_t pos = 0;
  while (pos < str.length()) {
    // 找到下一个逗号或字符串结尾
    size_t comma_pos = str.find(',', pos);
    if (comma_pos == std::string::npos) {
      comma_pos = str.length();
    }
    
    std::string item = str.substr(pos, comma_pos - pos);
    size_t colon_pos = item.find(':');
    
    if (colon_pos != std::string::npos) {
      std::string model_id = item.substr(0, colon_pos);
      bool enabled = (item.substr(colon_pos + 1) == "1");
      
      // 应用状态到模型
      for (auto &model : this->wake_word_models_) {
        auto *ww_model = static_cast<WakeWordModel *>(model.get());
        if (ww_model->get_model_id() == model_id && !ww_model->is_always_enabled()) {
          ww_model->set_enabled(enabled);
          ESP_LOGI(TAG, "   ✅ Restored '%s' -> %s", model_id.c_str(), enabled ? "enabled" : "disabled");
          restored_count++;
          break;
        }
      }
    }
    
    pos = comma_pos + 1;
  }
  
  ESP_LOGI(TAG, "✅ Restored %d model states from NVS", restored_count);
  return true;
}

}  // namespace micro_wake_word

