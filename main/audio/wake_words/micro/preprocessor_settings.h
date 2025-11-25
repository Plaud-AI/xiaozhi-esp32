#pragma once

#include <cstdint>
#include <cstddef>  // For size_t

namespace micro_wake_word {

// ========================================================================
// Settings for controlling the spectrogram feature generation by the preprocessor.
// These must match the settings used when training a particular model.
// All microWakeWord models have been trained with these specific parameters.
// Source: ESPHome official micro_wake_word component
// ========================================================================

// The number of features the audio preprocessor generates per slice
static const uint8_t PREPROCESSOR_FEATURE_SIZE = 40;
// Duration of each slice used as input into the preprocessor
static const uint8_t FEATURE_DURATION_MS = 30;
// Audio sample frequency in hertz
static const uint16_t AUDIO_SAMPLE_FREQUENCY = 16000;

// Ring buffer duration for audio samples (in milliseconds)
// ESPHome uses 120ms for efficient memory usage
static const uint32_t RING_BUFFER_DURATION_MS = 120;
// Calculate ring buffer size in samples
static const size_t RING_BUFFER_SIZE = (AUDIO_SAMPLE_FREQUENCY * RING_BUFFER_DURATION_MS) / 1000;

// Filterbank configuration
static const float FILTERBANK_LOWER_BAND_LIMIT = 125.0;
static const float FILTERBANK_UPPER_BAND_LIMIT = 7500.0;

// Noise reduction configuration
static const uint8_t NOISE_REDUCTION_SMOOTHING_BITS = 10;
static const float NOISE_REDUCTION_EVEN_SMOOTHING = 0.025;
static const float NOISE_REDUCTION_ODD_SMOOTHING = 0.06;
static const float NOISE_REDUCTION_MIN_SIGNAL_REMAINING = 0.05;  // ESPHome official: 0.05 (not 0.40)

// PCAN gain control configuration
static const bool PCAN_GAIN_CONTROL_ENABLE_PCAN = true;
static const float PCAN_GAIN_CONTROL_STRENGTH = 0.95;
static const float PCAN_GAIN_CONTROL_OFFSET = 80.0;
static const uint8_t PCAN_GAIN_CONTROL_GAIN_BITS = 21;

// Log scale configuration
static const bool LOG_SCALE_ENABLE_LOG = true;
static const uint8_t LOG_SCALE_SCALE_SHIFT = 6;

}  // namespace micro_wake_word

