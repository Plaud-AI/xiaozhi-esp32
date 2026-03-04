#include "inference_test_uploader.h"
#include <esp_log.h>
#include <esp_http_client.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <new>  // for std::nothrow

static const char* TAG = "InferenceTestUploader";

static std::string url_encode(const std::string& value) {
    std::ostringstream encoded;
    encoded << std::hex << std::uppercase;
    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0') << (int)c;
        }
    }
    return encoded.str();
}

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
    // 注意：std::vector 内部会使用默认分配器，在 ESP-IDF 中会优先使用 PSRAM（如果配置了）
    auto* heap_packet = new (std::nothrow) InferenceTestRecorder::UploadPacket(std::move(packet));
    if (!heap_packet) {
        ESP_LOGE(TAG, "Failed to allocate upload packet on heap!");
        return false;
    }
    
    if (xQueueSend(upload_queue_, &heap_packet, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Upload queue full, dropping packet (PCM: %lu, Models: %lu)",
                 (unsigned long)heap_packet->pcm_data.size(), 
                 (unsigned long)heap_packet->model_probabilities.size());
        delete heap_packet;
        return false;
    }
    
    ESP_LOGI(TAG, "📤 Packet submitted (PCM: %lu samples, Models: %lu)",
             (unsigned long)heap_packet->pcm_data.size(), 
             (unsigned long)heap_packet->model_probabilities.size());
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
            
            uint32_t pcm_samples = (uint32_t)packet->pcm_data.size();
            uint32_t audio_duration_ms = pcm_samples * 1000 / 16000;
            uint32_t pcm_bytes = pcm_samples * sizeof(int16_t);
            uint32_t pcm_kb = pcm_bytes / 1024;
            size_t num_models = packet->model_probabilities.size();
            
            ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
            ESP_LOGI(TAG, "║  📤 Inference Test Upload Started                        ║");
            ESP_LOGI(TAG, "╠══════════════════════════════════════════════════════════╣");
            ESP_LOGI(TAG, "║  Wake Word: %-43s ║", packet->wake_word.c_str());
            ESP_LOGI(TAG, "║  Detection Probability: %-31.3f ║", packet->final_probability);
            ESP_LOGI(TAG, "║  Detection Duration: %-34lu ms ║", (unsigned long)packet->duration_ms);
            ESP_LOGI(TAG, "║  Audio Duration: %-38lu ms ║", (unsigned long)audio_duration_ms);
            ESP_LOGI(TAG, "║  PCM Data: %lu samples (%lu KB)                          ║", 
                     (unsigned long)pcm_samples, (unsigned long)pcm_kb);
            ESP_LOGI(TAG, "║  Models: %-46lu ║", (unsigned long)num_models);
            for (const auto& kv : packet->model_probabilities) {
                ESP_LOGI(TAG, "║    '%s': %lu inferences                                ║",
                         kv.first.c_str(), (unsigned long)kv.second.size());
            }
            ESP_LOGI(TAG, "║  Server: %-46s ║", server_url_.c_str());
            ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
            
            bool pcm_success = true;
            bool pcm_saved = false;
            bool all_prob_success = true;
            bool all_prob_saved = true;
            
            // 上传 PCM 数据（所有模型共享）
            if (!packet->pcm_data.empty()) {
                ESP_LOGI(TAG, "📤 Uploading PCM data (%lu KB)...", (unsigned long)pcm_kb);
                pcm_success = UploadPCM(packet->pcm_data);
                if (pcm_success) {
                    ESP_LOGI(TAG, "✅ PCM upload success");
                    pcm_saved = SaveBytes();
                    if (!pcm_saved) {
                        ESP_LOGW(TAG, "⚠️ PCM save failed (upload was OK)");
                    }
                } else {
                    ESP_LOGE(TAG, "❌ PCM upload failed");
                }
            }
            
            // 逐个模型上传概率数据
            for (const auto& kv : packet->model_probabilities) {
                const std::string& model_name = kv.first;
                const std::vector<uint8_t>& probs = kv.second;
                
                if (probs.empty()) continue;
                
                ESP_LOGI(TAG, "📤 Uploading probabilities for '%s' (%lu values)...", 
                         model_name.c_str(), (unsigned long)probs.size());
                bool uploaded = UploadProbabilities(probs, model_name);
                if (uploaded) {
                    ESP_LOGI(TAG, "✅ '%s' probabilities uploaded", model_name.c_str());
                    bool saved = SaveText(model_name);
                    if (!saved) {
                        ESP_LOGW(TAG, "⚠️ '%s' probabilities save failed", model_name.c_str());
                        all_prob_saved = false;
                    }
                } else {
                    ESP_LOGE(TAG, "❌ '%s' probabilities upload failed", model_name.c_str());
                    all_prob_success = false;
                }
            }
            
            bool all_success = pcm_success && all_prob_success && pcm_saved && all_prob_saved;
            ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
            if (all_success) {
                ESP_LOGI(TAG, "║  ✅ Upload & Save Complete - SUCCESS                     ║");
            } else {
                ESP_LOGW(TAG, "║  ⚠️  Upload Complete - Partial failures                  ║");
            }
            ESP_LOGI(TAG, "║  PCM:  %-48s ║", pcm_success && pcm_saved ? "✅ OK" : "❌ FAILED");
            for (const auto& kv : packet->model_probabilities) {
                ESP_LOGI(TAG, "║  Prob [%-10s]: uploaded                               ║", kv.first.c_str());
            }
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

bool InferenceTestUploader::UploadProbabilities(const std::vector<uint8_t>& probabilities,
                                                const std::string& model_name) {
    std::string url = server_url_ + "/upload/text";
    if (!model_name.empty()) {
        url += "?model=" + url_encode(model_name);
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    for (size_t i = 0; i < probabilities.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << (static_cast<float>(probabilities[i]) / 255.0f);
    }
    std::string text = oss.str();
    
    ESP_LOGI(TAG, "📊 [%s] Probability sequence (%lu values):", 
             model_name.c_str(), (unsigned long)probabilities.size());
    
    ESP_LOGI(TAG, "   Non-zero probabilities:");
    int non_zero_count = 0;
    for (size_t i = 0; i < probabilities.size(); ++i) {
        if (probabilities[i] > 0) {
            float prob = static_cast<float>(probabilities[i]) / 255.0f;
            ESP_LOGI(TAG, "   [%3lu] %.4f (raw: %u)", (unsigned long)i, prob, probabilities[i]);
            non_zero_count++;
            if (non_zero_count >= 30) {
                ESP_LOGI(TAG, "   ... (truncated, too many non-zero values)");
                break;
            }
        }
    }
    if (non_zero_count == 0) {
        ESP_LOGI(TAG, "   (all zeros)");
    }
    
    size_t start = probabilities.size() > 20 ? probabilities.size() - 20 : 0;
    std::ostringstream last_oss;
    last_oss << std::fixed << std::setprecision(3);
    for (size_t i = start; i < probabilities.size(); ++i) {
        if (i > start) last_oss << ", ";
        last_oss << (static_cast<float>(probabilities[i]) / 255.0f);
    }
    ESP_LOGI(TAG, "   Last 20: %s", last_oss.str().c_str());
    
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

bool InferenceTestUploader::SaveBytes() {
    std::string url = server_url_ + "/save/bytes";
    
    ESP_LOGI(TAG, "💾 Saving bytes on server: %s", url.c_str());
    
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = kHttpTimeoutMs;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client for save/bytes");
        return false;
    }
    
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    
    esp_http_client_cleanup(client);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save/bytes request failed: %s", esp_err_to_name(err));
        return false;
    }
    
    if (status_code != 200) {
        ESP_LOGE(TAG, "save/bytes HTTP status %d", status_code);
        return false;
    }
    
    ESP_LOGI(TAG, "💾 Bytes saved successfully on server");
    return true;
}

bool InferenceTestUploader::SaveText(const std::string& model_name) {
    std::string url = server_url_ + "/save/text";
    if (!model_name.empty()) {
        url += "?model=" + url_encode(model_name);
    }
    
    ESP_LOGI(TAG, "💾 Saving text on server: %s", url.c_str());
    
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = kHttpTimeoutMs;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client for save/text");
        return false;
    }
    
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    
    esp_http_client_cleanup(client);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save/text request failed: %s", esp_err_to_name(err));
        return false;
    }
    
    if (status_code != 200) {
        ESP_LOGE(TAG, "save/text HTTP status %d", status_code);
        return false;
    }
    
    ESP_LOGI(TAG, "💾 Text saved successfully on server");
    return true;
}

}  // namespace micro_wake_word


