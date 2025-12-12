#include "inference_test_uploader.h"
#include <esp_log.h>
#include <esp_http_client.h>
#include <cstring>
#include <sstream>
#include <iomanip>

static const char* TAG = "InferenceTestUploader";

namespace micro_wake_word {

InferenceTestUploader::InferenceTestUploader() {
    ESP_LOGI(TAG, "InferenceTestUploader created, server: %s", server_url_.c_str());
}

InferenceTestUploader::~InferenceTestUploader() {
    Stop();
}

bool InferenceTestUploader::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Uploader already running");
        return true;
    }
    
    // 创建队列（存储指针，避免大数据拷贝）
    upload_queue_ = xQueueCreate(kQueueSize, sizeof(InferenceTestRecorder::UploadPacket*));
    if (!upload_queue_) {
        ESP_LOGE(TAG, "Failed to create upload queue");
        return false;
    }
    
    running_ = true;
    
    // 创建上传任务
    BaseType_t ret = xTaskCreate(
        UploadTaskEntry,
        "inference_upload",
        kTaskStackSize,
        this,
        kTaskPriority,
        &upload_task_
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create upload task");
        vQueueDelete(upload_queue_);
        upload_queue_ = nullptr;
        running_ = false;
        return false;
    }
    
    ESP_LOGI(TAG, "🚀 Uploader started, server: %s", server_url_.c_str());
    return true;
}

void InferenceTestUploader::Stop() {
    if (!running_) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping uploader...");
    running_ = false;
    
    if (upload_queue_ && upload_task_) {
        // 发送空指针唤醒任务退出
        InferenceTestRecorder::UploadPacket* null_ptr = nullptr;
        xQueueSend(upload_queue_, &null_ptr, pdMS_TO_TICKS(100));
        
        // 等待任务自己退出（任务会调用 vTaskDelete(nullptr) 自删除）
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    
    // 注意：不要调用 vTaskDelete(upload_task_)，因为任务已经自删除了
    upload_task_ = nullptr;
    
    // 清空队列中残留的数据
    if (upload_queue_) {
        InferenceTestRecorder::UploadPacket* packet = nullptr;
        while (xQueueReceive(upload_queue_, &packet, 0) == pdTRUE) {
            if (packet) {
                delete packet;
            }
        }
        vQueueDelete(upload_queue_);
        upload_queue_ = nullptr;
    }
    
    ESP_LOGI(TAG, "Uploader stopped");
}

bool InferenceTestUploader::Submit(InferenceTestRecorder::UploadPacket&& packet) {
    if (!running_ || !upload_queue_) {
        ESP_LOGW(TAG, "Uploader not running, cannot submit");
        return false;
    }
    
    // 在堆上分配，避免栈上大对象
    auto* heap_packet = new InferenceTestRecorder::UploadPacket(std::move(packet));
    
    // 非阻塞发送
    if (xQueueSend(upload_queue_, &heap_packet, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Upload queue full, dropping packet (PCM: %lu, Prob: %lu)",
                 (unsigned long)heap_packet->pcm_data.size(), 
                 (unsigned long)heap_packet->probabilities.size());
        delete heap_packet;
        return false;
    }
    
    ESP_LOGI(TAG, "📤 Packet submitted (PCM: %lu samples, Prob: %lu values)",
             (unsigned long)heap_packet->pcm_data.size(), 
             (unsigned long)heap_packet->probabilities.size());
    return true;
}

void InferenceTestUploader::UploadTaskEntry(void* param) {
    auto* self = static_cast<InferenceTestUploader*>(param);
    self->UploadTask();
}

void InferenceTestUploader::UploadTask() {
    ESP_LOGI(TAG, "Upload task started");
    
    while (running_) {
        InferenceTestRecorder::UploadPacket* packet = nullptr;
        
        // 等待队列数据，超时 1 秒
        if (xQueueReceive(upload_queue_, &packet, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (!packet) {
                // 收到空指针，退出信号
                ESP_LOGI(TAG, "Received exit signal");
                break;
            }
            
            // 计算音频时长和数据量
            uint32_t pcm_samples = (uint32_t)packet->pcm_data.size();
            uint32_t prob_count = (uint32_t)packet->probabilities.size();
            uint32_t audio_duration_ms = pcm_samples * 1000 / 16000;  // 16kHz
            uint32_t pcm_bytes = pcm_samples * sizeof(int16_t);
            uint32_t pcm_kb = pcm_bytes / 1024;
            
            // 上传开始总结
            ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
            ESP_LOGI(TAG, "║  📤 Inference Test Upload Started                        ║");
            ESP_LOGI(TAG, "╠══════════════════════════════════════════════════════════╣");
            ESP_LOGI(TAG, "║  Wake Word: %-43s ║", packet->wake_word.c_str());
            ESP_LOGI(TAG, "║  Detection Probability: %-31.3f ║", packet->final_probability);
            ESP_LOGI(TAG, "║  Detection Duration: %-34lu ms ║", (unsigned long)packet->duration_ms);
            ESP_LOGI(TAG, "║  Audio Duration: %-38lu ms ║", (unsigned long)audio_duration_ms);
            ESP_LOGI(TAG, "║  PCM Data: %lu samples (%lu KB)                          ║", 
                     (unsigned long)pcm_samples, (unsigned long)pcm_kb);
            ESP_LOGI(TAG, "║  Inference Count: %-37lu ║", (unsigned long)prob_count);
            ESP_LOGI(TAG, "║  Server: %-46s ║", server_url_.c_str());
            ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
            
            bool pcm_success = true;
            bool prob_success = true;
            
            // 上传 PCM 数据
            if (!packet->pcm_data.empty()) {
                ESP_LOGI(TAG, "📤 [1/2] Uploading PCM data (%lu KB)...", (unsigned long)pcm_kb);
                pcm_success = UploadPCM(packet->pcm_data);
                if (pcm_success) {
                    ESP_LOGI(TAG, "✅ [1/2] PCM upload success");
                } else {
                    ESP_LOGE(TAG, "❌ [1/2] PCM upload failed");
                }
            }
            
            // 上传概率数据
            if (!packet->probabilities.empty()) {
                ESP_LOGI(TAG, "📤 [2/2] Uploading probabilities (%lu values)...", 
                         (unsigned long)prob_count);
                prob_success = UploadProbabilities(packet->probabilities);
                if (prob_success) {
                    ESP_LOGI(TAG, "✅ [2/2] Probabilities upload success");
                } else {
                    ESP_LOGE(TAG, "❌ [2/2] Probabilities upload failed");
                }
            }
            
            // 上传完成总结
            ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
            if (pcm_success && prob_success) {
                ESP_LOGI(TAG, "║  ✅ Upload Complete - SUCCESS                            ║");
            } else {
                ESP_LOGE(TAG, "║  ❌ Upload Complete - FAILED                             ║");
            }
            ESP_LOGI(TAG, "╠══════════════════════════════════════════════════════════╣");
            ESP_LOGI(TAG, "║  PCM Upload:    %-39s ║", pcm_success ? "✅ SUCCESS" : "❌ FAILED");
            ESP_LOGI(TAG, "║  Prob Upload:   %-39s ║", prob_success ? "✅ SUCCESS" : "❌ FAILED");
            ESP_LOGI(TAG, "║  Total Data:    %lu KB + %lu values                       ║", 
                     (unsigned long)pcm_kb, (unsigned long)prob_count);
            ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
            
            // 释放数据包
            delete packet;
        }
    }
    
    ESP_LOGI(TAG, "Upload task exiting");
    vTaskDelete(nullptr);
}

bool InferenceTestUploader::UploadPCM(const std::vector<int16_t>& pcm_data) {
    std::string url = server_url_ + "/upload/bytes";
    
    ESP_LOGD(TAG, "POST %s (%lu bytes)", url.c_str(), 
             (unsigned long)(pcm_data.size() * sizeof(int16_t)));
    
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = kHttpTimeoutMs;
    config.buffer_size = 4096;
    config.buffer_size_tx = 4096;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }
    
    // 设置请求头
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    
    // 设置 POST 数据（直接发送二进制 PCM 数据）
    esp_http_client_set_post_field(client, 
        reinterpret_cast<const char*>(pcm_data.data()),
        pcm_data.size() * sizeof(int16_t));
    
    // 执行请求
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    int64_t content_length = esp_http_client_get_content_length(client);
    
    esp_http_client_cleanup(client);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        return false;
    }
    
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP status %d (content_length: %lld)", status_code, content_length);
        return false;
    }
    
    return true;
}

bool InferenceTestUploader::UploadProbabilities(const std::vector<uint8_t>& probabilities) {
    std::string url = server_url_ + "/upload/text";
    
    // 构造文本：逗号分隔的概率值（转换为 0.0-1.0 的浮点数）
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    for (size_t i = 0; i < probabilities.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << (static_cast<float>(probabilities[i]) / 255.0f);
    }
    std::string text = oss.str();
    
    ESP_LOGD(TAG, "POST %s (%lu chars)", url.c_str(), (unsigned long)text.length());
    
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = kHttpTimeoutMs;
    config.buffer_size = 4096;
    config.buffer_size_tx = 4096;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }
    
    // 设置请求头
    esp_http_client_set_header(client, "Content-Type", "text/plain; charset=utf-8");
    
    // 设置 POST 数据
    esp_http_client_set_post_field(client, text.c_str(), text.length());
    
    // 执行请求
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    int64_t content_length = esp_http_client_get_content_length(client);
    
    esp_http_client_cleanup(client);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        return false;
    }
    
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP status %d (content_length: %lld)", status_code, content_length);
        return false;
    }
    
    return true;
}

}  // namespace micro_wake_word


