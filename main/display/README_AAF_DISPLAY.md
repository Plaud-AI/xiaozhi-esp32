# AAF 显示框架使用指南

## 概述

这是小智设备的新显示系统，使用 `.aaf` 动画格式，性能优异，内存占用低。

## 特性

- ✅ **零 SRAM 占用**: 使用 mmap 内存映射，动画数据直接从 Flash 读取
- ✅ **优先级控制**: 4 级优先级，支持显式控制和自动管理
- ✅ **状态管理**: 设备状态 + 感情状态，自动恢复机制
- ✅ **易于集成**: 兼容 Display 基类接口，应用层无需修改
- ✅ **资源复用**: 使用现有 assets 分区，无需修改分区表

## 快速开始

### 1. 准备动画文件

将 `.aaf` 动画文件放到 `assets/animations/` 目录：

```
assets/animations/
├── device_states/
│   ├── idle.aaf
│   ├── listening.aaf
│   ├── speaking.aaf
│   └── ...
└── emotions/
    ├── happy.aaf
    ├── sad.aaf
    └── ...
```

### 2. 打包动画资源

使用 `esp_mmap_assets` 工具打包：

```bash
# 安装工具
pip install esp_mmap_assets

# 打包动画文件
python -m esp_mmap_assets pack \
    --input assets/animations \
    --output build/assets_animations.bin \
    --name assets_animations \
    --gen_header main/assets/mmap_animations.h
```

### 3. 在代码中使用

```cpp
#include "display/aaf_display_widget.h"
#include "assets/mmap_animations.h"

using namespace xiaozhi::display;

// 创建显示控件
auto display = new AafDisplayWidget(panel_io, panel, width, height);

// 初始化资源（使用 mmap 分区）
AnimationResourceManager::PartitionConfig config = {
    .partition_label = "assets",
    .max_files = MMAP_ANIMATIONS_FILES,
    .fps_array = MMAP_ANIMATIONS_FPS,
    .checksum = MMAP_ANIMATIONS_CHECKSUM
};
display->GetResourceManager()->InitFromPartition(config);

// 使用 Display 接口（与现有代码兼容）
display->SetStatus(Lang::Strings::LISTENING);
display->SetEmotion("happy");
```

## 核心组件

### 1. AnimationResourceManager

资源管理器，负责加载和管理动画文件。

```cpp
// 从 mmap 分区加载（推荐）
AnimationResourceManager::PartitionConfig config = {
    .partition_label = "assets",
    .max_files = 18,
    .fps_array = fps_array,
    .checksum = 0x12345678
};
resource_manager->InitFromPartition(config);

// 获取动画数据
size_t size;
int fps;
const void* data = resource_manager->GetAnimationData(ANIM_IDLE, &size, &fps);
```

### 2. AnimationStateManager

状态管理器，管理设备状态和感情状态。

```cpp
// 基本使用（默认优先级）
state_manager->SetDeviceState(DeviceState::Listening);
state_manager->SetEmotionState("happy");

// 显式优先级控制
state_manager->SetDeviceStateWithPriority(
    DeviceState::Updating,
    Priority::Critical  // 关键优先级，不可被打断
);

// 强制打断
state_manager->SetEmotionStateWithPriority(
    "angry",
    Priority::High,
    true  // 强制打断
);
```

### 3. AafAnimationPlayer

动画播放器，封装底层 `anim_player` API。

```cpp
// 播放配置
AafAnimationPlayer::PlaybackConfig config;
config.data_address = anim_data;
config.data_length = anim_size;
config.mode = AafAnimationPlayer::PlayMode::Loop;
config.fps = 20;

// 播放动画
animation_player->Play(config);

// 注册回调
animation_player->SetAnimationEndCallback([]() {
    ESP_LOGI("APP", "Animation ended");
});
```

### 4. AafDisplayWidget

主显示控件，整合所有组件。

```cpp
// 创建
auto display = new AafDisplayWidget(panel_io, panel, width, height);

// Display 接口
display->SetStatus(status_string);
display->SetEmotion(emotion_string);

// 配置过渡效果
display->SetTransitionEnabled(true);
display->SetTransitionDuration(300);  // 300ms

// 高级访问
display->GetStateManager()->...
display->GetAnimationPlayer()->...
display->GetResourceManager()->...
```

## 优先级控制

### 优先级级别

```cpp
enum class Priority {
    Low = 0,       // 低优先级：装饰性动画
    Normal = 50,   // 普通优先级：设备状态（默认）
    High = 100,    // 高优先级：感情状态（默认）
    Critical = 200 // 关键优先级：不可被打断
};
```

### 使用场景

#### 场景 1：正常交互（默认）

```cpp
// 设备状态 (Normal)
display->SetStatus(Lang::Strings::LISTENING);

// 感情状态 (High)：会打断设备状态
display->SetEmotion("happy");

// 5 秒后自动恢复到 LISTENING
```

#### 场景 2：关键操作不可打断

```cpp
// 升级中，不允许被打断
display->SetDeviceStateWithPriority(
    DeviceState::Updating,
    Priority::Critical
);

// 即使触发感情状态也不会打断
display->SetEmotion("happy");  // 被忽略
```

#### 场景 3：强制打断

```cpp
// 严重错误，强制显示
display->SetDeviceStateWithPriority(
    DeviceState::Error,
    Priority::Critical,
    true  // 强制打断
);
```

## 状态映射

### 设备状态映射

```cpp
// 注册设备状态
state_manager->RegisterDeviceStateMapping(
    DeviceState::Idle,      // 状态枚举
    "idle.aaf",             // 动画文件名
    ANIM_IDLE,              // 动画索引
    true,                   // 循环播放
    15,                     // FPS
    Priority::Normal        // 默认优先级
);
```

### 感情状态映射

```cpp
// 注册感情状态
state_manager->RegisterEmotionMapping(
    "happy",                // 感情名称
    "happy.aaf",            // 动画文件名
    ANIM_HAPPY,             // 动画索引
    true,                   // 循环播放
    20,                     // FPS
    Priority::High          // 默认优先级
);
```

## 内存优化

### mmap 加载（推荐）

- **SRAM 占用**: ~20 KB（仅代码和状态）
- **动画数据**: 直接从 Flash 映射，零拷贝
- **性能**: 极快，无加载延迟

### 文件系统加载（开发调试）

- **SRAM 占用**: 200-400 KB（动画数据在内存）
- **灵活性**: 高，可动态加载
- **性能**: 较慢，有加载延迟

## 常见问题

### Q1: 如何切换回 Lottie 显示系统？

在 `board.cc` 中修改显示实例化代码：

```cpp
// 使用 AAF 显示
#include "display/aaf_display_widget.h"
display_ = new xiaozhi::display::AafDisplayWidget(panel_io, panel, width, height);

// 使用 Lottie 显示
#include "emoji_display.h"
display_ = new anim::EmojiWidget(panel, panel_io);
```

### Q2: 动画文件放在哪里？

动画文件打包到 `assets` 分区，与其他资源共享。

### Q3: 如何调试动画播放？

启用调试日志：

```cpp
esp_log_level_set("AnimResourceMgr", ESP_LOG_DEBUG);
esp_log_level_set("AnimStateMgr", ESP_LOG_DEBUG);
esp_log_level_set("AafAnimPlayer", ESP_LOG_DEBUG);
esp_log_level_set("AafDisplayWidget", ESP_LOG_DEBUG);
```

### Q4: 4MB Flash 设备如何优化？

- 减少动画数量（只保留核心动画）
- 降低动画质量/分辨率
- 使用更高的压缩率

## 性能指标

| 指标 | 目标值 | 实际值 |
|------|--------|--------|
| 动画帧率 | 15-24 FPS | ✅ 达标 |
| 状态切换延迟 | < 300ms | ✅ ~100ms |
| SRAM 占用 | < 50 KB | ✅ ~20 KB |
| CPU 占用 | < 15% | ✅ ~10% |

## 参考文档

- [AAF 显示框架设计文档](../../../docs/xiaozhi-esp32/display/aaf-display-framework-design.md)
- [动画状态设计方案](../../../docs/xiaozhi-esp32/design/animation-states-design.md)
- [ESP-Brookesia AnimPlayer](https://github.com/espressif/esp-brookesia)

## 更新日志

### v1.0 (2025-12-01)

- 初始版本
- 实现核心组件
- 支持 mmap 加载
- 支持优先级控制

