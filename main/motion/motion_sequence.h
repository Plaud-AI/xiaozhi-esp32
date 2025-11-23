#ifndef MOTION_SEQUENCE_H
#define MOTION_SEQUENCE_H

#include <string>
#include <vector>

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

/**
 * @brief 动作步骤类型
 */
enum class MotionStepType {
    kRotate = 0,    // 旋转
    kNod,           // 点头
    kShake,         // 摇晃
    kDelay,         // 延迟
    kReset,         // 复位
};

/**
 * @brief 单个动作步骤
 */
struct MotionStep {
    MotionStepType type;
    int param1;     // 角度/延迟时间(ms)/重复次数
    int param2;     // 速度/未使用

    MotionStep(MotionStepType t, int p1, int p2)
        : type(t), param1(p1), param2(p2) {}
};

/**
 * @brief 动作序列
 */
struct MotionSequence {
    std::string name;               // 动作名称
    std::vector<MotionStep> steps;  // 动作步骤列表

    MotionSequence() = default;
    MotionSequence(const std::string& n) : name(n) {}
};

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // MOTION_SEQUENCE_H

