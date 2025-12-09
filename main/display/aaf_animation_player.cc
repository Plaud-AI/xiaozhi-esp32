#include "aaf_animation_player.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>

static const char* TAG = "AafAnimPlayer";

namespace xiaozhi {
namespace display {

AafAnimationPlayer::AafAnimationPlayer(esp_lcd_panel_io_handle_t panel_io,
                                       esp_lcd_panel_handle_t panel,
                                       int canvas_x, int canvas_y,
                                       int canvas_width, int canvas_height)
    : panel_io_(panel_io)
    , panel_(panel)
    , player_handle_(nullptr)
    , canvas_x_(canvas_x)
    , canvas_y_(canvas_y)
    , canvas_width_(canvas_width)
    , canvas_height_(canvas_height)
    , is_playing_(false)
    , is_paused_(false) {
    
    ESP_LOGI(TAG, "Creating AAF Animation Player: canvas=(%d,%d,%d,%d)",
             canvas_x, canvas_y, canvas_width, canvas_height);
    
    // 配置 anim_player
    // user_data 设置为 this，方便在回调中访问成员变量
    anim_player_config_t config = {
        .flush_cb = OnFlush,
        .update_cb = OnUpdate,
        .user_data = this,  // 传递 this 指针
        .flags = {
            .swap = true,  // 字节交换（适配 RGB565）
        },
        .task = {
            .task_priority = 5,
            .task_stack = 4096,
            .task_affinity = -1,  // 不固定 CPU
            .task_stack_caps = static_cast<unsigned>(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        },
    };
    
    player_handle_ = anim_player_init(&config);
    if (player_handle_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize anim_player");
        return;
    }
    
    // 保存 this 指针到静态变量，供回调使用
    instance_ = this;
    
    // 注意：不能注册 on_color_trans_done 回调！
    // 因为 LVGL 已经注册了该回调，覆盖它会导致 LVGL 在 wait_for_flushing 中卡死
    // 我们使用 LVGL port 锁 + 延时来同步 SPI 传输
    
    ESP_LOGI(TAG, "AAF Animation Player created successfully (using LVGL port sync)");
}

AafAnimationPlayer::~AafAnimationPlayer() {
    ESP_LOGI(TAG, "Destroying AAF Animation Player");
    
    // 清理静态实例指针
    if (instance_ == this) {
        instance_ = nullptr;
    }
    
    if (player_handle_) {
        Stop();
        anim_player_deinit(player_handle_);
        player_handle_ = nullptr;
    }
}

bool AafAnimationPlayer::Play(const PlaybackConfig& config) {
    if (!player_handle_) {
        ESP_LOGE(TAG, "Player not initialized");
        return false;
    }
    
    if (!config.data_address || config.data_length == 0) {
        ESP_LOGE(TAG, "Invalid playback config");
        return false;
    }
    
    ESP_LOGI(TAG, "Playing animation: addr=0x%08lx, size=%lu, fps=%d, mode=%s",
             (unsigned long)config.data_address, (unsigned long)config.data_length, config.fps,
             config.mode == PlayMode::Loop ? "Loop" : "Once");
    
    // 如果需要打断当前动画，先停止
    if (config.interrupt_current && is_playing_) {
        ESP_LOGD(TAG, "Interrupting current animation");
        Stop();
    }
    
    // 设置动画数据
    esp_err_t ret = anim_player_set_src_data(player_handle_, 
                                              config.data_address,
                                              config.data_length);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set src data: %s", esp_err_to_name(ret));
        return false;
    }
    
    // 获取动画段信息
    uint32_t start_frame = 0;
    uint32_t end_frame = 0;
    anim_player_get_segment(player_handle_, &start_frame, &end_frame);
    
    ESP_LOGD(TAG, "Animation segment: frames %lu-%lu", start_frame, end_frame);
    
    // 设置播放参数
    bool is_repeat = (config.mode == PlayMode::Loop);
    anim_player_set_segment(player_handle_, start_frame, end_frame, config.fps, is_repeat);
    
    // 开始播放
    anim_player_update(player_handle_, PLAYER_ACTION_START);
    
    is_playing_ = true;
    is_paused_ = false;
    
    ESP_LOGI(TAG, "Animation started successfully");
    return true;
}

bool AafAnimationPlayer::Stop() {
    if (!player_handle_) {
        ESP_LOGE(TAG, "Player not initialized");
        return false;
    }
    
    if (!is_playing_) {
        ESP_LOGD(TAG, "Player is not playing");
        return true;
    }
    
    ESP_LOGI(TAG, "Stopping animation");
    
    anim_player_update(player_handle_, PLAYER_ACTION_STOP);
    
    is_playing_ = false;
    is_paused_ = false;
    current_animation_.clear();
    
    return true;
}

bool AafAnimationPlayer::Pause() {
    if (!player_handle_) {
        ESP_LOGE(TAG, "Player not initialized");
        return false;
    }
    
    if (!is_playing_ || is_paused_) {
        ESP_LOGD(TAG, "Cannot pause: not playing or already paused");
        return false;
    }
    
    ESP_LOGI(TAG, "Pausing animation");
    
    // image_player 库不支持 PLAYER_ACTION_PAUSE，使用 STOP 作为替代
    anim_player_update(player_handle_, PLAYER_ACTION_STOP);
    
    is_paused_ = true;
    return true;
}

bool AafAnimationPlayer::Resume() {
    if (!player_handle_) {
        ESP_LOGE(TAG, "Player not initialized");
        return false;
    }
    
    if (!is_paused_) {
        ESP_LOGD(TAG, "Player is not paused");
        return false;
    }
    
    ESP_LOGI(TAG, "Resuming animation");
    
    anim_player_update(player_handle_, PLAYER_ACTION_START);
    
    is_paused_ = false;
    return true;
}

// 静态实例指针，用于在回调中访问成员变量
AafAnimationPlayer* AafAnimationPlayer::instance_ = nullptr;

bool AafAnimationPlayer::OnFlushIoReady(esp_lcd_panel_io_handle_t panel_io,
                                        esp_lcd_panel_io_event_data_t* edata,
                                        void* user_ctx) {
    // 此回调不再使用（我们不注册它，以避免覆盖 LVGL 的回调）
    // 保留函数定义以满足头文件声明
    return true;
}

void AafAnimationPlayer::OnFlush(anim_player_handle_t handle,
                                 int x_start, int y_start,
                                 int x_end, int y_end,
                                 const void* color_data) {
    // 获取 this 指针（存储在 user_data 中）
    auto* self = static_cast<AafAnimationPlayer*>(anim_player_get_user_data(handle));
    if (!self || !self->panel_) {
        ESP_LOGE(TAG, "OnFlush: self or panel is null");
        // 必须通知 flush_ready，否则播放器会挂起
        anim_player_flush_ready(handle);
        return;
    }
    
    // 调整坐标（考虑 canvas 偏移）
    int adj_x_start = x_start + self->canvas_x_;
    int adj_y_start = y_start + self->canvas_y_;
    int adj_x_end = x_end + self->canvas_x_;
    int adj_y_end = y_end + self->canvas_y_;
    
    // 获取 LVGL port 锁，避免与 LVGL 刷新冲突
    // 这确保我们不会在 LVGL 正在刷新时尝试写入 SPI
    if (!lvgl_port_lock(50)) {  // 50ms 超时
        ESP_LOGW(TAG, "OnFlush: Failed to acquire LVGL lock, skipping frame");
        anim_player_flush_ready(handle);
        return;
    }
    
    // 绘制到 LCD
    // esp_lcd_panel_draw_bitmap 将数据放入 SPI 队列
    esp_lcd_panel_draw_bitmap(self->panel_, adj_x_start, adj_y_start, 
                              adj_x_end, adj_y_end, color_data);
    
    // 释放 LVGL 锁
    lvgl_port_unlock();
    
    // 等待 SPI 传输完成
    // 计算大致的传输时间：
    // 数据大小 = (x_end - x_start) * (y_end - y_start) * 2 bytes (RGB565)
    // SPI 速度 = 40MHz, 有效速率约 20Mbps（考虑开销）
    // 对于 320x50 区域：320*50*2*8 / 20000000 ≈ 1.3ms
    // 使用固定的小延时来确保传输完成
    vTaskDelay(pdMS_TO_TICKS(2));  // 2ms 延时
    
    // 通知 anim_player 刷新完成，可以发送下一帧
    anim_player_flush_ready(handle);
}

void AafAnimationPlayer::OnUpdate(anim_player_handle_t handle, player_event_t event) {
    // 获取 this 指针（存储在 user_data 中）
    auto* self = static_cast<AafAnimationPlayer*>(anim_player_get_user_data(handle));
    if (!self) {
        return;
    }
    
    switch (event) {
        case PLAYER_EVENT_ALL_FRAME_DONE:
            ESP_LOGD(TAG, "Animation frame done");
            // 可以在这里触发帧回调
            break;
            
        case PLAYER_EVENT_IDLE:
            ESP_LOGI(TAG, "Animation ended");
            self->is_playing_ = false;
            self->is_paused_ = false;
            
            // 触发结束回调
            if (self->on_animation_end_) {
                self->on_animation_end_();
            }
            break;
            
        default:
            break;
    }
}

} // namespace display
} // namespace xiaozhi

