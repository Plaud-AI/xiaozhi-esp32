/**
 * @file aaf_display_example.cc
 * @brief AAF 显示框架使用示例 - 重写版本
 * 
 * 演示如何在开发板中集成和使用 AAF 显示系统（直接 LCD 驱动模式）
 */

#include "aaf_display_widget.h"
#include "assets/lang_config.h"
#include <esp_log.h>

static const char* TAG = "AAF_EXAMPLE";

using namespace xiaozhi::display;

// ===== 示例 1: 基本初始化 =====

AafDisplayWidget* CreateAafDisplay(esp_lcd_panel_io_handle_t panel_io,
                                   esp_lcd_panel_handle_t panel,
                                   int width, int height) {
    ESP_LOGI(TAG, "Creating AAF display: %dx%d", width, height);
    
    // 创建显示控件
    // 内部会自动完成：
    // 1. LVGL 初始化（仅用于状态栏）
    // 2. 资源管理器初始化（从 Assets 分区加载）
    // 3. 状态管理器初始化
    // 4. 动画播放器初始化（直接 LCD 驱动模式）
    auto display = new AafDisplayWidget(panel_io, panel, width, height);
    
    ESP_LOGI(TAG, "AAF display created successfully");
    return display;
}

// ===== 示例 2: 基本使用 =====

void Example_BasicUsage(AafDisplayWidget* display) {
    ESP_LOGI(TAG, "=== Example: Basic Usage ===");
    
    // 设置设备状态（使用 Display 基类接口）
    // SetStatus 会自动映射到对应的 AAF 动画
    display->SetStatus(Lang::Strings::STANDBY);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    display->SetStatus(Lang::Strings::LISTENING);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    display->SetStatus(Lang::Strings::SPEAKING);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 返回待机状态
    display->SetStatus(Lang::Strings::STANDBY);
}

// ===== 示例 3: 优先级控制 =====

void Example_PriorityControl(AafDisplayWidget* display) {
    ESP_LOGI(TAG, "=== Example: Priority Control ===");
    
    auto state_mgr = display->GetStateManager();
    
    // 场景 1: 正常状态切换
    state_mgr->SetDeviceState(AnimationStateManager::DeviceState::Idle);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    state_mgr->SetDeviceState(AnimationStateManager::DeviceState::Listening);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 场景 2: 不可被打断的关键操作
    state_mgr->SetDeviceStateWithPriority(
        AnimationStateManager::DeviceState::Updating,
        AnimationStateManager::Priority::Critical
    );
    
    // 尝试切换其他状态（会被忽略，因为 Updating 是 Critical 优先级）
    state_mgr->SetDeviceState(AnimationStateManager::DeviceState::Idle);  // 无效
    
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
    
    // 切换到其他状态
    state_mgr->SetDeviceState(AnimationStateManager::DeviceState::Listening);
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
    
    // 获取动画数据（通过文件名）
    size_t data_size = 0;
    int fps = 0;
    const void* anim_data = resource_mgr->GetAnimationData("idle.aaf", &data_size, &fps);
    
    if (anim_data) {
        ESP_LOGI(TAG, "Got animation data: size=%zu, fps=%d", data_size, fps);
        
        // 配置播放参数
        AnimDataConfig config = {
            .data_address = anim_data,
            .data_length = data_size,
            .fps = fps,
        };
        
        // 循环播放
        player->Play(config, true, true);
        
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        // 停止
        player->Stop();
    } else {
        ESP_LOGW(TAG, "Animation data not found");
    }
}

// ===== 示例 6: 回调处理 =====

void Example_Callbacks(AafDisplayWidget* display) {
    ESP_LOGI(TAG, "=== Example: Callbacks ===");
    
    // 注册状态变更回调
    display->GetStateManager()->SetStateChangeCallback(
        [](const AnimationStateManager::StateChangeEvent& event) {
            ESP_LOGI(TAG, "State changed to: %s (priority=%d)",
                     event.state_name.c_str(),
                     static_cast<int>(event.priority));
        }
    );
    
    // 触发一些状态变更
    display->SetStatus(Lang::Strings::LISTENING);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    display->SetStatus(Lang::Strings::SPEAKING);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    display->SetStatus(Lang::Strings::STANDBY);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

// ===== 主任务 =====

extern "C" void aaf_display_example_task(void* pvParameters) {
    ESP_LOGI(TAG, "AAF Display Example Task Started");
    
    // 这里需要根据实际硬件初始化 panel_io 和 panel
    // esp_lcd_panel_io_handle_t panel_io = ...;
    // esp_lcd_panel_handle_t panel = ...;
    // int width = 240;
    // int height = 240;
    
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
