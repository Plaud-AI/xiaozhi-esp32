//Copyright (c) 2025 Xiaozhi ESP32 Project
// PlaudSRCommand: Portable Speech Recognition Command Engine

#ifndef AUDIO_WAKE_WORDS_PLAUD_SR_COMMAND_H_
#define AUDIO_WAKE_WORDS_PLAUD_SR_COMMAND_H_

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cstddef>

#include "fbank.h"

// Forward declarations for TFLite
namespace tflite {
class Model;
class MicroInterpreter;
template<unsigned int tOpCount>
class MicroMutableOpResolver;
}

namespace plaud {

/**
 * @brief Speech recognition state (similar to ESP-SR MultiNet)
 */
enum class SRState {
    DETECTING = 0,   // Still detecting, accumulating audio
    DETECTED = 1,    // Command detected with high confidence
    TIMEOUT = 2      // Detection timeout, need to reset
};

/**
 * @brief Portable Speech Recognition Command Engine (platform-independent)
 * 
 * Features:
 * - Streaming model support (with cache state)
 * - Fbank feature extraction
 * - TFLite Micro inference
 * - Internal threshold processing
 * - Command management
 */
class PlaudSRCommand {
public:
    // ==================== Data Structures ====================
    
    /**
     * @brief Command definition
     */
    struct Command {
        int id;                          // Unique command ID
        std::string text;                // Command text (e.g., "hi plaud")
        float threshold;                 // Recognition threshold [0.0, 1.0], 0 = use default
        
        Command(int cmd_id = 0, const std::string& cmd_text = "", 
                float cmd_threshold = 0.0f)
            : id(cmd_id), text(cmd_text), threshold(cmd_threshold) {}
    };
    
    /**
     * @brief Recognition result
     */
    struct Result {
        int command_id;                  // Detected command ID (-1 = no detection)
        std::string text;                // Command text
        float confidence;                // Confidence [0.0, 1.0]
        uint32_t timestamp_ms;           // Detection timestamp in milliseconds
        bool is_valid;                   // Valid result flag
        
        Result() : command_id(-1), confidence(0.0f), timestamp_ms(0), is_valid(false) {}
    };
    
    /**
     * @brief Configuration parameters
     */
    struct Config {
        int num_bins;                    // Fbank feature dimension (default 40)
        int sample_rate;                 // Sample rate in Hz (default 16000)
        int frame_length;                // Frame length in samples (default 400, 25ms @ 16kHz)
        int frame_shift;                 // Frame shift in samples (default 160, 10ms @ 16kHz)
        int batch_size;                  // Number of frames per inference (default 80)
        float default_threshold;         // Default recognition threshold (default 0.7)
        int detection_frames;            // Consecutive frames needed for detection (default 3)
        int timeout_ms;                  // Detection timeout in milliseconds (default 5000)
        const uint8_t* model_data;       // TFLite model data pointer (required)
        size_t model_size;               // Model size in bytes
        size_t tensor_arena_size;        // Tensor arena size (default 100KB)
        
        Config()
            : num_bins(40), sample_rate(16000), 
              frame_length(400), frame_shift(160),
              batch_size(80), default_threshold(0.7f),
              detection_frames(3), timeout_ms(5000),
              model_data(nullptr), model_size(0), 
              tensor_arena_size(100 * 1024) {}
    };
    
    // ==================== Lifecycle Management ====================
    
    /**
     * @brief Constructor
     */
    PlaudSRCommand();
    
    /**
     * @brief Destructor (auto-cleanup)
     */
    ~PlaudSRCommand();
    
    // Disable copy
    PlaudSRCommand(const PlaudSRCommand&) = delete;
    PlaudSRCommand& operator=(const PlaudSRCommand&) = delete;
    
    /**
     * @brief Initialize engine
     * 
     * @param config Configuration parameters
     * @return true on success, false on failure
     */
    bool Initialize(const Config& config);
    
    /**
     * @brief Check if initialized
     * 
     * @return true if initialized, false otherwise
     */
    bool IsInitialized() const { return initialized_; }
    
    /**
     * @brief Reset engine state (clear buffers, reset cache)
     * 
     * Use this to start a new recognition session
     */
    void Reset();
    
    // ==================== Command Management ====================
    
    /**
     * @brief Add a command
     * 
     * @param command Command definition
     * @return true on success, false on failure (ID exists or invalid)
     */
    bool AddCommand(const Command& command);
    
    /**
     * @brief Remove a command
     * 
     * @param command_id Command ID
     * @return true on success, false on failure (ID not found)
     */
    bool RemoveCommand(int command_id);
    
    /**
     * @brief Clear all commands
     */
    void ClearCommands();
    
    /**
     * @brief Get number of commands
     * 
     * @return Number of registered commands
     */
    int GetCommandCount() const { return commands_.size(); }
    
    // ==================== Audio Processing (Core Interface) ====================
    
    /**
     * @brief Process audio data and return recognition state
     * 
     * This is the core interface. It will:
     * 1. Accumulate audio into internal buffer
     * 2. Extract fbank features when enough samples are accumulated
     * 3. Perform TFLite inference when batch_size frames are ready
     * 4. Match commands and apply thresholds
     * 5. Return state (DETECTING/DETECTED/TIMEOUT)
     * 
     * @param audio_data Audio data (int16_t, mono)
     * @param samples Number of samples
     * @param[out] result Recognition result (valid only if state == DETECTED)
     * @return SRState (DETECTING/DETECTED/TIMEOUT)
     * 
     * @note This function is synchronous and may take 50-200ms when inference occurs
     * @note Call this repeatedly with audio chunks (e.g., 160 samples / 10ms)
     * @note When DETECTED or TIMEOUT is returned, caller should call Reset() to start new session
     */
    SRState Process(const int16_t* audio_data, size_t samples, Result& result);
    
    /**
     * @brief Process audio data (vector version)
     * 
     * @param audio_data Audio data vector
     * @param[out] result Recognition result (valid only if state == DETECTED)
     * @return SRState (DETECTING/DETECTED/TIMEOUT)
     */
    SRState Process(const std::vector<int16_t>& audio_data, Result& result);
    
    /**
     * @brief Get recommended audio chunk size
     * 
     * @return Chunk size in samples (typically frame_shift, e.g., 160)
     */
    int GetFeedSize() const { return config_.frame_shift; }
    
    // ==================== Configuration ====================
    
    /**
     * @brief Set default recognition threshold
     * 
     * @param threshold Threshold [0.0, 1.0]
     * @return true on success, false on failure (invalid parameter)
     */
    bool SetDefaultThreshold(float threshold);
    
    /**
     * @brief Get default recognition threshold
     * 
     * @return Current default threshold
     */
    float GetDefaultThreshold() const { return config_.default_threshold; }
    
    /**
     * @brief Set command-specific threshold
     * 
     * @param command_id Command ID
     * @param threshold Threshold [0.0, 1.0]
     * @return true on success, false on failure (ID not found)
     */
    bool SetCommandThreshold(int command_id, float threshold);
    
    /**
     * @brief Get configuration
     * 
     * @return Current configuration
     */
    const Config& GetConfig() const { return config_; }
    
private:
    // ==================== Internal Methods ====================
    
    /**
     * @brief Load TFLite model
     * 
     * @return true on success, false on failure
     */
    bool LoadModel();
    
    /**
     * @brief Unload TFLite model
     */
    void UnloadModel();
    
    /**
     * @brief Extract fbank features from accumulated audio
     * 
     * @return Number of frames extracted
     */
    int ExtractFeatures();
    
    /**
     * @brief Perform TFLite inference
     * 
     * @param[out] result Recognition result (if detected)
     * @return true if command detected, false otherwise
     */
    bool RunInference(Result& result);
    
    /**
     * @brief Match command from output probabilities with state tracking
     * 
     * @param probs Output probabilities [num_outputs, num_classes]
     * @param num_outputs Number of output frames
     * @param num_classes Number of classes
     * @param[out] result Recognition result
     * @return SRState (DETECTING/DETECTED/TIMEOUT)
     */
    SRState MatchCommand(const float* probs, int num_outputs, int num_classes, Result& result);
    
    /**
     * @brief Check if detection has timed out
     * 
     * @return true if timed out, false otherwise
     */
    bool CheckTimeout();
    
    /**
     * @brief Validate threshold
     * 
     * @param threshold Threshold value
     * @return true if valid, false otherwise
     */
    bool ValidateThreshold(float threshold) const {
        return threshold >= 0.0f && threshold <= 1.0f;
    }
    
    // ==================== Member Variables ====================
    
    // Configuration and state
    Config config_;
    bool initialized_;
    
    // TFLite components
    const tflite::Model* model_;
    tflite::MicroInterpreter* interpreter_;
    uint8_t* tensor_arena_;
    
    // Feature extraction
    Fbank* fbank_;
    std::vector<float> audio_buffer_;          // Accumulated audio (float)
    std::vector<std::vector<float>> features_; // Extracted features [num_frames, num_bins]
    int feature_offset_;                       // Number of features processed
    
    // Cache for streaming model
    std::vector<float> cache_;                 // Model cache state
    int cache_dim_;                            // Cache dimension (from model metadata or default)
    int cache_len_;                            // Cache length (from model metadata or default)
    
    // Command management
    std::map<int, Command> commands_;          // Command ID -> Command
    
    // State tracking
    int last_detected_command_id_;             // Last detected command ID
    int detection_frame_count_;                // Consecutive high-confidence frames
    int64_t detection_start_time_us_;          // Detection start time (microseconds)
};

}  // namespace plaud

#endif  // AUDIO_WAKE_WORDS_PLAUD_SR_COMMAND_H_

