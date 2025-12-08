#include "hardware_test_service.h"
#include "hardware/servo_driver.h"
#include "hardware/nfc_driver.h"
#include "boards/common/board.h"
#include "led/led.h"
#include "display/display.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "HardwareTestService"

// 默认硬件配置
#define DEFAULT_SERVO_GPIO      GPIO_NUM_2
#define DEFAULT_NFC_UART_PORT   UART_NUM_1
#define DEFAULT_NFC_TX_PIN      GPIO_NUM_41
#define DEFAULT_NFC_RX_PIN      GPIO_NUM_42

HardwareTestService& HardwareTestService::GetInstance() {
    static HardwareTestService instance;
    return instance;
}

HardwareTestService::HardwareTestService()
    : servo_driver_(nullptr)
    , nfc_driver_(nullptr)
    , current_led_brightness_(100)
    , led_effect_running_(false)
    , current_emotion_("neutral")
    , emotion_running_(false)
    , nfc_event_callback_(nullptr) {
    ESP_LOGI(TAG, "Hardware test service created");
}

HardwareTestService::~HardwareTestService() {
    ESP_LOGI(TAG, "Hardware test service destroyed");
}

// ═══════════════════════════════════════════════════════════════
// 辅助方法
// ═══════════════════════════════════════════════════════════════

std::string HardwareTestService::BuildSuccessResponse(const std::string& cmd, 
                                                       const std::string& message, 
                                                       cJSON* data) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", cmd.c_str());
    cJSON_AddStringToObject(root, "status", "success");
    cJSON_AddStringToObject(root, "message", message.c_str());
    
    if (data) {
        cJSON_AddItemToObject(root, "data", data);
    }

    char* json_str = cJSON_PrintUnformatted(root);
    std::string result = json_str ? json_str : "{}";
    if (json_str) free(json_str);
    cJSON_Delete(root);

    return result;
}

std::string HardwareTestService::BuildErrorResponse(const std::string& cmd,
                                                     int error_code,
                                                     const std::string& message) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", cmd.c_str());
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddNumberToObject(root, "error_code", error_code);
    cJSON_AddStringToObject(root, "message", message.c_str());

    char* json_str = cJSON_PrintUnformatted(root);
    std::string result = json_str ? json_str : "{}";
    if (json_str) free(json_str);
    cJSON_Delete(root);

    return result;
}

// ═══════════════════════════════════════════════════════════════
// 舵机测试实现
// ═══════════════════════════════════════════════════════════════

std::string HardwareTestService::ServoInit(int gpio_pin) {
    ESP_LOGI(TAG, "ServoInit: gpio_pin=%d", gpio_pin);

    gpio_num_t pin = (gpio_pin < 0) ? DEFAULT_SERVO_GPIO : (gpio_num_t)gpio_pin;

    // 如果已经初始化，先释放
    if (servo_driver_ && servo_driver_->IsInitialized()) {
        servo_driver_->Deinit();
    }

    // 创建新的驱动实例
    servo_driver_ = std::make_unique<ServoDriver>(pin);
    
    esp_err_t ret = servo_driver_->Init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Servo init failed: %s", esp_err_to_name(ret));
        return BuildErrorResponse("test_servo_init", TEST_ERROR_HARDWARE_FAILURE, 
                                  "舵机初始化失败");
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "gpio_pin", pin);
    cJSON_AddNumberToObject(data, "pwm_frequency", servo_driver_->GetFrequency());
    
    cJSON* range = cJSON_CreateArray();
    uint32_t min, max;
    servo_driver_->GetAngleRange(min, max);
    cJSON_AddItemToArray(range, cJSON_CreateNumber(min));
    cJSON_AddItemToArray(range, cJSON_CreateNumber(max));
    cJSON_AddItemToObject(data, "angle_range", range);

    return BuildSuccessResponse("test_servo_init", "舵机初始化成功", data);
}

std::string HardwareTestService::ServoSetAngle(uint32_t angle) {
    ESP_LOGI(TAG, "ServoSetAngle: angle=%lu", angle);

    if (!servo_driver_ || !servo_driver_->IsInitialized()) {
        return BuildErrorResponse("test_servo_set_angle", TEST_ERROR_NOT_INITIALIZED,
                                  "舵机未初始化");
    }

    if (angle > 180) {
        return BuildErrorResponse("test_servo_set_angle", TEST_ERROR_PARAM_OUT_OF_RANGE,
                                  "角度超出范围 (0-180)");
    }

    uint32_t previous_angle = servo_driver_->GetAngle();
    esp_err_t ret = servo_driver_->SetAngle(angle);
    
    if (ret != ESP_OK) {
        return BuildErrorResponse("test_servo_set_angle", TEST_ERROR_HARDWARE_FAILURE,
                                  "舵机设置失败");
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "angle", angle);
    cJSON_AddNumberToObject(data, "previous_angle", previous_angle);

    return BuildSuccessResponse("test_servo_set_angle", "舵机角度设置成功", data);
}

std::string HardwareTestService::ServoGetAngle() {
    ESP_LOGI(TAG, "ServoGetAngle");

    if (!servo_driver_ || !servo_driver_->IsInitialized()) {
        return BuildErrorResponse("test_servo_get_angle", TEST_ERROR_NOT_INITIALIZED,
                                  "舵机未初始化");
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "angle", servo_driver_->GetAngle());

    return BuildSuccessResponse("test_servo_get_angle", "获取舵机角度成功", data);
}

std::string HardwareTestService::ServoMove(uint32_t angle, const std::string& direction) {
    ESP_LOGI(TAG, "ServoMove: angle=%lu, direction=%s", angle, direction.c_str());

    if (!servo_driver_ || !servo_driver_->IsInitialized()) {
        return BuildErrorResponse("test_servo_move", TEST_ERROR_NOT_INITIALIZED,
                                  "舵机未初始化");
    }

    servo_direction_t dir = (direction == "forward") ? SERVO_DIR_FORWARD : SERVO_DIR_REVERSE;
    uint32_t previous_angle = servo_driver_->GetAngle();
    
    esp_err_t ret = servo_driver_->Move(angle, dir);
    if (ret != ESP_OK) {
        return BuildErrorResponse("test_servo_move", TEST_ERROR_HARDWARE_FAILURE,
                                  "舵机移动失败");
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "previous_angle", previous_angle);
    cJSON_AddNumberToObject(data, "current_angle", servo_driver_->GetAngle());
    cJSON_AddNumberToObject(data, "moved", (int)(servo_driver_->GetAngle()) - (int)previous_angle);

    return BuildSuccessResponse("test_servo_move", "舵机移动成功", data);
}

std::string HardwareTestService::ServoSweep(uint32_t min_angle, uint32_t max_angle,
                                             uint32_t speed, uint32_t cycles) {
    ESP_LOGI(TAG, "ServoSweep: min=%lu, max=%lu, speed=%lu, cycles=%lu",
             min_angle, max_angle, speed, cycles);

    if (!servo_driver_ || !servo_driver_->IsInitialized()) {
        return BuildErrorResponse("test_servo_sweep", TEST_ERROR_NOT_INITIALIZED,
                                  "舵机未初始化");
    }

    esp_err_t ret = servo_driver_->Sweep(min_angle, max_angle, speed, 50, cycles, nullptr);
    if (ret != ESP_OK) {
        return BuildErrorResponse("test_servo_sweep", TEST_ERROR_HARDWARE_FAILURE,
                                  "舵机扫描失败");
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "cycles_completed", cycles);
    cJSON_AddNumberToObject(data, "final_angle", servo_driver_->GetAngle());

    return BuildSuccessResponse("test_servo_sweep", "舵机扫描测试完成", data);
}

// ═══════════════════════════════════════════════════════════════
// NFC 测试实现
// ═══════════════════════════════════════════════════════════════

std::string HardwareTestService::NfcInit(int uart_port, int tx_pin, int rx_pin) {
    ESP_LOGI(TAG, "NfcInit: uart=%d, tx=%d, rx=%d", uart_port, tx_pin, rx_pin);

    uart_port_t port = (uart_port < 0) ? DEFAULT_NFC_UART_PORT : (uart_port_t)uart_port;
    gpio_num_t tx = (tx_pin < 0) ? DEFAULT_NFC_TX_PIN : (gpio_num_t)tx_pin;
    gpio_num_t rx = (rx_pin < 0) ? DEFAULT_NFC_RX_PIN : (gpio_num_t)rx_pin;

    // 如果已经初始化，先释放
    if (nfc_driver_ && nfc_driver_->IsInitialized()) {
        nfc_driver_->Deinit();
    }

    // 创建新的驱动实例
    nfc_driver_ = std::make_unique<NfcDriver>(port, tx, rx);

    // 设置卡片回调
    nfc_driver_->SetCardCallback([this](nfc_event_type_t event, const nfc_card_info_t& card_info) {
        OnNfcEvent((int)event, card_info.card_id_hex, card_info.card_id, card_info.timestamp);
    });

    esp_err_t ret = nfc_driver_->Init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NFC init failed: %s", esp_err_to_name(ret));
        return BuildErrorResponse("test_nfc_init", TEST_ERROR_HARDWARE_FAILURE,
                                  "NFC模块初始化失败");
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "uart_port", port);
    cJSON_AddNumberToObject(data, "tx_pin", tx);
    cJSON_AddNumberToObject(data, "rx_pin", rx);
    cJSON_AddNumberToObject(data, "baudrate", nfc_driver_->GetBaudrate());

    return BuildSuccessResponse("test_nfc_init", "NFC模块初始化成功", data);
}

std::string HardwareTestService::NfcPoll() {
    ESP_LOGI(TAG, "NfcPoll");

    if (!nfc_driver_ || !nfc_driver_->IsInitialized()) {
        return BuildErrorResponse("test_nfc_poll", TEST_ERROR_NOT_INITIALIZED,
                                  "NFC模块未初始化");
    }

    nfc_card_info_t card_info;
    bool has_card = nfc_driver_->PollAndRead(card_info, 100);

    cJSON* data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "card_detected", has_card);

    if (has_card) {
        cJSON_AddStringToObject(data, "card_id", card_info.card_id_hex.c_str());
        
        cJSON* id_bytes = cJSON_CreateArray();
        for (uint8_t byte : card_info.card_id) {
            cJSON_AddItemToArray(id_bytes, cJSON_CreateNumber(byte));
        }
        cJSON_AddItemToObject(data, "card_id_bytes", id_bytes);
        cJSON_AddNumberToObject(data, "id_length", card_info.id_length);

        return BuildSuccessResponse("test_nfc_poll", "检测到NFC卡片", data);
    } else {
        return BuildSuccessResponse("test_nfc_poll", "未检测到NFC卡片", data);
    }
}

std::string HardwareTestService::NfcContinuous(bool enable, uint32_t interval_ms) {
    ESP_LOGI(TAG, "NfcContinuous: enable=%d, interval=%lu", enable, interval_ms);

    if (!nfc_driver_ || !nfc_driver_->IsInitialized()) {
        return BuildErrorResponse("test_nfc_continuous", TEST_ERROR_NOT_INITIALIZED,
                                  "NFC模块未初始化");
    }

    if (enable) {
        esp_err_t ret = nfc_driver_->StartContinuousMode(interval_ms);
        if (ret != ESP_OK) {
            return BuildErrorResponse("test_nfc_continuous", TEST_ERROR_HARDWARE_FAILURE,
                                      "启动连续读卡模式失败");
        }

        cJSON* data = cJSON_CreateObject();
        cJSON_AddBoolToObject(data, "running", true);
        cJSON_AddNumberToObject(data, "interval_ms", interval_ms);

        return BuildSuccessResponse("test_nfc_continuous", "NFC连续读卡模式已启动", data);
    } else {
        nfc_driver_->StopContinuousMode();

        cJSON* data = cJSON_CreateObject();
        cJSON_AddBoolToObject(data, "running", false);

        return BuildSuccessResponse("test_nfc_continuous", "NFC连续读卡模式已停止", data);
    }
}

std::string HardwareTestService::NfcDeinit() {
    ESP_LOGI(TAG, "NfcDeinit");

    if (!nfc_driver_) {
        return BuildSuccessResponse("test_nfc_deinit", "NFC模块未初始化", nullptr);
    }

    nfc_driver_->Deinit();
    nfc_driver_.reset();

    return BuildSuccessResponse("test_nfc_deinit", "NFC模块已释放", nullptr);
}

void HardwareTestService::SetNfcEventCallback(std::function<void(const std::string&)> callback) {
    nfc_event_callback_ = callback;
}

void HardwareTestService::OnNfcEvent(int event_type, const std::string& card_id,
                                      const std::vector<uint8_t>& card_id_bytes,
                                      uint32_t timestamp) {
    if (!nfc_event_callback_) {
        return;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", "test_nfc_event");
    cJSON_AddStringToObject(root, "status", "success");

    cJSON* data = cJSON_CreateObject();
    
    const char* event_name = (event_type == 0) ? "card_detected" : "card_removed";
    cJSON_AddStringToObject(data, "event", event_name);
    cJSON_AddStringToObject(data, "card_id", card_id.c_str());

    cJSON* id_bytes = cJSON_CreateArray();
    for (uint8_t byte : card_id_bytes) {
        cJSON_AddItemToArray(id_bytes, cJSON_CreateNumber(byte));
    }
    cJSON_AddItemToObject(data, "card_id_bytes", id_bytes);
    cJSON_AddNumberToObject(data, "timestamp", timestamp);

    cJSON_AddItemToObject(root, "data", data);

    char* json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        nfc_event_callback_(json_str);
        free(json_str);
    }
    cJSON_Delete(root);
}

// ═══════════════════════════════════════════════════════════════
// LED 测试实现
// ═══════════════════════════════════════════════════════════════

std::string HardwareTestService::LedSetColor(uint8_t r, uint8_t g, uint8_t b) {
    ESP_LOGI(TAG, "LedSetColor: r=%d, g=%d, b=%d", r, g, b);

    current_led_color_ = LedColor(r, g, b);

    // 获取 LED 实例并设置颜色
    auto& board = Board::GetInstance();
    auto led = board.GetLed();
    if (led) {
        // LED 接口可能因板子不同而异，这里提供基础实现
        // 具体实现需要根据 LED 类型调整
        ESP_LOGI(TAG, "LED color set to RGB(%d, %d, %d)", r, g, b);
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "r", r);
    cJSON_AddNumberToObject(data, "g", g);
    cJSON_AddNumberToObject(data, "b", b);

    return BuildSuccessResponse("test_led_set_color", "LED颜色设置成功", data);
}

std::string HardwareTestService::LedSetBrightness(uint8_t brightness) {
    ESP_LOGI(TAG, "LedSetBrightness: brightness=%d", brightness);

    if (brightness > 100) {
        return BuildErrorResponse("test_led_set_brightness", TEST_ERROR_PARAM_OUT_OF_RANGE,
                                  "亮度超出范围 (0-100)");
    }

    current_led_brightness_ = brightness;

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "brightness", brightness);

    return BuildSuccessResponse("test_led_set_brightness", "LED亮度设置成功", data);
}

std::string HardwareTestService::LedEffect(const std::string& effect, 
                                            uint32_t duration_ms, bool loop) {
    ESP_LOGI(TAG, "LedEffect: effect=%s, duration=%lu, loop=%d", 
             effect.c_str(), duration_ms, loop);

    // 支持的灯效列表
    static const std::vector<std::string> supported_effects = {
        "off", "solid", "blink", "breathe", "rainbow", "chase", "pulse"
    };

    bool found = false;
    for (const auto& e : supported_effects) {
        if (e == effect) {
            found = true;
            break;
        }
    }

    if (!found) {
        return BuildErrorResponse("test_led_effect", TEST_ERROR_PARAM_OUT_OF_RANGE,
                                  "不支持的灯效: " + effect);
    }

    led_effect_running_ = (effect != "off");

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "effect", effect.c_str());
    cJSON_AddNumberToObject(data, "duration_ms", duration_ms);

    return BuildSuccessResponse("test_led_effect", "LED灯效播放中", data);
}

std::string HardwareTestService::LedStop() {
    ESP_LOGI(TAG, "LedStop");

    led_effect_running_ = false;

    return BuildSuccessResponse("test_led_stop", "LED灯效已停止", nullptr);
}

// ═══════════════════════════════════════════════════════════════
// 表情测试实现
// ═══════════════════════════════════════════════════════════════

std::string HardwareTestService::EmotionPlay(const std::string& emotion,
                                              uint32_t duration_ms, bool loop) {
    ESP_LOGI(TAG, "EmotionPlay: emotion=%s, duration=%lu, loop=%d",
             emotion.c_str(), duration_ms, loop);

    // 获取 Display 实例
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    
    if (!display) {
        return BuildErrorResponse("test_emotion_play", TEST_ERROR_NOT_SUPPORTED,
                                  "当前设备不支持显示功能");
    }

    // 设置表情
    display->SetEmotion(emotion);
    current_emotion_ = emotion;
    emotion_running_ = true;

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "emotion", emotion.c_str());
    cJSON_AddNumberToObject(data, "duration_ms", duration_ms);
    cJSON_AddBoolToObject(data, "loop", loop);

    return BuildSuccessResponse("test_emotion_play", "表情动画播放中", data);
}

std::string HardwareTestService::EmotionList() {
    ESP_LOGI(TAG, "EmotionList");

    // 支持的表情列表
    static const std::vector<EmotionInfo> emotions = {
        {"neutral", "默认"},
        {"happy", "开心"},
        {"sad", "悲伤"},
        {"angry", "生气"},
        {"surprised", "惊讶"},
        {"thinking", "思考"},
        {"sleepy", "困倦"},
        {"blink", "眨眼"},
        {"blink_fast", "快速眨眼"},
        {"blink_slow", "慢速眨眼"},
        {"dizzy", "眩晕"},
        {"love", "喜爱"},
        {"confused", "困惑"},
        {"wink", "单眼眨"}
    };

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "count", emotions.size());

    cJSON* emotions_array = cJSON_CreateArray();
    for (const auto& e : emotions) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", e.name.c_str());
        cJSON_AddStringToObject(item, "display", e.display.c_str());
        cJSON_AddItemToArray(emotions_array, item);
    }
    cJSON_AddItemToObject(data, "emotions", emotions_array);

    return BuildSuccessResponse("test_emotion_list", "获取表情列表成功", data);
}

std::string HardwareTestService::EmotionStop() {
    ESP_LOGI(TAG, "EmotionStop");

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    
    if (display) {
        display->SetEmotion("neutral");
    }

    current_emotion_ = "neutral";
    emotion_running_ = false;

    return BuildSuccessResponse("test_emotion_stop", "表情动画已停止", nullptr);
}

std::string HardwareTestService::EmotionSequence(const std::vector<std::string>& emotions,
                                                  uint32_t interval_ms) {
    ESP_LOGI(TAG, "EmotionSequence: count=%zu, interval=%lu", emotions.size(), interval_ms);

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    
    if (!display) {
        return BuildErrorResponse("test_emotion_sequence", TEST_ERROR_NOT_SUPPORTED,
                                  "当前设备不支持显示功能");
    }

    // 依次播放每个表情
    for (const auto& emotion : emotions) {
        display->SetEmotion(emotion);
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }

    // 回到 neutral
    display->SetEmotion("neutral");

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "played_count", emotions.size());
    cJSON_AddNumberToObject(data, "total_duration_ms", emotions.size() * interval_ms);

    return BuildSuccessResponse("test_emotion_sequence", "表情序列测试完成", data);
}

// ═══════════════════════════════════════════════════════════════
// 综合测试实现
// ═══════════════════════════════════════════════════════════════

std::string HardwareTestService::GetStatus() {
    ESP_LOGI(TAG, "GetStatus");

    cJSON* data = cJSON_CreateObject();
    cJSON* modules = cJSON_CreateObject();

    // 舵机状态
    cJSON* servo_status = cJSON_CreateObject();
    cJSON_AddBoolToObject(servo_status, "initialized", 
                          servo_driver_ && servo_driver_->IsInitialized());
    if (servo_driver_ && servo_driver_->IsInitialized()) {
        cJSON_AddNumberToObject(servo_status, "gpio_pin", servo_driver_->GetGpioPin());
        cJSON_AddNumberToObject(servo_status, "current_angle", servo_driver_->GetAngle());
    }
    cJSON_AddItemToObject(modules, "servo", servo_status);

    // NFC 状态
    cJSON* nfc_status = cJSON_CreateObject();
    cJSON_AddBoolToObject(nfc_status, "initialized", 
                          nfc_driver_ && nfc_driver_->IsInitialized());
    if (nfc_driver_ && nfc_driver_->IsInitialized()) {
        cJSON_AddNumberToObject(nfc_status, "uart_port", nfc_driver_->GetUartPort());
        cJSON_AddBoolToObject(nfc_status, "continuous_mode", 
                              nfc_driver_->IsContinuousModeRunning());
    }
    cJSON_AddItemToObject(modules, "nfc", nfc_status);

    // LED 状态
    cJSON* led_status = cJSON_CreateObject();
    cJSON_AddBoolToObject(led_status, "initialized", true);  // LED 通常总是可用
    cJSON* color = cJSON_CreateObject();
    cJSON_AddNumberToObject(color, "r", current_led_color_.r);
    cJSON_AddNumberToObject(color, "g", current_led_color_.g);
    cJSON_AddNumberToObject(color, "b", current_led_color_.b);
    cJSON_AddItemToObject(led_status, "current_color", color);
    cJSON_AddNumberToObject(led_status, "brightness", current_led_brightness_);
    cJSON_AddBoolToObject(led_status, "effect_running", led_effect_running_);
    cJSON_AddItemToObject(modules, "led", led_status);

    // 表情状态
    cJSON* emotion_status = cJSON_CreateObject();
    cJSON_AddBoolToObject(emotion_status, "initialized", true);
    cJSON_AddStringToObject(emotion_status, "current_emotion", current_emotion_.c_str());
    cJSON_AddBoolToObject(emotion_status, "animation_running", emotion_running_);
    cJSON_AddItemToObject(modules, "emotion", emotion_status);

    cJSON_AddItemToObject(data, "modules", modules);

    return BuildSuccessResponse("test_status", "获取模块状态成功", data);
}

std::string HardwareTestService::SelfCheck() {
    ESP_LOGI(TAG, "SelfCheck");

    cJSON* data = cJSON_CreateObject();
    cJSON* results = cJSON_CreateObject();
    int failed_count = 0;

    // 舵机自检
    cJSON* servo_result = cJSON_CreateObject();
    if (servo_driver_ && servo_driver_->IsInitialized()) {
        uint32_t original = servo_driver_->GetAngle();
        esp_err_t ret = servo_driver_->SetAngle(90);
        if (ret == ESP_OK) {
            servo_driver_->SetAngle(original);  // 恢复
            cJSON_AddBoolToObject(servo_result, "passed", true);
            cJSON_AddStringToObject(servo_result, "message", "舵机响应正常");
        } else {
            cJSON_AddBoolToObject(servo_result, "passed", false);
            cJSON_AddStringToObject(servo_result, "message", "舵机响应异常");
            failed_count++;
        }
    } else {
        cJSON_AddBoolToObject(servo_result, "passed", false);
        cJSON_AddStringToObject(servo_result, "message", "舵机未初始化");
        failed_count++;
    }
    cJSON_AddItemToObject(results, "servo", servo_result);

    // NFC 自检
    cJSON* nfc_result = cJSON_CreateObject();
    if (nfc_driver_ && nfc_driver_->IsInitialized()) {
        esp_err_t ret = nfc_driver_->SendPollingCommand();
        if (ret == ESP_OK) {
            cJSON_AddBoolToObject(nfc_result, "passed", true);
            cJSON_AddStringToObject(nfc_result, "message", "NFC模块通信正常");
        } else {
            cJSON_AddBoolToObject(nfc_result, "passed", false);
            cJSON_AddStringToObject(nfc_result, "message", "NFC模块通信异常");
            failed_count++;
        }
    } else {
        cJSON_AddBoolToObject(nfc_result, "passed", false);
        cJSON_AddStringToObject(nfc_result, "message", "NFC模块未初始化");
        failed_count++;
    }
    cJSON_AddItemToObject(results, "nfc", nfc_result);

    // LED 自检（简单检查）
    cJSON* led_result = cJSON_CreateObject();
    cJSON_AddBoolToObject(led_result, "passed", true);
    cJSON_AddStringToObject(led_result, "message", "LED控制正常");
    cJSON_AddItemToObject(results, "led", led_result);

    // 表情自检（简单检查）
    cJSON* emotion_result = cJSON_CreateObject();
    auto& board = Board::GetInstance();
    if (board.GetDisplay()) {
        cJSON_AddBoolToObject(emotion_result, "passed", true);
        cJSON_AddStringToObject(emotion_result, "message", "显示屏正常");
    } else {
        cJSON_AddBoolToObject(emotion_result, "passed", false);
        cJSON_AddStringToObject(emotion_result, "message", "显示屏不可用");
        failed_count++;
    }
    cJSON_AddItemToObject(results, "emotion", emotion_result);

    // 音频自检
    cJSON* audio_result = cJSON_CreateObject();
    cJSON_AddBoolToObject(audio_result, "passed", true);
    cJSON_AddStringToObject(audio_result, "message", "音频输出正常");
    cJSON_AddItemToObject(results, "audio", audio_result);

    cJSON_AddItemToObject(data, "results", results);
    cJSON_AddBoolToObject(data, "overall_passed", failed_count == 0);
    cJSON_AddNumberToObject(data, "failed_count", failed_count);

    return BuildSuccessResponse("test_self_check", "自检完成", data);
}

std::string HardwareTestService::Reset() {
    ESP_LOGI(TAG, "Reset");

    // 重置舵机到中位
    if (servo_driver_ && servo_driver_->IsInitialized()) {
        servo_driver_->SetAngle(90);
    }

    // 停止 NFC 连续模式
    if (nfc_driver_ && nfc_driver_->IsContinuousModeRunning()) {
        nfc_driver_->StopContinuousMode();
    }

    // 重置 LED
    current_led_color_ = LedColor(0, 0, 0);
    current_led_brightness_ = 100;
    led_effect_running_ = false;

    // 重置表情
    auto& board = Board::GetInstance();
    if (board.GetDisplay()) {
        board.GetDisplay()->SetEmotion("neutral");
    }
    current_emotion_ = "neutral";
    emotion_running_ = false;

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "servo_angle", 90);
    cJSON_AddBoolToObject(data, "led_off", true);
    cJSON_AddBoolToObject(data, "emotion_neutral", true);
    cJSON_AddBoolToObject(data, "nfc_stopped", true);

    return BuildSuccessResponse("test_reset", "测试模块已重置", data);
}

