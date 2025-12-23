#include "aaf_animation_player.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <algorithm>
#include <thread>
#include <chrono>

static const char* TAG = "AafAnimPlayer";

// 事件线程退出检查间隔
#define THREAD_EXIT_CHECK_INTERVAL_MS   100
// 事件线程配置
#define ANIM_EVENT_THREAD_STACK_SIZE    (8 * 1024)

namespace xiaozhi {
namespace display {

AafAnimationPlayer::~AafAnimationPlayer() {
    if (is_begun_) {
        if (!Delete()) {
            ESP_LOGE(TAG, "Failed to delete anim player in destructor");
        }
    }
}

bool AafAnimationPlayer::Begin(const AnimPlayerInitData& data) {
    ESP_LOGI(TAG, "Begin: canvas=(%d,%d,%dx%d)",
             data.canvas.coord_x, data.canvas.coord_y,
             data.canvas.width, data.canvas.height);
    
    if (is_begun_) {
        ESP_LOGW(TAG, "Already begun");
        return true;
    }
    
    // 保存画布配置
    canvas_config_ = data.canvas;
    
    // 初始化 anim_player
    anim_player_config_t config = {
        .flush_cb = [](anim_player_handle_t handle, int x1, int y1, int x2, int y2, const void* data) {
            auto* self = static_cast<AafAnimationPlayer*>(anim_player_get_user_data(handle));
            
            // 调试日志：跟踪 anim_player 的 flush 回调（每1000次输出一次，debug级别）
            static int anim_flush_count = 0;
            anim_flush_count++;
            if (anim_flush_count % 1000 == 1) {
                ESP_LOGD(TAG, "anim_flush_cb #%d: (%d,%d)-(%d,%d), self=%p, data=%p",
                         anim_flush_count, x1, y1, x2, y2, (void*)self, data);
            }
            
            if (!self) {
                ESP_LOGE(TAG, "Invalid user data in flush callback");
                anim_player_flush_ready(handle);
                return;
            }
            
            auto& canvas = self->canvas_config_;
            
            // **关键修复**: 检查条带是否完全超出画布范围
            // 动画可能比画布大（如 1000x1000 动画在 240x240 画布上），
            // 超出部分应该直接跳过，不传递给回调
            if (y1 >= canvas.height || x1 >= canvas.width) {
                // 条带起始点在画布外，直接跳过
                anim_player_flush_ready(handle);
                return;
            }
            
            // 计算原始动画条带尺寸
            int src_width = x2 - x1;
            int src_height = y2 - y1;
            
            // 计算实际要绘制的尺寸（裁剪到画布范围）
            int dst_width = std::min(x2, canvas.width) - x1;
            int dst_height = std::min(y2, canvas.height) - y1;
            
            // 计算屏幕坐标（加上画布偏移）
            int x_start = x1 + canvas.coord_x;
            int y_start = y1 + canvas.coord_y;
            int x_end = x_start + dst_width;
            int y_end = y_start + dst_height;
            
            // 最终有效性检查
            if (x_start >= x_end || y_start >= y_end || dst_width <= 0 || dst_height <= 0) {
                anim_player_flush_ready(handle);
                return;
            }
            
            // **关键修复**: 如果动画宽度大于画布宽度，需要裁剪数据
            // 因为 data 缓冲区的行宽是 src_width，但我们只绘制 dst_width
            // 直接传递会导致 LCD 驱动读取错误的像素位置
            if (src_width > dst_width && self->flush_callback_) {
                // 分配临时缓冲区（只需要裁剪后的大小）
                size_t crop_size = dst_width * dst_height * 2;  // RGB565
                uint8_t* crop_buffer = (uint8_t*)heap_caps_malloc(crop_size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
                if (!crop_buffer) {
                    // 内存不足，跳过此条带
                    static int alloc_fail_count = 0;
                    if (++alloc_fail_count <= 3) {
                        ESP_LOGW(TAG, "Failed to allocate crop buffer (%zu bytes), skipping", crop_size);
                    }
                    anim_player_flush_ready(handle);
                    return;
                }
                
                // 逐行复制数据（只复制每行的前 dst_width 像素）
                const uint8_t* src_ptr = (const uint8_t*)data;
                for (int row = 0; row < dst_height; row++) {
                    memcpy(crop_buffer + row * dst_width * 2,
                           src_ptr + row * src_width * 2,
                           dst_width * 2);
                }
                
                // 使用裁剪后的数据绘制
                self->flush_callback_(x_start, y_start, x_end, y_end, crop_buffer, self->callback_user_data_);
                
                heap_caps_free(crop_buffer);
            } else if (self->flush_callback_) {
                // 动画尺寸正常，直接绘制
                self->flush_callback_(x_start, y_start, x_end, y_end, data, self->callback_user_data_);
            }
            
            // **同步模式**: 在 flush_cb 中同步等待 DMA 完成后调用 flush_ready
            // 这确保了每帧完全传输后才进行下一帧
            anim_player_flush_ready(handle);
        },
        .update_cb = [](anim_player_handle_t handle, player_event_t event) {
            auto* self = static_cast<AafAnimationPlayer*>(anim_player_get_user_data(handle));
            if (!self) {
                ESP_LOGE(TAG, "Invalid user data in update callback");
                return;
            }
            
            // 只处理帧完成和空闲事件
            if (event != PLAYER_EVENT_ALL_FRAME_DONE && event != PLAYER_EVENT_IDLE) {
                return;
            }
            
            std::unique_lock<std::mutex> lock(self->player_mutex_);
            
            if (event == PLAYER_EVENT_ALL_FRAME_DONE) {
                // 一轮播放完成
                self->player_flags_.is_frame_done = true;
                ESP_LOGD(TAG, "All frames done (one loop)");
            } else if (event == PLAYER_EVENT_IDLE) {
                // 播放器进入空闲
                ESP_LOGD(TAG, "Player idle event received");
                self->state_ = State::Stopped;
                
                auto& event_wrapper = self->current_event_;
                if (event_wrapper) {
                    auto& play_event = event_wrapper->event;
                    
                    if (play_event.operation == Operation::PlayOnceStop) {
                        ESP_LOGI(TAG, "Animation play once stop");
                        
                        // 检查是否有待处理事件
                        if (self->event_queue_.empty() && !self->player_flags_.is_starting) {
                            // 发送停止事件清理画面
                            self->SendEvent({INDEX_NONE, Operation::Stop, nullptr, {1, 1}}, false);
                        } else {
                            // 有待处理事件，完成当前 promise
                            if (event_wrapper->promise) {
                                event_wrapper->promise->set_value();
                            }
                            event_wrapper.reset();
                        }
                    } else {
                        if (play_event.operation == Operation::PlayOncePause) {
                            ESP_LOGI(TAG, "Animation play once pause");
                            self->state_ = State::Paused;
                        } else {
                            ESP_LOGI(TAG, "Animation stopped");
                        }
                        
                        // 完成 promise
                        if (event_wrapper->promise) {
                            event_wrapper->promise->set_value();
                        }
                        event_wrapper.reset();
                    }
                }
            }
            
            self->player_cv_.notify_all();
        },
        .user_data = this,
        .flags = {
            .swap = static_cast<unsigned char>(data.flags.enable_data_swap_bytes),
        },
        .task = {
            .task_priority = data.task.task_priority,
            .task_stack = data.task.task_stack,
            .task_affinity = data.task.task_affinity,
            .task_stack_caps = static_cast<unsigned>(
                (data.task.task_stack_in_ext ? MALLOC_CAP_SPIRAM : MALLOC_CAP_DEFAULT) | MALLOC_CAP_8BIT
            ),
        },
    };
    
    player_handle_ = anim_player_init(&config);
    if (!player_handle_) {
        ESP_LOGE(TAG, "Failed to init anim_player");
        return false;
    }
    
    // 启动事件处理线程
    event_thread_exit_ = false;
    event_thread_ = std::thread([this]() {
        ESP_LOGI(TAG, "Event thread started");
        
        while (!event_thread_exit_) {
            std::unique_lock<std::mutex> lock(event_mutex_);
            
            // 等待事件或退出信号
            while (event_queue_.empty() && !event_thread_exit_) {
                event_cv_.wait_for(lock, std::chrono::milliseconds(THREAD_EXIT_CHECK_INTERVAL_MS));
            }
            
            if (event_thread_exit_) {
                ESP_LOGI(TAG, "Event thread exit requested");
                break;
            }
            
            // 处理队列中的所有事件
            while (!event_queue_.empty() && !event_thread_exit_) {
                auto event_wrapper = event_queue_.front();
                event_queue_.pop();
                
                lock.unlock();
                if (!ProcessEvent(event_wrapper)) {
                    ESP_LOGE(TAG, "Failed to process event");
                }
                lock.lock();
            }
        }
        
        ESP_LOGI(TAG, "Event thread exited");
    });
    
    is_begun_ = true;
    state_ = State::Stopped;
    
    ESP_LOGI(TAG, "Animation player initialized successfully");
    return true;
}

bool AafAnimationPlayer::Delete() {
    ESP_LOGI(TAG, "Delete");
    
    // 停止事件线程
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        event_thread_exit_ = true;
        event_cv_.notify_all();
    }
    
    if (event_thread_.joinable()) {
        event_thread_.join();
    }
    
    // 清理事件队列
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        while (!event_queue_.empty()) {
            auto event_wrapper = event_queue_.front();
            event_queue_.pop();
            if (event_wrapper && event_wrapper->promise) {
                event_wrapper->promise->set_value();
            }
        }
        if (current_event_ && current_event_->promise) {
            current_event_->promise->set_value();
        }
        current_event_.reset();
    }
    
    // 销毁 anim_player
    if (player_handle_) {
        anim_player_deinit(player_handle_);
        player_handle_ = nullptr;
    }
    
    is_begun_ = false;
    state_ = State::Stopped;
    
    ESP_LOGI(TAG, "Animation player deleted");
    return true;
}

bool AafAnimationPlayer::SendEvent(const PlayEvent& event, bool clear_queue, EventFuture* future) {
    ESP_LOGD(TAG, "SendEvent: index=%d, op=%d, interrupt=%d, force=%d",
             event.index, static_cast<int>(event.operation),
             event.flags.enable_interrupt, event.flags.force);
    
    std::lock_guard<std::mutex> lock(event_mutex_);
    
    // 清空队列
    if (clear_queue) {
        while (!event_queue_.empty()) {
            auto wrapper = event_queue_.front();
            event_queue_.pop();
            ESP_LOGD(TAG, "Pop event from queue");
            if (wrapper && wrapper->promise) {
                wrapper->promise->set_value();
            }
        }
    }
    
    // 创建 promise（如果需要）
    std::shared_ptr<EventPromise> promise = nullptr;
    if (future) {
        promise = std::make_shared<EventPromise>();
    }
    
    // 创建事件包装器
    auto event_wrapper = std::make_shared<EventWrapper>();
    event_wrapper->event = event;
    event_wrapper->promise = promise;
    
    // 如果有数据指针，复制数据配置
    if (event.data) {
        event_wrapper->data_copy = *event.data;
        event_wrapper->event.data = &event_wrapper->data_copy;
    }
    
    event_queue_.push(event_wrapper);
    event_cv_.notify_all();
    
    if (future && promise) {
        *future = promise->get_future();
    }
    
    return true;
}

bool AafAnimationPlayer::Play(const AnimDataConfig& data, bool loop, bool interrupt) {
    // **DEBUG**: 设置为 true 来禁用动画切换（仅播放第一个动画）
    // 这可以帮助排查白屏是否由动画切换引起
    static constexpr bool DEBUG_DISABLE_ANIMATION_SWITCH = false;
    static bool first_animation_played = false;
    
    if (DEBUG_DISABLE_ANIMATION_SWITCH && first_animation_played) {
        ESP_LOGW(TAG, "DEBUG: Animation switch disabled, skipping new animation");
        return true;  // 假装成功，但不实际切换
    }
    
    if (!is_begun_) {
        ESP_LOGE(TAG, "Player not initialized");
        return false;
    }
    
    if (!data.data_address || data.data_length == 0) {
        ESP_LOGE(TAG, "Invalid animation data: addr=%p, size=%zu",
                 data.data_address, data.data_length);
        return false;
    }
    
    ESP_LOGI(TAG, "Play: addr=%p, size=%zu, fps=%d, loop=%d",
             data.data_address, data.data_length, data.fps, loop);
    
    first_animation_played = true;  // 标记第一次播放完成
    
    // 创建临时数据配置
    AnimDataConfig data_config = data;
    
    PlayEvent event = {
        .index = 0,
        .operation = loop ? Operation::PlayLoop : Operation::PlayOnceStop,
        .data = &data_config,
        .flags = {
            .enable_interrupt = static_cast<uint8_t>(interrupt ? 1 : 0),
            .force = 0,
        },
    };
    
    return SendEvent(event, interrupt, nullptr);
}

bool AafAnimationPlayer::Stop() {
    if (!is_begun_) {
        return false;
    }
    
    ESP_LOGI(TAG, "Stop");
    
    PlayEvent event = {
        .index = INDEX_NONE,
        .operation = Operation::Stop,
        .data = nullptr,
        .flags = {
            .enable_interrupt = 1,
            .force = 1,
        },
    };
    
    return SendEvent(event, true, nullptr);
}

bool AafAnimationPlayer::NotifyFlushFinished() const {
    if (!player_handle_) {
        return false;
    }
    
    anim_player_flush_ready(player_handle_);
    return true;
}

void AafAnimationPlayer::SetFlushCallback(FlushCallback callback, void* user_data) {
    flush_callback_ = callback;
    callback_user_data_ = user_data;
}

void AafAnimationPlayer::SetStopCallback(StopCallback callback, void* user_data) {
    stop_callback_ = callback;
    // 共用 user_data
    if (!callback_user_data_) {
        callback_user_data_ = user_data;
    }
}

bool AafAnimationPlayer::WaitPlayerFrameDone() {
    std::unique_lock<std::mutex> lock(player_mutex_);
    player_flags_.is_frame_done = false;
    
    while (!event_thread_exit_ && !player_flags_.is_frame_done && (state_ != State::Stopped)) {
        player_cv_.wait_for(lock, std::chrono::milliseconds(THREAD_EXIT_CHECK_INTERVAL_MS));
    }
    
    return true;
}

bool AafAnimationPlayer::WaitPlayerIdle() {
    std::unique_lock<std::mutex> lock(player_mutex_);
    
    int wait_count = 0;
    const int max_wait_count = 50;  // 最多等待 5 秒 (50 * 100ms)
    
    while (!event_thread_exit_ && (state_ != State::Stopped) && (state_ != State::Paused)) {
        player_cv_.wait_for(lock, std::chrono::milliseconds(THREAD_EXIT_CHECK_INTERVAL_MS));
        wait_count++;
        
        if (wait_count % 10 == 0) {
            ESP_LOGW(TAG, "WaitPlayerIdle: still waiting... (state=%d, count=%d/%d)",
                     static_cast<int>(state_.load()), wait_count, max_wait_count);
        }
        
        // 超时保护：如果等待超过 5 秒，强制退出
        if (wait_count >= max_wait_count) {
            ESP_LOGE(TAG, "WaitPlayerIdle: TIMEOUT! Forcing state to Stopped");
            state_ = State::Stopped;
            break;
        }
    }
    
    return true;
}

bool AafAnimationPlayer::WaitPlayerState(State target_state) {
    ESP_LOGD(TAG, "WaitPlayerState: target=%d", static_cast<int>(target_state));
    
    if (state_ == target_state) {
        return true;
    }
    
    std::unique_lock<std::mutex> lock(player_mutex_);
    while (!event_thread_exit_ && (state_ != target_state)) {
        player_cv_.wait_for(lock, std::chrono::milliseconds(THREAD_EXIT_CHECK_INTERVAL_MS));
    }
    
    return true;
}

bool AafAnimationPlayer::ProcessEvent(std::shared_ptr<EventWrapper> event_wrapper) {
    if (!event_wrapper) {
        ESP_LOGE(TAG, "Invalid event wrapper");
        return false;
    }
    
    auto& event = event_wrapper->event;
    ESP_LOGD(TAG, "ProcessEvent: index=%d, op=%d, has_current=%d",
             event.index, static_cast<int>(event.operation), current_event_ ? 1 : 0);
    
    // 检查是否与当前动画相同（非强制）
    if (!event.flags.force && current_event_) {
        auto& cur = current_event_->event;
        if (event.data && current_event_->event.data) {
            // 比较数据地址
            if (event.data->data_address == cur.data->data_address &&
                event.operation == cur.operation) {
                ESP_LOGD(TAG, "Same animation and operation, skip");
                if (event_wrapper->promise) {
                    event_wrapper->promise->set_value();
                }
                return true;
            }
        }
    }
    
    {
        player_flags_.is_starting = true;
        
        // 如果有正在播放的动画，需要先停止
        if (current_event_) {
            if (!event.flags.enable_interrupt) {
                ESP_LOGD(TAG, "Wait for current frame done...");
                WaitPlayerFrameDone();
                ESP_LOGD(TAG, "Frame done, continuing...");
                if (event_thread_exit_) {
                    return true;
                }
            }
            
            ESP_LOGD(TAG, "Stopping current animation...");
            anim_player_update(player_handle_, PLAYER_ACTION_STOP);
            
            ESP_LOGD(TAG, "Waiting for player idle (state=%d)...", static_cast<int>(state_.load()));
            WaitPlayerIdle();
            ESP_LOGD(TAG, "Player idle, continuing...");
            if (event_thread_exit_) {
                return true;
            }
        }
        
        // 处理操作
        switch (event.operation) {
        case Operation::PlayLoop:
        case Operation::PlayOnceStop:
        case Operation::PlayOncePause: {
            current_event_ = event_wrapper;
            
            if (!event.data || !event.data->data_address || event.data->data_length == 0) {
                ESP_LOGE(TAG, "Invalid animation data for play");
                player_flags_.is_starting = false;
                return false;
            }
            
            auto& anim_data = *event.data;
            uint32_t start = 0;
            uint32_t end = 0;
            bool is_repeat = (event.operation == Operation::PlayLoop);
            
            ESP_LOGD(TAG, "Set animation data: addr=%p, size=%zu",
                     anim_data.data_address, anim_data.data_length);
            
            esp_err_t ret = anim_player_set_src_data(player_handle_,
                                                     anim_data.data_address,
                                                     anim_data.data_length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set src data: %s", esp_err_to_name(ret));
                player_flags_.is_starting = false;
                return false;
            }
            
            state_ = State::Playing;
            anim_player_get_segment(player_handle_, &start, &end);
            
            int fps = anim_data.fps > 0 && anim_data.fps < 120 ? anim_data.fps : 25;
            anim_player_set_segment(player_handle_, start, end, fps, is_repeat);
            anim_player_update(player_handle_, PLAYER_ACTION_START);
            
            ESP_LOGI(TAG, "Animation started: frames=%lu-%lu, fps=%d, repeat=%d",
                     start, end, fps, is_repeat);
            
            // 保存当前动画配置
            current_anim_ = anim_data;
            break;
        }
        
        case Operation::Pause:
            // 暂停播放
            ESP_LOGI(TAG, "Pause animation");
            state_ = State::Paused;
            if (event_wrapper->promise) {
                event_wrapper->promise->set_value();
            }
            break;
            
        case Operation::Stop:
            // 停止播放，清空画面
            ESP_LOGI(TAG, "Stop animation");
            state_ = State::Stopped;
            
            // 调用停止回调（用于清空画面）
            if (stop_callback_) {
                stop_callback_(
                    canvas_config_.coord_x,
                    canvas_config_.coord_y,
                    canvas_config_.coord_x + canvas_config_.width,
                    canvas_config_.coord_y + canvas_config_.height,
                    callback_user_data_
                );
            }
            
            if (current_event_ && current_event_->promise) {
                current_event_->promise->set_value();
            }
            current_event_.reset();
            
            if (event_wrapper->promise) {
                event_wrapper->promise->set_value();
            }
            break;
            
        default:
            ESP_LOGE(TAG, "Invalid operation: %d", static_cast<int>(event.operation));
            player_flags_.is_starting = false;
            return false;
        }
        
        player_flags_.is_starting = false;
    }
    
    return true;
}

} // namespace display
} // namespace xiaozhi
