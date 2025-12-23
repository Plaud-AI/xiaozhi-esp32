# Micro Wake Word - 独立唤醒词检测模块

这是从 xiaozhi-esp32-xr 项目中剥离的独立 Micro Wake Word 模块，可以移植到其他平台。

## 特性

- 🎯 基于 TensorFlow Lite Micro 的轻量级唤醒词检测
- 🔊 使用 Google Audio Frontend 进行特征提取 (MFCC/频谱图)
- 🔌 平台无关设计，易于移植
- 💾 支持 PSRAM 等外部内存
- ⚡ 流式推理，低延迟检测

## 目录结构

```
micro_wake_word/
├── core/                       # 核心实现（平台无关）
│   ├── micro_wake_word.h/cc    # 主唤醒词检测类
│   ├── streaming_model.h/cc    # TFLite 流式模型封装
│   ├── preprocessor_settings.h # 特征提取参数配置
│   └── platform_compat.h       # 平台抽象层接口
│
├── platform/                   # 平台适配层
│   ├── esp_idf_compat.h        # ESP-IDF 适配
│   └── linux_compat.h          # Linux/桌面系统适配
│
├── models/                     # 唤醒词模型
│   └── README.md               # 模型使用说明
│
└── examples/                   # 使用示例
    ├── basic_example.cc        # 基础示例
    └── esp_idf_example.cc      # ESP-IDF 集成示例
```

## 依赖

### 必需依赖

1. **TensorFlow Lite Micro**
   - 用于运行神经网络推理
   - 项目地址: https://github.com/tensorflow/tflite-micro

2. **Google Audio Frontend (esp_micro_speech_features)**
   - 用于音频特征提取 (MFCC/频谱图)
   - 需要 `frontend.h` 和 `frontend_util.h`
   - 可以从 ESP-IDF 组件或 TensorFlow 项目获取

### 可选依赖

- C++14 或更高版本
- 标准库 (`<vector>`, `<string>`, `<functional>`, 等)

## 快速开始

### 1. 包含头文件

```cpp
// 先包含平台适配层
#ifdef ESP_PLATFORM
#include "platform/esp_idf_compat.h"
#else
#include "platform/linux_compat.h"
#endif

// 再包含核心头文件
#include "core/micro_wake_word.h"
```

### 2. 基本使用

```cpp
#include "core/micro_wake_word.h"
#include "my_model.h"  // 你的模型数据

using namespace micro_wake_word;

int main() {
    // 创建实例
    MicroWakeWord mww;
    
    // 初始化
    if (!mww.Initialize()) {
        return -1;
    }
    
    // 添加唤醒词模型
    mww.add_wake_word_model(
        my_model_tflite,  // 模型数据
        0.5f,             // 检测阈值 (0.0-1.0)
        10,               // 滑动窗口大小
        "hey assistant",  // 唤醒词名称
        30000             // Tensor Arena 大小
    );
    
    // 设置检测回调
    mww.OnWakeWordDetected([](const std::string& wake_word) {
        printf("检测到唤醒词: %s\n", wake_word.c_str());
    });
    
    // 开始检测
    mww.Start();
    
    // 在音频处理循环中喂入数据
    std::vector<int16_t> audio_chunk(480);  // 30ms @ 16kHz
    while (true) {
        // 从麦克风读取音频...
        read_audio(audio_chunk.data(), audio_chunk.size());
        
        // 喂入检测器
        mww.Feed(audio_chunk);
    }
    
    // 停止检测
    mww.Stop();
    
    return 0;
}
```

## 音频格式要求

- **采样率**: 16000 Hz
- **位深**: 16-bit signed
- **通道**: 单声道 (Mono)
- **推荐块大小**: 480 样本 (30ms)

## 移植到新平台

### 步骤 1: 创建平台适配文件

在 `platform/` 目录下创建新的适配文件，例如 `your_platform_compat.h`:

```cpp
#pragma once

// 1. 定义日志宏
#define MWW_LOGE(tag, fmt, ...) your_log_error(tag, fmt, ##__VA_ARGS__)
#define MWW_LOGW(tag, fmt, ...) your_log_warn(tag, fmt, ##__VA_ARGS__)
#define MWW_LOGI(tag, fmt, ...) your_log_info(tag, fmt, ##__VA_ARGS__)
#define MWW_LOGD(tag, fmt, ...) your_log_debug(tag, fmt, ##__VA_ARGS__)

// 2. 定义内存分配 (可选，用于支持外部 RAM)
#define MWW_CUSTOM_ALLOCATOR 1

namespace micro_wake_word {

template<class T>
void* ExternalRAMAllocator<T>::mww_alloc_external(size_t size) {
    return your_psram_malloc(size);
}

template<class T>
void* ExternalRAMAllocator<T>::mww_alloc_internal(size_t size) {
    return your_sram_malloc(size);
}

template<class T>
void ExternalRAMAllocator<T>::mww_free(void* ptr) {
    your_free(ptr);
}

}
```

### 步骤 2: 集成 TFLite Micro

确保你的平台上有可用的 TensorFlow Lite Micro 实现。

### 步骤 3: 集成 Google Audio Frontend

需要以下文件:
- `frontend.h` / `frontend.c`
- `frontend_util.h` / `frontend_util.c`
- 及其依赖 (filterbank, fft, noise_reduction, 等)

可以从以下位置获取:
- ESP-IDF: `esp_micro_speech_features` 组件
- TensorFlow: `tensorflow/lite/experimental/microfrontend`

## 兼容的唤醒词模型

本实现兼容以下模型:

1. **ESPHome microWakeWord 模型**
   - 仓库: https://github.com/esphome/micro-wake-word-models
   - 预训练模型: "okay nabu", "hey jarvis", 等

2. **自定义训练模型**
   - 训练工具: https://github.com/kahrendt/microWakeWord
   - 需要 INT8 量化的 TFLite 模型

## 模型参数调优

| 参数 | 说明 | 典型值 |
|------|------|--------|
| `probability_cutoff` | 检测阈值，越高误触越少 | 0.5 - 0.97 |
| `sliding_window_size` | 平均窗口大小，越大越稳定 | 5 - 15 |
| `tensor_arena_size` | 推理内存，取决于模型 | 20000 - 40000 |
| `features_step_size` | 特征步长 (ms) | 10 |

## 内存占用

典型配置下:
- Ring Buffer: ~4 KB
- Preprocessor Buffer: ~320 bytes
- Tensor Arena: ~20-40 KB (每个模型)
- 模型权重: 存储在 Flash (不占 RAM)

## 许可证

MIT License

## 致谢

- [ESPHome microWakeWord](https://github.com/esphome/micro-wake-word-models)
- [TensorFlow Lite Micro](https://github.com/tensorflow/tflite-micro)
- [Google Audio Frontend](https://github.com/tensorflow/tensorflow/tree/master/tensorflow/lite/experimental/microfrontend)

