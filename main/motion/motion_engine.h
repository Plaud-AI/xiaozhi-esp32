#ifndef MOTION_ENGINE_H
#define MOTION_ENGINE_H

#include <string>
#include <map>
#include <memory>
#include <functional>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include "motion_sequence.h"

class MotorController;

/**
 * @brief 动作引擎 - 负责动作编排和执行
 * 
 * 功能：
 * - 9 个 P0 基础动作预设
 * - 动作序列播放
 * - 循环动作支持（speaking、thinking、idle_alive）
 * - 完整测试接口
 */
class MotionEngine {
public:
    static MotionEngine& GetInstance();

    // ==================== 初始化 ====================
    void Initialize();
    void Start();
    void Stop();
    bool IsRunning() const { return running_; }

    // ==================== 动作播放 ====================
    
    /**
     * @brief 播放预设动作（按名称）
     * @param motion_name 动作名称，如 "nod", "shake", "greeting"
     */
    void PlayMotion(const std::string& motion_name);
    
    /**
     * @brief 播放 P0 动作（按 ID）
     * @param id P0 动作 ID
     */
    void PlayP0Motion(P0ActionId id);
    
    /**
     * @brief 播放自定义动作序列
     * @param sequence 动作序列
     */
    void PlayMotionSequence(const MotionSequence& sequence);
    
    /**
     * @brief 播放情绪对应的动作
     * @param emotion 情绪名称，如 "happy", "sad"
     */
    void PlayEmotionMotion(const std::string& emotion);

    // ==================== 动作控制 ====================
    void StopMotion();
    void PauseMotion();
    void ResumeMotion();

    // ==================== 状态查询 ====================
    bool IsPlaying() const;
    bool IsPaused() const { return is_paused_; }
    std::string GetCurrentMotion() const;

    // ==================== 动作注册 ====================
    void RegisterMotion(const std::string& name, const MotionSequence& sequence);
    bool HasMotion(const std::string& name) const;
    std::vector<std::string> GetMotionNames() const;

    // ==================== 回调 ====================
    void SetOnMotionCompleteCallback(std::function<void()> callback);
    void SetOnMotionStartCallback(std::function<void(const std::string&)> callback);

    // ==================== 测试接口 ====================
    
    /**
     * @brief 获取所有已注册动作列表
     * @return 动作名称和描述的列表
     */
    std::string GetMotionListString() const;
    
    /**
     * @brief 获取当前状态字符串
     */
    std::string GetStatusString() const;
    
    /**
     * @brief 测试单个动作步骤
     * @param step 动作步骤
     */
    void TestStep(const MotionStep& step);
    
    /**
     * @brief 测试所有 P0 动作
     * @param delay_between_ms 动作间隔时间
     */
    void TestAllP0Actions(uint32_t delay_between_ms = 2000);
    
    /**
     * @brief 启动/停止 idle_alive 定时器
     */
    void StartIdleAliveTimer();
    void StopIdleAliveTimer();
    bool IsIdleAliveTimerRunning() const { return idle_alive_timer_ != nullptr; }

private:
    MotionEngine();
    ~MotionEngine();
    MotionEngine(const MotionEngine&) = delete;
    MotionEngine& operator=(const MotionEngine&) = delete;

    static void PlaybackTask(void* param);
    void RunPlaybackLoop();
    void LoadP0Presets();
    void LoadEmotionMappings();
    void ExecuteMotionStep(const MotionStep& step);
    
    static void IdleAliveTimerCallback(TimerHandle_t timer);

    bool running_;
    bool is_playing_;
    bool is_paused_;
    bool should_stop_;
    std::string current_motion_;
    MotionSequence current_sequence_;
    size_t current_step_index_;

    MotorController* motor_controller_;
    std::map<std::string, MotionSequence> motion_presets_;
    std::map<std::string, std::string> emotion_motion_map_;

    std::function<void()> on_motion_complete_;
    std::function<void(const std::string&)> on_motion_start_;
    TaskHandle_t playback_task_handle_;
    TimerHandle_t idle_alive_timer_;
};

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // MOTION_ENGINE_H
