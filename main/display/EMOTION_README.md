# 情感动画系统快速参考

## 文件列表

- `emotion_animation_manager.h` - 情感动画管理器头文件
- `emotion_animation_manager.cc` - 情感动画管理器实现
- `emotion_test.cc` - 测试代码和使用示例

## 快速开始

### 1. 初始化

```cpp
#include "emotion_animation_manager.h"

// 在 LVGL 初始化后
AnimationManager::Instance().Init(parent, 240, 240);
EmotionAnimationManager::Instance().Init();
```

### 2. 注册情感

```cpp
auto& mgr = EmotionAnimationManager::Instance();

// 批量注册
mgr.RegisterEmotionsFromDirectory("/spiffs/emotions/");

// 或单个注册
mgr.RegisterEmotion(EmotionType::HAPPY, "/spiffs/emotions/happy.json");
```

### 3. 显示情感

```cpp
// 显示单个情感
mgr.ShowEmotion(EmotionType::HAPPY);

// 播放序列
std::vector<EmotionSequenceItem> seq;
seq.emplace_back(EmotionType::HAPPY, 2000);
seq.emplace_back(EmotionType::EXCITED, 2000);
mgr.PlayEmotionSequence(seq, false);
```

## 支持的情感

| 类型 | 字符串 | 描述 |
|------|--------|------|
| HAPPY | "happy" | 😊 开心 |
| SAD | "sad" | 😢 难过 |
| SURPRISED | "surprised" | 😮 惊讶 |
| ANGRY | "angry" | 😠 生气 |
| CONFUSED | "confused" | 🤔 疑惑 |
| SLEEPY | "sleepy" | 😴 困倦 |
| NEUTRAL | "neutral" | 😐 中性 |
| EXCITED | "excited" | 😆 兴奋 |
| CALM | "calm" | 😌 平静 |
| THINKING | "thinking" | 🤓 思考 |
| LOVE | "love" | 😍 喜爱 |

## 测试方法

```cpp
// C++ 接口
EmotionAnimationManager::Instance().TestEmotion(EmotionType::HAPPY, 1);

// C 接口
emotion_anim_test("happy", 1);
```

## 详细文档

完整使用指南请查看：
`/Users/xionghao/Documents/plaud/GitHub/docs/xiaozhi-esp32/display/emotion-animation-system.md`

