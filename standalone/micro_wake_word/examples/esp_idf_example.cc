/**
 * @file esp_idf_example.cc
 * @brief ESP-IDF integration example for Micro Wake Word
 * 
 * This example demonstrates how to integrate Micro Wake Word with ESP-IDF:
 * 1. Using I2S for audio input
 * 2. Processing audio in a FreeRTOS task
 * 3. Handling wake word detection
 * 
 * To use:
 * 1. Copy the standalone/micro_wake_word directory to your ESP-IDF project
 * 2. Include the core and platform files in your CMakeLists.txt
 * 3. Link with TFLite Micro and Google Audio Frontend
 */

#ifdef ESP_PLATFORM  // Only compile on ESP-IDF

#include "platform/esp_idf_compat.h"
#include "core/micro_wake_word.h"

// Include your wake word model
// #include "models/okay_nabu.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2s_std.h>
#include <esp_log.h>

static const char* TAG = "MWW_Example";

using namespace micro_wake_word;

// Global wake word detector
static MicroWakeWord* g_wake_word = nullptr;

// Audio task handle
static TaskHandle_t audio_task_handle = nullptr;

/**
 * @brief Initialize I2S for microphone input
 */
static i2s_chan_handle_t init_i2s_microphone() {
    i2s_chan_handle_t rx_handle = nullptr;
    
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, nullptr, &rx_handle));
    
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),  // 16kHz sample rate
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_26,  // Configure for your board
            .ws = GPIO_NUM_25,
            .dout = I2S_GPIO_UNUSED,
            .din = GPIO_NUM_22,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    
    ESP_LOGI(TAG, "I2S microphone initialized");
    return rx_handle;
}

/**
 * @brief Audio processing task
 */
static void audio_task(void* arg) {
    i2s_chan_handle_t rx_handle = (i2s_chan_handle_t)arg;
    
    const size_t feed_size = g_wake_word->GetFeedSize();
    std::vector<int16_t> audio_buffer(feed_size);
    size_t bytes_read = 0;
    
    ESP_LOGI(TAG, "Audio task started, feed size: %u samples", (unsigned)feed_size);
    
    while (true) {
        // Read audio from I2S
        esp_err_t err = i2s_channel_read(rx_handle, audio_buffer.data(), 
                                          feed_size * sizeof(int16_t), 
                                          &bytes_read, portMAX_DELAY);
        
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "I2S read error: %d", err);
            continue;
        }
        
        // Feed audio to wake word detector
        g_wake_word->Feed(audio_buffer);
        
        // Small delay to prevent watchdog timeout
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * @brief Wake word detection callback
 */
static void on_wake_word_detected(const std::string& wake_word) {
    ESP_LOGI(TAG, "🎯 Wake word detected: %s", wake_word.c_str());
    
    // Stop detection
    g_wake_word->Stop();
    
    // TODO: Start voice assistant / send audio to server
    // ...
    
    // Restart detection after a delay
    vTaskDelay(pdMS_TO_TICKS(2000));
    g_wake_word->Start();
}

/**
 * @brief Initialize wake word detection
 */
void init_wake_word() {
    ESP_LOGI(TAG, "Initializing Micro Wake Word...");
    
    // Create wake word detector
    g_wake_word = new MicroWakeWord();
    
    if (!g_wake_word->Initialize()) {
        ESP_LOGE(TAG, "Failed to initialize wake word detector!");
        return;
    }
    
    // Add wake word model
    // Uncomment and use your model data:
    /*
    g_wake_word->add_wake_word_model(
        okay_nabu_tflite,  // Model data
        0.97f,             // Probability cutoff
        5,                 // Sliding window size
        "okay nabu",       // Wake word name
        26080              // Tensor arena size
    );
    */
    
    // Set detection callback
    g_wake_word->OnWakeWordDetected(on_wake_word_detected);
    
    // Initialize I2S microphone
    i2s_chan_handle_t rx_handle = init_i2s_microphone();
    
    // Create audio processing task
    xTaskCreatePinnedToCore(
        audio_task,          // Task function
        "audio_task",        // Name
        8192,                // Stack size
        rx_handle,           // Parameter
        5,                   // Priority
        &audio_task_handle,  // Task handle
        1                    // Core ID (run on core 1)
    );
    
    // Start wake word detection
    g_wake_word->Start();
    
    ESP_LOGI(TAG, "Wake word detection started!");
}

/**
 * @brief Main application entry point (for ESP-IDF)
 */
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Micro Wake Word ESP-IDF Example");
    
    // Initialize wake word detection
    init_wake_word();
    
    // Main loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Log status
        if (g_wake_word) {
            ESP_LOGI(TAG, "Wake word state: %d", (int)g_wake_word->GetState());
        }
    }
}

#endif  // ESP_PLATFORM

