#pragma once

#include "../wake_word.h"
#include "streaming_model.h"
#include "preprocessor_settings.h"
#include "helpers.h"

#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"
#include <tensorflow/lite/core/c/common.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>

#include <vector>
#include <deque>
#include <memory>
#include <functional>
#include <string>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace micro_wake_word {

enum class State {
  IDLE,
  DETECTING_WAKE_WORD,
  DETECTED,
};

// The number of audio slices to process before accepting a positive detection
// ⚠️ MUST match ESPHome: allows detection after sufficient samples processed
static const uint8_t MIN_SLICES_BEFORE_DETECTION = 100;

class MicroWakeWord : public WakeWord {
 public:
  MicroWakeWord();
  ~MicroWakeWord() override;

  // WakeWord interface implementation
  bool Initialize(AudioCodec *codec, srmodel_list_t *models_list) override;
  void Feed(const std::vector<int16_t> &data) override;
  void OnWakeWordDetected(std::function<void(const std::string &)> callback) override;
  void Start() override;
  void Stop() override;
  size_t GetFeedSize() override;
  void EncodeWakeWordData() override;
  bool GetWakeWordOpus(std::vector<uint8_t> &opus) override;
  const std::string &GetLastDetectedWakeWord() const override;

  // Configuration methods
  void set_features_step_size(uint8_t step_size) { this->features_step_size_ = step_size; }

  void add_wake_word_model(const uint8_t *model_start, float probability_cutoff,
                           size_t sliding_window_average_size, const std::string &wake_word,
                           size_t tensor_arena_size, const std::string &model_id = "",
                           bool always_enabled = false, bool initial_enabled = true);

  // Runtime model control methods
  /// @brief Get list of all registered model IDs
  std::vector<std::string> get_model_ids() const;
  
  /// @brief Get model info (id, wake_word, enabled, always_enabled)
  struct ModelInfo {
    std::string model_id;
    std::string wake_word;
    bool enabled;
    bool always_enabled;
    bool loaded;
  };
  std::vector<ModelInfo> get_models_info() const;
  
  /// @brief Enable a model by ID
  /// @return true if successful, false if model not found or is always_enabled
  bool enable_model(const std::string &model_id);
  
  /// @brief Disable a model by ID
  /// @return true if successful, false if model not found or is always_enabled
  bool disable_model(const std::string &model_id);
  
  /// @brief Check if a model is enabled
  bool is_model_enabled(const std::string &model_id) const;
  
  /// @brief Get count of enabled models
  size_t get_enabled_model_count() const;
  
  /// @brief Save model enabled states to NVS
  /// @return true if successful
  bool save_model_states_to_nvs();
  
  /// @brief Load model enabled states from NVS
  /// @return true if successful (states were loaded)
  bool load_model_states_from_nvs();

  // ──────────────────────────────────────────────────────────────────
  // Dynamic model APIs (runtime-loaded from SPIFFS, buffer owned by caller)
  // ──────────────────────────────────────────────────────────────────

  /// 注册并立即加载一个动态模型（由调用方持有 model_start buffer 的生命周期）。
  /// 加载失败会回滚（pop_back），buffer 不会被本类释放，由调用方负责。
  /// @param initial_enabled 加载后的启用状态（默认 true）
  /// @return true 成功；false model_id 已存在 / Initialize 未跑 / load_model 失败
  bool AddDynamicModel(const uint8_t *model_start, size_t model_size,
                       const std::string &model_id, const std::string &wake_word,
                       float probability_cutoff, size_t sliding_window_size,
                       size_t tensor_arena_size, bool initial_enabled = true);

  /// 卸载并从注册表移除一个动态模型；调用方负责释放对应 buffer。
  /// @return true 移除成功；false 未找到或是 always_enabled（禁止移除）
  bool RemoveDynamicModel(const std::string &model_id);

  /// 把所有 label（=wake_word）匹配的模型 disable，跳过 always_enabled 和 except_model_id。
  /// 匹配前通过 NormalizeLabel 做忽略大小写 / 压缩空白处理。
  /// @return 被禁用的模型数量
  size_t DisableModelsByLabel(const std::string &label,
                              const std::string &except_model_id = "");

  /// 归一化 label：转小写 + 压缩连续空白为单空格 + 去前后空白。
  static std::string NormalizeLabel(const std::string &s);

 protected:
  AudioCodec *codec_{nullptr};
  State state_{State::IDLE};

  std::vector<std::unique_ptr<WakeWordModel>> wake_word_models_;

  // 保护 wake_word_models_ 的并发访问。recursive 以便 enable/disable 内可调用
  // save_model_states_to_nvs 等其他加锁方法。Feed 推理路径和 AddDynamicModel/
  // Remove/Disable 系列运行在不同 FreeRTOS task。
  mutable std::recursive_mutex models_mutex_;

  tflite::MicroMutableOpResolver<20> streaming_op_resolver_;
  bool ops_registered_{false};  // Flag to prevent duplicate registration

  // Audio frontend handles generating spectrogram features
  struct FrontendConfig frontend_config_;
  struct FrontendState frontend_state_;

  // When the wake word detection first starts, we ignore this many audio
  // feature slices before accepting a positive detection
  int16_t ignore_windows_{-MIN_SLICES_BEFORE_DETECTION};

  uint8_t features_step_size_{10};  // Default 10ms step (must match model training!)

  // Ring buffer for audio samples (allocated from PSRAM)
  int16_t *ring_buffer_{nullptr};
  size_t ring_buffer_size_{0};
  size_t ring_buffer_write_pos_{0};
  size_t ring_buffer_read_pos_{0};
  size_t ring_buffer_available_{0};

  // Stores audio to be fed into the audio frontend for generating features.
  int16_t *preprocessor_audio_buffer_{nullptr};

  bool detected_{false};
  std::string detected_wake_word_{""};
  std::function<void(const std::string &)> detection_callback_;

  // Wake word recording for OPUS encoding
  std::vector<int16_t> wake_word_pcm_;
  std::deque<std::vector<uint8_t>> wake_word_opus_;

  // Async encoding task members
  std::mutex wake_word_mutex_;
  std::condition_variable wake_word_cv_;
  TaskHandle_t wake_word_encode_task_ = nullptr;
  StackType_t* wake_word_encode_task_stack_ = nullptr;
  StaticTask_t* wake_word_encode_task_buffer_ = nullptr;
  std::atomic<bool> encode_task_done_{true};

  void set_state_(State state);

  /// @brief Tests if there are enough samples in the ring buffer to generate new features.
  /// @return True if enough samples, false otherwise.
  bool has_enough_samples_();

  /// @brief Reads samples from ring buffer to process
  size_t read_from_ring_buffer_(int16_t *buffer, size_t samples);

  /// @brief Writes samples to ring buffer
  size_t write_to_ring_buffer_(const int16_t *buffer, size_t samples);

  /// @brief Allocates memory for preprocessor_audio_buffer_ and ring_buffer_
  /// @return True if successful, false otherwise
  bool allocate_buffers_();

  /// @brief Frees memory allocated for preprocessor_audio_buffer_
  void deallocate_buffers_();

  /// @brief Loads streaming models and prepares the feature generation frontend
  /// @return True if successful, false otherwise
  bool load_models_();

  /// @brief Deletes each model's TFLite interpreters and frees tensor arena memory.
  /// Frees memory used by the feature generation frontend.
  void unload_models_();

  /** Performs inference with each configured model
   *
   * If enough audio samples are available, it will generate one slice of new
   * features. It then loops through and performs inference with each of the
   * loaded models.
   */
  void update_model_probabilities_();

  /** Checks every model's recent probabilities to determine if the wake word has been predicted
   *
   * Verifies the models have processed enough new samples for accurate predictions.
   * Sets detected_wake_word_ to the wake word, if one is detected.
   * @return True if a wake word is predicted, false otherwise
   */
  bool detect_wake_words_();

  /** Generates features for a window of audio samples
   *
   * Reads samples from the ring buffer and feeds them into the preprocessor frontend.
   * Adapted from TFLite microspeech frontend.
   * @param features int8_t array to store the audio features
   * @return True if successful, false otherwise.
   */
  bool generate_features_for_window_(int8_t features[PREPROCESSOR_FEATURE_SIZE]);

  /// @brief Resets the ring buffer, ignore_windows_, and sliding window probabilities
  void reset_states_();

  /// @brief Returns true if successfully registered the streaming model's TensorFlow operations
  bool register_streaming_ops_(tflite::MicroMutableOpResolver<20> &op_resolver);

  inline uint16_t new_samples_to_get_() { return (this->features_step_size_ * (AUDIO_SAMPLE_FREQUENCY / 1000)); }
};

}  // namespace micro_wake_word

