# Micro Wake Word - TFLite Micro 唤醒词检测

**最后更新**: 2025-11-24  
**适用版本**: v2.x  

## 概述

Micro Wake Word 是基于 TensorFlow Lite Micro 的轻量级唤醒词检测模块，使用 Google Audio Frontend 进行特征提取和流式神经网络模型进行检测。该模块平行于 AFE Wake Word 和 Custom Wake Word，提供了另一种唤醒词检测方案。

## 功能特性

- ✅ 基于 TFLite Micro 的流式推理
- ✅ Google Audio Frontend MFCC 特征提取
- ✅ 支持多个唤醒词模型同时运行
- ✅ PSRAM 优先内存分配策略
- ✅ 与现有 WakeWord 接口完全兼容
- ✅ 低延迟检测（70-100ms）
- ✅ 支持自定义模型

## 架构

```
音频输入 (16kHz PCM)
    ↓
Feed() → Ring Buffer
    ↓
Google Audio Frontend
    ↓
MFCC 特征 (40维, 30ms窗口)
    ↓
TFLite Micro 推理
    ↓
滑动窗口平均
    ↓
阈值判断 → 唤醒词检测
```

## 文件结构

```
micro/
├── README.md                 # 本文档
├── preprocessor_settings.h   # 预处理器配置
├── helpers.h                 # 辅助函数（内存分配器等）
├── streaming_model.h         # 流式模型基类
├── streaming_model.cc
├── micro_wake_word.h         # 主要类声明
├── micro_wake_word.cc        # 主要类实现
├── hey_jarvis.h              # 示例模型数据
└── example_usage.cc          # 使用示例
```

## API 参考

### MicroWakeWord 类

继承自 `WakeWord` 接口，提供标准的唤醒词检测功能。

#### 主要方法

```cpp
// 初始化
bool Initialize(AudioCodec* codec, srmodel_list_t* models_list);

// 喂入音频数据（16kHz, mono, int16）
void Feed(const std::vector<int16_t>& data);

// 设置检测回调
void OnWakeWordDetected(std::function<void(const std::string&)> callback);

// 开始/停止检测
void Start();
void Stop();

// 添加唤醒词模型
void add_wake_word_model(
    const uint8_t* model_start,      // 模型数据指针
    float probability_cutoff,        // 检测阈值 (0.0-1.0)
    size_t sliding_window_size,      // 滑动窗口大小
    const std::string& wake_word,    // 唤醒词名称
    size_t tensor_arena_size         // Tensor Arena 大小（字节）
);

// 配置特征步长
void set_features_step_size(uint8_t step_size);  // 默认 20ms
```

## 使用示例

### 基础用法

```cpp
#include "audio/wake_words/micro/micro_wake_word.h"
#include "audio/wake_words/micro/hey_jarvis.h"

using namespace micro_wake_word;

// 创建实例
auto micro_ww = std::make_unique<MicroWakeWord>();

// 初始化
micro_ww->Initialize(nullptr, nullptr);

// 添加模型
micro_ww->add_wake_word_model(
    hey_jarvis_tflite,  // 模型数据
    0.5f,               // 检测阈值
    10,                 // 滑动窗口
    "hey jarvis",       // 名称
    30000               // Arena 大小
);

// 设置回调
micro_ww->OnWakeWordDetected([](const std::string& wake_word) {
    ESP_LOGI("APP", "检测到唤醒词: %s", wake_word.c_str());
});

// 开始检测
micro_ww->Start();

// 在音频循环中喂入数据
std::vector<int16_t> audio_chunk(480);  // 30ms @ 16kHz
// ... 获取音频数据 ...
micro_ww->Feed(audio_chunk);
```

### 与现有架构集成

```cpp
// 在 Application 中使用
std::unique_ptr<WakeWord> wake_word_;

void InitWakeWord() {
    wake_word_ = std::make_unique<micro_wake_word::MicroWakeWord>();
    
    // 配置模型
    auto* micro = dynamic_cast<micro_wake_word::MicroWakeWord*>(wake_word_.get());
    if (micro) {
        micro->add_wake_word_model(hey_jarvis_tflite, 0.5f, 10, "hey jarvis", 30000);
    }
    
    wake_word_->OnWakeWordDetected([this](const std::string& ww) {
        this->HandleWakeWord(ww);
    });
    
    wake_word_->Start();
}
```

## 模型训练与定制

### 模型要求

- **格式**: TensorFlow Lite (quantized int8)
- **输入**: [1, stride, 40] (MFCC 特征)
- **输出**: [1, 1] (uint8 概率)
- **架构**: 支持流式推理（内部状态变量）

### 训练工具

推荐使用 [micro-wake-word](https://github.com/kahrendt/microWakeWord) 工具链训练自定义模型。

训练步骤：
1. 收集正负样本音频
2. 使用 micro-wake-word 训练脚本
3. 导出为 TFLite 模型
4. 转换为 C 数组（使用 `xxd -i model.tflite > model.h`）
5. 集成到项目中

## 性能调优

### 参数说明

| 参数 | 说明 | 推荐值 | 影响 |
|------|------|--------|------|
| `probability_cutoff` | 检测阈值 | 0.5 - 0.7 | 越高越严格，误触发越少 |
| `sliding_window_size` | 滑动窗口大小 | 10 - 15 | 越大越稳定，但响应稍慢 |
| `features_step_size` | 特征步长 (ms) | 20 | 越小推理越频繁，CPU 占用越高 |
| `tensor_arena_size` | Tensor Arena | 30000+ | 取决于模型复杂度 |

### 内存使用

- **Ring Buffer**: ~32KB (1 秒音频缓冲)
- **Preprocessor Buffer**: ~640 字节
- **Tensor Arena**: 30-50KB / 模型
- **Frontend State**: ~10KB

总计: ~70-100KB / 模型（优先使用 PSRAM）

### CPU 使用

- **特征提取**: ~2-3ms / 30ms 音频
- **推理**: ~5-10ms / 推理步（取决于模型大小）
- **总 CPU**: ~10-15% @ 240MHz

## 故障排查

### 问题 1: 模型加载失败

**现象**: `Failed to initialize a wake word model`  
**原因**: 
- Tensor arena 太小
- 模型格式不兼容
- PSRAM 不足

**解决方案**:
1. 增加 `tensor_arena_size` 参数
2. 检查模型是否为 quantized int8 格式
3. 确认 PSRAM 可用：`idf.py menuconfig` → Component config → ESP PSRAM

### 问题 2: 检测不准确

**现象**: 误触发或漏检  
**解决方案**:
1. 调整 `probability_cutoff`（误触发多→提高，漏检多→降低）
2. 增大 `sliding_window_size` 提高稳定性
3. 收集更多样本重新训练模型

### 问题 3: 内存不足

**现象**: `Could not allocate tensor arena`  
**解决方案**:
1. 启用 PSRAM
2. 减小 `tensor_arena_size`（可能影响精度）
3. 使用更小的模型

### 问题 4: 延迟过高

**现象**: 检测响应慢  
**解决方案**:
1. 减小 `sliding_window_size`
2. 降低 `features_step_size`（会增加 CPU 占用）
3. 优化模型结构（减少层数/参数）

## 技术细节

### Google Audio Frontend

使用 TFLite Micro 自带的音频前端库进行特征提取：

- **窗口**: 30ms Hann 窗
- **步长**: 可配置（默认 20ms）
- **特征**: 40 维 MFCC
- **频率范围**: 125Hz - 7500Hz
- **降噪**: PCAN + Even/Odd Smoothing

### 流式推理

模型使用流式架构，内部维护状态变量：

1. **输入缓冲**: 按 stride 累积特征
2. **状态保持**: 使用 `MicroResourceVariables` 保存 RNN/GRU 状态
3. **增量输出**: 每个 stride 产生一次输出概率

### 检测逻辑

```
1. 提取 MFCC 特征 (40维)
2. 流式推理 → 输出概率 (uint8: 0-255)
3. 添加到滑动窗口
4. 计算窗口平均
5. 与阈值比较 → 判断检测
```

## 与其他方案对比

| 特性 | Micro Wake Word | AFE Wake Word | Custom Wake Word |
|------|----------------|---------------|------------------|
| 依赖库 | TFLite Micro | ESP-SR | ESP-SR MultiNet |
| 模型格式 | TFLite | ESP-SR | ESP-SR |
| 自定义模型 | ✅ 容易 | ❌ 困难 | ⚠️ 需音素 |
| 内存占用 | 中等 | 大 | 大 |
| 检测精度 | 高 | 高 | 高 |
| CPU 占用 | 中等 | 高 | 高 |
| 多语言 | ✅ | ✅ | ✅ |

## 相关资源

- [TFLite Micro 文档](https://www.tensorflow.org/lite/microcontrollers)
- [micro-wake-word 项目](https://github.com/kahrendt/microWakeWord)
- [Google Audio Frontend](https://github.com/tensorflow/tflite-micro/tree/main/tensorflow/lite/experimental/microfrontend)
- [参考项目](https://github.com/dscripka/micro_wake_word_standalone)

## 更新日志

### v1.0.0 (2025-11-24)
- ✨ 初始版本
- ✅ 移植 micro-wake-word 核心功能
- ✅ 集成 Google Audio Frontend
- ✅ 实现 WakeWord 接口
- ✅ 支持 hey_jarvis 示例模型

