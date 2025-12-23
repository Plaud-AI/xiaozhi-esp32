/**
 * @file micro_wake_word.h
 * @brief Micro Wake Word - Lightweight wake word detection using TFLite Micro
 * 
 * This is a platform-independent implementation of wake word detection using:
 * - TensorFlow Lite Micro for neural network inference
 * - Google Audio Frontend for feature extraction (MFCC/spectrogram)
 * 
 * The implementation is based on ESPHome's micro_wake_word component.
 * 
 * Dependencies:
 * - TensorFlow Lite Micro
 * - Google Audio Frontend (esp_micro_speech_features or similar)
 * 
 * Usage:
 * 1. Create MicroWakeWord instance
 * 2. Call Initialize()
 * 3. Add wake word models with add_wake_word_model()
 * 4. Set detection callback with OnWakeWordDetected()
 * 5. Call Start() to begin detection
 * 6. Feed audio samples with Feed()
 * 7. Call Stop() when done
 * 
 * @see example_usage.cc for detailed examples
 */

#pragma once

#include "streaming_model.h"
#include "preprocessor_settings.h"
#include "platform_compat.h"

// Google Audio Frontend headers
#include "frontend.h"
#include "frontend_util.h"

#include <tensorflow/lite/core/c/common.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>

#include <vector>
#include <deque>
#include <memory>
#include <functional>
#include <string>

namespace micro_wake_word {

/// State machine for wake word detection
enum class State {
  IDLE,                   ///< Not running
  DETECTING_WAKE_WORD,    ///< Actively detecting
  DETECTED,               ///< Wake word detected
};

/// Minimum slices to process before accepting detection (prevents false positives at startup)
static const uint8_t MIN_SLICES_BEFORE_DETECTION = 100;

/**
 * @brief Main class for Micro Wake Word detection
 * 
 * This class provides a complete wake word detection pipeline:
 * 1. Audio buffering with ring buffer
 * 2. Feature extraction using Google Audio Frontend
 * 3. Streaming neural network inference with TFLite Micro
 * 4. Probability smoothing with sliding window
 * 5. Detection callback mechanism
 */
class MicroWakeWord {
 public:
  MicroWakeWord();
  virtual ~MicroWakeWord();

  /**
   * @brief Initialize the wake word detector
   * @return True if initialization successful
   */
  bool Initialize();

  /**
   * @brief Feed audio samples for processing
   * @param data Audio samples (16kHz, 16-bit signed PCM, mono)
   * 
   * Call this method continuously with new audio data.
   * Recommended chunk size: 480 samples (30ms at 16kHz)
   */
  void Feed(const std::vector<int16_t> &data);

  /**
   * @brief Set callback for wake word detection
   * @param callback Function to call when wake word is detected
   */
  void OnWakeWordDetected(std::function<void(const std::string &)> callback);

  /// Start wake word detection
  void Start();

  /// Stop wake word detection
  void Stop();

  /**
   * @brief Get recommended feed size
   * @return Number of samples to feed at once (480 = 30ms at 16kHz)
   */
  size_t GetFeedSize() const;

  /// Get the last detected wake word name
  const std::string &GetLastDetectedWakeWord() const;

  /// Check if wake word was detected
  bool WasDetected() const { return detected_; }

  /// Get current state
  State GetState() const { return state_; }

  /// Set features step size in milliseconds (default: 10ms)
  void set_features_step_size(uint8_t step_size) { this->features_step_size_ = step_size; }

  /**
   * @brief Add a wake word model
   * @param model_start Pointer to TFLite model data (must remain valid)
   * @param probability_cutoff Detection threshold (0.0-1.0, higher = stricter)
   * @param sliding_window_average_size Number of predictions to average
   * @param wake_word Name of the wake word
   * @param tensor_arena_size Size of tensor arena in bytes
   */
  void add_wake_word_model(const uint8_t *model_start, float probability_cutoff,
                           size_t sliding_window_average_size, const std::string &wake_word,
                           size_t tensor_arena_size);

  // ==========================================================================
  // Optional: Wake word audio recording (for voice recognition, speaker ID)
  // ==========================================================================

  /// Get the recorded PCM audio around wake word detection
  const std::vector<int16_t>& GetWakeWordPCM() const { return wake_word_pcm_; }

  /// Clear recorded wake word audio
  void ClearWakeWordPCM() { wake_word_pcm_.clear(); }

 protected:
  State state_{State::IDLE};

  std::vector<std::unique_ptr<WakeWordModel>> wake_word_models_;

  tflite::MicroMutableOpResolver<20> streaming_op_resolver_;
  bool ops_registered_{false};

  // Audio frontend for feature generation
  struct FrontendConfig frontend_config_;
  struct FrontendState frontend_state_;

  // Ignore first N slices to prevent false positives at startup
  int16_t ignore_windows_{-MIN_SLICES_BEFORE_DETECTION};

  uint8_t features_step_size_{10};  // Default 10ms step

  // Ring buffer for audio samples
  int16_t *ring_buffer_{nullptr};
  size_t ring_buffer_size_{0};
  size_t ring_buffer_write_pos_{0};
  size_t ring_buffer_read_pos_{0};
  size_t ring_buffer_available_{0};

  // Buffer for feeding audio frontend
  int16_t *preprocessor_audio_buffer_{nullptr};

  bool detected_{false};
  std::string detected_wake_word_{""};
  std::function<void(const std::string &)> detection_callback_;

  // Wake word audio recording
  std::vector<int16_t> wake_word_pcm_;

  void set_state_(State state);

  /// Check if ring buffer has enough samples for feature generation
  bool has_enough_samples_();

  /// Read samples from ring buffer
  size_t read_from_ring_buffer_(int16_t *buffer, size_t samples);

  /// Write samples to ring buffer
  size_t write_to_ring_buffer_(const int16_t *buffer, size_t samples);

  /// Allocate memory buffers
  bool allocate_buffers_();

  /// Free memory buffers
  void deallocate_buffers_();

  /// Load all wake word models
  bool load_models_();

  /// Unload all models and free resources
  void unload_models_();

  /// Run inference on all models with new features
  void update_model_probabilities_();

  /// Check if any wake word is detected
  bool detect_wake_words_();

  /// Generate features from audio samples
  bool generate_features_for_window_(int8_t features[PREPROCESSOR_FEATURE_SIZE]);

  /// Reset all internal states
  void reset_states_();

  /// Register TFLite operations needed for streaming models
  bool register_streaming_ops_(tflite::MicroMutableOpResolver<20> &op_resolver);

  /// Calculate samples needed for one feature window
  inline uint16_t new_samples_to_get_() { 
    return (this->features_step_size_ * (AUDIO_SAMPLE_FREQUENCY / 1000)); 
  }
};

}  // namespace micro_wake_word

