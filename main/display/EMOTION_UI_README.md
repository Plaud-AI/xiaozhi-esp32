# EmotionDisplay UI 系统

## 简介

EmotionDisplay 是一个专门用于显示 Lottie 情感动画的 UI 系统，提供全屏动画显示和可选的顶部状态栏。

## 快速开始

```cpp
#include "emotion_display_factory.h"

// 1. 创建显示
EmotionDisplay* display = CreateEmotionDisplay(240, 240);

// 2. 初始化情感系统
display->InitEmotionSystem("/spiffs/emotions/");

// 3. 显示情感
display->ShowEmotion(EmotionType::HAPPY);

// 4. 显示顶部消息（可选）
display->ShowTopBar(nullptr, "你好！", 3000);
```

## 文件列表

- `emotion_display.h/.cc` - EmotionDisplay 类
- `emotion_display_factory.h/.cc` - 工厂函数
- `emotion_display_example.cc` - 使用示例（7 个完整示例）

## 主要功能

### 情感显示
```cpp
display->ShowEmotion(EmotionType::HAPPY);        // 单个情感
display->PlayEmotionSequence(sequence, false);   // 序列播放
display->TestEmotion(EmotionType::SAD, 3);       // 测试
```

### 顶部状态栏
```cpp
display->ShowTopBar(nullptr, "消息", 3000);      // 显示消息
display->HideTopBar();                           // 隐藏
display->UpdateTopBarText("新消息");             // 更新
```

### 高级功能
```cpp
// 回调
display->SetEmotionChangeCallback([](EmotionType e) {
    ESP_LOGI("APP", "Emotion: %s", EmotionTypeToString(e));
});

// 自动返回中性状态
display->SetAutoReturnNeutral(true, 5000);

// 过渡动画
display->SetTransitionAnimation("/spiffs/transitions/fade.json");
```

## C 接口

```c
// 初始化
emotion_display_init(240, 240, "/spiffs/emotions/");

// 显示情感
emotion_display_show("happy", 0);

// 显示消息
emotion_display_show_message("系统消息", 3000);
```

## 界面布局

```
┌─────────────────────────────────┐
│  [顶部状态栏]（可选，默认隐藏）  │ ← 40px
├─────────────────────────────────┤
│                                 │
│                                 │
│        😊 情感动画              │ ← 主要区域
│        (Lottie)                 │   200-240px
│                                 │   居中显示
│                                 │
└─────────────────────────────────┘
```

## 支持的情感

HAPPY | SAD | SURPRISED | ANGRY | CONFUSED | SLEEPY  
NEUTRAL | EXCITED | CALM | THINKING | LOVE

## 详细文档

完整文档请查看：  
`/Users/xionghao/Documents/plaud/GitHub/docs/xiaozhi-esp32/display/emotion-display-ui.md`

## 示例代码

查看 `emotion_display_example.cc` 中的 7 个完整示例：
1. 基础使用
2. 带顶部状态栏
3. 情感序列播放
4. 响应系统事件
5. 与 AI 集成
6. 高级功能
7. 完整应用集成



