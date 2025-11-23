#include "doll_service.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "nfc_reader.h"
#include "pressure_sensor.h"
#include "touch_sensor.h"

#define TAG "DollService"

DollService& DollService::GetInstance() {
    static DollService instance;
    return instance;
}

DollService::DollService()
    : running_(false),
      doll_present_(false),
      current_touch_(TouchPosition::kTouchNone),
      detection_task_handle_(nullptr) {
}

DollService::~DollService() {
    Stop();
}

void DollService::Initialize() {
    ESP_LOGI(TAG, "Initializing DollService...");

    // 创建硬件接口实例（当前为模拟实现）
    nfc_reader_ = std::make_unique<NfcReader>();
    pressure_sensor_ = std::make_unique<PressureSensor>();
    touch_sensor_ = std::make_unique<TouchSensor>();

    // 初始化硬件
    nfc_reader_->Initialize();
    pressure_sensor_->Initialize();
    touch_sensor_->Initialize();

    ESP_LOGI(TAG, "DollService initialized (simulation mode)");
}

void DollService::Start() {
    if (running_) {
        ESP_LOGW(TAG, "DollService already running");
        return;
    }

    ESP_LOGI(TAG, "Starting DollService...");
    running_ = true;

    // 创建检测任务
    xTaskCreate(DetectionTask, "doll_detect", 4096, this, 5, &detection_task_handle_);

    ESP_LOGI(TAG, "DollService started");
}

void DollService::Stop() {
    if (!running_) {
        return;
    }

    ESP_LOGI(TAG, "Stopping DollService...");
    running_ = false;

    if (detection_task_handle_) {
        vTaskDelete(detection_task_handle_);
        detection_task_handle_ = nullptr;
    }

    ESP_LOGI(TAG, "DollService stopped");
}

bool DollService::IsDollPresent() const {
    return doll_present_;
}

std::string DollService::GetCurrentDollId() const {
    return current_doll_id_;
}

bool DollService::IsTouched() const {
    return current_touch_ != TouchPosition::kTouchNone;
}

TouchPosition DollService::GetTouchPosition() const {
    return current_touch_;
}

void DollService::SetEventCallbacks(const DollEventCallbacks& callbacks) {
    callbacks_ = callbacks;
}

void DollService::SimulateDollPlaced(const std::string& doll_id) {
    ESP_LOGI(TAG, "Simulating doll placed: %s", doll_id.c_str());
    HandleDollPlaced(doll_id);
}

void DollService::SimulateDollRemoved() {
    ESP_LOGI(TAG, "Simulating doll removed");
    HandleDollRemoved();
}

void DollService::SimulateTouch(TouchPosition pos) {
    ESP_LOGI(TAG, "Simulating touch at position: %d", static_cast<int>(pos));
    HandleTouched(pos);
}

bool DollService::RetryNfcRead() {
    if (!nfc_reader_) {
        return false;
    }

    ESP_LOGI(TAG, "Retrying NFC read...");
    std::string doll_id;
    if (nfc_reader_->ReadTag(doll_id)) {
        HandleDollPlaced(doll_id);
        return true;
    }
    return false;
}

void DollService::DetectionTask(void* param) {
    DollService* service = static_cast<DollService*>(param);
    service->RunDetectionLoop();
    vTaskDelete(nullptr);
}

void DollService::RunDetectionLoop() {
    ESP_LOGI(TAG, "Detection loop started");

    while (running_) {
        // TODO: 实际硬件到位后实现以下逻辑
        
        // 1. 检测压力传感器
        bool pressure_detected = pressure_sensor_->IsPressed();
        
        if (pressure_detected && !doll_present_) {
            // 手办放置 -> 读取 NFC
            std::string doll_id;
            if (nfc_reader_->ReadTag(doll_id)) {
                HandleDollPlaced(doll_id);
            } else {
                ESP_LOGW(TAG, "Failed to read NFC tag");
                // TODO: 实现重试机制
            }
        } else if (!pressure_detected && doll_present_) {
            // 手办移除
            HandleDollRemoved();
        }

        // 2. 检测触摸传感器
        TouchPosition touch_pos = touch_sensor_->GetTouchPosition();
        if (touch_pos != TouchPosition::kTouchNone && touch_pos != current_touch_) {
            HandleTouched(touch_pos);
        } else if (touch_pos == TouchPosition::kTouchNone && current_touch_ != TouchPosition::kTouchNone) {
            current_touch_ = TouchPosition::kTouchNone;
        }

        // 降低轮询频率
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Detection loop stopped");
}

void DollService::HandleDollPlaced(const std::string& doll_id) {
    if (doll_present_ && current_doll_id_ == doll_id) {
        return; // 重复事件
    }

    current_doll_id_ = doll_id;
    doll_present_ = true;

    ESP_LOGI(TAG, "Doll placed: %s", doll_id.c_str());

    // 触发回调
    if (callbacks_.on_doll_placed) {
        callbacks_.on_doll_placed(doll_id);
    }
}

void DollService::HandleDollRemoved() {
    if (!doll_present_) {
        return; // 重复事件
    }

    ESP_LOGI(TAG, "Doll removed: %s", current_doll_id_.c_str());

    current_doll_id_.clear();
    doll_present_ = false;

    // 触发回调
    if (callbacks_.on_doll_removed) {
        callbacks_.on_doll_removed();
    }
}

void DollService::HandleTouched(TouchPosition pos) {
    current_touch_ = pos;

    ESP_LOGI(TAG, "Touch detected at position: %d", static_cast<int>(pos));

    // 触发回调
    if (callbacks_.on_touched) {
        callbacks_.on_touched(pos);
    }
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION

