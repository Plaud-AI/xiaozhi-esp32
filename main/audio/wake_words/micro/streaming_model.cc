#include "streaming_model.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <cstring>

static const char *const TAG = "micro_wake_word";

namespace micro_wake_word {

void WakeWordModel::log_model_config() {
  ESP_LOGI(TAG, "    - Wake Word: %s", this->wake_word_.c_str());
  ESP_LOGI(TAG, "      Probability cutoff: %.3f", this->probability_cutoff_);
  ESP_LOGI(TAG, "      Sliding window size: %u", (unsigned int)this->sliding_window_size_);
}

void VADModel::log_model_config() {
  ESP_LOGI(TAG, "    - VAD Model");
  ESP_LOGI(TAG, "      Probability cutoff: %.3f", this->probability_cutoff_);
  ESP_LOGI(TAG, "      Sliding window size: %u", (unsigned int)this->sliding_window_size_);
}

bool StreamingModel::load_model(tflite::MicroMutableOpResolver<20> &op_resolver) {
  ExternalRAMAllocator<uint8_t> arena_allocator(ExternalRAMAllocator<uint8_t>::ALLOW_FAILURE);

  if (this->tensor_arena_ == nullptr) {
    this->tensor_arena_ = arena_allocator.allocate(this->tensor_arena_size_);
    if (this->tensor_arena_ == nullptr) {
      ESP_LOGE(TAG, "Could not allocate the streaming model's tensor arena.");
      return false;
    }
    // Check if allocation is from PSRAM or SRAM
    if (esp_ptr_external_ram(this->tensor_arena_)) {
      ESP_LOGI(TAG, "✅ Tensor Arena (%u bytes) allocated from PSRAM", (unsigned int)this->tensor_arena_size_);
    } else {
      ESP_LOGW(TAG, "⚠️  Tensor Arena (%u bytes) allocated from SRAM! This will cause memory issues.", (unsigned int)this->tensor_arena_size_);
    }
  }

  if (this->var_arena_ == nullptr) {
    this->var_arena_ = arena_allocator.allocate(STREAMING_MODEL_VARIABLE_ARENA_SIZE);
    if (this->var_arena_ == nullptr) {
      ESP_LOGE(TAG, "Could not allocate the streaming model's variable tensor arena.");
      return false;
    }
    this->ma_ = tflite::MicroAllocator::Create(this->var_arena_, STREAMING_MODEL_VARIABLE_ARENA_SIZE);
    this->mrv_ = tflite::MicroResourceVariables::Create(this->ma_, 20);
  }

  const tflite::Model *model = tflite::GetModel(this->model_start_);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    ESP_LOGE(TAG, "Streaming model's schema is not supported");
    return false;
  }

  if (this->interpreter_ == nullptr) {
    this->interpreter_ = make_unique<tflite::MicroInterpreter>(
        tflite::GetModel(this->model_start_), op_resolver, this->tensor_arena_,
        this->tensor_arena_size_, this->mrv_);
    if (this->interpreter_->AllocateTensors() != kTfLiteOk) {
      ESP_LOGE(TAG, "Failed to allocate tensors for the streaming model");
      return false;
    }

    // Verify input tensor matches expected values
    // Dimension 3 will represent the first layer stride, so skip it may vary
    TfLiteTensor *input = this->interpreter_->input(0);
    if ((input->dims->size != 3) || (input->dims->data[0] != 1) ||
        (input->dims->data[2] != PREPROCESSOR_FEATURE_SIZE)) {
      ESP_LOGE(TAG, "Streaming model tensor input dimensions has improper dimensions.");
      return false;
    }

    if (input->type != kTfLiteInt8) {
      ESP_LOGE(TAG, "Streaming model tensor input is not int8.");
      return false;
    }

    // Verify output tensor matches expected values
    TfLiteTensor *output = this->interpreter_->output(0);
    if ((output->dims->size != 2) || (output->dims->data[0] != 1) ||
        (output->dims->data[1] != 1)) {
      ESP_LOGE(TAG, "Streaming model tensor output dimension is not 1x1.");
    }

    if (output->type != kTfLiteUInt8) {
      ESP_LOGE(TAG, "Streaming model tensor output is not uint8.");
      return false;
    }
  }

  ESP_LOGI(TAG, "Actual tensor arena size is %u", (unsigned int)this->interpreter_->arena_used_bytes());

  return true;
}

void StreamingModel::unload_model() {
  this->interpreter_.reset();

  ExternalRAMAllocator<uint8_t> arena_allocator(ExternalRAMAllocator<uint8_t>::ALLOW_FAILURE);

  arena_allocator.deallocate(this->tensor_arena_, this->tensor_arena_size_);
  this->tensor_arena_ = nullptr;
  arena_allocator.deallocate(this->var_arena_, STREAMING_MODEL_VARIABLE_ARENA_SIZE);
  this->var_arena_ = nullptr;
}

float StreamingModel::get_sliding_window_average() const {
  if (this->recent_streaming_probabilities_.empty()) {
    return 0.0f;
  }
  
  float sum = 0.0f;
  for (uint8_t val : this->recent_streaming_probabilities_) {
    sum += static_cast<float>(val) / 255.0f;
  }
  
  return sum / this->recent_streaming_probabilities_.size();
}

bool StreamingModel::perform_streaming_inference(const int8_t features[PREPROCESSOR_FEATURE_SIZE]) {
  if (this->interpreter_ != nullptr) {
    TfLiteTensor *input = this->interpreter_->input(0);
    
    static bool logged_once = false;
    if (!logged_once) {
      ESP_LOGI(TAG, "📐 Model input tensor shape: [%d, %d, %d]", 
               input->dims->data[0], input->dims->data[1], input->dims->data[2]);
      ESP_LOGI(TAG, "   Input quantization: scale=%f, zero_point=%d",
               input->params.scale, input->params.zero_point);
      
      TfLiteTensor *output = this->interpreter_->output(0);
      ESP_LOGI(TAG, "   Output quantization: scale=%f, zero_point=%d",
               output->params.scale, output->params.zero_point);
      logged_once = true;
    }

    // 🔍 诊断：打印输入特征统计
    static int copy_count = 0;
    if (++copy_count % 300 == 0) {  // 每300次打印一次
      int32_t sum = 0, count_neg128 = 0;
      int8_t min_val = 127, max_val = -128;
      for (int i = 0; i < PREPROCESSOR_FEATURE_SIZE; ++i) {
        int8_t val = features[i];
        sum += val;
        if (val == -128) count_neg128++;
        if (val < min_val) min_val = val;
        if (val > max_val) max_val = val;
      }
      int8_t avg = sum / PREPROCESSOR_FEATURE_SIZE;
      ESP_LOGI(TAG, "🎯 Copying features #%d (stride step %d): avg=%d, min=%d, max=%d, -128 count=%d/40",
               copy_count, this->current_stride_step_, avg, min_val, max_val, count_neg128);
    }
    
    std::memmove((int8_t *)(tflite::GetTensorData<int8_t>(input)) +
                     PREPROCESSOR_FEATURE_SIZE * this->current_stride_step_,
                 features, PREPROCESSOR_FEATURE_SIZE);
    ++this->current_stride_step_;

    uint8_t stride = this->interpreter_->input(0)->dims->data[1];

    if (this->current_stride_step_ >= stride) {
      this->current_stride_step_ = 0;

      // 🔍 实验：在推理前打印完整的输入张量（每 100 次）
      static uint32_t pre_invoke_count = 0;
      pre_invoke_count++;
      if (pre_invoke_count % 100 == 0) {
        ESP_LOGI(TAG, "🔬 Pre-Invoke #%u: Dumping full input tensor [stride=%d]...", pre_invoke_count, stride);
        int8_t *input_data = tflite::GetTensorData<int8_t>(input);
        for (int s = 0; s < stride; ++s) {
          int32_t sum = 0;
          int8_t min_val = 127, max_val = -128;
          for (int i = 0; i < PREPROCESSOR_FEATURE_SIZE; ++i) {
            int8_t val = input_data[s * PREPROCESSOR_FEATURE_SIZE + i];
            sum += val;
            if (val < min_val) min_val = val;
            if (val > max_val) max_val = val;
          }
          int8_t avg = sum / PREPROCESSOR_FEATURE_SIZE;
          ESP_LOGI(TAG, "   Stride[%d]: avg=%d, min=%d, max=%d", s, avg, min_val, max_val);
        }
      }

      TfLiteStatus invoke_status = this->interpreter_->Invoke();
      if (invoke_status != kTfLiteOk) {
        ESP_LOGW(TAG, "Streaming interpreter invoke failed");
        return false;
      }

      TfLiteTensor *output = this->interpreter_->output(0);
      
      uint8_t raw_output = output->data.uint8[0];
      static uint32_t invoke_count = 0;
      invoke_count++;
      if (invoke_count % 100 == 0) {
        ESP_LOGI(TAG, "🔍 Invoke #%u: raw model output = %u (%.3f)", 
                 (unsigned int)invoke_count, raw_output, raw_output / 255.0f);
      }

      ++this->last_n_index_;
      if (this->last_n_index_ == this->sliding_window_size_)
        this->last_n_index_ = 0;
      this->recent_streaming_probabilities_[this->last_n_index_] = raw_output;
    }
    return true;
  }
  ESP_LOGE(TAG, "Streaming interpreter is not initialized.");
  return false;
}

void StreamingModel::reset_probabilities() {
  for (auto &prob : this->recent_streaming_probabilities_) {
    prob = 0;
  }
}

WakeWordModel::WakeWordModel(const uint8_t *model_start, float probability_cutoff,
                             size_t sliding_window_average_size, const std::string &wake_word,
                             size_t tensor_arena_size) {
  this->model_start_ = model_start;
  this->probability_cutoff_ = probability_cutoff;
  this->sliding_window_size_ = sliding_window_average_size;
  this->recent_streaming_probabilities_.resize(sliding_window_average_size, 0);
  this->wake_word_ = wake_word;
  this->tensor_arena_size_ = tensor_arena_size;
};

bool WakeWordModel::determine_detected() {
  uint32_t sum = 0;
  for (auto &prob : this->recent_streaming_probabilities_) {
    sum += prob;
  }

  float sliding_window_average =
      static_cast<float>(sum) / static_cast<float>(255 * this->sliding_window_size_);

  // Detect the wake word if the sliding window average is above the cutoff
  if (sliding_window_average > this->probability_cutoff_) {
    ESP_LOGD(TAG,
             "The '%s' model sliding average probability is %.3f and most recent "
             "probability is %.3f",
             this->wake_word_.c_str(), sliding_window_average,
             this->recent_streaming_probabilities_[this->last_n_index_] / (255.0));
    return true;
  }
  return false;
}

VADModel::VADModel(const uint8_t *model_start, float probability_cutoff, size_t sliding_window_size,
                   size_t tensor_arena_size) {
  this->model_start_ = model_start;
  this->probability_cutoff_ = probability_cutoff;
  this->sliding_window_size_ = sliding_window_size;
  this->recent_streaming_probabilities_.resize(sliding_window_size, 0);
  this->tensor_arena_size_ = tensor_arena_size;
};

bool VADModel::determine_detected() {
  uint32_t sum = 0;
  for (auto &prob : this->recent_streaming_probabilities_) {
    sum += prob;
  }

  float sliding_window_average =
      static_cast<float>(sum) / static_cast<float>(255 * this->sliding_window_size_);

  return sliding_window_average > this->probability_cutoff_;
}

}  // namespace micro_wake_word

