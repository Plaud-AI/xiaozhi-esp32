# MultiNet Only 改造 - 文件变更总结

## ✅ 改造完成！

已成功将小智 ESP32 项目从 **WakeNet + MultiNet** 模式改造为 **MultiNet Only** 模式。

---

## 📝 文件变更清单

### 1. 已修改的文件

| 文件 | 状态 | 说明 |
|-----|------|------|
| `main/audio/wake_words/custom_wake_word.cc` | ✅ 已修改 | 核心实现文件 |
| `main/audio/wake_words/custom_wake_word.cc.backup` | ✅ 已创建 | 原文件备份 |
| `main/audio/wake_words/custom_wake_word_multinet_only.cc` | ✅ 已创建 | 新实现（已复制到 custom_wake_word.cc） |
| `main/audio/audio_service.cc` | ✅ 无需修改 | 模型选择逻辑已经优先选择 MultiNet |

### 2. 新增的文档

| 文件 | 说明 |
|-----|------|
| `MULTINET_ONLY_MIGRATION.md` | 详细的改造方案和代码说明 |
| `MULTINET_QUICK_START.md` | 快速开始指南 |
| `CHANGES_MULTINET.md` | 本文档 - 变更总结 |

---

## 🔄 核心改动

### custom_wake_word.cc 主要变更

#### 1. Initialize() 函数

**改动前:**
```cpp
language_ = "cn";
models_ = esp_srmodel_init("model");
#ifdef CONFIG_CUSTOM_WAKE_WORD
    commands_.push_back({CONFIG_CUSTOM_WAKE_WORD, ...});
#endif
```

**改动后:**
```cpp
language_ = "en";  // 使用英文模型
models_ = esp_srmodel_init("model");

// 添加固定的英文音素唤醒词（与 esp-sr-multinet 项目一致）
commands_.push_back({"hi PLeD", "hi plaud", "wake"});
commands_.push_back({"hi NgSgBcLD", "hi nicebuild", "wake"});

// 使用 esp_mn_commands_add 添加命令
for (int i = 0; i < commands_.size(); i++) {
    esp_mn_commands_add(i, commands_[i].command.c_str());
}
esp_mn_commands_update();
```

#### 2. Feed() 函数

**改动前:**
```cpp
// 原逻辑可能依赖 WakeNet 或其他条件
```

**改动后:**
```cpp
// 直接使用 MultiNet 检测（不依赖 WakeNet）
esp_mn_state_t mn_state = multinet_->detect(multinet_model_data_, data.data());

if (mn_state == ESP_MN_STATE_DETECTED) {
    esp_mn_results_t* mn_result = multinet_->get_results(multinet_model_data_);
    int command_id = mn_result->phrase_id[0];
    
    if (commands_[command_id].action == "wake") {
        last_detected_wake_word_ = commands_[command_id].text;
        wake_word_detected_callback_(last_detected_wake_word_);
    }
}
```

---

## 🎯 关键特性

### 1. 唤醒词列表

**默认唤醒词（英文音素格式）:**
- `"hi PLeD"` → 显示为 "hi plaud"
- `"hi NgSgBcLD"` → 显示为 "hi nicebuild"

### 2. 与参考项目对比

| 特性 | esp-sr-multinet | xiaozhi-esp32 (改造后) | 一致性 |
|-----|-----------------|------------------------|-------|
| MultiNet 初始化 | ✓ | ✓ | ✅ |
| esp_mn_commands_add | ✓ | ✓ | ✅ |
| 唤醒词（hi plaud） | ✓ | ✓ | ✅ |
| 唤醒词（hi nicebuild） | ✓ | ✓ | ✅ |
| 直接检测（无 WakeNet） | ✓ | ✓ | ✅ |
| AFE 配置 | wakenet_init=false | 由模型选择决定 | ✅ |

**✅ 核心逻辑完全一致！**

---

## 📋 下一步操作

### 步骤 1: 配置 sdkconfig

**必须配置:**
```ini
# 启用 CustomWakeWord
CONFIG_USE_CUSTOM_WAKE_WORD=y

# 启用 MultiNet 英文模型
CONFIG_SR_MN5Q8_EN=y

# 禁用 WakeNet
# CONFIG_USE_AFE_WAKE_WORD is not set
```

**快速配置方法:**
```bash
cd /Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32
idf.py menuconfig

# 导航到:
# XiaoZhi AI Configuration → Wake Word Configuration
# 选择: (*) Multinet model (Custom Wake Word)

# 导航到:
# Component config → ESP Speech Recognition → Speech Recognition Model Configuration
# 选择: [*] Select MultiNet5 Quantized (English)
```

### 步骤 2: 编译固件

```bash
# 清理旧的构建（推荐）
idf.py fullclean

# 重新配置
idf.py reconfigure

# 编译
idf.py build
```

**预期输出（关键日志）:**
```
[...] Found MultiNet model: mn5q8_en (language: en)
[...] Adding 2 wake word commands:
[...] [0] command="hi PLeD", text="hi plaud", action="wake"
[...] [1] command="hi NgSgBcLD", text="hi nicebuild", action="wake"
[...] ✓ Successfully added 2 commands to MultiNet
[...] CustomWakeWord initialized successfully (MultiNet only mode)
```

### 步骤 3: 烧录和测试

```bash
# 烧录并监控
idf.py flash monitor

# 测试唤醒词
# 说: "Hi Plaud" 或 "Hi Nicebuild"
```

**成功标志:**
- 日志显示: `✓ Wake word detected: hi plaud`
- 设备状态: `STATE: connecting` → `STATE: listening`
- 显示屏: "连接中" → "聆听中"

---

## 🔧 自定义配置

### 添加更多唤醒词

编辑 `custom_wake_word.cc` 的 `Initialize()` 函数（第 101-105 行）:

```cpp
// 原有的
commands_.push_back({"hi PLeD", "hi plaud", "wake"});
commands_.push_back({"hi NgSgBcLD", "hi nicebuild", "wake"});

// 添加新的
commands_.push_back({"hi TpG", "hi espressif", "wake"});
commands_.push_back({"TlGSfVr", "turn on", "wake"});
```

### 调整检测参数

```cpp
// 第 99 行
threshold_ = 0.5;  // 范围: 0.0-1.0（越低越敏感）
duration_ = 5000;  // 超时时间（毫秒）
```

### 切换到中文模型

```cpp
// 第 98 行
language_ = "cn";

// 第 101-105 行
commands_.push_back({"ni hao xiao zhi", "你好小智", "wake"});
commands_.push_back({"xiao zhi xiao zhi", "小智小智", "wake"});
```

**配置:**
```ini
CONFIG_SR_MN_CN=y
# CONFIG_SR_MN5Q8_EN is not set
```

---

## 📊 测试清单

### 功能测试

- [ ] 编译成功，无错误
- [ ] 烧录成功
- [ ] 设备启动，进入待机状态
- [ ] 日志显示 MultiNet 已加载
- [ ] 日志显示命令已添加
- [ ] 说 "Hi Plaud" 能成功唤醒
- [ ] 说 "Hi Nicebuild" 能成功唤醒
- [ ] 唤醒后进入对话流程
- [ ] 对话流程正常（STT、TTS）

### 日志检查

**必须出现的关键日志:**
```
✅ [CustomWakeWord] Found MultiNet model: mn5q8_en
✅ [CustomWakeWord] Adding 2 wake word commands:
✅ [CustomWakeWord] ✓ Successfully added 2 commands to MultiNet
✅ [CustomWakeWord] CustomWakeWord initialized successfully (MultiNet only mode)
✅ [AudioService] Creating CustomWakeWord (MN prefix found)
✅ [AudioService] Wake word object created successfully
```

**检测成功的日志:**
```
✅ [CustomWakeWord] ✓ Detected command ID: 0, prob: 0.85
✅ [CustomWakeWord] ✓ Wake word detected: hi plaud
✅ [Application] *** Wake word detected: hi plaud ***
```

---

## 🐛 常见问题

### Q1: 编译错误 - 找不到 MultiNet 模型

**错误:**
```
Failed to find MultiNet model for language: en
```

**解决:**
```bash
idf.py menuconfig
# 确保选中: CONFIG_SR_MN5Q8_EN=y
```

### Q2: 无法检测到唤醒词

**检查:**
1. 音频电平是否正常（不是 0）
2. MultiNet 是否正确加载
3. 命令是否正确添加
4. 发音是否清晰

**调试:**
```cpp
// 在 Feed() 中添加
ESP_LOGI(TAG, "mn_state: %d", mn_state);
```

### Q3: 想要回滚到原版本

```bash
cp main/audio/wake_words/custom_wake_word.cc.backup \
   main/audio/wake_words/custom_wake_word.cc

idf.py menuconfig
# 选择: Wake Word Configuration → Wakenet model with AFE

idf.py fullclean
idf.py build
```

---

## 📚 参考资料

### 文档

- **详细改造方案**: `MULTINET_ONLY_MIGRATION.md`
- **快速开始指南**: `MULTINET_QUICK_START.md`
- **参考项目**: `/Users/xionghao/Documents/plaud/GitHub/esp-sr-multinet/main/blink_example_main.c`

### 相关代码

- **核心实现**: `main/audio/wake_words/custom_wake_word.cc`
- **模型选择**: `main/audio/audio_service.cc` (第 662-704 行)
- **唤醒处理**: `main/application.cc` (第 626-669 行)

### 外部链接

- ESP-SR 官方文档: https://docs.espressif.com/projects/esp-sr/
- MultiNet 说明: https://github.com/espressif/esp-sr
- ESP32-S3 Korvo-2: https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/board-esp32-s3-korvo-2.html

---

## ✅ 改造状态

| 项目 | 状态 | 备注 |
|-----|------|------|
| 代码修改 | ✅ 完成 | custom_wake_word.cc 已修改 |
| 备份原文件 | ✅ 完成 | custom_wake_word.cc.backup |
| 文档创建 | ✅ 完成 | 3 份文档已创建 |
| 配置指南 | ✅ 完成 | MULTINET_QUICK_START.md |
| 测试就绪 | ⏳ 待用户 | 需要配置 sdkconfig 并编译 |

---

**改造完成日期:** 2025-10-10  
**改造版本:** v1.0  
**适用固件:** xiaozhi-esp32 v2.0.3+  
**测试板型:** ESP32-S3-Korvo-2 V3.0

