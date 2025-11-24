/**
 * @file example_usage.cc
 * @brief Example of how to use MicroWakeWord in the xiaozhi-esp32 project
 * 
 * This file demonstrates the integration of micro wake word detection into the existing
 * audio processing pipeline.
 */

#include "micro_wake_word.h"
#include "hey_jarvis.h"  // Model data

using namespace micro_wake_word;

// Example 1: Basic initialization and usage
void example_basic_usage() {
    // Create MicroWakeWord instance
    auto micro_ww = std::make_unique<MicroWakeWord>();
    
    // Initialize (codec can be null for micro wake word)
    if (!micro_ww->Initialize(nullptr, nullptr)) {
        // Handle initialization failure
        return;
    }
    
    // Add wake word model
    // hey_jarvis_tflite is the model data array from hey_jarvis.h
    // Parameters: model_data, probability_cutoff, sliding_window_size, wake_word_name, tensor_arena_size
    micro_ww->add_wake_word_model(
        hey_jarvis_tflite,  // Model data
        0.5f,               // Probability cutoff (0.0-1.0, higher = stricter)
        10,                 // Sliding window size for averaging probabilities
        "hey jarvis",       // Wake word name
        30000               // Tensor arena size in bytes
    );
    
    // Set detection callback
    micro_ww->OnWakeWordDetected([](const std::string& wake_word) {
        ESP_LOGI("Example", "Wake word detected: %s", wake_word.c_str());
        // Handle wake word detection
    });
    
    // Start detection
    micro_ww->Start();
    
    // In your audio processing loop, feed audio data:
    // std::vector<int16_t> audio_chunk = ... // Get 480 samples (30ms at 16kHz)
    // micro_ww->Feed(audio_chunk);
    
    // When done:
    micro_ww->Stop();
}

// Example 2: Integration with existing WakeWord interface
class Application {
private:
    std::unique_ptr<WakeWord> wake_word_;
    AudioCodec* codec_;
    
public:
    void InitializeMicroWakeWord() {
        wake_word_ = std::make_unique<MicroWakeWord>();
        
        if (!wake_word_->Initialize(codec_, nullptr)) {
            ESP_LOGE("Application", "Failed to initialize micro wake word");
            return;
        }
        
        // Cast to MicroWakeWord to access specific configuration
        auto* micro_ww = dynamic_cast<MicroWakeWord*>(wake_word_.get());
        if (micro_ww) {
            // Add model with custom parameters
            micro_ww->add_wake_word_model(
                hey_jarvis_tflite,
                0.6f,     // Higher threshold = less false positives
                15,       // Larger window = more stable detection
                "hey jarvis",
                35000     // Larger arena if needed
            );
            
            // Optional: adjust step size (default is 20ms)
            micro_ww->set_features_step_size(20);
        }
        
        // Set callback
        wake_word_->OnWakeWordDetected([this](const std::string& wake_word) {
            ESP_LOGI("Application", "✨ Wake word '%s' detected!", wake_word.c_str());
            this->OnWakeWordDetected(wake_word);
        });
        
        wake_word_->Start();
    }
    
    void OnWakeWordDetected(const std::string& wake_word) {
        // Encode wake word audio
        wake_word_->EncodeWakeWordData();
        
        // Get encoded OPUS data
        std::vector<uint8_t> opus_data;
        if (wake_word_->GetWakeWordOpus(opus_data)) {
            ESP_LOGI("Application", "Got %zu bytes of OPUS data", opus_data.size());
            // Send to server or process further
        }
    }
    
    void AudioProcessingLoop() {
        size_t feed_size = wake_word_->GetFeedSize();  // Returns 480 (30ms)
        std::vector<int16_t> audio_buffer(feed_size);
        
        while (true) {
            // Read audio from codec
            if (ReadAudioFromCodec(audio_buffer.data(), feed_size)) {
                // Feed to wake word detector
                wake_word_->Feed(audio_buffer);
            }
            
            // Other processing...
        }
    }
    
    bool ReadAudioFromCodec(int16_t* buffer, size_t samples) {
        // Implementation depends on your codec
        return false;
    }
};

// Example 3: Multiple wake word models
void example_multiple_models() {
    auto micro_ww = std::make_unique<MicroWakeWord>();
    micro_ww->Initialize(nullptr, nullptr);
    
    // Add first wake word
    micro_ww->add_wake_word_model(
        hey_jarvis_tflite,
        0.5f, 10, "hey jarvis", 30000
    );
    
    // You can add more models if you have them:
    // micro_ww->add_wake_word_model(
    //     hey_alexa_tflite,
    //     0.6f, 12, "hey alexa", 30000
    // );
    
    micro_ww->OnWakeWordDetected([](const std::string& wake_word) {
        ESP_LOGI("Example", "Detected: %s", wake_word.c_str());
    });
    
    micro_ww->Start();
}

/**
 * Integration notes:
 * 
 * 1. Model Data:
 *    - hey_jarvis.h contains the TFLite model as a byte array
 *    - You can train custom models using micro-wake-word tools
 *    - Models should be quantized int8 for best performance
 * 
 * 2. Audio Format:
 *    - Input: 16kHz, mono, int16 PCM
 *    - Feed size: 480 samples (30ms chunks)
 *    - The system handles buffering internally
 * 
 * 3. Memory:
 *    - Uses PSRAM preferentially via ExternalRAMAllocator
 *    - Tensor arena size depends on model complexity
 *    - Typical usage: ~30-50KB per model
 * 
 * 4. Performance:
 *    - Google Audio Frontend extracts MFCC features
 *    - Streaming inference processes 30ms audio slices
 *    - Detection latency: ~70-100ms after wake word spoken
 * 
 * 5. Tuning:
 *    - probability_cutoff: Lower = more sensitive, more false positives
 *    - sliding_window_size: Larger = more stable, slower response
 *    - features_step_size: Smaller = more frequent inference, higher CPU
 */

