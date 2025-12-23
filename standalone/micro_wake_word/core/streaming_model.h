/**
 * @file streaming_model.h
 * @brief TensorFlow Lite Micro streaming model wrapper for wake word detection
 * 
 * This file provides classes for loading and running TFLite streaming models
 * that perform incremental inference on audio features.
 * 
 * Dependencies:
 * - TensorFlow Lite Micro
 */

#pragma once

#include "preprocessor_settings.h"
#include "platform_compat.h"

#include <tensorflow/lite/core/c/common.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>

#include <string>
#include <vector>
#include <memory>

namespace micro_wake_word {

/// Size of variable arena for streaming state
static const uint32_t STREAMING_MODEL_VARIABLE_ARENA_SIZE = 1024;

/**
 * @brief Base class for streaming TFLite models
 * 
 * This class handles:
 * - Loading TFLite models and setting up interpreters
 * - Running streaming inference with feature accumulation
 * - Tracking probability history with sliding window
 */
class StreamingModel {
 public:
  virtual ~StreamingModel() = default;
  
  /// Log model configuration (platform-specific)
  virtual void log_model_config() = 0;
  
  /// Determine if the model has detected its target
  virtual bool determine_detected() = 0;

  /**
   * @brief Perform streaming inference on new audio features
   * @param features Array of int8 features (size: PREPROCESSOR_FEATURE_SIZE)
   * @return True if inference was successful
   */
  bool perform_streaming_inference(const int8_t features[PREPROCESSOR_FEATURE_SIZE]);

  /// Reset all probabilities in the sliding window to 0
  void reset_probabilities();

  /**
   * @brief Load the TFLite model and allocate tensors
   * @param op_resolver MicroMutableOpResolver with required operations registered
   * @return True if successful
   */
  bool load_model(tflite::MicroMutableOpResolver<20> &op_resolver);

  /// Unload model and free allocated memory
  void unload_model();

  /// Get the probability cutoff threshold
  float get_probability_cutoff() const { return probability_cutoff_; }

  /// Get the current sliding window average probability
  float get_sliding_window_average() const;

 protected:
  uint8_t current_stride_step_{0};

  float probability_cutoff_;
  size_t sliding_window_size_;
  size_t last_n_index_{0};
  size_t tensor_arena_size_;
  std::vector<uint8_t> recent_streaming_probabilities_;

  const uint8_t *model_start_;
  uint8_t *tensor_arena_{nullptr};
  uint8_t *var_arena_{nullptr};
  std::unique_ptr<tflite::MicroInterpreter> interpreter_;
  tflite::MicroResourceVariables *mrv_{nullptr};
  tflite::MicroAllocator *ma_{nullptr};
};

/**
 * @brief Wake word detection model
 * 
 * Uses sliding window average to determine if wake word is detected.
 */
class WakeWordModel final : public StreamingModel {
 public:
  WakeWordModel(const uint8_t *model_start, float probability_cutoff, 
                size_t sliding_window_average_size, const std::string &wake_word,
                size_t tensor_arena_size);

  void log_model_config() override;

  /**
   * @brief Check if wake word is detected
   * @return True if sliding window average exceeds probability cutoff
   */
  bool determine_detected() override;

  const std::string &get_wake_word() const { return this->wake_word_; }

 protected:
  std::string wake_word_;
};

/**
 * @brief Voice Activity Detection (VAD) model
 * 
 * Uses max probability in sliding window for detection.
 */
class VADModel final : public StreamingModel {
 public:
  VADModel(const uint8_t *model_start, float probability_cutoff, 
           size_t sliding_window_size, size_t tensor_arena_size);

  void log_model_config() override;

  /**
   * @brief Check if voice activity is detected
   * @return True if max probability exceeds cutoff
   */
  bool determine_detected() override;
};

}  // namespace micro_wake_word

