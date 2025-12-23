#include "emotion_display.h"
#include "emotion_display_factory.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "EmotionDisplayExample";

using namespace display;
using namespace lottie;

/**
 * @file emotion_display_example.cc
 * @brief EmotionDisplay 使用示例
 * 
 * 展示如何使用 EmotionDisplay 系统
 */

// ============================================================================
// 示例 1: 基础使用
// ============================================================================

void example_basic_usage() {
    ESP_LOGI(TAG, "=== Example: Basic Usage ===");

    // 创建 EmotionDisplay（假设 LVGL 已初始化）
    EmotionDisplay* display = CreateEmotionDisplay(240, 240);

    // 初始化情感系统
    if (!display->InitEmotionSystem("/spiffs/emotions/")) {
        ESP_LOGE(TAG, "Failed to initialize emotion system");
        return;
    }

    // 显示开心表情
    display->ShowEmotion(EmotionType::HAPPY);

    // 等待 3 秒
    vTaskDelay(pdMS_TO_TICKS(3000));

    // 显示难过表情
    display->ShowEmotion(EmotionType::SAD);

    ESP_LOGI(TAG, "Basic usage example complete");
}

// ============================================================================
// 示例 2: 带顶部状态栏
// ============================================================================

void example_with_topbar() {
    ESP_LOGI(TAG, "=== Example: With Top Bar ===");

    EmotionDisplay* display = CreateEmotionDisplay(240, 240);
    display->InitEmotionSystem("/spiffs/emotions/");

    // 显示开心表情，同时在顶部显示文字
    display->ShowEmotion(EmotionType::HAPPY);
    display->ShowTopBar(nullptr, "很高兴见到你！", 3000);

    vTaskDelay(pdMS_TO_TICKS(3500));

    // 显示思考表情，顶部显示加载提示
    display->ShowEmotion(EmotionType::THINKING);
    display->ShowTopBar(nullptr, "正在思考...", -1);  // 持续显示

    vTaskDelay(pdMS_TO_TICKS(3000));

    // 隐藏顶部栏
    display->HideTopBar();

    ESP_LOGI(TAG, "Top bar example complete");
}

// ============================================================================
// 示例 3: 情感序列播放
// ============================================================================

void example_emotion_sequence() {
    ESP_LOGI(TAG, "=== Example: Emotion Sequence ===");

    EmotionDisplay* display = CreateEmotionDisplay(240, 240);
    display->InitEmotionSystem("/spiffs/emotions/");

    // 创建问候序列
    std::vector<EmotionSequenceItem> greeting;
    greeting.emplace_back(EmotionType::NEUTRAL, 1000);
    greeting.emplace_back(EmotionType::HAPPY, 1500, true);    // 带过渡
    greeting.emplace_back(EmotionType::EXCITED, 2000, true);  // 带过渡

    // 播放序列
    display->PlayEmotionSequence(greeting, false);

    ESP_LOGI(TAG, "Emotion sequence started");
}

// ============================================================================
// 示例 4: 响应系统事件
// ============================================================================

class EmotionEventHandler {
public:
    EmotionEventHandler(EmotionDisplay* display) : display_(display) {}

    void OnWifiConnecting() {
        display_->ShowEmotion(EmotionType::CONFUSED, -1);
        display_->ShowTopBar(nullptr, "正在连接 WiFi...", -1);
    }

    void OnWifiConnected() {
        display_->HideTopBar();
        display_->ShowEmotion(EmotionType::HAPPY, 2000);
        display_->ShowTopBar(nullptr, "✓ WiFi 已连接", 2000);
    }

    void OnWifiFailed() {
        display_->ShowEmotion(EmotionType::SAD, 3000);
        display_->ShowTopBar(nullptr, "✗ WiFi 连接失败", 3000);
    }

    void OnListening() {
        display_->ShowEmotion(EmotionType::NEUTRAL, -1);
        display_->ShowTopBar(nullptr, "🎤 聆听中...", -1);
    }

    void OnThinking() {
        display_->HideTopBar();
        display_->ShowEmotion(EmotionType::THINKING, -1);
    }

    void OnSpeaking() {
        display_->HideTopBar();
        display_->ShowEmotion(EmotionType::HAPPY, -1);
    }

    void OnError(const char* error_msg) {
        display_->ShowEmotion(EmotionType::ANGRY, 3000);
        display_->ShowTopBar(nullptr, error_msg, 3000);
    }

    void OnLowBattery(int percentage) {
        display_->ShowEmotion(EmotionType::SLEEPY, -1);
        
        char msg[64];
        snprintf(msg, sizeof(msg), "⚠️ 电量: %d%%", percentage);
        display_->ShowTopBar(nullptr, msg, -1);
    }

private:
    EmotionDisplay* display_;
};

void example_event_handling() {
    ESP_LOGI(TAG, "=== Example: Event Handling ===");

    EmotionDisplay* display = CreateEmotionDisplay(240, 240);
    display->InitEmotionSystem("/spiffs/emotions/");

    EmotionEventHandler handler(display);

    // 模拟事件
    handler.OnWifiConnecting();
    vTaskDelay(pdMS_TO_TICKS(2000));

    handler.OnWifiConnected();
    vTaskDelay(pdMS_TO_TICKS(2000));

    handler.OnListening();
    vTaskDelay(pdMS_TO_TICKS(2000));

    handler.OnThinking();
    vTaskDelay(pdMS_TO_TICKS(3000));

    handler.OnSpeaking();

    ESP_LOGI(TAG, "Event handling example complete");
}

// ============================================================================
// 示例 5: 与 AI 集成
// ============================================================================

void example_ai_integration(EmotionDisplay* display, const char* ai_response_json) {
    ESP_LOGI(TAG, "=== Example: AI Integration ===");

    // 假设 AI 返回的 JSON 格式：
    // {
    //   "text": "我很高兴能帮助你！",
    //   "emotion": "happy"
    // }

    // 解析情感
    const char* emotion_name = "happy";  // 从 JSON 中解析
    const char* text = "我很高兴能帮助你！";  // 从 JSON 中解析

    // 显示对应情感
    display->ShowEmotion(emotion_name, 0);

    // 可选：在顶部显示文字
    display->ShowTopBar(nullptr, text, 3000);

    ESP_LOGI(TAG, "AI integration example complete");
}

// ============================================================================
// 示例 6: 高级功能 - 回调和自动返回
// ============================================================================

void example_advanced_features() {
    ESP_LOGI(TAG, "=== Example: Advanced Features ===");

    EmotionDisplay* display = CreateEmotionDisplay(240, 240);
    display->InitEmotionSystem("/spiffs/emotions/");

    // 设置情感切换回调
    display->SetEmotionChangeCallback([](EmotionType emotion) {
        ESP_LOGI(TAG, "🔔 Emotion changed to: %s", EmotionTypeToString(emotion));
        
        // 在这里可以同步其他模块
        // - LED 颜色
        // - 音效
        // - 事件总线
    });

    // 启用自动返回中性状态（5 秒后）
    display->SetAutoReturnNeutral(true, 5000);

    // 显示开心表情
    display->ShowEmotion(EmotionType::HAPPY);
    ESP_LOGI(TAG, "Will auto return to NEUTRAL after 5 seconds");

    // 等待自动返回
    vTaskDelay(pdMS_TO_TICKS(6000));

    ESP_LOGI(TAG, "Advanced features example complete");
}

// ============================================================================
// 示例 7: 完整的应用集成
// ============================================================================

class EmotionApplication {
public:
    EmotionApplication() : display_(nullptr) {}

    bool Initialize() {
        ESP_LOGI(TAG, "Initializing EmotionApplication...");

        // 创建显示
        display_ = CreateEmotionDisplay(240, 240);
        if (!display_) {
            ESP_LOGE(TAG, "Failed to create display");
            return false;
        }

        // 初始化情感系统
        if (!display_->InitEmotionSystem("/spiffs/emotions/")) {
            ESP_LOGE(TAG, "Failed to initialize emotion system");
            return false;
        }

        // 配置顶部状态栏
        TopBarConfig config;
        config.visible = false;  // 默认隐藏
        config.height = 40;
        config.bg_color = lv_color_black();
        config.bg_opacity = LV_OPA_70;
        display_->SetTopBarConfig(config);

        // 设置回调
        display_->SetEmotionChangeCallback([this](EmotionType emotion) {
            OnEmotionChanged(emotion);
        });

        // 启用自动返回
        display_->SetAutoReturnNeutral(true, 5000);

        // 显示初始表情
        display_->ShowEmotion(EmotionType::NEUTRAL);

        ESP_LOGI(TAG, "EmotionApplication initialized");
        return true;
    }

    void HandleUserInput(const char* input) {
        // 聆听状态
        display_->ShowEmotion(EmotionType::NEUTRAL);
        display_->ShowTopBar(nullptr, "🎤 聆听中...", -1);
    }

    void HandleAIThinking() {
        display_->HideTopBar();
        display_->ShowEmotion(EmotionType::THINKING, -1);
    }

    void HandleAIResponse(const char* text, const char* emotion) {
        display_->ShowEmotion(emotion, 0);
        
        // 可选：显示文字
        if (text && strlen(text) < 30) {
            display_->ShowTopBar(nullptr, text, 3000);
        }
    }

    void HandleSystemNotification(const char* message) {
        display_->ShowTopBar(nullptr, message, 3000);
    }

    void HandleError(const char* error_msg) {
        display_->ShowEmotion(EmotionType::ANGRY, 3000);
        display_->ShowTopBar(nullptr, error_msg, 3000);
    }

    EmotionDisplay* GetDisplay() { return display_; }

private:
    void OnEmotionChanged(EmotionType emotion) {
        ESP_LOGI(TAG, "Emotion changed: %s", EmotionTypeToString(emotion));
        
        // 同步 LED、音效等其他模块
        // SyncLED(emotion);
        // PlayEmotionSound(emotion);
    }

    EmotionDisplay* display_;
};

void example_full_application() {
    ESP_LOGI(TAG, "=== Example: Full Application ===");

    EmotionApplication app;
    if (!app.Initialize()) {
        ESP_LOGE(TAG, "Failed to initialize application");
        return;
    }

    // 模拟应用流程
    ESP_LOGI(TAG, "Simulating user interaction...");

    // 1. 用户说话
    app.HandleUserInput("你好");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 2. AI 思考
    app.HandleAIThinking();
    vTaskDelay(pdMS_TO_TICKS(3000));

    // 3. AI 回复
    app.HandleAIResponse("你好！很高兴见到你", "happy");
    vTaskDelay(pdMS_TO_TICKS(4000));

    // 4. 系统通知
    app.HandleSystemNotification("新消息");
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Full application example complete");
}

// ============================================================================
// C 接口封装
// ============================================================================

extern "C" {

static EmotionDisplay* g_emotion_display = nullptr;

/**
 * @brief 初始化情感显示系统（C 接口）
 */
int emotion_display_init(int width, int height, const char* emotions_dir) {
    if (g_emotion_display) {
        ESP_LOGW(TAG, "Emotion display already initialized");
        return 0;
    }

    g_emotion_display = CreateEmotionDisplay(width, height);
    if (!g_emotion_display) {
        ESP_LOGE(TAG, "Failed to create emotion display");
        return -1;
    }

    if (!g_emotion_display->InitEmotionSystem(emotions_dir)) {
        ESP_LOGE(TAG, "Failed to initialize emotion system");
        delete g_emotion_display;
        g_emotion_display = nullptr;
        return -1;
    }

    return 0;
}

/**
 * @brief 显示情感（C 接口）
 */
int emotion_display_show(const char* emotion_name, int duration_ms) {
    if (!g_emotion_display) {
        ESP_LOGE(TAG, "Emotion display not initialized");
        return -1;
    }

    return g_emotion_display->ShowEmotion(emotion_name, duration_ms) ? 0 : -1;
}

/**
 * @brief 显示顶部消息（C 接口）
 */
void emotion_display_show_message(const char* message, int duration_ms) {
    if (g_emotion_display) {
        g_emotion_display->ShowTopBar(nullptr, message, duration_ms);
    }
}

/**
 * @brief 隐藏顶部消息（C 接口）
 */
void emotion_display_hide_message() {
    if (g_emotion_display) {
        g_emotion_display->HideTopBar();
    }
}

/**
 * @brief 停止情感动画（C 接口）
 */
void emotion_display_stop() {
    if (g_emotion_display) {
        g_emotion_display->StopEmotion();
    }
}

} // extern "C"


