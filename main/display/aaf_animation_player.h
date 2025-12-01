#pragma once

#include <functional>
#include <string>
#include <atomic>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include "anim_player.h"

namespace xiaozhi {
namespace display {

/**
 * @brief AAF Animation Player
 * 
 * 封装底层 anim_player API，提供播放、停止、循环等功能
 */
class AafAnimationPlayer {
public:
    // 播放模式
    enum class PlayMode {
        Once,      // 播放一次
        Loop       // 循环播放
    };
    
    // 播放配置
    struct PlaybackConfig {
        const void* data_address;  // 动画数据地址（Flash 或 SRAM）
        size_t data_length;        // 数据长度
        PlayMode mode;             // 播放模式
        int fps;                   // 帧率
        bool interrupt_current;    // 是否打断当前动画
        
        PlaybackConfig()
            : data_address(nullptr)
            , data_length(0)
            , mode(PlayMode::Loop)
            , fps(15)
            , interrupt_current(true) {
        }
    };
    
    // 回调类型
    using AnimationEndCallback = std::function<void()>;
    using AnimationFrameCallback = std::function<void(int current_frame, int total_frames)>;
    
    AafAnimationPlayer(esp_lcd_panel_io_handle_t panel_io,
                       esp_lcd_panel_handle_t panel,
                       int canvas_x, int canvas_y,
                       int canvas_width, int canvas_height);
    ~AafAnimationPlayer();
    
    // 禁止拷贝
    AafAnimationPlayer(const AafAnimationPlayer&) = delete;
    AafAnimationPlayer& operator=(const AafAnimationPlayer&) = delete;
    
    // 播放控制
    bool Play(const PlaybackConfig& config);
    bool Stop();
    bool Pause();
    bool Resume();
    
    // 状态查询
    bool IsPlaying() const { return is_playing_; }
    bool IsPaused() const { return is_paused_; }
    const char* GetCurrentAnimation() const { return current_animation_.c_str(); }
    
    // 回调注册
    void SetAnimationEndCallback(AnimationEndCallback callback) {
        on_animation_end_ = callback;
    }
    
    void SetFrameCallback(AnimationFrameCallback callback) {
        on_frame_update_ = callback;
    }
    
private:
    esp_lcd_panel_io_handle_t panel_io_;
    esp_lcd_panel_handle_t panel_;
    anim_player_handle_t player_handle_;
    
    int canvas_x_;
    int canvas_y_;
    int canvas_width_;
    int canvas_height_;
    
    std::string current_animation_;
    std::atomic<bool> is_playing_;
    std::atomic<bool> is_paused_;
    
    AnimationEndCallback on_animation_end_;
    AnimationFrameCallback on_frame_update_;
    
    // 底层回调
    static bool OnFlushIoReady(esp_lcd_panel_io_handle_t panel_io,
                               esp_lcd_panel_io_event_data_t* edata,
                               void* user_ctx);
    
    static void OnFlush(anim_player_handle_t handle,
                        int x_start, int y_start,
                        int x_end, int y_end,
                        const void* color_data);
    
    static void OnUpdate(anim_player_handle_t handle, player_event_t event);
};

} // namespace display
} // namespace xiaozhi

