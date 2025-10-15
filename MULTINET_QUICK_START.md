# MultiNet 唤醒模型 - 快速开始指南

## ✅ 改造完成！

已将小智 ESP32 项目改造为 **MultiNet Only 模式**（直接使用 MultiNet 作为唤醒模型，禁用 WakeNet）。

---

## 🎯 改造内容

### 已修改的文件

1. **`main/audio/wake_words/custom_wake_word.cc`** ✓
   - 禁用 WakeNet 依赖
   - 直接使用 MultiNet 持续检测
   - 使用 `esp_mn_commands_add()` 添加唤醒词
   - 默认唤醒词: "hi plaud", "hi nicebuild"

2. **备份文件**: `custom_wake_word.cc.backup` ✓
   - 原始文件已备份，如需恢复可使用

3. **`audio_service.cc`** ✓
   - 模型选择逻辑已经是优先选择 MultiNet（无需修改）

---

## 📝 配置步骤

### 步骤 1: 修改 sdkconfig

需要启用 MultiNet 英文模型并禁用 WakeNet。

#### 选项 A: 使用 menuconfig（推荐）

```bash
cd /Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32
idf.py menuconfig
```

**导航路径:**
```
Component config → 
  ESP Speech Recognition → 
    Speech Recognition Model Configuration → 
      *** MultiNet Model ***
      [*] Select MultiNet5 Quantized (English)  ← 选中
      
XiaoZhi AI Configuration →
  Wake Word Configuration →
    ( ) Disabled                                ← 不选
    ( ) Wakenet model without AFE              ← 不选
    ( ) Wakenet model with AFE                 ← 不选
    (*) Multinet model (Custom Wake Word)      ← 选中！
    
    Custom Wake Word (hi plaud)                ← 默认即可
    Custom Wake Word Display (hi plaud)        ← 默认即可
    Custom Wake Word Threshold (50)            ← 默认即可
```

#### 选项 B: 直接修改 sdkconfig（快速）

在 `sdkconfig` 文件中查找并修改以下配置：

```ini
#
# Wake Word Configuration
#
# CONFIG_WAKE_WORD_DISABLED is not set
# CONFIG_USE_ESP_WAKE_WORD is not set
# CONFIG_USE_AFE_WAKE_WORD is not set
CONFIG_USE_CUSTOM_WAKE_WORD=y           ← 确保是 y
CONFIG_CUSTOM_WAKE_WORD="hi plaud"
CONFIG_CUSTOM_WAKE_WORD_DISPLAY="hi plaud"
CONFIG_CUSTOM_WAKE_WORD_THRESHOLD=50
CONFIG_SEND_WAKE_WORD_DATA=y

#
# Speech Recognition Model Configuration  
#
# WakeNet Models
# CONFIG_SR_WN9_HIESP is not set         ← 确保全部禁用
# CONFIG_SR_WN9_HILEXIN is not set
# ... (其他 WN 模型都设为 not set)

# MultiNet Models
CONFIG_SR_MN5Q8_EN=y                     ← 确保是 y (英文模型)
# CONFIG_SR_MN_CN is not set             ← 中文模型不需要
```

---

## 🔨 编译和烧录

### 1. 清理旧的构建（推荐）

```bash
cd /Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32
idf.py fullclean
```

### 2. 重新配置（如果修改了 sdkconfig）

```bash
idf.py reconfigure
```

### 3. 编译

```bash
idf.py build
```

**预期输出（部分）:**
```
[...] Found MultiNet model: mn5q8_en (language: en)
[...] MultiNet threshold set to: 0.50
[...] Adding 2 wake word commands:
[...] [0] command="hi PLeD", text="hi plaud", action="wake"
[...] [1] command="hi NgSgBcLD", text="hi nicebuild", action="wake"
[...] ✓ Successfully added 2 commands to MultiNet
[...] CustomWakeWord initialized successfully (MultiNet only mode)
```

### 4. 烧录并监控

```bash
idf.py flash monitor
```

---

## 🧪 测试唤醒

### 测试步骤

1. **等待设备进入待机状态**
   - 串口输出: `STATE: idle`
   - 显示: "待命"

2. **说出唤醒词**（英文，清晰发音）
   - **"Hi Plaud"** (发音: /haɪ plɔːd/)
   - **"Hi Nicebuild"** (发音: /haɪ naɪsbɪld/)

3. **观察日志输出**

**成功检测的日志:**
```
I (12345) CustomWakeWord: ✓ Detected command ID: 0, prob: 0.85
I (12345) CustomWakeWord:   Command: hi PLeD, Text: hi plaud, Action: wake
I (12345) CustomWakeWord: ✓ Wake word detected: hi plaud
I (12346) AudioService: 📣 Calling on_wake_word_detected callback: hi plaud
I (12347) Application: 🔔 Wake word event bit set!
I (12348) Application: 🎯 OnWakeWordDetected() called, state=3
I (12348) Application: Device in IDLE state, processing wake word...
I (12349) Application: 🔗 Opening audio channel...
```

---

## 📊 与参考项目对比

### 参考项目 (esp-sr-multinet)

```c
// 添加命令
esp_mn_commands_clear();
esp_mn_commands_add(0, "hi PLeD");
esp_mn_commands_add(1, "hi NgSgBcLD");

// 配置 AFE
afe_config->wakenet_init = false;  // 禁用 WakeNet
afe_config->vad_init = true;

// 检测循环
while (true) {
    res = afe_handle->fetch(afe_data);
    mn_state = multinet->detect(model_data, res->data);
    if (mn_state == ESP_MN_STATE_DETECTED) {
        // 处理检测结果
    }
}
```

### 小智项目 (改造后)

```cpp
// Initialize() - 添加命令
esp_mn_commands_clear();
commands_.push_back({"hi PLeD", "hi plaud", "wake"});
commands_.push_back({"hi NgSgBcLD", "hi nicebuild", "wake"});
for (int i = 0; i < commands_.size(); i++) {
    esp_mn_commands_add(i, commands_[i].command.c_str());
}
esp_mn_commands_update();

// Feed() - 检测循环
void CustomWakeWord::Feed(const std::vector<int16_t>& data) {
    mn_state = multinet_->detect(multinet_model_data_, data.data());
    if (mn_state == ESP_MN_STATE_DETECTED) {
        // 获取命令 ID
        esp_mn_results_t* mn_result = multinet_->get_results(multinet_model_data_);
        int command_id = mn_result->phrase_id[0];
        
        // 触发回调
        if (commands_[command_id].action == "wake") {
            wake_word_detected_callback_(commands_[command_id].text);
        }
    }
}
```

✅ **核心逻辑完全一致！**

---

## 🎨 自定义唤醒词

### 方式 1: 修改代码（固定唤醒词）

编辑 `custom_wake_word.cc` 的 `Initialize()` 函数：

```cpp
// 第 101-105 行
commands_.push_back({"hi PLeD", "hi plaud", "wake"});
commands_.push_back({"hi NgSgBcLD", "hi nicebuild", "wake"});

// 添加新的唤醒词
commands_.push_back({"hi TpG", "hi espressif", "wake"});
commands_.push_back({"TlGSfVr", "turn on", "wake"});
```

**音素格式:**
- 英文单词需要转换为音素格式
- 参考: ESP-SR 文档 https://docs.espressif.com/projects/esp-sr/

### 方式 2: 通过 assets 配置（动态）

在 assets 分区的 `index.json` 中配置：

```json
{
  "multinet_model": {
    "language": "en",
    "duration": 5000,
    "threshold": 0.5,
    "commands": [
      {
        "command": "hi PLeD",
        "text": "hi plaud",
        "action": "wake"
      },
      {
        "command": "hi NgSgBcLD",
        "text": "hi nicebuild",
        "action": "wake"
      }
    ]
  }
}
```

### 中文唤醒词

如果需要中文唤醒词，修改：

```cpp
language_ = "cn";
commands_.push_back({"ni hao xiao zhi", "你好小智", "wake"});
commands_.push_back({"xiao zhi xiao zhi", "小智小智", "wake"});
```

**配置:**
```ini
CONFIG_SR_MN_CN=y
# CONFIG_SR_MN5Q8_EN is not set
```

---

## ⚙️ 调优参数

### 检测阈值 (threshold)

```cpp
threshold_ = 0.5;  // 范围: 0.0 - 1.0
```

| 值 | 效果 |
|----|------|
| 0.3 | 更敏感，容易触发，可能误触发 |
| **0.5** | **平衡（推荐）** |
| 0.7 | 更严格，不容易误触发，可能漏检 |

### 超时时间 (duration)

```cpp
duration_ = 5000;  // 单位: 毫秒
```

- **5000ms**: 用户有 5 秒时间说完整命令
- 过短: 可能检测不完整
- 过长: 延迟增加

---

## 🐛 故障排除

### 问题 1: 编译错误 - 找不到 MultiNet 模型

**错误信息:**
```
Failed to find MultiNet model for language: en
```

**解决方案:**
```bash
idf.py menuconfig
# 确保选中: CONFIG_SR_MN5Q8_EN=y
```

### 问题 2: 无法检测到唤醒词

**可能原因:**
1. 麦克风配置错误（音频电平为 0）
2. 阈值过高
3. 发音不清晰

**调试步骤:**
```cpp
// 在 Feed() 函数中添加调试日志
ESP_LOGI(TAG, "mn_state: %d", mn_state);
```

### 问题 3: MultiNet 模型未加载

**检查日志:**
```
I (xxx) CustomWakeWord: Found MultiNet model: mn5q8_en
```

如果没有这行日志，说明模型未正确加载。

**解决:**
```bash
idf.py fullclean
idf.py reconfigure
idf.py build
```

---

## 📚 相关文档

- **改造方案**: `MULTINET_ONLY_MIGRATION.md` - 详细的改造步骤
- **参考项目**: `/Users/xionghao/Documents/plaud/GitHub/esp-sr-multinet`
- **ESP-SR 文档**: https://docs.espressif.com/projects/esp-sr/
- **MultiNet 说明**: https://github.com/espressif/esp-sr/blob/master/README_CN.md

---

## 🔄 回滚到原版本

如果需要恢复到 WakeNet 模式：

```bash
cd /Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32
cp main/audio/wake_words/custom_wake_word.cc.backup main/audio/wake_words/custom_wake_word.cc

# 修改 sdkconfig
idf.py menuconfig
# 选择: Wake Word Configuration → Wakenet model with AFE

idf.py fullclean
idf.py build
```

---

**文档版本:** v1.0  
**创建日期:** 2025-10-10  
**测试板型:** ESP32-S3-Korvo-2 V3.0  
**固件版本:** xiaozhi-esp32 v2.0.3+

