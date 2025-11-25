# 🔴 关键修复：MicroWakeWord 需要单通道数据

**日期**: 2025-11-25  
**问题**: 概率始终为 0.000-0.027，远低于阈值 0.500  
**根本原因**: 2通道交织数据直接喂给了期望单通道数据的 MicroWakeWord

---

## 🔍 **问题诊断**

### 日志证据

```
AudioService: 🔎 ReadAudioData返回前 (count #1000, 16kHz): size=960
MicroWakeWord: Feed #1000: Received 960 samples
```

**分析**:
- `size=960` = 480 samples × 2 channels (2通道交织数据)
- `MicroWakeWord` 期望 480 samples 的**单通道**数据
- 实际收到的是 960 个交织的 int16_t 值（Mic + Ref 交替）

### 数据格式错误

#### 期望的数据（单通道）:
```
[Mic0, Mic1, Mic2, Mic3, ...]  // 480 samples
```

#### 实际收到的数据（2通道交织）:
```
[Mic0, Ref0, Mic1, Ref1, Mic2, Ref2, ...]  // 960 values
```

**结果**: 模型把 `[Mic0, Ref0, Mic1, Ref1, ...]` 当作单通道音频处理，导致：
- 频率特征错误（相当于采样率翻倍 = 32kHz）
- Ref（参考通道）干扰 Mic（麦克风）信号
- 模型无法识别唤醒词

---

## ✅ **修复方案**

### 修改 `audio_service.cc` 的 `AudioInputTask()`

**位置**: 第 318-334 行

#### 修复前 ❌

```cpp
if (bits & AS_EVENT_WAKE_WORD_RUNNING) {
    std::vector<int16_t> data;
    int samples = wake_word_->GetFeedSize();
    if (samples > 0) {
        if (ReadAudioData(data, 16000, samples)) {
            wake_word_->Feed(data);  // ❌ 直接Feed 2通道交织数据
            continue;
        }
    }
}
```

#### 修复后 ✅

```cpp
if (bits & AS_EVENT_WAKE_WORD_RUNNING) {
    std::vector<int16_t> data;
    int samples = wake_word_->GetFeedSize();
    if (samples > 0) {
        if (ReadAudioData(data, 16000, samples)) {
            // 🔧 如果是2通道，需要提取左声道（麦克风通道）
            if (codec_->input_channels() == 2) {
                auto mono_data = std::vector<int16_t>(data.size() / 2);
                for (size_t i = 0, j = 0; i < mono_data.size(); ++i, j += 2) {
                    mono_data[i] = data[j];  // ✅ 提取 Ch0 (左声道/麦克风)
                }
                data = std::move(mono_data);
            }
            wake_word_->Feed(data);  // ✅ Feed 单通道数据
            continue;
        }
    }
}
```

---

## 📊 **预期效果**

### 修复前

```
AudioService: 🔎 ReadAudioData返回前: size=960, avg=20, max=138
MicroWakeWord: Feed #1000: Received 960 samples  ❌ 错误！
  Model 'okay nabu': probability 0.000
```

**分析**:
- MicroWakeWord 收到 960 个值
- 实际包含 480 个 Mic 样本 + 480 个 Ref 样本（交织）
- 模型困惑，概率几乎为 0

### 修复后（预期）

```
AudioService: 🔎 ReadAudioData返回前 (2-ch交织): size=960, avg=20, max=138
  ↓ 提取单通道
AudioService: 提取单通道后: size=480, avg=XX, max=XX
MicroWakeWord: Feed #1000: Received 480 samples  ✅ 正确！
  Model 'okay nabu': probability 0.XXX  ← 应显著提升
```

**分析**:
- ReadAudioData 返回 960 个值（2通道交织）
- 提取 Ch0（麦克风通道）→ 480 个单通道样本
- MicroWakeWord 收到正确格式的数据
- 概率应该显著提升（预期 > 0.50）

---

## 🎯 **为什么之前没发现？**

### 原因 1：配置混乱

之前错误地配置为 **4 通道 TDM 模式**，并在 `ReadAudioData()` 中做了通道提取/混音，掩盖了真正的问题。

### 原因 2：日志不清晰

`ReadAudioData()` 的日志没有标注数据格式（单通道 vs 多通道），难以发现问题。

### 原因 3：忽略了原始工程的逻辑

原始工程在 `audio_testing` 模式下**明确提取了左声道**：

```cpp
// 原始工程 audio_service.cc (第 215-221 行)
if (codec_->input_channels() == 2) {
    auto mono_data = std::vector<int16_t>(data.size() / 2);
    for (size_t i = 0, j = 0; i < mono_data.size(); ++i, j += 2) {
        mono_data[i] = data[j];  // 提取左声道
    }
    data = std::move(mono_data);
}
```

但在 `wake_word` 模式下没有提取（可能原始工程的 wake_word 实现能处理2通道数据，或者原始工程没有使用 `MicroWakeWord`）。

---

## 🔬 **技术细节**

### 2通道交织格式

```
I2S 输出 (24kHz, 2-ch):
[Mic0, Ref0, Mic1, Ref1, Mic2, Ref2, ...]
 ↓ 重采样到 16kHz
[Mic0, Ref0, Mic1, Ref1, Mic2, Ref2, ...]  (960 values = 480 frames × 2 channels)
```

### 提取左声道（麦克风）

```cpp
for (size_t i = 0, j = 0; i < 480; ++i, j += 2) {
    mono_data[i] = data[j];  // 取偶数位 = Ch0 = 麦克风
}
```

**结果**:
```
[Mic0, Mic1, Mic2, Mic3, ...]  (480 samples, 单通道)
```

### MicroWakeWord 的期望

- **输入**: 单通道 16kHz 音频数据
- **每次 Feed**: 480 samples (30ms @ 16kHz)
- **Frontend**: 生成 40 个特征值
- **Model**: 输入 shape `[1, 3, 40]`（3 个时间步，每步 40 个特征）

---

## 📝 **教训**

### 1. 理解数据格式

- **多通道音频**通常以**交织格式**存储（LRLRLR...）
- 不同模块对数据格式的期望不同
- 必须显式转换格式

### 2. 完整的数据链路

```
硬件 (2-ch) → ReadAudioData() (2-ch交织) → 提取单通道 → MicroWakeWord (单通道)
```

**不能跳过任何一步！**

### 3. 日志的重要性

- 日志应该清晰标注数据格式（单通道 / 2通道 / 4通道）
- 日志应该标注数据的 size 和含义
- 例如：`size=960 (2-ch交织)` vs `size=480 (单通道)`

### 4. 参考原始实现

原始工程在 `audio_testing` 模式下已经有了通道提取的代码，应该意识到其他模式也需要类似处理。

---

## 🚀 **测试步骤**

### 1. 编译和烧录

```bash
cd /Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32
idf.py build
idf.py flash monitor
```

### 2. 观察关键日志

#### 启动时

```
🎤 BoxAudioCodec constructor: ..., input_channels=2, input_gain=30.0 dB
```

**验证**: `input_channels=2` ✅

#### 运行时

```
AudioService: 🔎 ReadAudioData返回前 (2-ch交织): size=960, avg=XX, max=XX
MicroWakeWord: Feed #1000: Received 480 samples  ← 应该是 480 而不是 960！
```

**验证**: `Received 480 samples` ✅

#### 说"Okay Nabu"时

```
MicroWakeWord: Model 'okay nabu': probability 0.XXX (threshold: 0.500)
```

**预期**: 概率应该显著提升（> 0.50），并触发唤醒

### 3. 测试唤醒

- **距离**: 20-50 cm
- **音量**: 正常对话音量
- **发音**: "**Okay Nabu**" (清晰，正常语速)
- **重复**: 3-5 次

**预期日志**:
```
Model 'okay nabu': probability > 0.50
🎉 Model 'okay nabu' detected!
```

---

## ✅ **总结**

| 问题 | 修复 |
|------|------|
| **数据格式错误** | 2通道交织 → 单通道提取 |
| **概率过低 (0.000-0.027)** | 应显著提升到 > 0.50 |
| **无法唤醒** | 修复后应该能唤醒 |

**关键修复点**: 在 `AudioInputTask()` 的 wake_word 分支中，添加单通道提取逻辑。

**预计效果**: 唤醒词检测概率从 0.027 提升到 > 0.50，设备能够正常唤醒。

---

**这是真正的根本原因！** 🎯

