# 依赖说明

本文档详细说明 Micro Wake Word 模块的依赖项及其获取方式。

## 必需依赖

### 1. TensorFlow Lite Micro

TFLite Micro 是一个为微控制器优化的机器学习推理引擎。

#### 获取方式

**方式 A: ESP-IDF 组件 (推荐用于 ESP32)**
```bash
# 在 idf_component.yml 中添加
dependencies:
  esp-tflite-micro:
    git: https://github.com/espressif/esp-tflite-micro.git
```

**方式 B: 源码编译**
```bash
git clone https://github.com/tensorflow/tflite-micro.git
cd tflite-micro
make -f tensorflow/lite/micro/tools/make/Makefile
```

**方式 C: 使用项目中已有的**
本项目的 `components/esp-tflite-micro/` 目录包含完整的 TFLite Micro。

#### 需要的头文件
```cpp
#include <tensorflow/lite/core/c/common.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
```

---

### 2. Google Audio Frontend (esp_micro_speech_features)

用于从原始音频生成 MFCC/频谱图特征。

#### 获取方式

**方式 A: 使用项目中已有的**
本项目的 `components/esp_micro_speech_features/` 目录包含完整实现。

复制以下文件:
```
esp_micro_speech_features/
├── include/
│   ├── frontend.h
│   └── frontend_util.h
└── src/
    ├── frontend.c
    ├── frontend_util.c
    ├── filterbank.c / filterbank.h / filterbank_util.c / filterbank_util.h
    ├── fft.c / fft.h / fft_util.c / fft_util.h
    ├── window.c / window.h / window_util.c / window_util.h
    ├── log_scale.c / log_scale.h / log_scale_util.c / log_scale_util.h
    ├── noise_reduction.c / noise_reduction.h / noise_reduction_util.c / noise_reduction_util.h
    ├── pcan_gain_control.c / pcan_gain_control.h / pcan_gain_control_util.c / pcan_gain_control_util.h
    ├── kiss_fft.c / kiss_fft.h
    ├── kiss_fftr.c / kiss_fftr.h
    └── log_lut.c / log_lut.h
```

**方式 B: TensorFlow 源码**
```bash
# 从 TensorFlow 获取
git clone https://github.com/tensorflow/tensorflow.git
# 文件位置: tensorflow/lite/experimental/microfrontend/
```

#### 需要的头文件
```cpp
#include "frontend.h"
#include "frontend_util.h"
```

#### 主要 API
```c
// 配置前端
struct FrontendConfig config;
struct FrontendState state;

// 填充状态
FrontendPopulateState(&config, &state, sample_rate);

// 处理音频样本
struct FrontendOutput output = FrontendProcessSamples(&state, samples, num_samples, &num_read);

// 释放资源
FrontendFreeStateContents(&state);
```

---

## 可选依赖

### 3. FreeRTOS (用于多任务)

如果需要在独立任务中运行唤醒词检测:

```cpp
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
```

### 4. OPUS Encoder (用于音频编码)

如果需要编码检测到的唤醒词音频:

```cpp
#include <opus_encoder.h>
```

---

## 平台特定依赖

### ESP-IDF

```
esp_timer      # 计时器
esp_psram      # PSRAM 支持 (可选)
driver         # I2S 驱动 (用于麦克风输入)
```

### Linux/桌面

```
<chrono>       # 计时器
<pthread>      # 多线程 (可选)
ALSA/PortAudio # 音频输入 (可选)
```

---

## 依赖版本建议

| 依赖 | 推荐版本 | 备注 |
|------|----------|------|
| TFLite Micro | 最新 | 持续更新中 |
| ESP-IDF | 5.0+ | 对于 ESP32 平台 |
| C++ 标准 | C++14+ | 需要 auto, lambda 等特性 |

---

## 文件清单

移植时需要复制的最小文件集:

```
standalone/micro_wake_word/
├── core/
│   ├── micro_wake_word.h
│   ├── micro_wake_word.cc
│   ├── streaming_model.h
│   ├── streaming_model.cc
│   ├── preprocessor_settings.h
│   └── platform_compat.h
├── platform/
│   └── (选择适合你平台的 xxx_compat.h)
└── models/
    └── (你的模型 .h 文件)
```

外部依赖:
```
tflite-micro/           # TensorFlow Lite Micro
audio-frontend/         # Google Audio Frontend
├── frontend.h/c
├── frontend_util.h/c
└── (其他依赖文件)
```

