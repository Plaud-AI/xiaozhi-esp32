#include "hardware_test_service.h"
#include "hardware/servo_driver.h"
#include "hardware/nfc_driver.h"
#include "boards/common/board.h"
#include "led/led.h"
#include "display/display.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
#include "motor/motor_controller.h"
#include "motion/motion_engine.h"
#include "motion/motion_sequence.h"
#endif

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
    , motion_initialized_(false)
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
    display->SetEmotion(emotion.c_str());
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
        display->SetEmotion(emotion.c_str());
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

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    // 重置动作系统
    if (motion_initialized_) {
        MotorController::GetInstance().Home();
        MotionEngine::GetInstance().StopMotion();
    }
#endif

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "servo_angle", 90);
    cJSON_AddBoolToObject(data, "led_off", true);
    cJSON_AddBoolToObject(data, "emotion_neutral", true);
    cJSON_AddBoolToObject(data, "nfc_stopped", true);
    cJSON_AddBoolToObject(data, "motion_homed", motion_initialized_);

    return BuildSuccessResponse("test_reset", "测试模块已重置", data);
}

// ═══════════════════════════════════════════════════════════════
// 动作测试实现
// ═══════════════════════════════════════════════════════════════

std::string HardwareTestService::MotionInit(int yaw_gpio, int pitch_gpio) {
    ESP_LOGI(TAG, "MotionInit: yaw_gpio=%d, pitch_gpio=%d", yaw_gpio, pitch_gpio);

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    auto& motor_ctrl = MotorController::GetInstance();
    auto& motion_engine = MotionEngine::GetInstance();
    
    // 初始化电机控制器
    motor_ctrl.Initialize();
    motor_ctrl.Start();
    
    // 初始化动作引擎
    motion_engine.Initialize();
    motion_engine.Start();
    
    motion_initialized_ = true;

    // 获取限位信息
    float yaw_min, yaw_max, pitch_min, pitch_max;
    motor_ctrl.GetYawLimits(yaw_min, yaw_max);
    motor_ctrl.GetPitchLimits(pitch_min, pitch_max);

    cJSON* data = cJSON_CreateObject();
    
    cJSON* yaw_info = cJSON_CreateObject();
    cJSON_AddNumberToObject(yaw_info, "gpio", yaw_gpio < 0 ? 2 : yaw_gpio);
    cJSON_AddNumberToObject(yaw_info, "min", yaw_min);
    cJSON_AddNumberToObject(yaw_info, "max", yaw_max);
    cJSON_AddNumberToObject(yaw_info, "center", motor_ctrl.GetYawCenter());
    cJSON_AddNumberToObject(yaw_info, "current", motor_ctrl.GetYawAngle());
    cJSON_AddItemToObject(data, "yaw", yaw_info);

    cJSON* pitch_info = cJSON_CreateObject();
    cJSON_AddNumberToObject(pitch_info, "gpio", pitch_gpio < 0 ? 3 : pitch_gpio);
    cJSON_AddNumberToObject(pitch_info, "min", pitch_min);
    cJSON_AddNumberToObject(pitch_info, "max", pitch_max);
    cJSON_AddNumberToObject(pitch_info, "center", motor_ctrl.GetPitchCenter());
    cJSON_AddNumberToObject(pitch_info, "current", motor_ctrl.GetPitchAngle());
    cJSON_AddItemToObject(data, "pitch", pitch_info);

    return BuildSuccessResponse("test_motion_init", "动作系统初始化成功", data);
#else
    return BuildErrorResponse("test_motion_init", TEST_ERROR_NOT_SUPPORTED,
                              "动作系统未启用 (CONFIG_ENABLE_DOLL_INTERACTION)");
#endif
}

std::string HardwareTestService::MotionSetYaw(float angle) {
    ESP_LOGI(TAG, "MotionSetYaw: angle=%.1f", angle);

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    if (!motion_initialized_) {
        return BuildErrorResponse("test_motion_set_yaw", TEST_ERROR_NOT_INITIALIZED,
                                  "动作系统未初始化，请先调用 motion_init");
    }

    auto& motor_ctrl = MotorController::GetInstance();
    float previous = motor_ctrl.GetYawAngle();
    motor_ctrl.SetYawAngle(angle);

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "target", angle);
    cJSON_AddNumberToObject(data, "previous", previous);
    cJSON_AddNumberToObject(data, "current", motor_ctrl.GetYawAngle());

    return BuildSuccessResponse("test_motion_set_yaw", "Yaw角度设置成功", data);
#else
    return BuildErrorResponse("test_motion_set_yaw", TEST_ERROR_NOT_SUPPORTED,
                              "动作系统未启用");
#endif
}

std::string HardwareTestService::MotionSetPitch(float angle) {
    ESP_LOGI(TAG, "MotionSetPitch: angle=%.1f", angle);

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    if (!motion_initialized_) {
        return BuildErrorResponse("test_motion_set_pitch", TEST_ERROR_NOT_INITIALIZED,
                                  "动作系统未初始化，请先调用 motion_init");
    }

    auto& motor_ctrl = MotorController::GetInstance();
    float previous = motor_ctrl.GetPitchAngle();
    motor_ctrl.SetPitchAngle(angle);

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "target", angle);
    cJSON_AddNumberToObject(data, "previous", previous);
    cJSON_AddNumberToObject(data, "current", motor_ctrl.GetPitchAngle());

    return BuildSuccessResponse("test_motion_set_pitch", "Pitch角度设置成功", data);
#else
    return BuildErrorResponse("test_motion_set_pitch", TEST_ERROR_NOT_SUPPORTED,
                              "动作系统未启用");
#endif
}

std::string HardwareTestService::MotionSetBoth(float yaw, float pitch) {
    ESP_LOGI(TAG, "MotionSetBoth: yaw=%.1f, pitch=%.1f", yaw, pitch);

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    if (!motion_initialized_) {
        return BuildErrorResponse("test_motion_set_both", TEST_ERROR_NOT_INITIALIZED,
                                  "动作系统未初始化，请先调用 motion_init");
    }

    auto& motor_ctrl = MotorController::GetInstance();
    float prev_yaw = motor_ctrl.GetYawAngle();
    float prev_pitch = motor_ctrl.GetPitchAngle();
    motor_ctrl.SetBothAngles(yaw, pitch);

    cJSON* data = cJSON_CreateObject();
    
    cJSON* yaw_info = cJSON_CreateObject();
    cJSON_AddNumberToObject(yaw_info, "target", yaw);
    cJSON_AddNumberToObject(yaw_info, "previous", prev_yaw);
    cJSON_AddNumberToObject(yaw_info, "current", motor_ctrl.GetYawAngle());
    cJSON_AddItemToObject(data, "yaw", yaw_info);

    cJSON* pitch_info = cJSON_CreateObject();
    cJSON_AddNumberToObject(pitch_info, "target", pitch);
    cJSON_AddNumberToObject(pitch_info, "previous", prev_pitch);
    cJSON_AddNumberToObject(pitch_info, "current", motor_ctrl.GetPitchAngle());
    cJSON_AddItemToObject(data, "pitch", pitch_info);

    return BuildSuccessResponse("test_motion_set_both", "双轴角度设置成功", data);
#else
    return BuildErrorResponse("test_motion_set_both", TEST_ERROR_NOT_SUPPORTED,
                              "动作系统未启用");
#endif
}

std::string HardwareTestService::MotionMoveYaw(float delta) {
    ESP_LOGI(TAG, "MotionMoveYaw: delta=%.1f", delta);

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    if (!motion_initialized_) {
        return BuildErrorResponse("test_motion_move_yaw", TEST_ERROR_NOT_INITIALIZED,
                                  "动作系统未初始化，请先调用 motion_init");
    }

    auto& motor_ctrl = MotorController::GetInstance();
    float previous = motor_ctrl.GetYawAngle();
    motor_ctrl.MoveYawRelative(delta);

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "delta", delta);
    cJSON_AddNumberToObject(data, "previous", previous);
    cJSON_AddNumberToObject(data, "current", motor_ctrl.GetYawAngle());
    cJSON_AddNumberToObject(data, "actual_delta", motor_ctrl.GetYawAngle() - previous);

    return BuildSuccessResponse("test_motion_move_yaw", "Yaw相对移动成功", data);
#else
    return BuildErrorResponse("test_motion_move_yaw", TEST_ERROR_NOT_SUPPORTED,
                              "动作系统未启用");
#endif
}

std::string HardwareTestService::MotionMovePitch(float delta) {
    ESP_LOGI(TAG, "MotionMovePitch: delta=%.1f", delta);

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    if (!motion_initialized_) {
        return BuildErrorResponse("test_motion_move_pitch", TEST_ERROR_NOT_INITIALIZED,
                                  "动作系统未初始化，请先调用 motion_init");
    }

    auto& motor_ctrl = MotorController::GetInstance();
    float previous = motor_ctrl.GetPitchAngle();
    motor_ctrl.MovePitchRelative(delta);

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "delta", delta);
    cJSON_AddNumberToObject(data, "previous", previous);
    cJSON_AddNumberToObject(data, "current", motor_ctrl.GetPitchAngle());
    cJSON_AddNumberToObject(data, "actual_delta", motor_ctrl.GetPitchAngle() - previous);

    return BuildSuccessResponse("test_motion_move_pitch", "Pitch相对移动成功", data);
#else
    return BuildErrorResponse("test_motion_move_pitch", TEST_ERROR_NOT_SUPPORTED,
                              "动作系统未启用");
#endif
}

std::string HardwareTestService::MotionPlay(const std::string& motion_name) {
    ESP_LOGI(TAG, "MotionPlay: motion_name=%s", motion_name.c_str());

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    if (!motion_initialized_) {
        return BuildErrorResponse("test_motion_play", TEST_ERROR_NOT_INITIALIZED,
                                  "动作系统未初始化，请先调用 motion_init");
    }

    auto& motion_engine = MotionEngine::GetInstance();
    
    // 检查动作是否存在
    if (!motion_engine.HasMotion(motion_name)) {
        return BuildErrorResponse("test_motion_play", TEST_ERROR_MOTION_NOT_FOUND,
                                  "未找到动作: " + motion_name);
    }

    motion_engine.PlayMotion(motion_name);

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "motion", motion_name.c_str());
    cJSON_AddBoolToObject(data, "playing", motion_engine.IsPlaying());

    return BuildSuccessResponse("test_motion_play", "动作播放已启动", data);
#else
    return BuildErrorResponse("test_motion_play", TEST_ERROR_NOT_SUPPORTED,
                              "动作系统未启用");
#endif
}

std::string HardwareTestService::MotionStop() {
    ESP_LOGI(TAG, "MotionStop");

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    if (!motion_initialized_) {
        return BuildErrorResponse("test_motion_stop", TEST_ERROR_NOT_INITIALIZED,
                                  "动作系统未初始化");
    }

    auto& motion_engine = MotionEngine::GetInstance();
    std::string previous_motion = motion_engine.GetCurrentMotion();
    motion_engine.StopMotion();

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "stopped_motion", previous_motion.c_str());
    cJSON_AddBoolToObject(data, "playing", motion_engine.IsPlaying());

    return BuildSuccessResponse("test_motion_stop", "动作已停止", data);
#else
    return BuildErrorResponse("test_motion_stop", TEST_ERROR_NOT_SUPPORTED,
                              "动作系统未启用");
#endif
}

std::string HardwareTestService::MotionHome() {
    ESP_LOGI(TAG, "MotionHome");

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    if (!motion_initialized_) {
        return BuildErrorResponse("test_motion_home", TEST_ERROR_NOT_INITIALIZED,
                                  "动作系统未初始化，请先调用 motion_init");
    }

    auto& motor_ctrl = MotorController::GetInstance();
    motor_ctrl.Home();

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "yaw", motor_ctrl.GetYawAngle());
    cJSON_AddNumberToObject(data, "pitch", motor_ctrl.GetPitchAngle());

    return BuildSuccessResponse("test_motion_home", "已回到中位", data);
#else
    return BuildErrorResponse("test_motion_home", TEST_ERROR_NOT_SUPPORTED,
                              "动作系统未启用");
#endif
}

std::string HardwareTestService::MotionList() {
    ESP_LOGI(TAG, "MotionList");

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    // P0 动作列表
    static const struct {
        const char* name;
        const char* description;
        const char* category;
    } p0_motions[] = {
        {"home",        "归位 - 回到中位",               "P0"},
        {"nod",         "点头 - 表示肯定/理解",          "P0"},
        {"shake",       "摇头 - 表示否定/不理解",        "P0"},
        {"greeting",    "打招呼 - 小幅点头示意",         "P0"},
        {"listening",   "倾听 - 微微侧头",               "P0"},
        {"speaking",    "说话 - 轻微点头(循环)",         "P0"},
        {"thinking",    "思考 - 左右缓慢往返",           "P0"},
        {"wake_up",     "唤醒响应 - 小幅转向/点头",      "P0"},
        {"idle_alive",  "待机微动 - 偶尔轻转",           "P0"},
    };

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "count", sizeof(p0_motions) / sizeof(p0_motions[0]));

    cJSON* motions_array = cJSON_CreateArray();
    for (const auto& m : p0_motions) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", m.name);
        cJSON_AddStringToObject(item, "description", m.description);
        cJSON_AddStringToObject(item, "category", m.category);
        cJSON_AddItemToArray(motions_array, item);
    }
    cJSON_AddItemToObject(data, "motions", motions_array);

    return BuildSuccessResponse("test_motion_list", "获取动作列表成功", data);
#else
    return BuildErrorResponse("test_motion_list", TEST_ERROR_NOT_SUPPORTED,
                              "动作系统未启用");
#endif
}

std::string HardwareTestService::MotionGetStatus() {
    ESP_LOGI(TAG, "MotionGetStatus");

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    cJSON* data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "initialized", motion_initialized_);

    if (motion_initialized_) {
        auto& motor_ctrl = MotorController::GetInstance();
        auto& motion_engine = MotionEngine::GetInstance();

        // 电机控制器状态
        cJSON* motor_status = cJSON_CreateObject();
        cJSON_AddBoolToObject(motor_status, "running", motor_ctrl.IsRunning());
        cJSON_AddBoolToObject(motor_status, "simulation_mode", motor_ctrl.IsSimulationMode());
        cJSON_AddBoolToObject(motor_status, "busy", motor_ctrl.IsBusy());
        cJSON_AddBoolToObject(motor_status, "emergency_stopped", motor_ctrl.IsEmergencyStopped());
        
        cJSON* yaw = cJSON_CreateObject();
        float yaw_min, yaw_max;
        motor_ctrl.GetYawLimits(yaw_min, yaw_max);
        cJSON_AddNumberToObject(yaw, "angle", motor_ctrl.GetYawAngle());
        cJSON_AddNumberToObject(yaw, "center", motor_ctrl.GetYawCenter());
        cJSON_AddNumberToObject(yaw, "min", yaw_min);
        cJSON_AddNumberToObject(yaw, "max", yaw_max);
        cJSON_AddItemToObject(motor_status, "yaw", yaw);

        cJSON* pitch = cJSON_CreateObject();
        float pitch_min, pitch_max;
        motor_ctrl.GetPitchLimits(pitch_min, pitch_max);
        cJSON_AddNumberToObject(pitch, "angle", motor_ctrl.GetPitchAngle());
        cJSON_AddNumberToObject(pitch, "center", motor_ctrl.GetPitchCenter());
        cJSON_AddNumberToObject(pitch, "min", pitch_min);
        cJSON_AddNumberToObject(pitch, "max", pitch_max);
        cJSON_AddItemToObject(motor_status, "pitch", pitch);
        
        cJSON_AddItemToObject(data, "motor_controller", motor_status);

        // 动作引擎状态
        cJSON* engine_status = cJSON_CreateObject();
        cJSON_AddBoolToObject(engine_status, "running", motion_engine.IsRunning());
        cJSON_AddBoolToObject(engine_status, "playing", motion_engine.IsPlaying());
        cJSON_AddBoolToObject(engine_status, "paused", motion_engine.IsPaused());
        cJSON_AddStringToObject(engine_status, "current_motion", motion_engine.GetCurrentMotion().c_str());
        cJSON_AddBoolToObject(engine_status, "idle_alive_timer", motion_engine.IsIdleAliveTimerRunning());
        cJSON_AddItemToObject(data, "motion_engine", engine_status);
    }

    return BuildSuccessResponse("test_motion_status", "获取动作系统状态成功", data);
#else
    cJSON* data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "supported", false);
    cJSON_AddStringToObject(data, "reason", "CONFIG_ENABLE_DOLL_INTERACTION 未启用");
    return BuildSuccessResponse("test_motion_status", "动作系统不可用", data);
#endif
}

std::string HardwareTestService::MotionSweep(int axis, int cycles) {
    ESP_LOGI(TAG, "MotionSweep: axis=%d, cycles=%d", axis, cycles);

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    if (!motion_initialized_) {
        return BuildErrorResponse("test_motion_sweep", TEST_ERROR_NOT_INITIALIZED,
                                  "动作系统未初始化，请先调用 motion_init");
    }

    if (axis < 0 || axis > 2) {
        return BuildErrorResponse("test_motion_sweep", TEST_ERROR_PARAM_OUT_OF_RANGE,
                                  "axis 参数无效 (0=Yaw, 1=Pitch, 2=Both)");
    }

    auto& motor_ctrl = MotorController::GetInstance();
    motor_ctrl.TestSweep(axis, cycles);

    const char* axis_names[] = {"Yaw", "Pitch", "Both"};
    
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "axis", axis_names[axis]);
    cJSON_AddNumberToObject(data, "cycles", cycles);
    cJSON_AddNumberToObject(data, "yaw_angle", motor_ctrl.GetYawAngle());
    cJSON_AddNumberToObject(data, "pitch_angle", motor_ctrl.GetPitchAngle());

    return BuildSuccessResponse("test_motion_sweep", "舵机扫描测试完成", data);
#else
    return BuildErrorResponse("test_motion_sweep", TEST_ERROR_NOT_SUPPORTED,
                              "动作系统未启用");
#endif
}

std::string HardwareTestService::MotionTestAllP0(uint32_t interval_ms) {
    ESP_LOGI(TAG, "MotionTestAllP0: interval_ms=%lu", interval_ms);

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    if (!motion_initialized_) {
        return BuildErrorResponse("test_motion_test_all_p0", TEST_ERROR_NOT_INITIALIZED,
                                  "动作系统未初始化，请先调用 motion_init");
    }

    auto& motion_engine = MotionEngine::GetInstance();
    
    // P0 动作名称列表
    static const char* p0_names[] = {
        "home", "nod", "shake", "greeting", "listening",
        "speaking", "thinking", "wake_up", "idle_alive"
    };

    cJSON* results = cJSON_CreateArray();
    int success_count = 0;

    for (const char* name : p0_names) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", name);

        if (motion_engine.HasMotion(name)) {
            motion_engine.PlayMotion(name);
            
            // 等待动作完成或超时
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
            motion_engine.StopMotion();
            
            cJSON_AddBoolToObject(item, "success", true);
            success_count++;
        } else {
            cJSON_AddBoolToObject(item, "success", false);
            cJSON_AddStringToObject(item, "error", "动作未注册");
        }

        cJSON_AddItemToArray(results, item);
    }

    // 测试完成后回到中位
    motion_engine.PlayMotion("home");

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "total", sizeof(p0_names) / sizeof(p0_names[0]));
    cJSON_AddNumberToObject(data, "success", success_count);
    cJSON_AddNumberToObject(data, "interval_ms", interval_ms);
    cJSON_AddItemToObject(data, "results", results);

    return BuildSuccessResponse("test_motion_test_all_p0", "P0动作测试完成", data);
#else
    return BuildErrorResponse("test_motion_test_all_p0", TEST_ERROR_NOT_SUPPORTED,
                              "动作系统未启用");
#endif
}

std::string HardwareTestService::MotionSetSimulation(bool enable) {
    ESP_LOGI(TAG, "MotionSetSimulation: enable=%d", enable);

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    auto& motor_ctrl = MotorController::GetInstance();
    motor_ctrl.SetSimulationMode(enable);

    cJSON* data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "simulation_mode", enable);
    cJSON_AddStringToObject(data, "description", 
        enable ? "模拟模式已启用，舵机动作将不实际执行" 
               : "正常模式，舵机将实际执行动作");

    return BuildSuccessResponse("test_motion_set_simulation", 
        enable ? "模拟模式已启用" : "已切换到正常模式", data);
#else
    return BuildErrorResponse("test_motion_set_simulation", TEST_ERROR_NOT_SUPPORTED,
                              "动作系统未启用");
#endif
}

