# 唤醒词测试模式 - 快速启用示例

## 🚀 快速开始

### **步骤 1：在 `main/application.cc` 中启用测试模式**

找到 `Application::Start()` 函数（大约在第 150 行左右），在设备初始化完成后添加：

```cpp
void Application::Start() {
    // ... 其他初始化代码 ...
    
    // 等待初始化完成
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 🧪 启用唤醒词测试模式
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  🧪 Enabling Wake Word Test Mode                        ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    EnableWakeWordTestMode(true);
    
    // 测试模式已启动，等待用户说唤醒词
}
```

### **步骤 2：编译和烧录**

```bash
cd /Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32
idf.py build flash monitor
```

### **步骤 3：开始测试**

1. **听到低音** "嘟~" → 设备准备就绪
2. **说出唤醒词** "Hey Ploud"
3. **听到高音** "嘀嘀~" → 检测成功
4. **听到数字播报** "零五六七" → 概率 0.567
5. **查看日志** 查看检测概率
6. **等待 2 秒** → 自动循环到步骤 1

---

## 📊 **日志输出示例**

```
I (5000) Application: 
I (5000) Application: ╔══════════════════════════════════════════════════════════╗
I (5000) Application: ║  🧪 Wake Word Test Mode ENABLED                         ║
I (5000) Application: ║                                                          ║
I (5000) Application: ║  Test Flow:                                              ║
I (5000) Application: ║  1. Low beep (ready)                                     ║
I (5000) Application: ║  2. Say wake word                                        ║
I (5000) Application: ║  3. High beep (detected)                                 ║
I (5000) Application: ║  4. Speak probability digits (e.g. 0567 for 0.567)       ║
I (5000) Application: ║  5. Cooldown 2 seconds                                   ║
I (5000) Application: ║  6. Auto repeat                                          ║
I (5000) Application: ╚══════════════════════════════════════════════════════════╝
I (5100) Application: 🔔 Playing beep tone: 500 Hz, 200 ms
I (5400) Application: ✅ Beep tone played
I (5700) Application: 🎤 Enabling wake word detection...
I (5700) Application: ✅ Ready! Please say the wake word...

[你说: "Hey Ploud"]

I (8234) MicroWakeWord: 🎉 Model 'hey ploud' detected! (probability: 0.567)
I (8234) Application: 
I (8234) Application: ╔══════════════════════════════════════════════════════════╗
I (8234) Application: ║  🎉 Wake Word Detected in Test Mode!                    ║
I (8234) Application: ║  Wake Word: hey ploud                                   ║
I (8234) Application: ║  Probability: 0.567                                     ║
I (8234) Application: ╚══════════════════════════════════════════════════════════╝
I (8334) Application: 🔔 Playing beep tone: 1000 Hz, 100 ms
I (8484) Application: 🔔 Playing beep tone: 1200 Hz, 100 ms
I (8984) Application: 🔊 Speaking probability digits...
I (8984) Application: 📢 Reading digits: 0567 (from probability 0.567)
I (8984) Application:    Reading digit: 0
I (9584) Application:    Reading digit: 5
I (10184) Application:    Reading digit: 6
I (10784) Application:    Reading digit: 7
I (11384) Application: ✅ Finished reading probability digits
I (11384) Application: ⏳ Cooling down for 2 seconds before next test cycle...
I (13384) Application: 🔄 Starting next test cycle...
I (12684) Application: 🔔 Playing beep tone: 500 Hz, 200 ms
... (循环继续)
```

---

## 🎯 **测试数据记录表**

建议使用这个表格记录测试结果：

| # | 时间 | 唤醒词 | 概率 | 成功 | 语速 | 距离 | 备注 |
|---|------|-------|------|------|------|------|------|
| 1 | 10:23 | hey ploud | 0.567 | ✅ | 正常 | 30cm | 清晰 |
| 2 | 10:24 | hey ploud | 0.523 | ✅ | 快 | 30cm | |
| 3 | 10:24 | hey ploud | 0.489 | ❌ | 正常 | 50cm | 距离远 |
| 4 | 10:25 | hey ploud | 0.601 | ✅ | 慢 | 20cm | |
| ... | ... | ... | ... | ... | ... | ... | ... |

---

## ⚙️ **可选：调整参数**

### **修改阈值（如果检测率太低/太高）**

在 `main/audio/audio_service.cc` 第 786 行左右：

```cpp
float threshold_hey_ploud = 0.55;  // 改为 0.50 或 0.60
```

### **修改冷却时间**

在 `main/application.cc` 的 `OnWakeWordDetectedInTestMode()` 函数中：

```cpp
vTaskDelay(pdMS_TO_TICKS(2000));  // 2秒（当前值）→ 改为 3000（3秒）或 5000（5秒）
```

### **调整数字播报间隔**

在 `main/application.cc` 的 `SpeakProbability()` 函数中：

```cpp
vTaskDelay(pdMS_TO_TICKS(600));  // 每个数字间隔 600ms → 改为 400 或 800
```

### **修改提示音**

在 `main/application.cc` 的 `StartWakeWordTestCycle()` 函数中：

```cpp
PlayBeepTone(500, 200);   // 低音：频率 Hz, 时长 ms
PlayBeepTone(1000, 100);  // 高音1
PlayBeepTone(1200, 100);  // 高音2
```

---

## 🛑 **停止测试模式**

### **方法 1：修改代码**

注释掉 `EnableWakeWordTestMode(true);` 并重新编译烧录。

### **方法 2：添加串口命令（需要实现）**

```cpp
// 在串口命令处理中添加
Application::GetInstance().EnableWakeWordTestMode(false);
```

### **方法 3：重启设备**

按 RST 按钮或执行 `esp_restart()`。

---

## 🔗 **完整文档**

详细说明请查看：`docs/xiaozhi-esp32/audio/wake-word-test-mode.md`

---

## ✅ **检查清单**

开始测试前确认：

- [x] ✅ Hey Ploud 模型已集成
- [x] ✅ 麦克风工作正常
- [x] ✅ 扬声器/音频输出工作正常
- [x] ✅ 测试环境安静
- [x] ✅ 准备好数据记录工具
- [x] ✅ 代码已添加 `EnableWakeWordTestMode(true)`
- [x] ✅ 固件已编译烧录

**开始测试！🎤**

