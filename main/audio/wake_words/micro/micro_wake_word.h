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
#include <memory>
#include <functional>
#include <string>

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
  float GetLastDetectedProbability() const { return detected_probability_; }  // 获取最后检测到的概率

  // Configuration methods
  void set_features_step_size(uint8_t step_size) { this->features_step_size_ = step_size; }

  void add_wake_word_model(const uint8_t *model_start, float probability_cutoff,
                           size_t sliding_window_average_size, const std::string &wake_word,
                           size_t tensor_arena_size);

 protected:
  AudioCodec *codec_{nullptr};
  State state_{State::IDLE};

  std::vector<std::unique_ptr<WakeWordModel>> wake_word_models_;

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
  float detected_probability_{0.0f};  // 最后检测到的概率
  std::function<void(const std::string &)> detection_callback_;

  // Wake word recording for OPUS encoding
  std::vector<int16_t> wake_word_pcm_;
  std::vector<uint8_t> wake_word_opus_;

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

