#pragma once

#include "display/lcd_display.h"
#include "aaf_animation_resource_manager.h"
#include "animation_state_manager.h"
#include "aaf_animation_player.h"
#include <memory>
#include <mutex>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace xiaozhi {
namespace display {

/**
 * @brief AAF Display Widget - 重写版本
 * 
 * 主显示控件，管理 AAF 动画播放和状态栏
 * 
 * 重写要点（参考 esp_brookesia 官方实现）：
 * 1. 直接使用 LCD 驱动绘制帧数据，而非 LVGL Canvas
 * 2. 使用回调机制解耦动画播放器和显示驱动
 * 3. 避免与 LVGL 刷新机制冲突
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
        
        TransitionConfig() : enabled(false), duration_ms(0) {}
    };
    
    AafDisplayWidget(esp_lcd_panel_io_handle_t panel_io,
                     esp_lcd_panel_handle_t panel,
                     int width, int height);
    
    ~AafDisplayWidget() override;
    
    // 获取 panel 句柄
    esp_lcd_panel_handle_t GetPanel() const { return panel_; }
    esp_lcd_panel_io_handle_t GetPanelIO() const { return panel_io_; }
    
    // LCD 传输完成回调（供 board 初始化时设置）
    static bool OnLcdTransferDone(esp_lcd_panel_io_handle_t panel_io, 
                                   esp_lcd_panel_io_event_data_t* edata, 
                                   void* user_ctx);
    
    // 等待 LCD 传输完成
    bool WaitForTransferDone(int timeout_ms = 100);
    
    // 禁止拷贝
    AafDisplayWidget(const AafDisplayWidget&) = delete;
    AafDisplayWidget& operator=(const AafDisplayWidget&) = delete;
    
    // Display 基类接口实现
    void SetStatus(const char* status) override;
    void SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ShowAnimationByPath(const char* animation_path, bool loop = true) override;
    
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
    lv_display_t* lvgl_display_;  // LVGL display handle（仅用于状态栏）
    
    // 核心组件
    std::unique_ptr<AnimationResourceManager> resource_manager_;
    std::unique_ptr<AnimationStateManager> state_manager_;
    std::unique_ptr<AafAnimationPlayer> animation_player_;
    
    // UI 组件（LVGL 状态栏）
    lv_obj_t* status_bar_;
    lv_obj_t* ble_icon_label_;  // 蓝牙状态图标
    
    // 配置
    ScreenConfig screen_config_;
    TransitionConfig transition_config_;
    
    // 超时定时器（用于感情状态自动恢复）
    esp_timer_handle_t timeout_timer_;
    
    // LCD 传输完成信号量（用于 DMA 同步）
    SemaphoreHandle_t trans_done_sem_;
    
    // 绘制互斥锁（确保同一时间只有一个绘制操作）
    // 参考 xr-esp-brookesia-master/products/speaker/main/modules/display.cpp
    std::mutex draw_mutex_;
    
    // 初始化
    void InitializeLvgl();  // 初始化 LVGL port
    bool InitializeResources();
    bool InitializeStateManager();
    bool InitializeAnimationPlayer();
    void InitializeUI();
    
    // LCD 直接绘制（核心改进）
    // 异步版本：用于动画播放（不等待 DMA 完成）
    bool DrawBitmapToLcd(int x_start, int y_start, int x_end, int y_end, const void* data);
    // 同步版本：用于 ClearLcdArea 等需要确保完成的操作
    bool DrawBitmapToLcdSync(int x_start, int y_start, int x_end, int y_end, const void* data);
    bool ClearLcdArea(int x_start, int y_start, int x_end, int y_end);
    
    // 动画播放器回调
    static void OnAnimationFlush(int x_start, int y_start, int x_end, int y_end,
                                  const void* data, void* user_data);
    static void OnAnimationStop(int x_start, int y_start, int x_end, int y_end,
                                 void* user_data);
    
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
