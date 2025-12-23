/**
 * @file basic_example.cc
 * @brief Basic usage example of Micro Wake Word
 * 
 * This example demonstrates:
 * 1. Initializing MicroWakeWord
 * 2. Adding wake word models
 * 3. Setting detection callbacks
 * 4. Processing audio data
 * 
 * To compile (Linux):
 * g++ -std=c++14 -I../core -I../platform -I<tflite-micro-path> \
 *     -I<google-audio-frontend-path> basic_example.cc ../core/*.cc \
 *     -o wake_word_demo -ltensorflow-lite
 */

// Include platform compatibility first
#ifdef ESP_PLATFORM
#include "../platform/esp_idf_compat.h"
#else
#include "../platform/linux_compat.h"
#endif

// Then include core headers
#include "../core/micro_wake_word.h"

// Include your model data
// #include "../models/your_model.h"

#include <iostream>
#include <vector>
#include <fstream>

using namespace micro_wake_word;

// Example: Dummy model data for demonstration
// Replace with actual model data from hey_jarvis.h, okay_nabu.h, etc.
// extern const unsigned char your_model_tflite[];

/**
 * @brief Example 1: Basic initialization
 */
void example_basic_init() {
    std::cout << "=== Example 1: Basic Initialization ===" << std::endl;
    
    // Create wake word detector
    MicroWakeWord mww;
    
    // Initialize
    if (!mww.Initialize()) {
        std::cerr << "Failed to initialize!" << std::endl;
        return;
    }
    
    std::cout << "Initialized successfully!" << std::endl;
    
    // Note: You would add models here with add_wake_word_model()
    // mww.add_wake_word_model(model_data, 0.5f, 10, "hey jarvis", 30000);
}

/**
 * @brief Example 2: With detection callback
 */
void example_with_callback() {
    std::cout << "\n=== Example 2: With Detection Callback ===" << std::endl;
    
    MicroWakeWord mww;
    
    if (!mww.Initialize()) {
        std::cerr << "Failed to initialize!" << std::endl;
        return;
    }
    
    // Set detection callback
    mww.OnWakeWordDetected([](const std::string& wake_word) {
        std::cout << "🎯 WAKE WORD DETECTED: " << wake_word << std::endl;
        
        // Here you would typically:
        // 1. Stop wake word detection
        // 2. Start voice assistant / command processing
        // 3. Send audio to speech-to-text service
    });
    
    // Add your model here
    // mww.add_wake_word_model(hey_jarvis_tflite, 0.5f, 10, "hey jarvis", 30000);
    
    std::cout << "Callback registered!" << std::endl;
}

/**
 * @brief Example 3: Processing audio data
 */
void example_audio_processing() {
    std::cout << "\n=== Example 3: Audio Processing ===" << std::endl;
    
    MicroWakeWord mww;
    
    if (!mww.Initialize()) {
        std::cerr << "Failed to initialize!" << std::endl;
        return;
    }
    
    // Add model (example only - replace with real model)
    // mww.add_wake_word_model(model_data, 0.5f, 10, "hey jarvis", 30000);
    
    mww.OnWakeWordDetected([&mww](const std::string& wake_word) {
        std::cout << "Detected: " << wake_word << std::endl;
        mww.Stop();  // Stop after detection
    });
    
    // Start detection
    // mww.Start();
    
    // Get recommended feed size
    size_t feed_size = mww.GetFeedSize();
    std::cout << "Recommended feed size: " << feed_size << " samples" << std::endl;
    
    // Simulate audio processing loop
    std::vector<int16_t> audio_buffer(feed_size);
    
    for (int i = 0; i < 100; i++) {
        // Fill buffer with audio data
        // In real application, read from microphone or audio file
        for (size_t j = 0; j < feed_size; j++) {
            audio_buffer[j] = 0;  // Silence for demo
        }
        
        // Feed audio to detector
        // mww.Feed(audio_buffer);
        
        // Check if detected
        if (mww.WasDetected()) {
            std::cout << "Wake word was detected!" << std::endl;
            break;
        }
    }
    
    // Stop detection
    // mww.Stop();
}

/**
 * @brief Example 4: Reading audio from WAV file (for testing)
 */
bool read_wav_file(const std::string& filename, std::vector<int16_t>& samples) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open file: " << filename << std::endl;
        return false;
    }
    
    // Skip WAV header (44 bytes for standard PCM WAV)
    file.seekg(44);
    
    // Read samples
    int16_t sample;
    while (file.read(reinterpret_cast<char*>(&sample), sizeof(sample))) {
        samples.push_back(sample);
    }
    
    std::cout << "Read " << samples.size() << " samples from " << filename << std::endl;
    return true;
}

void example_wav_file_test() {
    std::cout << "\n=== Example 4: WAV File Test ===" << std::endl;
    
    MicroWakeWord mww;
    
    if (!mww.Initialize()) {
        std::cerr << "Failed to initialize!" << std::endl;
        return;
    }
    
    // Load audio from WAV file
    std::vector<int16_t> audio_samples;
    // read_wav_file("test_audio.wav", audio_samples);  // Uncomment to use
    
    // Add model and process
    // mww.add_wake_word_model(model_data, 0.5f, 10, "hey jarvis", 30000);
    
    mww.OnWakeWordDetected([](const std::string& wake_word) {
        std::cout << "DETECTED: " << wake_word << std::endl;
    });
    
    // mww.Start();
    
    // Process in chunks
    size_t chunk_size = mww.GetFeedSize();
    std::vector<int16_t> chunk(chunk_size);
    
    for (size_t i = 0; i + chunk_size <= audio_samples.size(); i += chunk_size) {
        std::copy(audio_samples.begin() + i, 
                  audio_samples.begin() + i + chunk_size, 
                  chunk.begin());
        // mww.Feed(chunk);
        
        if (mww.WasDetected()) {
            std::cout << "Wake word found at sample " << i << std::endl;
            break;
        }
    }
    
    // mww.Stop();
}

int main(int argc, char* argv[]) {
    std::cout << "Micro Wake Word - Basic Examples" << std::endl;
    std::cout << "=================================" << std::endl;
    
    // Run examples
    example_basic_init();
    example_with_callback();
    example_audio_processing();
    example_wav_file_test();
    
    std::cout << "\n✅ All examples completed!" << std::endl;
    std::cout << "\nNote: These examples are for demonstration only." << std::endl;
    std::cout << "Uncomment the model loading and Start()/Stop() calls to use." << std::endl;
    
    return 0;
}

