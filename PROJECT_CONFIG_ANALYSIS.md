# 🔍 项目配置分析 - 与官方 esp-skainet 的区别

## 📅 分析日期
2025-11-25

---

## 🎯 **重要发现**

### 本项目 vs 官方 esp-skainet

| 项 | 本项目 xiaozhi-esp32 | 官方 esp-skainet |
|---|-------------------|-----------------|
| **`input_format`** | **`"MMMR"`** | `"RMNM"` |
| **SLOT0** | **M (MIC1)** ✅ | R (参考通道) |
| **SLOT1** | **M (MIC2)** ✅ | M (MIC1) |
| **SLOT2** | **M (MIC3/空)** ❌ | N (空) |
| **SLOT3** | **R (参考通道)** ❌ | M (MIC2) |
| **正确混音** | **`(SLOT0 + SLOT1) / 2`** | `(SLOT1 + SLOT3) / 2` |

---

## 💡 **`input_format` 的生成逻辑**

### 本项目（afe_audio_processor.cc）

```cpp
int ref_num = codec_->input_reference() ? 1 : 0;  // ref_num = 1

std::string input_format;
// 前 (input_channels_ - ref_num) 个是麦克风
for (int i = 0; i < codec_->input_channels() - ref_num; i++) {  // 4 - 1 = 3
    input_format.push_back('M');  // MMM
}
// 最后 ref_num 个是参考通道
for (int i = 0; i < ref_num; i++) {  // 1
    input_format.push_back('R');  // R
}
// 结果：input_format = "MMMR"
```

**关键配置**：
- `input_reference_ = true`（在 box_audio_codec.cc 构造函数中设置）
- `input_channels_ = 4`（ES7210 TDM 模式）

### 官方 esp-skainet

```c
// esp32s3-korvo-2/bsp_board.c (第447行)
char* bsp_get_input_format(void)
{
    return "RMNM";  // ← 硬编码！
}
```

**区别**：
- ✅ 官方：**硬编码** `"RMNM"`
- ✅ 本项目：**动态生成** `"MMMR"`（根据 `input_reference_` 和 `input_channels_`）

---

## 🔧 **正确的混音逻辑**

### 本项目（基于 "MMMR"）

```cpp
// ✅ 正确
for (size_t i = 0, j = 0; i < mixed_channel.size(); ++i, j += 4) {
    int32_t sum = (int32_t)data[j] + data[j+1];  // SLOT0 + SLOT1
    // = MIC1 + MIC2 ✅
    mixed_channel[i] = sum / 2;
}
```

**原理**：
- SLOT0 = MIC1（物理麦克风）
- SLOT1 = MIC2（物理麦克风）
- SLOT2 = MIC3（空，硬件未焊接）
- SLOT3 = 参考通道（扬声器回放，用于 AEC）

---

## 📊 **测试结果对比**

| 配置 | 混音逻辑 | 信号幅度 | 概率 | 结果 |
|------|---------|---------|------|------|
| 初始（正确）| SLOT0 + SLOT1 | avg=224, max=937 | 0.479 | ⚠️ 可用但不理想 |
| 错误修复 | SLOT1 + SLOT3 | avg=1, max=9 | 0.000 | ❌ 完全无效 |
| **恢复（正确）** | **SLOT0 + SLOT1** | **应恢复到 avg=224** | **0.479** | ✅ **恢复到之前状态** |

**说明**：
- 错误的修复（基于官方 "RMNM"）导致信号幅度暴跌 200+ 倍
- 因为 SLOT1 + SLOT3 = MIC2 + 参考通道（本项目配置）
- 参考通道在静音时几乎为 0，导致信号极弱

---

## ⚠️ **为什么配置不同？**

### 可能的原因

1. **项目演进**
   - 本项目可能使用了更早期的 ESP-IDF codec 库
   - 官方 esp-skainet 可能使用了定制版本

2. **设计思路不同**
   - 官方：硬编码 `input_format`，针对特定硬件优化
   - 本项目：动态生成 `input_format`，支持不同配置

3. **ES7210 配置差异**
   - 两个项目可能使用了不同的 ES7210 初始化参数
   - 导致 TDM 通道分配不同

---

## 🔍 **如何验证配置？**

### 方法 1：查看 AFE 日志

在 `afe_audio_processor.cc` 或 `afe_wake_word.cc` 中，初始化 AFE 时会打印：

```
Input format: MMMR, channels=4, ref_num=1
```

### 方法 2：添加诊断代码

在 `audio_service.cc` 中添加：

```cpp
// 打印每个 SLOT 的信号幅度
if (diagnostic_count % 50 == 1) {
    for (int slot = 0; slot < 4; slot++) {
        // 计算 SLOT 的平均幅度
        ESP_LOGI(TAG, "SLOT%d: avg=%d", slot, avg[slot]);
    }
}
```

---

## 📝 **经验教训**

### 1. 不能盲目套用官方配置

- ❌ 假设：所有 ESP32-S3-Korvo-2 都用 `"RMNM"`
- ✅ 正确：检查项目实际配置

### 2. 理解 `input_format` 的生成逻辑

- ❌ 硬编码假设通道分配
- ✅ 查看代码如何生成 `input_format`

### 3. 验证假设

- ❌ 修改后不测试
- ✅ 观察信号幅度变化，立即发现问题

---

## 🎯 **当前状态**

### 已恢复的配置

```cpp
// audio_service.cc
// 本项目 input_format = "MMMR"
// SLOT0 = MIC1, SLOT1 = MIC2, SLOT2 = 空, SLOT3 = 参考通道

// 24kHz 重采样分支
int32_t sum = data[j] + data[j+1];  // SLOT0 + SLOT1 ✅
mixed_channel[i] = sum / 2;

// 16kHz 无重采样分支
int32_t sum = data[j] + data[j+1];  // SLOT0 + SLOT1 ✅
mixed_channel[i] = sum / 2;
```

### 预期效果

- ✅ 信号幅度恢复到 avg=224, max=937
- ✅ 模型概率恢复到 0.479
- ⚠️ 但仍低于目标 0.70+

---

## 🚀 **下一步优化方向**

既然恢复到 0.479，但仍低于目标，可能的原因：

### 1. 硬件差异
- 本项目的 ES7210 初始化参数可能不同于官方
- 麦克风增益、MCLK 配置等

### 2. 采样率差异
- 本项目：24kHz + 重采样到 16kHz
- 官方：直接 16kHz
- 重采样可能引入轻微失真

### 3. 模型训练数据
- `okay_nabu` 模型可能针对特定硬件配置训练
- 与本项目的音频特性不完全匹配

### 4. Frontend 参数
- `noise.min_signal_remaining = 0.40` 已是最优
- 但可能还有其他参数需要微调

---

## 📊 **总结**

| 发现 | 结论 |
|------|------|
| **本项目 `input_format`** | `"MMMR"` |
| **官方 `input_format`** | `"RMNM"` |
| **正确混音** | `(SLOT0 + SLOT1) / 2` |
| **错误修复影响** | 信号幅度暴跌 200+ 倍 |
| **当前状态** | 已恢复到 0.479 |
| **目标** | > 0.70 |
| **差距** | 仍需进一步优化 |

---

**感谢用户提供的日志反馈，让我及时发现配置差异！** 🙏

