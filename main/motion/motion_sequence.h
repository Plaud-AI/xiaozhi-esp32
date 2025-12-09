#ifndef MOTION_SEQUENCE_H
#define MOTION_SEQUENCE_H

#include <string>
#include <vector>

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

/**
 * @brief 动作步骤类型
 */
enum class MotionStepType {
    // 基础动作
    kRotate = 0,        // 设置 Yaw 角度（绝对）
    kNod,               // 设置 Pitch 角度（绝对）
    kDelay,             // 延迟
    kReset,             // 复位到中位
    
    // 相对移动
    kYawRelative,       // Yaw 相对移动
    kPitchRelative,     // Pitch 相对移动
    kBothRelative,      // 双轴同时相对移动
    
    // 复合动作
    kShake,             // 摇头（param1=幅度, param2=次数）
    kNodRepeat,         // 点头（param1=幅度, param2=次数）
    
    // 特殊
    kHome,              // 回到中位
    kLoop,              // 循环标记（用于 speaking 等持续动作）
};

/**
 * @brief 动作速度预设
 */
enum class MotionSpeed {
    kUltraFast = 100,   // 超快（惊讶、紧急）
    kFast = 200,        // 快速（开心、愤怒）
    kNormal = 300,      // 正常（日常交互）
    kSlow = 500,        // 慢速（思考、倾听）
    kGentle = 800,      // 柔和（悲伤、困倦）
};

/**
 * @brief 单个动作步骤
 */
struct MotionStep {
    MotionStepType type;
    float param1;       // Yaw 角度 / 延迟时间(ms) / 幅度
    float param2;       // Pitch 角度 / 速度(ms) / 重复次数
    float param3;       // 附加参数（用于某些特殊动作）

    // 构造函数
    MotionStep(MotionStepType t, float p1 = 0, float p2 = 0, float p3 = 0)
        : type(t), param1(p1), param2(p2), param3(p3) {}
    
    // 兼容旧接口（int 参数）
    MotionStep(MotionStepType t, int p1, int p2)
        : type(t), param1((float)p1), param2((float)p2), param3(0) {}
};

/**
 * @brief 动作序列
 */
struct MotionSequence {
    std::string name;                   // 动作名称
    std::vector<MotionStep> steps;      // 动作步骤列表
    bool interruptible;                 // 是否可中断
    bool loop;                          // 是否循环播放
    uint8_t priority;                   // 优先级（0=P0, 1=P1, 2=P2）

    MotionSequence() 
        : interruptible(true), loop(false), priority(0) {}
    
    MotionSequence(const std::string& n) 
        : name(n), interruptible(true), loop(false), priority(0) {}
    
    // 便捷构建方法
    MotionSequence& SetName(const std::string& n) { name = n; return *this; }
    MotionSequence& SetInterruptible(bool v) { interruptible = v; return *this; }
    MotionSequence& SetLoop(bool v) { loop = v; return *this; }
    MotionSequence& SetPriority(uint8_t p) { priority = p; return *this; }
    
    MotionSequence& AddStep(const MotionStep& step) {
        steps.push_back(step);
        return *this;
    }
    
    // 便捷添加步骤方法
    MotionSequence& YawRel(float delta, float duration_ms = 200) {
        steps.emplace_back(MotionStepType::kYawRelative, delta, duration_ms);
        return *this;
    }
    
    MotionSequence& PitchRel(float delta, float duration_ms = 200) {
        steps.emplace_back(MotionStepType::kPitchRelative, delta, duration_ms);
        return *this;
    }
    
    MotionSequence& BothRel(float yaw_delta, float pitch_delta, float duration_ms = 200) {
        steps.emplace_back(MotionStepType::kBothRelative, yaw_delta, pitch_delta, duration_ms);
        return *this;
    }
    
    MotionSequence& Delay(float ms) {
        steps.emplace_back(MotionStepType::kDelay, ms);
        return *this;
    }
    
    MotionSequence& Home() {
        steps.emplace_back(MotionStepType::kHome);
        return *this;
    }
    
    MotionSequence& Shake(float amplitude, int repeat) {
        steps.emplace_back(MotionStepType::kShake, amplitude, (float)repeat);
        return *this;
    }
    
    MotionSequence& NodRepeat(float amplitude, int repeat) {
        steps.emplace_back(MotionStepType::kNodRepeat, amplitude, (float)repeat);
        return *this;
    }
};

/**
 * @brief P0 动作 ID 枚举（用于快速索引）
 */
enum class P0ActionId {
    kHome = 0,          // 归位
    kNod,               // 点头
    kShake,             // 摇头
    kGreeting,          // 打招呼
    kListening,         // 倾听
    kSpeaking,          // 说话
    kThinking,          // 思考
    kWakeUp,            // 唤醒响应
    kIdleAlive,         // 待机微动
    kCount              // 总数
};

/**
 * @brief 将 P0ActionId 转换为字符串名称
 */
inline const char* P0ActionIdToName(P0ActionId id) {
    static const char* names[] = {
        "home", "nod", "shake", "greeting", "listening",
        "speaking", "thinking", "wake_up", "idle_alive"
    };
    int idx = static_cast<int>(id);
    if (idx >= 0 && idx < static_cast<int>(P0ActionId::kCount)) {
        return names[idx];
    }
    return "unknown";
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // MOTION_SEQUENCE_H
