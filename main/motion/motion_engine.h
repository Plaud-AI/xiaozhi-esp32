#ifndef MOTION_ENGINE_H
#define MOTION_ENGINE_H

#include <string>
#include <map>
#include <memory>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include "motion_sequence.h"

class MotorController;

/**
 * @brief 动作引擎 - 负责动作编排和执行
 * 
 * 职责：
 * - 动作序列管理
 * - 动作预设库
 * - 情绪动作映射
 * - 动作播放控制
 */
class MotionEngine {
public:
    static MotionEngine& GetInstance();

    // 初始化
    void Initialize();
    void Start();
    void Stop();
    bool IsRunning() const { return running_; }

    // 动作播放
    void PlayMotion(const std::string& motion_name);
    void PlayMotionSequence(const MotionSequence& sequence);
    void PlayEmotionMotion(const std::string& emotion);

    // 动作控制
    void StopMotion();
    void PauseMotion();
    void ResumeMotion();

    // 状态查询
    bool IsPlaying() const;
    std::string GetCurrentMotion() const;

    // 动作注册
    void RegisterMotion(const std::string& name, const MotionSequence& sequence);
    bool HasMotion(const std::string& name) const;

    // 回调
    void SetOnMotionCompleteCallback(std::function<void()> callback);

private:
    MotionEngine();
    ~MotionEngine();
    MotionEngine(const MotionEngine&) = delete;
    MotionEngine& operator=(const MotionEngine&) = delete;

    static void PlaybackTask(void* param);
    void RunPlaybackLoop();
    void LoadPresets();
    void ExecuteMotionStep(const MotionStep& step);

    bool running_;
    bool is_playing_;
    bool is_paused_;
    std::string current_motion_;
    MotionSequence current_sequence_;
    size_t current_step_index_;

    MotorController* motor_controller_;
    std::map<std::string, MotionSequence> motion_presets_;
    std::map<std::string, std::string> emotion_motion_map_;

    std::function<void()> on_motion_complete_;
    TaskHandle_t playback_task_handle_;
};

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // MOTION_ENGINE_H

