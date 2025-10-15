# sdkconfig 配置对比 - 参考工程 vs 小智项目

## ✅ 配置已同步！

小智 ESP32 项目的 `sdkconfig` 已更新，现在与参考工程 `esp-sr-multinet` **完全一致**。

---

## 📊 配置对比表

### Wake Word Configuration

| 配置项 | 参考工程 (esp-sr-multinet) | 小智项目 (改造后) | 状态 |
|--------|---------------------------|------------------|------|
| `CONFIG_USE_AFE_WAKE_WORD` | ❌ is not set | ❌ is not set | ✅ 一致 |
| `CONFIG_USE_CUSTOM_WAKE_WORD` | ✅ y | ✅ y | ✅ 一致 |
| `CONFIG_CUSTOM_WAKE_WORD` | "hi plaud" | "hi plaud" | ✅ 一致 |
| `CONFIG_CUSTOM_WAKE_WORD_DISPLAY` | "hi plaud" | "hi plaud" | ✅ 一致 |
| `CONFIG_CUSTOM_WAKE_WORD_THRESHOLD` | 50 | 50 | ✅ 一致 |

---

### WakeNet Models (形式上保留，代码中禁用)

| 配置项 | 参考工程 | 小智项目 | 状态 |
|--------|---------|---------|------|
| `CONFIG_SR_WN_WN9_HILEXIN` | ✅ y | ✅ y | ✅ 一致 |
| `CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS` | ❌ is not set | ❌ is not set | ✅ 一致 |

**说明:** 虽然配置中选择了 WakeNet 模型，但在代码中通过 `afe_config->wakenet_init = false` 禁用了。

---

### MultiNet Models - 英文

| 配置项 | 参考工程 | 小智项目 | 状态 |
|--------|---------|---------|------|
| `CONFIG_SR_MN_EN_NONE` | ❌ is not set | ❌ is not set | ✅ 一致 |
| `CONFIG_SR_MN_EN_MULTINET5_SINGLE_RECOGNITION_QUANT8` | ✅ y | ✅ y | ✅ **一致！** |
| `CONFIG_SR_MN_EN_MULTINET6_QUANT` | ❌ is not set | ❌ is not set | ✅ 一致 |
| `CONFIG_SR_MN_EN_MULTINET7_QUANT` | ❌ is not set | ❌ is not set | ✅ 一致 |

**核心配置:** `CONFIG_SR_MN_EN_MULTINET5_SINGLE_RECOGNITION_QUANT8=y`  
这是 **MultiNet5 英文单识别量化版本**，与参考工程完全一致！

---

### MultiNet Models - 中文

| 配置项 | 参考工程 | 小智项目 | 状态 |
|--------|---------|---------|------|
| `CONFIG_SR_MN_CN_NONE` | ✅ y | ✅ y | ✅ 一致 |
| `CONFIG_SR_MN_CN_MULTINET5_RECOGNITION_QUANT8` | ❌ is not set | ❌ is not set | ✅ 一致 |
| `CONFIG_SR_MN_CN_MULTINET6_QUANT` | ❌ is not set | ❌ is not set | ✅ 一致 |
| `CONFIG_SR_MN_CN_MULTINET7_QUANT` | ❌ is not set | ❌ is not set | ✅ 一致 |

**说明:** 两个项目都不使用中文 MultiNet 模型。

---

### AFE Configuration

| 配置项 | 参考工程 | 小智项目 | 状态 |
|--------|---------|---------|------|
| `CONFIG_AFE_INTERFACE_V1` | ✅ y | ✅ y | ✅ 一致 |

---

## 🎯 关键变更总结

### 改造前（小智项目）
```ini
CONFIG_USE_AFE_WAKE_WORD=y
# CONFIG_USE_CUSTOM_WAKE_WORD is not set
CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y
CONFIG_SR_MN_EN_NONE=y
# CONFIG_SR_MN_EN_MULTINET5_SINGLE_RECOGNITION_QUANT8 is not set
```

### 改造后（与参考工程一致）
```ini
# CONFIG_USE_AFE_WAKE_WORD is not set
CONFIG_USE_CUSTOM_WAKE_WORD=y
CONFIG_CUSTOM_WAKE_WORD="hi plaud"
CONFIG_CUSTOM_WAKE_WORD_DISPLAY="hi plaud"
CONFIG_CUSTOM_WAKE_WORD_THRESHOLD=50
CONFIG_SR_WN_WN9_HILEXIN=y
# CONFIG_SR_MN_EN_NONE is not set
CONFIG_SR_MN_EN_MULTINET5_SINGLE_RECOGNITION_QUANT8=y
CONFIG_SR_MN_CN_NONE=y
```

---

## ✅ 验证配置

运行以下命令验证关键配置：

```bash
cd /Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32

# 验证 CustomWakeWord 已启用
grep "CONFIG_USE_CUSTOM_WAKE_WORD" sdkconfig

# 验证 MultiNet5 英文模型已选择
grep "CONFIG_SR_MN_EN_MULTINET5_SINGLE_RECOGNITION_QUANT8" sdkconfig

# 验证 WakeNet 模型
grep "CONFIG_SR_WN_WN9_HILEXIN" sdkconfig

# 验证中文模型未启用
grep "CONFIG_SR_MN_CN_NONE" sdkconfig
```

**预期输出:**
```
CONFIG_USE_CUSTOM_WAKE_WORD=y
CONFIG_SR_MN_EN_MULTINET5_SINGLE_RECOGNITION_QUANT8=y
CONFIG_SR_WN_WN9_HILEXIN=y
CONFIG_SR_MN_CN_NONE=y
```

---

## 📝 模型文件路径

### MultiNet5 英文模型

**小智项目:**
```
managed_components/espressif__esp-sr/model/multinet_model/mn5q8_en/
```

**参考工程:**
```
managed_components/espressif__esp-sr/model/multinet_model/mn5q8_en/
```

✅ **模型路径一致！**

---

## 🔄 下一步操作

### 1. 重新配置和编译

```bash
cd /Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32

# 清理旧构建
idf.py fullclean

# 重新配置（应用新的 sdkconfig）
idf.py reconfigure

# 编译
idf.py build
```

### 2. 预期编译日志

**应该看到:**
```
[...] Found MultiNet model: mn5q8_en (language: en)
[...] Adding 2 wake word commands:
[...] [0] command="hi PLeD", text="hi plaud", action="wake"
[...] [1] command="hi NgSgBcLD", text="hi nicebuild", action="wake"
[...] ✓ Successfully added 2 commands to MultiNet
[...] CustomWakeWord initialized successfully (MultiNet only mode)
```

### 3. 烧录和测试

```bash
idf.py flash monitor
```

**测试唤醒词:**
- 🗣️ "Hi Plaud"
- 🗣️ "Hi Nicebuild"

---

## 📚 参考文档

- **参考工程路径:** `/Users/xionghao/Documents/plaud/GitHub/esp-sr-multinet`
- **参考工程 sdkconfig:** `/Users/xionghao/Documents/plaud/GitHub/esp-sr-multinet/sdkconfig`
- **参考工程核心代码:** `/Users/xionghao/Documents/plaud/GitHub/esp-sr-multinet/main/blink_example_main.c`

- **小智项目路径:** `/Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32`
- **配置备份:** `sdkconfig.backup_before_multinet`
- **核心实现:** `main/audio/wake_words/custom_wake_word.cc`

---

## 🎨 配置说明

### MultiNet5 Single Recognition Quant8

- **全名:** MultiNet5 Single Recognition Quantized 8-bit
- **语言:** 英文
- **特点:** 
  - 单次识别模式（适合唤醒词检测）
  - 8 位量化（减少内存占用）
  - 低延迟，高准确率
- **适用场景:** 作为唤醒模型，检测固定的英文命令

### WakeNet9 Hilexin

- **用途:** 原本用于唤醒词检测（"Hi Lexin"）
- **当前状态:** 配置中选择，但代码中禁用
- **原因:** MultiNet Only 模式不需要 WakeNet

---

## ✅ 配置一致性检查清单

- [x] `CONFIG_USE_CUSTOM_WAKE_WORD=y`
- [x] `CONFIG_SR_MN_EN_MULTINET5_SINGLE_RECOGNITION_QUANT8=y`
- [x] `CONFIG_SR_WN_WN9_HILEXIN=y`
- [x] `CONFIG_SR_MN_CN_NONE=y`
- [x] `CONFIG_AFE_INTERFACE_V1=y`
- [x] CustomWakeWord 默认唤醒词: "hi plaud", "hi nicebuild"
- [x] 代码实现与参考工程一致
- [x] 模型路径一致

---

**文档版本:** v1.0  
**对比基准:** esp-sr-multinet (2025-10-10)  
**小智项目版本:** xiaozhi-esp32 v2.0.3+  
**配置状态:** ✅ 完全一致

