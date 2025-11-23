#ifndef WAKE_WORD_CONSTANTS_H
#define WAKE_WORD_CONSTANTS_H

/**
 * 唤醒词相关常量定义
 * 
 * 此文件集中管理所有唤醒词相关的常量，便于统一维护
 */

// 默认唤醒词检测阈值（范围 0.0-1.0）
// - 值越高：越严格，误触发越少，但可能漏检真实唤醒
// - 值越低：越灵敏，检测率越高，但误触发也越多
// - 推荐值：0.2（平衡灵敏度和准确率）
constexpr float DEFAULT_WAKE_WORD_THRESHOLD = 0.2f;

#endif // WAKE_WORD_CONSTANTS_H

