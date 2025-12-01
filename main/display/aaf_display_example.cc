/**
 * @file aaf_display_example.cc
 * @brief AAF 显示框架使用示例
 * 
 * 演示如何在开发板中集成和使用 AAF 显示系统
 */

#include "aaf_display_widget.h"
#include "assets/mmap_animations.h"  // 由 esp_mmap_assets 自动生成
#include "assets/lang_config.h"
#include <esp_log.h>

static const char* TAG = "AAF_EXAMPLE";

using namespace xiaozhi::display;

// ===== 示例 1: 基本初始化 =====

AafDisplayWidget* CreateAafDisplay(esp_lcd_panel_io_handle_t panel_io,
                                   esp_lcd_panel_handle_t panel,
                                   int width, int height) {
    ESP_LOGI(TAG, "Creating AAF display: %dx%d", width, height);
    
    // 1. 创建显示控件
    auto display = new AafDisplayWidget(panel_io, panel, width, height);
    
    // 2. 初始化动画资源（从 assets 分区加载）
    AnimationResourceManager::PartitionConfig res_config = {
        .partition_label = "assets",
        .max_files = MMAP_ANIMATIONS_FILES,
        .fps_array = MMAP_ANIMATIONS_FPS,
        .checksum = MMAP_ANIMATIONS_CHECKSUM
    };
    
    esp_err_t ret = display->GetResourceManager()->InitFromPartition(res_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize resources: %s", esp_err_to_name(ret));
        delete display;
        return nullptr;
    }
    
    // 3. 注册状态映射（设备状态）
    auto state_mgr = display->GetStateManager();
    
    state_mgr->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Idle,
        "idle.aaf", ANIM_IDLE, true, 15,
        AnimationStateManager::Priority::Normal
    );
    
    state_mgr->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Listening,
        "listening.aaf", ANIM_LISTENING, true, 20,
        AnimationStateManager::Priority::Normal
    );
    
    state_mgr->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Speaking,
        "speaking.aaf", ANIM_SPEAKING, true, 20,
        AnimationStateManager::Priority::Normal
    );
    
    state_mgr->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Loading,
        "loading.aaf", ANIM_LOADING, true, 15,
        AnimationStateManager::Priority::Normal
    );
    
    state_mgr->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Settings,
        "settings.aaf", ANIM_SETTINGS, true, 15,
        AnimationStateManager::Priority::Normal
    );
    
    state_mgr->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Updating,
        "updating.aaf", ANIM_UPDATING, true, 15,
        AnimationStateManager::Priority::Critical  // 升级不可被打断
    );
    
    state_mgr->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Success,
        "success.aaf", ANIM_SUCCESS, false, 24,  // 播放一次
        AnimationStateManager::Priority::High
    );
    
    state_mgr->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Error,
        "error.aaf", ANIM_ERROR, true, 15,
        AnimationStateManager::Priority::Critical  // 错误不可被打断
    );
    
    // 4. 注册状态映射（感情状态）
    const struct {
        const char* name;
        const char* file;
        int index;
        int fps;
    } emotions[] = {
        {"happy", "happy.aaf", ANIM_HAPPY, 20},
        {"sad", "sad.aaf", ANIM_SAD, 15},
        {"angry", "angry.aaf", ANIM_ANGRY, 20},
        {"surprised", "surprised.aaf", ANIM_SURPRISED, 24},
        {"confused", "confused.aaf", ANIM_CONFUSED, 15},
        {"relaxed", "relaxed.aaf", ANIM_RELAXED, 15},
        {"thinking", "thinking.aaf", ANIM_THINKING, 15},
        {"sleepy", "sleepy.aaf", ANIM_SLEEPY, 10},
        {"loving", "loving.aaf", ANIM_LOVING, 20},
        {"embarrassed", "embarrassed.aaf", ANIM_EMBARRASSED, 15},
    };
    
    for (const auto& emotion : emotions) {
        state_mgr->RegisterEmotionMapping(
            emotion.name, emotion.file, emotion.index, true, emotion.fps,
            AnimationStateManager::Priority::High
        );
    }
    
    // 5. 配置过渡效果（可选）
    display->SetTransitionEnabled(true);
    display->SetTransitionDuration(300);
    
    ESP_LOGI(TAG, "AAF display created successfully");
    return display;
}

// ===== 示例 2: 基本使用 =====

void Example_BasicUsage(AafDisplayWidget* display) {
    ESP_LOGI(TAG, "=== Example: Basic Usage ===");
    
    // 设置设备状态（使用 Display 基类接口）
    display->SetStatus(Lang::Strings::STANDBY);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    display->SetStatus(Lang::Strings::LISTENING);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    display->SetStatus(Lang::Strings::SPEAKING);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 设置感情状态（会打断设备状态）
    display->SetEmotion("happy");
    vTaskDelay(pdMS_TO_TICKS(5000));  // 5 秒后自动恢复
}

// ===== 示例 3: 优先级控制 =====

void Example_PriorityControl(AafDisplayWidget* display) {
    ESP_LOGI(TAG, "=== Example: Priority Control ===");
    
    auto state_mgr = display->GetStateManager();
    
    // 场景 1: 正常打断
    state_mgr->SetDeviceState(AnimationStateManager::DeviceState::Idle);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 感情状态（高优先级）打断设备状态（普通优先级）
    state_mgr->SetEmotionState("happy");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 场景 2: 不可被打断的关键操作
    state_mgr->SetDeviceStateWithPriority(
        AnimationStateManager::DeviceState::Updating,
        AnimationStateManager::Priority::Critical
    );
    
    // 尝试切换感情状态（会被忽略）
    state_mgr->SetEmotionState("sad");  // 无效
    
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 升级完成，显示成功
    state_mgr->SetDeviceStateWithPriority(
        AnimationStateManager::DeviceState::Success,
        AnimationStateManager::Priority::High
    );
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 场景 3: 强制打断
    state_mgr->SetDeviceState(AnimationStateManager::DeviceState::Speaking);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 强制打断，显示错误
    state_mgr->SetDeviceStateWithPriority(
        AnimationStateManager::DeviceState::Error,
        AnimationStateManager::Priority::Critical,
        true  // 强制打断
    );
    
    vTaskDelay(pdMS_TO_TICKS(3000));
}

// ===== 示例 4: 状态恢复 =====

void Example_StateRestore(AafDisplayWidget* display) {
    ESP_LOGI(TAG, "=== Example: State Restore ===");
    
    auto state_mgr = display->GetStateManager();
    
    // 设置初始状态
    state_mgr->SetDeviceState(AnimationStateManager::DeviceState::Speaking);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 显示感情状态（会保存上一个状态）
    state_mgr->SetEmotionStateWithPriority(
        "happy",
        AnimationStateManager::Priority::High,
        false,  // 不强制
        3000    // 3 秒超时
    );
    
    // 3 秒后自动恢复到 Speaking 状态
    vTaskDelay(pdMS_TO_TICKS(4000));
    
    // 手动恢复
    state_mgr->SetEmotionState("sad");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 手动恢复到上一个状态
    state_mgr->RestorePreviousState();
    vTaskDelay(pdMS_TO_TICKS(2000));
}

// ===== 示例 5: 直接控制动画播放器 =====

void Example_DirectPlayerControl(AafDisplayWidget* display) {
    ESP_LOGI(TAG, "=== Example: Direct Player Control ===");
    
    auto player = display->GetAnimationPlayer();
    auto resource_mgr = display->GetResourceManager();
    
    // 获取动画数据
    size_t data_size;
    int fps;
    const void* anim_data = resource_mgr->GetAnimationData(ANIM_HAPPY, &data_size, &fps);
    
    if (anim_data) {
        // 配置播放参数
        AafAnimationPlayer::PlaybackConfig config;
        config.data_address = anim_data;
        config.data_length = data_size;
        config.mode = AafAnimationPlayer::PlayMode::Loop;
        config.fps = fps;
        config.interrupt_current = true;
        
        // 播放
        player->Play(config);
        
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        // 暂停
        player->Pause();
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // 恢复
        player->Resume();
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // 停止
        player->Stop();
    }
}

// ===== 示例 6: 回调处理 =====

void Example_Callbacks(AafDisplayWidget* display) {
    ESP_LOGI(TAG, "=== Example: Callbacks ===");
    
    auto player = display->GetAnimationPlayer();
    
    // 注册动画结束回调
    player->SetAnimationEndCallback([]() {
        ESP_LOGI(TAG, "Animation ended callback triggered");
    });
    
    // 注册状态变更回调
    display->GetStateManager()->SetStateChangeCallback(
        [](const AnimationStateManager::StateChangeEvent& event) {
            ESP_LOGI(TAG, "State changed to: %s (priority=%d)",
                     event.state_name.c_str(),
                     static_cast<int>(event.priority));
        }
    );
    
    // 触发一些状态变更
    display->SetEmotion("happy");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    display->SetStatus(Lang::Strings::LISTENING);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

// ===== 主任务 =====

extern "C" void aaf_display_example_task(void* pvParameters) {
    ESP_LOGI(TAG, "AAF Display Example Task Started");
    
    // 这里需要根据实际硬件初始化 panel_io 和 panel
    // esp_lcd_panel_io_handle_t panel_io = ...;
    // esp_lcd_panel_handle_t panel = ...;
    // int width = 160;
    // int height = 80;
    
    // AafDisplayWidget* display = CreateAafDisplay(panel_io, panel, width, height);
    
    // if (display) {
    //     // 运行示例
    //     Example_BasicUsage(display);
    //     Example_PriorityControl(display);
    //     Example_StateRestore(display);
    //     Example_DirectPlayerControl(display);
    //     Example_Callbacks(display);
    // }
    
    ESP_LOGI(TAG, "AAF Display Example Task Ended");
    vTaskDelete(NULL);
}

