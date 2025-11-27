#pragma once

/**
 * @file xiaozhi_emotion_integration.h
 * @brief 小智项目情感系统集成指南
 * 
 * 提供完整的集成示例和最佳实践
 */

#include "emotion_coordinator.h"
#include "esp_log.h"

namespace xiaozhi {

static const char* TAG = "XiaozhiEmotion";

/**
 * @brief 小智情感系统集成类
 * 
 * 封装了情感系统的初始化和使用，简化集成流程
 */
class EmotionSystemIntegration {
public:
    /**
     * @brief 获取单例
     */
    static EmotionSystemIntegration& Instance() {
        static EmotionSystemIntegration instance;
        return instance;
    }

    /**
     * @brief 初始化情感系统
     * 
     * @param anim_path 动画文件路径
     * @param width 屏幕宽度
     * @param height 屏幕高度
     * @param parent LVGL 父对象
     * @return true 成功
     */
    bool Init(const char* anim_path = "/spiffs/anim/",
              int width = 240, int height = 240,
              lv_obj_t* parent = nullptr)
    {
        ESP_LOGI(TAG, "Initializing Xiaozhi Emotion System");

        // 初始化情感协调器
        auto& coordinator = emotion::EmotionCoordinator::Instance();
        
        if (!coordinator.QuickInit(anim_path, width, height, parent)) {
            ESP_LOGE(TAG, "Failed to initialize emotion coordinator");
            return false;
        }

        // 设置状态变化回调
        coordinator.SetStateChangeCallback([this](const emotion::EmotionChangeEvent& event) {
            OnEmotionChanged(event);
        });

        // 打印系统信息
        coordinator.PrintSystemInfo();

        ESP_LOGI(TAG, "Xiaozhi Emotion System initialized successfully");
        return true;
    }

    // ========================================================================
    // 便捷接口：基于设备状态
    // ========================================================================

    /**
     * @brief 启动中
     */
    void OnBooting() {
        SetDeviceState(emotion::DeviceState::BOOTING);
    }

    /**
     * @brief 空闲状态
     */
    void OnIdle() {
        SetDeviceState(emotion::DeviceState::IDLE);
    }

    /**
     * @brief 唤醒
     */
    void OnWakeup() {
        SetDeviceState(emotion::DeviceState::WAKING);
    }

    /**
     * @brief 开始聆听
     */
    void OnStartListening() {
        SetDeviceState(emotion::DeviceState::LISTENING);
    }

    /**
     * @brief 处理中/思考
     */
    void OnProcessing() {
        SetDeviceState(emotion::DeviceState::PROCESSING);
    }

    /**
     * @brief 开始说话
     */
    void OnStartSpeaking() {
        SetDeviceState(emotion::DeviceState::SPEAKING);
    }

    /**
     * @brief 网络连接中
     */
    void OnConnecting() {
        SetDeviceState(emotion::DeviceState::CONNECTING);
    }

    /**
     * @brief 已连接
     */
    void OnConnected() {
        SetDeviceState(emotion::DeviceState::CONNECTED);
    }

    /**
     * @brief 播放音乐
     */
    void OnPlayingMusic() {
        SetDeviceState(emotion::DeviceState::PLAYING_MUSIC);
    }

    /**
     * @brief 发生错误
     */
    void OnError() {
        SetDeviceState(emotion::DeviceState::ERROR);
    }

    /**
     * @brief 低电量
     */
    void OnLowBattery() {
        SetDeviceState(emotion::DeviceState::LOW_BATTERY);
    }

    /**
     * @brief 固件更新中
     */
    void OnUpdating() {
        SetDeviceState(emotion::DeviceState::UPDATING);
    }

    // ========================================================================
    // 高级接口：直接控制情感（可选）
    // ========================================================================

    /**
     * @brief 显示开心表情
     */
    void ShowHappy(int duration_ms = 0) {
        if (duration_ms > 0) {
            emotion::EmotionCoordinator::Instance().SetTemporaryEmotion(
                emotion::EmotionState::HAPPY, duration_ms);
        } else {
            emotion::EmotionCoordinator::Instance().SetEmotion(
                emotion::EmotionState::HAPPY);
        }
    }

    /**
     * @brief 显示悲伤表情
     */
    void ShowSad(int duration_ms = 0) {
        if (duration_ms > 0) {
            emotion::EmotionCoordinator::Instance().SetTemporaryEmotion(
                emotion::EmotionState::SAD, duration_ms);
        } else {
            emotion::EmotionCoordinator::Instance().SetEmotion(
                emotion::EmotionState::SAD);
        }
    }

    /**
     * @brief 显示惊讶表情
     */
    void ShowSurprised(int duration_ms = 2000) {
        emotion::EmotionCoordinator::Instance().SetTemporaryEmotion(
            emotion::EmotionState::SURPRISED, duration_ms);
    }

    /**
     * @brief 显示兴奋表情
     */
    void ShowExcited(int duration_ms = 0) {
        if (duration_ms > 0) {
            emotion::EmotionCoordinator::Instance().SetTemporaryEmotion(
                emotion::EmotionState::EXCITED, duration_ms);
        } else {
            emotion::EmotionCoordinator::Instance().SetEmotion(
                emotion::EmotionState::EXCITED);
        }
    }

    /**
     * @brief 重置到默认状态
     */
    void Reset() {
        emotion::EmotionCoordinator::Instance().ResetToDefault();
    }

    // ========================================================================
    // 动画控制
    // ========================================================================

    /**
     * @brief 暂停当前动画
     */
    void Pause() {
        emotion::EmotionCoordinator::Instance().Pause();
    }

    /**
     * @brief 恢复播放
     */
    void Resume() {
        emotion::EmotionCoordinator::Instance().Resume();
    }

    /**
     * @brief 播放自定义动画
     * 
     * @param animation_name 动画文件名（不含路径，如 "singing.json"）
     * @param loop 是否循环
     * @param duration_ms 播放时长（-1 表示播放完整动画）
     */
    void PlayCustomAnimation(const char* animation_name, bool loop = false, int duration_ms = -1) {
        std::string path = "/spiffs/anim/" + std::string(animation_name);
        emotion::EmotionCoordinator::Instance().PlayCustomAnimation(path, loop, true);
    }

    // ========================================================================
    // 状态查询
    // ========================================================================

    /**
     * @brief 获取当前情感
     */
    emotion::EmotionState GetCurrentEmotion() const {
        return emotion::EmotionCoordinator::Instance().GetCurrentEmotion();
    }

    /**
     * @brief 打印系统信息
     */
    void PrintInfo() const {
        emotion::EmotionCoordinator::Instance().PrintSystemInfo();
    }

private:
    EmotionSystemIntegration() = default;

    void SetDeviceState(emotion::DeviceState state) {
        emotion::EmotionCoordinator::Instance().SetDeviceState(state);
    }

    void OnEmotionChanged(const emotion::EmotionChangeEvent& event) {
        ESP_LOGI(TAG, ">>> Emotion: %s -> %s", 
                 emotion::EmotionStateToString(event.from_state),
                 emotion::EmotionStateToString(event.to_state));
        
        // 这里可以添加自定义逻辑
        // 例如：播放音效、LED 指示、发送事件等
    }
};

// ============================================================================
// 使用示例
// ============================================================================

/**
 * @brief 示例：在 app_main 中初始化
 */
inline void Example_InitInAppMain()
{
    /*
    extern "C" void app_main(void)
    {
        // ... 其他初始化代码 ...

        // 初始化 SPIFFS
        esp_vfs_spiffs_conf_t conf = {
            .base_path = "/spiffs",
            .partition_label = NULL,
            .max_files = 5,
            .format_if_mount_failed = false
        };
        ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));

        // 初始化 LVGL
        // ... LVGL 初始化代码 ...

        // 初始化情感系统
        auto& emotion = xiaozhi::EmotionSystemIntegration::Instance();
        emotion.Init("/spiffs/anim/", 240, 240);

        // 显示启动动画
        emotion.OnBooting();

        // ... 其他代码 ...
    }
    */
}

/**
 * @brief 示例：在音频回调中使用
 */
inline void Example_AudioCallback()
{
    /*
    void on_wake_word_detected() {
        xiaozhi::EmotionSystemIntegration::Instance().OnWakeup();
    }

    void on_start_recording() {
        xiaozhi::EmotionSystemIntegration::Instance().OnStartListening();
    }

    void on_start_processing() {
        xiaozhi::EmotionSystemIntegration::Instance().OnProcessing();
    }

    void on_start_playback() {
        xiaozhi::EmotionSystemIntegration::Instance().OnStartSpeaking();
    }

    void on_idle() {
        xiaozhi::EmotionSystemIntegration::Instance().OnIdle();
    }
    */
}

/**
 * @brief 示例：网络状态变化
 */
inline void Example_NetworkCallback()
{
    /*
    void on_wifi_connecting() {
        xiaozhi::EmotionSystemIntegration::Instance().OnConnecting();
    }

    void on_wifi_connected() {
        xiaozhi::EmotionSystemIntegration::Instance().OnConnected();
        
        // 2 秒后自动恢复到空闲状态
        vTaskDelay(pdMS_TO_TICKS(2000));
        xiaozhi::EmotionSystemIntegration::Instance().OnIdle();
    }

    void on_wifi_disconnected() {
        xiaozhi::EmotionSystemIntegration::Instance().ShowSad(3000);
    }
    */
}

/**
 * @brief 示例：播放特殊动画
 */
inline void Example_PlaySpecialAnimation()
{
    /*
    // 庆祝成功
    void celebrate_success() {
        auto& emotion = xiaozhi::EmotionSystemIntegration::Instance();
        emotion.PlayCustomAnimation("champion.json", false);
        
        // 播放完成后自动恢复
    }

    // 播放音乐时的动画
    void play_music_animation() {
        auto& emotion = xiaozhi::EmotionSystemIntegration::Instance();
        emotion.PlayCustomAnimation("singing.json", true);
    }
    */
}

} // namespace xiaozhi

// ============================================================================
// 更简洁的全局函数（可选）
// ============================================================================

// 在 C++ 代码中可以直接使用这些全局函数

#define XIAOZHI_EMOTION xiaozhi::EmotionSystemIntegration::Instance()

// 使用宏简化调用：
// XIAOZHI_EMOTION.OnWakeup();
// XIAOZHI_EMOTION.OnStartListening();
// XIAOZHI_EMOTION.ShowHappy();

