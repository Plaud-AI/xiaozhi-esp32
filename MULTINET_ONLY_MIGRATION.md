# MultiNet 唯一唤醒模型改造方案

## 📋 改造目标

将小智 ESP32 项目从 **WakeNet + MultiNet** 模式改造为 **MultiNet 唯一**模式：
- ❌ 禁用 WakeNet 唤醒词检测
- ✅ 直接使用 MultiNet 作为唤醒模型
- ✅ 使用 `esp_mn_commands_add()` 添加唤醒指令
- ✅ 参考 `esp-sr-multinet` 项目实现

---

## 🎯 核心区别

### 原模式（WakeNet + MultiNet）
```
用户说话 → AFE → WakeNet 检测 "你好小智" → 激活 MultiNet → 识别命令词
```

### 新模式（MultiNet Only）
```
用户说话 → AFE → MultiNet 持续检测唤醒命令 → 直接触发
```

---

## 📝 改造步骤

### 步骤 1: 修改 CustomWakeWord 实现

禁用对 WakeNet 的依赖，直接使用 MultiNet 持续检测。

#### 文件: `main/audio/wake_words/custom_wake_word.cc`

##### A. 修改 Initialize() 函数

```cpp
// 原代码: 第 87-130 行
bool CustomWakeWord::Initialize(AudioCodec* codec, srmodel_list_t* models_list) {
    codec_ = codec;
    commands_.clear();
    
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 修改点 1: 简化模型加载逻辑
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    if (models_list == nullptr) {
        models_ = esp_srmodel_init("model");
        language_ = "en";  // 使用英文模型（参考项目用的）
        threshold_ = 0.5;  // 设置检测阈值
        
        // 添加固定的唤醒词（英文音素格式）
        commands_.push_back({"hi PLeD", "hi plaud", "wake"});
        commands_.push_back({"hi NgSgBcLD", "hi nicebuild", "wake"});
    } else {
        models_ = models_list;
        // 从 assets 读取配置（如果有）
        ParseWakenetModelConfig();
    }
    
    if (models_ == nullptr || models_->num == -1) {
        ESP_LOGE(TAG, "Failed to initialize model");
        return false;
    }
    
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 修改点 2: 初始化 MultiNet
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    mn_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, language_.c_str());
    if (mn_name_ == nullptr) {
        ESP_LOGE(TAG, "Failed to find MultiNet model for language: %s", language_.c_str());
        return false;
    }
    
    ESP_LOGI(TAG, "Found MultiNet model: %s", mn_name_);
    
    multinet_ = esp_mn_handle_from_name(mn_name_);
    if (multinet_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create MultiNet handle");
        return false;
    }
    
    // 创建 MultiNet 模型数据（5000ms 超时）
    multinet_model_data_ = multinet_->create(mn_name_, 5000);
    if (multinet_model_data_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create MultiNet model data");
        return false;
    }
    
    // 设置检测阈值
    multinet_->set_det_threshold(multinet_model_data_, threshold_);
    
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 修改点 3: 添加唤醒命令
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    esp_mn_commands_clear();
    
    for (int i = 0; i < commands_.size(); i++) {
        ESP_LOGI(TAG, "Adding command %d: %s (%s)", 
                 i, commands_[i].command.c_str(), commands_[i].text.c_str());
        
        // 使用音素格式添加命令（例如: "hi PLeD"）
        esp_mn_commands_add(i, commands_[i].command.c_str());
    }
    
    // 更新命令到模型
    esp_mn_error_t* err = esp_mn_commands_update();
    if (err) {
        ESP_LOGE(TAG, "Failed to update commands, %d errors:", err->num);
        for (int i = 0; i < err->num; i++) {
            ESP_LOGE(TAG, "  Error command ID: %d, content: %s", 
                     err->phrases[i]->command_id, 
                     err->phrases[i]->string);
        }
        esp_mn_commands_print_error(err);
        return false;
    }
    
    // 打印已添加的命令
    ESP_LOGI(TAG, "Successfully added %d commands:", commands_.size());
    esp_mn_commands_print();
    
    ESP_LOGI(TAG, "CustomWakeWord initialized successfully (MultiNet only mode)");
    return true;
}
```

##### B. 修改 Feed() 函数（核心检测逻辑）

```cpp
// 原代码: 第 137-183 行
void CustomWakeWord::Feed(const std::vector<int16_t>& data) {
    if (multinet_model_data_ == nullptr || !running_) {
        return;
    }
    
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 修改点 4: 直接使用 MultiNet 检测（不依赖 WakeNet）
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    esp_mn_state_t mn_state;
    
    // 处理双通道音频（如果是）
    if (codec_->input_channels() == 2) {
        // 提取左声道
        auto mono_data = std::vector<int16_t>(data.size() / 2);
        for (size_t i = 0, j = 0; i < mono_data.size(); ++i, j += 2) {
            mono_data[i] = data[j];
        }
        
        StoreWakeWordData(mono_data);
        mn_state = multinet_->detect(multinet_model_data_, 
                                     const_cast<int16_t*>(mono_data.data()));
    } else {
        StoreWakeWordData(data);
        mn_state = multinet_->detect(multinet_model_data_, 
                                     const_cast<int16_t*>(data.data()));
    }
    
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 处理检测结果
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    if (mn_state == ESP_MN_STATE_DETECTING) {
        // 正在检测中，无需处理
        return;
    } 
    else if (mn_state == ESP_MN_STATE_DETECTED) {
        // 检测到命令！
        esp_mn_results_t* mn_result = multinet_->get_results(multinet_model_data_);
        
        if (mn_result != NULL && mn_result->num > 0) {
            // 获取第一个检测到的命令 ID
            int command_id = mn_result->phrase_id[0];
            
            ESP_LOGI(TAG, "✓ Detected command ID: %d, prob: %.2f", 
                     command_id, mn_result->prob[command_id]);
            
            // 检查命令 ID 是否有效
            if (command_id >= 0 && command_id < commands_.size()) {
                auto& command = commands_[command_id];
                
                // 只响应 "wake" 动作的命令
                if (command.action == "wake") {
                    last_detected_wake_word_ = command.text;  // 例如: "hi plaud"
                    running_ = false;  // 停止检测
                    
                    ESP_LOGI(TAG, "✓ Wake word detected: %s", last_detected_wake_word_.c_str());
                    
                    // 触发回调
                    if (wake_word_detected_callback_) {
                        wake_word_detected_callback_(last_detected_wake_word_);
                    }
                }
            }
        }
        
        // 清理 MultiNet 状态
        multinet_->clean(multinet_model_data_);
    } 
    else if (mn_state == ESP_MN_STATE_TIMEOUT) {
        // 超时，清理状态
        ESP_LOGD(TAG, "MultiNet timeout");
        multinet_->clean(multinet_model_data_);
    }
}
```

---

### 步骤 2: 修改音频服务模型选择逻辑

确保优先选择 CustomWakeWord（MultiNet）。

#### 文件: `main/audio/audio_service.cc`

##### 修改 SetModelsList() 函数

```cpp
// 位置: 第 662-704 行
void AudioService::SetModelsList(srmodel_list_t* models_list) {
    models_list_ = models_list;
    
    ESP_LOGI(TAG, "SetModelsList called, models_list: %p", models_list);
    if (models_list_ != nullptr) {
        ESP_LOGI(TAG, "Models list count: %d", models_list_->num);
        for (int i = 0; i < models_list_->num && i < 10; i++) {
            ESP_LOGI(TAG, "  Model %d: %s", i, models_list_->model_name[i]);
        }
    } else {
        ESP_LOGW(TAG, "Models list is NULL!");
    }
    
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 修改点 5: 优先选择 MultiNet（不依赖 WakeNet）
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    
    // 检查是否有 MultiNet 模型
    if (esp_srmodel_filter(models_list_, ESP_MN_PREFIX, NULL) != nullptr) {
        ESP_LOGI(TAG, "Creating CustomWakeWord (MultiNet only mode)");
        wake_word_ = std::make_unique<CustomWakeWord>();
    } 
    else {
        // 如果没有 MultiNet，尝试 WakeNet（兼容性）
        if (esp_srmodel_filter(models_list_, ESP_WN_PREFIX, NULL) != nullptr) {
            ESP_LOGI(TAG, "Creating AfeWakeWord (WN prefix found)");
            wake_word_ = std::make_unique<AfeWakeWord>();
        } else {
            ESP_LOGW(TAG, "No wake word model found in models list!");
            wake_word_ = nullptr;
        }
    }
#else
    // 其他芯片保持原逻辑
    if (esp_srmodel_filter(models_list_, ESP_WN_PREFIX, NULL) != nullptr) {
        ESP_LOGI(TAG, "Creating EspWakeWord (WN prefix found)");
        wake_word_ = std::make_unique<EspWakeWord>();
    } else {
        ESP_LOGW(TAG, "No wake word model found in models list!");
        wake_word_ = nullptr;
    }
#endif
    
    if (wake_word_) {
        ESP_LOGI(TAG, "Wake word object created successfully");
        wake_word_->OnWakeWordDetected([this](const std::string& wake_word) {
            if (callbacks_.on_wake_word_detected) {
                callbacks_.on_wake_word_detected(wake_word);
            }
        });
    }
}
```

---

### 步骤 3: 配置 sdkconfig

#### 方式 A: 使用 menuconfig（推荐）

```bash
cd /Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32
idf.py menuconfig
```

配置路径：
```
Component config → 
  ESP Speech Recognition → 
    MultiNet Model Configuration → 
      [*] Select MultiNet5 Quantized (English)
```

**关键配置项:**
- ✅ `CONFIG_SR_MN5Q8_EN=y` - 启用 MultiNet5 英文模型
- ❌ `CONFIG_USE_AFE_WAKE_WORD=n` - 禁用 WakeNet
- ✅ `CONFIG_USE_CUSTOM_WAKE_WORD=y` - 启用 CustomWakeWord

#### 方式 B: 直接修改 sdkconfig（快速）

在 `sdkconfig` 中添加/修改：

```ini
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 禁用 WakeNet
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# CONFIG_USE_AFE_WAKE_WORD is not set
CONFIG_WAKE_WORD_DISABLED=n

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 启用 CustomWakeWord（MultiNet）
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
CONFIG_USE_CUSTOM_WAKE_WORD=y
CONFIG_CUSTOM_WAKE_WORD="hi plaud"
CONFIG_CUSTOM_WAKE_WORD_DISPLAY="hi plaud"
CONFIG_CUSTOM_WAKE_WORD_THRESHOLD=50

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# MultiNet 模型选择（英文）
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
CONFIG_SR_MN5Q8_EN=y
# CONFIG_SR_MN_CN is not set

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# AFE 配置（禁用 WakeNet）
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
CONFIG_AFE_WAKENET_INIT=n
CONFIG_AFE_VAD_INIT=y
```

---

### 步骤 4: 修改 Kconfig 配置（可选）

如果需要更灵活的配置，可以修改 `main/Kconfig.projbuild`：

```kconfig
# 位置: 第 548-576 行
config USE_CUSTOM_WAKE_WORD
    bool "Multinet model (Custom Wake Word / MultiNet Only)"
    depends on (IDF_TARGET_ESP32S3 || IDF_TARGET_ESP32P4) && SPIRAM
    help
        Use MultiNet for wake word detection instead of WakeNet.
        This mode continuously detects custom commands without 
        requiring a separate wake word.

config CUSTOM_WAKE_WORD
    string "Custom Wake Word (Phoneme format for English)"
    default "hi PLeD"
    depends on USE_CUSTOM_WAKE_WORD
    help
        Custom Wake Word in phoneme format (English).
        Examples:
          - "hi PLeD" for "hi plaud"
          - "hi NgSgBcLD" for "hi nicebuild"

config CUSTOM_WAKE_WORD_DISPLAY
    string "Custom Wake Word Display Text"
    default "hi plaud"
    depends on USE_CUSTOM_WAKE_WORD
    help
        The text to display/send when wake word is detected.

config CUSTOM_WAKE_WORD_THRESHOLD
    int "Custom Wake Word Threshold (%)"
    default 50
    range 1 99
    depends on USE_CUSTOM_WAKE_WORD
    help
        Detection threshold (1-99). Lower = more sensitive.
        Recommended: 50 for English phoneme commands.
```

---

## 🧪 测试步骤

### 1. 编译固件

```bash
cd /Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32
idf.py build
```

**预期输出:**
```
[...] CustomWakeWord initialized successfully (MultiNet only mode)
[...] Successfully added 2 commands:
[...] Command 0: hi PLeD (hi plaud)
[...] Command 1: hi NgSgBcLD (hi nicebuild)
```

### 2. 烧录固件

```bash
idf.py flash monitor
```

### 3. 测试唤醒

对着麦克风说：
- **"Hi Plaud"** → 应该检测到并响应
- **"Hi Nicebuild"** → 应该检测到并响应

**预期日志:**
```
I (12345) CustomWakeWord: ✓ Detected command ID: 0, prob: 0.85
I (12345) CustomWakeWord: ✓ Wake word detected: hi plaud
I (12346) Application: *** Wake word detected: hi plaud ***
I (12347) Application: Opening audio channel...
```

---

## 📊 对比参考项目

### 参考项目关键代码

```c
// esp-sr-multinet/main/blink_example_main.c: 196-219

// 添加唤醒词
esp_mn_commands_clear();
esp_mn_commands_add(0, "hi PLeD");           // hi plaud
esp_mn_commands_add(1, "hi NgSgBcLD");       // hi nicebuild
esp_mn_error_t* err = esp_mn_commands_update();

// AFE 配置（禁用 WakeNet）
afe_config->wakenet_init = false;
afe_config->vad_init = true;

// 检测循环
while (true) {
    res = afe_handle->fetch(afe_data);
    esp_mn_state_t mn_state = multinet->detect(model_data, res->data);
    
    if (mn_state == ESP_MN_STATE_DETECTED) {
        // 检测到命令
        esp_mn_results_t* mn_result = multinet->get_results(model_data);
        int command_id = mn_result->phrase_id[0];
        // 处理命令...
    }
}
```

### 小智项目改造后

```cpp
// main/audio/wake_words/custom_wake_word.cc

// 添加唤醒词（Initialize）
esp_mn_commands_clear();
commands_.push_back({"hi PLeD", "hi plaud", "wake"});
commands_.push_back({"hi NgSgBcLD", "hi nicebuild", "wake"});
for (int i = 0; i < commands_.size(); i++) {
    esp_mn_commands_add(i, commands_[i].command.c_str());
}
esp_mn_commands_update();

// 检测循环（Feed）
void CustomWakeWord::Feed(const std::vector<int16_t>& data) {
    esp_mn_state_t mn_state = multinet_->detect(multinet_model_data_, data.data());
    
    if (mn_state == ESP_MN_STATE_DETECTED) {
        esp_mn_results_t* mn_result = multinet_->get_results(multinet_model_data_);
        int command_id = mn_result->phrase_id[0];
        
        if (commands_[command_id].action == "wake") {
            last_detected_wake_word_ = commands_[command_id].text;
            wake_word_detected_callback_(last_detected_wake_word_);
        }
    }
}
```

---

## ❓ 常见问题

### Q1: 如何添加更多唤醒词？

**A:** 在 `Initialize()` 中添加更多命令：

```cpp
commands_.push_back({"hi PLeD", "hi plaud", "wake"});
commands_.push_back({"hi NgSgBcLD", "hi nicebuild", "wake"});
commands_.push_back({"hi TpG", "hi espressif", "wake"});  // 新增
```

### Q2: 中文唤醒词如何添加？

**A:** 需要使用中文 MultiNet 模型（mn*_cn）并使用拼音格式：

```cpp
language_ = "cn";
commands_.push_back({"ni hao xiao zhi", "你好小智", "wake"});
```

**配置:**
```ini
CONFIG_SR_MN_CN=y
# CONFIG_SR_MN5Q8_EN is not set
```

### Q3: 检测灵敏度如何调整？

**A:** 修改阈值（threshold），范围 0.0-1.0：

```cpp
threshold_ = 0.3;  // 更敏感（更容易触发）
threshold_ = 0.7;  // 更严格（不容易误触发）
```

### Q4: 为什么要禁用 WakeNet？

**A:** MultiNet 本身就可以作为唤醒模型：
- ✅ **更灵活**: 可以添加任意命令作为唤醒词
- ✅ **更简单**: 不需要两级检测（WakeNet → MultiNet）
- ✅ **低延迟**: 直接检测，无需等待 WakeNet 激活
- ❌ **功耗稍高**: MultiNet 持续运行

### Q5: 模型文件在哪里？

**A:** MultiNet 模型路径：
```
managed_components/espressif__esp-sr/model/multinet5_quantized/
├── mn5q8_en/  (英文模型)
└── mn*_cn/    (中文模型)
```

---

## 🔧 调试技巧

### 1. 查看加载的模型

```cpp
// 在 Initialize() 中添加
ESP_LOGI(TAG, "Available models:");
for (int i = 0; i < models_->num; i++) {
    ESP_LOGI(TAG, "  [%d] %s", i, models_->model_name[i]);
}
```

### 2. 监控检测状态

```cpp
// 在 Feed() 中添加（调试模式）
static int frame_count = 0;
if (++frame_count % 100 == 0) {
    ESP_LOGI(TAG, "Processed %d frames, state: %d", frame_count, mn_state);
}
```

### 3. 查看命令列表

```cpp
// 在 Initialize() 之后调用
esp_mn_commands_print();
```

---

## 📚 参考资料

- ESP-SR 官方文档: https://github.com/espressif/esp-sr
- MultiNet 模型说明: https://docs.espressif.com/projects/esp-sr/
- 参考项目: `/Users/xionghao/Documents/plaud/GitHub/esp-sr-multinet`
- 小智项目文档: `WAKE_WORD_MODEL_ANALYSIS.md`

---

**文档版本:** v1.0  
**创建日期:** 2025-10-10  
**作者:** AI Assistant  
**适用固件:** xiaozhi-esp32 v2.0.3+

