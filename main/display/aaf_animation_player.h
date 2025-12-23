#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <string>
#include <vector>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include "anim_player.h"

namespace xiaozhi {
namespace display {

/**
 * @brief AAF Animation Player - 参考 esp_brookesia 官方实现重写
 * 
 * 核心设计原则：
 * 1. 使用事件队列 + 专用线程处理动画控制
 * 2. 通过回调/信号机制解耦帧渲染和 LCD 驱动
 * 3. 使用 std::future/promise 支持异步等待
 * 4. 正确的状态同步避免竞态条件
 */

// 画布配置
struct AnimCanvasConfig {
    int coord_x;       // 画布 X 偏移
    int coord_y;       // 画布 Y 偏移
    int width;         // 画布宽度
    int height;        // 画布高度
};

// 动画数据配置
struct AnimDataConfig {
    const void* data_address;   // 动画数据地址（Flash mmap 或 SRAM）
    size_t data_length;         // 数据长度
    int fps;                    // 帧率
};

// 播放器任务配置
struct AnimTaskConfig {
    int task_priority;      // 任务优先级
    int task_stack;         // 栈大小
    int task_affinity;      // CPU 亲和性 (-1 = 不固定)
    bool task_stack_in_ext; // 栈是否在外部 SRAM
};

// 播放器初始化数据
struct AnimPlayerInitData {
    AnimCanvasConfig canvas;
    AnimTaskConfig task;
    struct {
        uint8_t enable_data_swap_bytes: 1;  // RGB565 字节交换
    } flags;
};

/**
 * @brief AAF 动画播放器类
 */
class AafAnimationPlayer {
public:
    // 播放操作类型
    enum class Operation {
        Stop,           // 停止播放
        PlayLoop,       // 循环播放
        PlayOnceStop,   // 播放一次后停止
        PlayOncePause,  // 播放一次后暂停（保持最后一帧）
        Pause,          // 暂停当前播放
    };
    
    // 播放器状态
    enum class State {
        Stopped = (1U << 0),
        Playing = (1U << 1),
        Paused  = (1U << 2),
    };
    
    // 播放事件
    struct PlayEvent {
        int index;                  // 动画索引 (-1 表示当前)
        Operation operation;        // 操作类型
        const AnimDataConfig* data; // 动画数据（可选，用于直接播放数据）
        struct {
            uint8_t enable_interrupt: 1;  // 是否中断当前播放
            uint8_t force: 1;             // 强制执行（即使相同动画）
        } flags;
    };
    
    // Future 类型，用于异步等待
    using EventFuture = std::future<void>;
    
    // 帧刷新回调
    // 参数: x_start, y_start, x_end, y_end, color_data, user_data
    using FlushCallback = std::function<void(int, int, int, int, const void*, void*)>;
    
    // 动画停止回调
    // 参数: x_start, y_start, x_end, y_end, user_data
    using StopCallback = std::function<void(int, int, int, int, void*)>;
    
    // 特殊索引
    static constexpr int INDEX_NONE = -1;
    static constexpr int INDEX_CURRENT = -2;
    
    AafAnimationPlayer() = default;
    ~AafAnimationPlayer();
    
    // 禁止拷贝
    AafAnimationPlayer(const AafAnimationPlayer&) = delete;
    AafAnimationPlayer& operator=(const AafAnimationPlayer&) = delete;
    
    /**
     * @brief 初始化播放器
     * @param data 初始化配置
     * @return 是否成功
     */
    bool Begin(const AnimPlayerInitData& data);
    
    /**
     * @brief 销毁播放器
     * @return 是否成功
     */
    bool Delete();
    
    /**
     * @brief 发送播放事件
     * @param event 事件配置
     * @param clear_queue 是否清空事件队列
     * @param future 可选的 future 指针，用于异步等待
     * @return 是否成功
     */
    bool SendEvent(const PlayEvent& event, bool clear_queue, EventFuture* future = nullptr);
    
    /**
     * @brief 快捷方法：播放指定动画数据
     * @param data 动画数据配置
     * @param loop 是否循环
     * @param interrupt 是否中断当前
     * @return 是否成功
     */
    bool Play(const AnimDataConfig& data, bool loop = true, bool interrupt = true);
    
    /**
     * @brief 快捷方法：停止播放
     * @return 是否成功
     */
    bool Stop();
    
    /**
     * @brief 通知帧刷新完成（由外部 LCD 驱动调用）
     * @return 是否成功
     */
    bool NotifyFlushFinished() const;
    
    /**
     * @brief 设置帧刷新回调
     * @param callback 回调函数
     * @param user_data 用户数据
     */
    void SetFlushCallback(FlushCallback callback, void* user_data);
    
    /**
     * @brief 设置动画停止回调
     * @param callback 回调函数
     * @param user_data 用户数据
     */
    void SetStopCallback(StopCallback callback, void* user_data);
    
    // 状态查询
    bool IsPlaying() const { return state_ == State::Playing; }
    bool IsPaused() const { return state_ == State::Paused; }
    bool IsStopped() const { return state_ == State::Stopped; }
    State GetState() const { return state_; }
    
private:
    using EventPromise = std::promise<void>;
    struct EventWrapper {
        PlayEvent event;
        AnimDataConfig data_copy;  // 动画数据的本地副本
        std::shared_ptr<EventPromise> promise;
    };
    
    // 等待播放器帧完成
    bool WaitPlayerFrameDone();
    // 等待播放器空闲
    bool WaitPlayerIdle();
    // 等待播放器达到指定状态
    bool WaitPlayerState(State target_state);
    // 处理播放事件
    bool ProcessEvent(std::shared_ptr<EventWrapper> event_wrapper);
    
    // 初始化标志
    bool is_begun_ = false;
    
    // 画布配置
    AnimCanvasConfig canvas_config_ = {};
    
    // 当前动画数据（用于状态比较）
    AnimDataConfig current_anim_ = {};
    
    // 事件处理线程
    std::atomic<bool> event_thread_exit_ = false;
    std::thread event_thread_;
    std::queue<std::shared_ptr<EventWrapper>> event_queue_;
    std::shared_ptr<EventWrapper> current_event_;
    std::mutex event_mutex_;
    std::condition_variable event_cv_;
    
    // 播放器状态
    std::mutex player_mutex_;
    struct {
        uint8_t is_starting: 1;
        uint8_t is_frame_done: 1;
    } player_flags_ = {};
    std::atomic<State> state_ = State::Stopped;
    std::condition_variable player_cv_;
    
    // anim_player 句柄
    anim_player_handle_t player_handle_ = nullptr;
    
    // 回调
    FlushCallback flush_callback_;
    StopCallback stop_callback_;
    void* callback_user_data_ = nullptr;
};

} // namespace display
} // namespace xiaozhi
