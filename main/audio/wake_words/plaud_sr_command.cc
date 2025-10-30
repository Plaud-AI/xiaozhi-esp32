// Copyright (c) 2025 Xiaozhi ESP32 Project
// PlaudSRCommand: Portable Speech Recognition Command Engine

#include "plaud_sr_command.h"

#include <algorithm>
#include <cstring>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

// TFLite Micro includes
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/schema/schema_generated.h"

#define TAG "PlaudSRCommand"

namespace plaud {

PlaudSRCommand::PlaudSRCommand()
    : config_(),
      initialized_(false),
      model_(nullptr),
      interpreter_(nullptr),
      tensor_arena_(nullptr),
      fbank_(nullptr),
      feature_offset_(0),
      cache_dim_(0),
      cache_len_(0),
      last_detected_command_id_(-1),
      detection_frame_count_(0),
      detection_start_time_us_(0) {
}

PlaudSRCommand::~PlaudSRCommand() {
    UnloadModel();
    if (fbank_ != nullptr) {
        delete fbank_;
        fbank_ = nullptr;
    }
}

bool PlaudSRCommand::Initialize(const Config& config) {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }
    
    if (config.model_data == nullptr || config.model_size == 0) {
        ESP_LOGE(TAG, "Invalid model data");
        return false;
    }
    
    config_ = config;
    
    ESP_LOGI(TAG, "Initializing PlaudSRCommand:");
    ESP_LOGI(TAG, "  num_bins=%d, sample_rate=%d", config_.num_bins, config_.sample_rate);
    ESP_LOGI(TAG, "  frame_length=%d, frame_shift=%d", config_.frame_length, config_.frame_shift);
    ESP_LOGI(TAG, "  batch_size=%d, default_threshold=%.2f", config_.batch_size, config_.default_threshold);
    
    // Create fbank extractor
    fbank_ = new Fbank(config_.num_bins, config_.sample_rate, 
                       config_.frame_length, config_.frame_shift);
    if (fbank_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create Fbank");
        return false;
    }
    
    // Load TFLite model
    if (!LoadModel()) {
        ESP_LOGE(TAG, "Failed to load TFLite model");
        delete fbank_;
        fbank_ = nullptr;
        return false;
    }
    
    initialized_ = true;
    ESP_LOGI(TAG, "PlaudSRCommand initialized successfully");
    
    return true;
}

void PlaudSRCommand::Reset() {
    ESP_LOGI(TAG, "🔄 ═══════════════════════════════════════");
    ESP_LOGI(TAG, "🔄 RESETTING PlaudSRCommand");
    ESP_LOGD(TAG, "🔄   Buffer: %zu samples, Features: %zu frames", 
             audio_buffer_.size(), features_.size());
    
    audio_buffer_.clear();
    features_.clear();
    feature_offset_ = 0;
    
    // Reset cache
    if (!cache_.empty()) {
        std::fill(cache_.begin(), cache_.end(), 0.0f);
        ESP_LOGD(TAG, "🔄   Cache cleared: %zu elements", cache_.size());
    }
    
    // Reset state tracking
    last_detected_command_id_ = -1;
    detection_frame_count_ = 0;
    detection_start_time_us_ = 0;
    
    ESP_LOGI(TAG, "🔄   Reset complete, ready for next detection");
    ESP_LOGI(TAG, "🔄 ═══════════════════════════════════════");
}

bool PlaudSRCommand::AddCommand(const Command& command) {
    if (commands_.find(command.id) != commands_.end()) {
        ESP_LOGW(TAG, "Command ID %d already exists", command.id);
        return false;
    }
    
    if (command.threshold != 0.0f && !ValidateThreshold(command.threshold)) {
        ESP_LOGE(TAG, "Invalid threshold %.2f for command %d", command.threshold, command.id);
        return false;
    }
    
    commands_[command.id] = command;
    ESP_LOGI(TAG, "Added command: ID=%d, text='%s', threshold=%.2f", 
             command.id, command.text.c_str(), command.threshold);
    
    return true;
}

bool PlaudSRCommand::RemoveCommand(int command_id) {
    auto it = commands_.find(command_id);
    if (it == commands_.end()) {
        ESP_LOGW(TAG, "Command ID %d not found", command_id);
        return false;
    }
    
    commands_.erase(it);
    ESP_LOGI(TAG, "Removed command ID=%d", command_id);
    
    return true;
}

void PlaudSRCommand::ClearCommands() {
    commands_.clear();
    ESP_LOGI(TAG, "Cleared all commands");
}

SRState PlaudSRCommand::Process(const std::vector<int16_t>& audio_data, Result& result) {
    return Process(audio_data.data(), audio_data.size(), result);
}

SRState PlaudSRCommand::Process(const int16_t* audio_data, size_t samples, Result& result) {
    if (!initialized_) {
        ESP_LOGE(TAG, "Not initialized");
        return SRState::TIMEOUT;
    }
    
    // Record start time on first call
    if (detection_start_time_us_ == 0) {
        detection_start_time_us_ = esp_timer_get_time();
        ESP_LOGI(TAG, "🎬 Detection session started");
    }
    
    // Check for timeout
    if (CheckTimeout()) {
        ESP_LOGI(TAG, "⏱️  Detection timeout (%d ms)", config_.timeout_ms);
        return SRState::TIMEOUT;
    }
    
    // Convert int16 to float and accumulate
    size_t buffer_size_before = audio_buffer_.size();
    for (size_t i = 0; i < samples; ++i) {
        audio_buffer_.push_back(static_cast<float>(audio_data[i]));
    }
    
    static int process_count = 0;
    process_count++;
    if (process_count % 100 == 0) {
        ESP_LOGD(TAG, "🔊 [Process #%d] samples=%zu, buffer=%zu→%zu", 
                 process_count, samples, buffer_size_before, audio_buffer_.size());
    }
    
    // Extract features if we have enough samples
    int extracted = ExtractFeatures();
    if (extracted > 0) {
        ESP_LOGD(TAG, "🎵 Extracted %d feature frames, total features: %zu/%d", 
                 extracted, features_.size(), config_.batch_size);
    }
    
    // Run inference if we have enough frames
    if (features_.size() >= config_.batch_size) {
        // Only log periodically to reduce spam (especially with placeholder model)
        static int inference_count = 0;
        inference_count++;
        if (inference_count <= 5 || inference_count % 20 == 0) {
            ESP_LOGI(TAG, "🧠 Ready for inference: %zu frames (batch_size=%d) [inference #%d]", 
                     features_.size(), config_.batch_size, inference_count);
        }
        if (RunInference(result)) {
            // Inference completed, check result state
            return result.is_valid ? SRState::DETECTED : SRState::DETECTING;
        }
    }
    
    return SRState::DETECTING;
}

bool PlaudSRCommand::SetDefaultThreshold(float threshold) {
    if (!ValidateThreshold(threshold)) {
        ESP_LOGE(TAG, "Invalid threshold %.2f", threshold);
        return false;
    }
    
    config_.default_threshold = threshold;
    ESP_LOGI(TAG, "Set default threshold to %.2f", threshold);
    
    return true;
}

bool PlaudSRCommand::SetCommandThreshold(int command_id, float threshold) {
    auto it = commands_.find(command_id);
    if (it == commands_.end()) {
        ESP_LOGW(TAG, "Command ID %d not found", command_id);
        return false;
    }
    
    if (threshold != 0.0f && !ValidateThreshold(threshold)) {
        ESP_LOGE(TAG, "Invalid threshold %.2f", threshold);
        return false;
    }
    
    it->second.threshold = threshold;
    ESP_LOGI(TAG, "Set threshold %.2f for command ID=%d", threshold, command_id);
    
    return true;
}

bool PlaudSRCommand::LoadModel() {
    // Map the model
    model_ = tflite::GetModel(config_.model_data);
    if (model_->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema version %d != supported version %d",
                 model_->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }
    
    ESP_LOGI(TAG, "Model schema version: %d", model_->version());
    
    // Allocate tensor arena
    tensor_arena_ = (uint8_t*)heap_caps_malloc(config_.tensor_arena_size, 
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (tensor_arena_ == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate tensor arena (%zu bytes)", config_.tensor_arena_size);
        return false;
    }
    
    ESP_LOGI(TAG, "Allocated tensor arena: %zu bytes", config_.tensor_arena_size);
    
    // Create op resolver - add common ops for streaming keyword spotting
    // Note: Increase the size if model requires more ops
    static tflite::MicroMutableOpResolver<27> resolver;
    
    // Basic neural network ops
    resolver.AddFullyConnected();
    resolver.AddSoftmax();
    resolver.AddRelu();
    resolver.AddQuantize();
    resolver.AddDequantize();
    resolver.AddReshape();
    
    // Convolutional ops
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddAveragePool2D();
    resolver.AddMaxPool2D();
    
    // Arithmetic ops (for RNN/LSTM)
    resolver.AddAdd();
    resolver.AddMul();
    resolver.AddSub();
    
    // Activation functions (for RNN/LSTM)
    resolver.AddTanh();
    resolver.AddLogistic();  // Sigmoid
    
    // Control flow (for streaming models)
    resolver.AddWhile();
    
    // Tensor manipulation
    resolver.AddConcatenation();
    resolver.AddSplit();
    resolver.AddTranspose();     // Required for attention/conformer models
    resolver.AddPack();          // May be needed for sequence models
    resolver.AddUnpack();        // May be needed for sequence models
    resolver.AddStridedSlice();  // Required for tensor slicing operations
    resolver.AddShape();         // Required for dynamic shape operations
    
    // Create interpreter
    static tflite::MicroInterpreter static_interpreter(
        model_, resolver, tensor_arena_, config_.tensor_arena_size);
    interpreter_ = &static_interpreter;
    
    // Allocate tensors
    TfLiteStatus allocate_status = interpreter_->AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors() failed");
        return false;
    }
    
    ESP_LOGI(TAG, "Tensors allocated successfully");
    ESP_LOGI(TAG, "Arena used: %zu bytes", interpreter_->arena_used_bytes());
    
    // Get input tensor info
    TfLiteTensor* input = interpreter_->input(0);
    ESP_LOGI(TAG, "Input tensor: dims=%d, shape=[%d, %d, %d]",
             input->dims->size,
             input->dims->data[0],
             input->dims->size > 1 ? input->dims->data[1] : 0,
             input->dims->size > 2 ? input->dims->data[2] : 0);
    
    // Validate input tensor dimensions
    if (input->dims->size < 3) {
        ESP_LOGW(TAG, "⚠️  Input tensor has only %d dimensions (expected 3: [batch, frames, features])", input->dims->size);
        ESP_LOGW(TAG, "⚠️  This is a PLACEHOLDER MODEL and will NOT work for wake word detection!");
    }
    
    // Get output tensor info
    TfLiteTensor* output = interpreter_->output(0);
    ESP_LOGI(TAG, "Output tensor: dims=%d, shape=[%d, %d, %d]",
             output->dims->size,
             output->dims->data[0],
             output->dims->size > 1 ? output->dims->data[1] : 0,
             output->dims->size > 2 ? output->dims->data[2] : 0);
    
    // Validate output tensor dimensions
    if (output->dims->size < 3) {
        ESP_LOGW(TAG, "⚠️  Output tensor has only %d dimensions (expected 3: [batch, frames, classes])", output->dims->size);
        ESP_LOGW(TAG, "⚠️  This is a PLACEHOLDER MODEL and will NOT work for wake word detection!");
        ESP_LOGW(TAG, "⚠️  Please replace wake_word_model_data.cc with a real trained TFLite model");
    }
    
    // Initialize cache (check if model has cache input/output)
    // For simplicity, we'll use fixed cache dimensions
    // In a real streaming model, these would come from model metadata
    cache_dim_ = 128;   // Default cache dimension
    cache_len_ = 4;     // Default cache length
    cache_.resize(cache_dim_ * cache_len_, 0.0f);
    
    ESP_LOGI(TAG, "Initialized cache: dim=%d, len=%d", cache_dim_, cache_len_);
    
    return true;
}

void PlaudSRCommand::UnloadModel() {
    // Note: interpreter_ points to static memory, no delete needed
    interpreter_ = nullptr;
    model_ = nullptr;
    
    if (tensor_arena_ != nullptr) {
        heap_caps_free(tensor_arena_);
        tensor_arena_ = nullptr;
    }
}

int PlaudSRCommand::ExtractFeatures() {
    // Check if we have enough samples for at least one frame
    int min_samples_needed = config_.frame_length;
    if (audio_buffer_.size() < min_samples_needed) {
        return 0;
    }
    
    ESP_LOGD(TAG, "🎵 [ExtractFeatures] buffer_size=%zu, frame_length=%d, frame_shift=%d",
             audio_buffer_.size(), config_.frame_length, config_.frame_shift);
    
    // Extract features
    std::vector<std::vector<float>> new_feats;
    int num_frames = fbank_->Compute(audio_buffer_, &new_feats);
    
    if (num_frames > 0) {
        // Append new features
        size_t features_before = features_.size();
        features_.insert(features_.end(), new_feats.begin(), new_feats.end());
        
        // Remove processed samples from buffer
        int samples_processed = config_.frame_shift * num_frames;
        if (samples_processed < audio_buffer_.size()) {
            audio_buffer_.erase(audio_buffer_.begin(), 
                               audio_buffer_.begin() + samples_processed);
        } else {
            audio_buffer_.clear();
        }
        
        ESP_LOGD(TAG, "🎵 Extracted %d frames (%zu→%zu), processed %d samples, buffer remaining: %zu", 
                 num_frames, features_before, features_.size(), samples_processed, audio_buffer_.size());
    }
    
    return num_frames;
}

bool PlaudSRCommand::RunInference(Result& result) {
    if (features_.size() < config_.batch_size) {
        return false;
    }
    
    // Reduce log frequency for placeholder model scenario
    static int run_inference_count = 0;
    run_inference_count++;
    bool should_log = (run_inference_count <= 5 || run_inference_count % 20 == 0);
    
    if (should_log) {
        ESP_LOGI(TAG, "🧠 ═══════════════════════════════════════");
        ESP_LOGI(TAG, "🧠 RUNNING INFERENCE [#%d]", run_inference_count);
        ESP_LOGI(TAG, "🧠   Features: %zu frames", features_.size());
    }
    
    // Get tensors
    TfLiteTensor* input = interpreter_->input(0);
    TfLiteTensor* output = interpreter_->output(0);
    
    if (input == nullptr || output == nullptr) {
        ESP_LOGE(TAG, "Invalid input/output tensors");
        return false;
    }
    
    ESP_LOGD(TAG, "🧠   Input tensor: type=%d, shape=[%d,%d,%d]",
             input->type, input->dims->data[0], input->dims->data[1], input->dims->data[2]);
    ESP_LOGD(TAG, "🧠   Output tensor: type=%d", output->type);
    
    // Prepare input: [1, batch_size, num_bins]
    int batch_size = std::min((int)features_.size(), config_.batch_size);
    
    // Copy features to input tensor
    if (input->type == kTfLiteFloat32) {
        float* input_data = input->data.f;
        for (int i = 0; i < batch_size; ++i) {
            for (int j = 0; j < config_.num_bins; ++j) {
                input_data[i * config_.num_bins + j] = features_[i][j];
            }
        }
    } else if (input->type == kTfLiteInt8) {
        // Quantized input
        int8_t* input_data = input->data.int8;
        float scale = input->params.scale;
        int zero_point = input->params.zero_point;
        
        for (int i = 0; i < batch_size; ++i) {
            for (int j = 0; j < config_.num_bins; ++j) {
                float value = features_[i][j];
                int32_t quantized = static_cast<int32_t>(value / scale + zero_point);
                quantized = std::max(static_cast<int32_t>(-128), std::min(static_cast<int32_t>(127), quantized));
                input_data[i * config_.num_bins + j] = static_cast<int8_t>(quantized);
            }
        }
    } else {
        ESP_LOGE(TAG, "Unsupported input tensor type: %d", input->type);
        return false;
    }
    
    // Run inference
    if (should_log) {
        ESP_LOGI(TAG, "🧠   Invoking TFLite interpreter...");
    }
    int64_t start_time = esp_timer_get_time();
    TfLiteStatus invoke_status = interpreter_->Invoke();
    int64_t inference_time_us = esp_timer_get_time() - start_time;
    
    if (invoke_status != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke() failed");
        return false;
    }
    
    if (should_log) {
        ESP_LOGI(TAG, "🧠   Inference completed in %.2f ms", inference_time_us / 1000.0f);
    }
    
    // Get output probabilities
    // Validate output tensor dimensions
    if (output->dims->size < 3) {
        // Only log error periodically to avoid spam
        static int error_count = 0;
        error_count++;
        if (error_count <= 3 || error_count % 50 == 0) {
            ESP_LOGE(TAG, "❌ Invalid output tensor dimensions: size=%d (expected >= 3) [error #%d]", 
                     output->dims->size, error_count);
            if (error_count <= 3) {
                ESP_LOGE(TAG, "❌ This is likely a placeholder model issue!");
                ESP_LOGE(TAG, "❌ Please replace wake_word_model_data.cc with a real trained model");
            }
        }
        
        // Clear accumulated features to prevent infinite accumulation
        features_.clear();
        feature_offset_ = 0;
        
        return false;
    }
    
    int num_outputs = output->dims->data[1];  // Number of output frames
    int num_classes = output->dims->data[2];  // Number of classes
    
    if (should_log) {
        ESP_LOGI(TAG, "🧠   Output: %d frames × %d classes", num_outputs, num_classes);
    }
    
    // Validate dimensions are reasonable
    if (num_outputs <= 0 || num_outputs > 1000 || num_classes <= 0 || num_classes > 100) {
        // Only log error periodically to avoid spam
        static int dim_error_count = 0;
        dim_error_count++;
        if (dim_error_count <= 3 || dim_error_count % 50 == 0) {
            ESP_LOGE(TAG, "❌ Invalid output dimensions: num_outputs=%d, num_classes=%d [error #%d]", 
                     num_outputs, num_classes, dim_error_count);
            if (dim_error_count <= 3) {
                ESP_LOGE(TAG, "❌ This is likely a placeholder model issue!");
                ESP_LOGE(TAG, "❌ Please replace wake_word_model_data.cc with a real trained model");
            }
        }
        
        // Clear accumulated features to prevent infinite accumulation
        features_.clear();
        feature_offset_ = 0;
        
        return false;
    }
    
    const float* output_data = nullptr;
    std::vector<float> dequantized_output;
    
    if (output->type == kTfLiteFloat32) {
        output_data = output->data.f;
    } else if (output->type == kTfLiteInt8) {
        // Dequantize output
        int8_t* quantized_output = output->data.int8;
        float scale = output->params.scale;
        int zero_point = output->params.zero_point;
        
        size_t output_size = static_cast<size_t>(num_outputs) * static_cast<size_t>(num_classes);
        if (should_log) {
            ESP_LOGD(TAG, "🧠   Dequantizing %zu elements...", output_size);
        }
        dequantized_output.resize(output_size);
        for (int i = 0; i < num_outputs * num_classes; ++i) {
            dequantized_output[i] = (quantized_output[i] - zero_point) * scale;
        }
        output_data = dequantized_output.data();
    } else {
        ESP_LOGE(TAG, "Unsupported output tensor type: %d", output->type);
        return false;
    }
    
    // Match command (with state tracking)
    SRState state = MatchCommand(output_data, num_outputs, num_classes, result);
    
    // Remove processed features
    if (batch_size > 0) {
        features_.erase(features_.begin(), features_.begin() + batch_size);
        feature_offset_ += batch_size;
    }
    
    ESP_LOGD(TAG, "Inference complete, state=%d, remaining features: %zu", 
             static_cast<int>(state), features_.size());
    
    // Set result validity based on state
    result.is_valid = (state == SRState::DETECTED);
    
    return true;  // Inference succeeded
}

SRState PlaudSRCommand::MatchCommand(const float* probs, int num_outputs, int num_classes, Result& result) {
    ESP_LOGI(TAG, "🔍 ═══════════════════════════════════════");
    ESP_LOGI(TAG, "🔍 MATCHING COMMAND");
    ESP_LOGI(TAG, "🔍   Frames: %d, Classes: %d", num_outputs, num_classes);
    
    // Find maximum probability across all output frames
    float max_prob = 0.0f;
    int max_class = -1;
    
    // Also collect average probabilities per class for debugging
    std::vector<float> avg_probs(num_classes, 0.0f);
    
    for (int t = 0; t < num_outputs; ++t) {
        for (int c = 0; c < num_classes; ++c) {
            float prob = probs[t * num_classes + c];
            avg_probs[c] += prob;
            if (prob > max_prob) {
                max_prob = prob;
                max_class = c;
            }
        }
    }
    
    // Calculate and display average probabilities
    for (int c = 0; c < num_classes; ++c) {
        avg_probs[c] /= num_outputs;
    }
    
    ESP_LOGI(TAG, "🔍   Max: class=%d, prob=%.3f (%.1f%%)", max_class, max_prob, max_prob * 100.0f);
    
    // Display probabilities for each class
    for (int c = 0; c < num_classes; ++c) {
        ESP_LOGD(TAG, "🔍     Class[%d]: avg=%.3f (%.1f%%)", c, avg_probs[c], avg_probs[c] * 100.0f);
    }
    
    // Find the matching command with highest confidence
    int matched_command_id = -1;
    float matched_confidence = 0.0f;
    std::string matched_text;
    
    for (const auto& pair : commands_) {
        const Command& cmd = pair.second;
        
        // Use command-specific threshold or default threshold
        float threshold = (cmd.threshold > 0.0f) ? cmd.threshold : config_.default_threshold;
        
        // Check if this command matches (command.id == class index)
        if (max_class == cmd.id && max_prob >= threshold) {
            if (max_prob > matched_confidence) {
                matched_command_id = cmd.id;
                matched_confidence = max_prob;
                matched_text = cmd.text;
            }
        }
    }
    
    // State tracking: require consecutive high-confidence frames
    if (matched_command_id >= 0) {
        // High confidence detected
        ESP_LOGI(TAG, "🔍   Matched: '%s' (ID=%d, confidence=%.2f, threshold=%.2f)", 
                 matched_text.c_str(), matched_command_id, matched_confidence,
                 (commands_[matched_command_id].threshold > 0.0f) 
                    ? commands_[matched_command_id].threshold 
                    : config_.default_threshold);
        
        if (matched_command_id == last_detected_command_id_) {
            // Same command as before, increment counter
            detection_frame_count_++;
            
            ESP_LOGI(TAG, "🔍   Progress: '%s' (%d/%d frames) ✓", 
                     matched_text.c_str(), detection_frame_count_, config_.detection_frames);
            
            if (detection_frame_count_ >= config_.detection_frames) {
                // Confirmed detection!
                result.command_id = matched_command_id;
                result.text = matched_text;
                result.confidence = matched_confidence;
                result.timestamp_ms = static_cast<uint32_t>((esp_timer_get_time() - detection_start_time_us_) / 1000);
                result.is_valid = true;
                
                ESP_LOGI(TAG, "🔍 ═══════════════════════════════════════");
                ESP_LOGI(TAG, "✅ COMMAND CONFIRMED!");
                ESP_LOGI(TAG, "✅   '%s' (ID=%d)", result.text.c_str(), result.command_id);
                ESP_LOGI(TAG, "✅   Confidence: %.1f%%", result.confidence * 100.0f);
                ESP_LOGI(TAG, "✅   Frames: %d/%d", detection_frame_count_, config_.detection_frames);
                ESP_LOGI(TAG, "🔍 ═══════════════════════════════════════");
                
                return SRState::DETECTED;
            }
        } else {
            // Different command, reset counter
            if (last_detected_command_id_ >= 0) {
                ESP_LOGI(TAG, "🔍   Switching candidate: (previous ID=%d) → '%s' (ID=%d)", 
                         last_detected_command_id_, matched_text.c_str(), matched_command_id);
            }
            last_detected_command_id_ = matched_command_id;
            detection_frame_count_ = 1;
            
            ESP_LOGI(TAG, "🔍   New candidate: '%s' (1/%d frames)", 
                     matched_text.c_str(), config_.detection_frames);
        }
    } else {
        // No high confidence, reset tracking
        if (last_detected_command_id_ >= 0) {
            ESP_LOGI(TAG, "🔍   Lost detection (ID=%d), resetting", last_detected_command_id_);
        }
        last_detected_command_id_ = -1;
        detection_frame_count_ = 0;
        ESP_LOGD(TAG, "🔍   No command matched threshold");
    }
    
    // Still detecting
    result.is_valid = false;
    return SRState::DETECTING;
}

bool PlaudSRCommand::CheckTimeout() {
    if (detection_start_time_us_ == 0) {
        return false;  // Not started yet
    }
    
    int64_t elapsed_us = esp_timer_get_time() - detection_start_time_us_;
    int64_t timeout_us = static_cast<int64_t>(config_.timeout_ms) * 1000;
    
    return elapsed_us >= timeout_us;
}

}  // namespace plaud

