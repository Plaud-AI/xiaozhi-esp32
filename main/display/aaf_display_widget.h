#pragma once

#include "display/lcd_display.h"
#include "aaf_animation_resource_manager.h"
#include "animation_state_manager.h"
#include "aaf_animation_player.h"
#include <memory>
#include <esp_timer.h>

namespace xiaozhi {
namespace display {

/**
 * @brief AAF Display Widget
 * 
 * 主显示控件，管理 AAF 动画播放和状态栏
 * 继承自 Display 基类，与现有系统兼容
 */
class AafDisplayWidget : public Display {
public:
    // 屏幕配置
    struct ScreenConfig {
        int width;
        int height;
        int status_bar_height;
        
        struct {
            int x;
            int y;
            int width;
            int height;
        } animation_canvas;
        
        // 根据屏幕尺寸自动计算
        static ScreenConfig CreateForResolution(int width, int height);
    };
    
    // 过渡配置
    struct TransitionConfig {
        bool enabled;
        int duration_ms;
        
        TransitionConfig() : enabled(true), duration_ms(300) {}
    };
    
    AafDisplayWidget(esp_lcd_panel_io_handle_t panel_io,
                     esp_lcd_panel_handle_t panel,
                     int width, int height);
    
    ~AafDisplayWidget() override;
    
    // 获取 panel 句柄
    esp_lcd_panel_handle_t GetPanel() const { return panel_; }
    esp_lcd_panel_io_handle_t GetPanelIO() const { return panel_io_; }
    
    // 禁止拷贝
    AafDisplayWidget(const AafDisplayWidget&) = delete;
    AafDisplayWidget& operator=(const AafDisplayWidget&) = delete;
    
    // Display 基类接口实现
    void SetStatus(const char* status) override;
    void SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* content) override;
    
    // 配置接口
    void SetTransitionEnabled(bool enabled);
    void SetTransitionDuration(int duration_ms);
    
    // 访问器（用于高级控制）
    AnimationStateManager* GetStateManager() { return state_manager_.get(); }
    AafAnimationPlayer* GetAnimationPlayer() { return animation_player_.get(); }
    AnimationResourceManager* GetResourceManager() { return resource_manager_.get(); }
    
protected:
    bool Lock(int timeout_ms = 0) override;
    void Unlock() override;
    
private:
    // 硬件句柄
    esp_lcd_panel_io_handle_t panel_io_;
    esp_lcd_panel_handle_t panel_;
    lv_display_t* lvgl_display_;  // LVGL display handle
    
    // 核心组件
    std::unique_ptr<AnimationResourceManager> resource_manager_;
    std::unique_ptr<AnimationStateManager> state_manager_;
    std::unique_ptr<AafAnimationPlayer> animation_player_;
    
    // UI 组件
    lv_obj_t* status_bar_;
    lv_obj_t* animation_canvas_;
    
    // 配置
    ScreenConfig screen_config_;
    TransitionConfig transition_config_;
    
    // 超时定时器（用于感情状态自动恢复）
    esp_timer_handle_t timeout_timer_;
    
    // 初始化
    void InitializeLvgl();  // 初始化 LVGL port
    bool InitializeResources();
    bool InitializeStateManager();
    bool InitializeAnimationPlayer();
    void InitializeUI();
    
    // 状态变更处理
    void OnStateChanged(const AnimationStateManager::StateChangeEvent& event);
    
    // 定时器回调
    static void OnTimeoutTimer(void* arg);
    
    // 辅助函数
    void UpdateStatusBar();
    void StartTimeoutTimer(uint32_t timeout_ms);
    void StopTimeoutTimer();
};

} // namespace display
} // namespace xiaozhi

