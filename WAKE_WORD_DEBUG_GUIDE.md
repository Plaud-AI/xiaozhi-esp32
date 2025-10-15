# 唤醒词调试指南

## 问题描述
ESP32-S3-Korvo-2 V3.0 设备已成功连接 WiFi，但说出"你好，小智"无法唤醒交互。

## 已添加的调试日志

### 1. 模型加载日志
**位置：** `main/audio/audio_service.cc` - `SetModelsList()` 函数
**输出内容：**
- 模型列表指针地址
- 模型数量
- 每个模型的名称
- 创建的唤醒词类型（AfeWakeWord/CustomWakeWord/EspWakeWord）

### 2. 唤醒词初始化日志
**位置：** `main/audio/audio_service.cc` - `EnableWakeWordDetection()` 函数
**输出内容：**
- 唤醒词对象是否为空
- 初始化参数（采样率、模型列表指针）
- 初始化成功/失败状态
- Feed size（音频块大小）

### 3. AFE 唤醒词详细日志
**位置：** `main/audio/wake_words/afe_wake_word.cc`
**输出内容：**
- 初始化过程详细信息
- 模型列表中的所有模型
- 找到的唤醒词名称
- 音频输入格式（麦克风+参考通道）
- AFE 配置参数
- 检测任务运行状态（每 100 次循环打印一次）
- 唤醒词检测成功事件

### 4. 应用状态切换日志
**位置：** `main/application.cc`
**输出内容：**
- 进入 IDLE 状态时的日志
- OnWakeWordDetected 回调触发日志
- 设备状态和协议状态

### 5. Assets 加载日志
**位置：** `main/assets.cc` 和 `main/application.cc`
**输出内容：**
- Assets 分区是否有效
- SR 模型文件加载状态
- 内置模型加载（如果 assets 没有模型）

## 编译和烧录

```bash
# 清理构建缓存（推荐）
idf.py fullclean

# 重新编译
idf.py build

# 烧录到设备
idf.py flash

# 查看实时日志
idf.py monitor
```

## 日志分析要点

### 正常启动流程应该看到：

1. **模型加载阶段**
```
I (xxx) AudioService: SetModelsList called, models_list: 0x...
I (xxx) AudioService: Models list count: X
I (xxx) AudioService: Model 0: wn9_xxx
I (xxx) AudioService: Creating AfeWakeWord (WN prefix found)
I (xxx) AudioService: Wake word object created successfully
```

2. **唤醒词初始化阶段**
```
I (xxx) Application: Entering IDLE state, enabling wake word detection...
I (xxx) AudioService: Enabling wake word detection
I (xxx) AudioService: Initializing wake word with codec input_sample_rate=24000, models_list=0x...
I (xxx) AfeWakeWord: AfeWakeWord::Initialize called, codec=0x..., models_list=0x...
I (xxx) AfeWakeWord: Found X models in list
I (xxx) AfeWakeWord: Model 0: wn9_xxx
I (xxx) AfeWakeWord: -> Wake word model found! Words: 你好小智;...
I (xxx) AfeWakeWord: Added wake word: 你好小智
I (xxx) AfeWakeWord: Input format: MR, channels=2, ref_num=1
I (xxx) AfeWakeWord: AFE interface created successfully, feed_size=480
I (xxx) AfeWakeWord: AfeWakeWord initialization completed successfully!
I (xxx) AudioService: Wake word initialized successfully, feed_size=480
I (xxx) AudioService: Wake word detection started, event bit set
```

3. **检测运行阶段**（每 100 次循环打印）
```
I (xxx) AfeWakeWord: Wake word detection running... (loop 100, wakeup_state=0)
I (xxx) AfeWakeWord: Wake word detection running... (loop 200, wakeup_state=0)
```

4. **唤醒词检测成功**
```
I (xxx) AfeWakeWord: *** WAKE WORD DETECTED! *** model_index=1
I (xxx) AfeWakeWord: Wake word name: 你好小智
I (xxx) Application: OnWakeWordDetected() called, device_state=3, protocol=0x...
I (xxx) Application: Device in IDLE state, processing wake word...
I (xxx) Application: *** Wake word detected: 你好小智 ***
```

## 常见问题排查

### 问题 1：没有看到 "SetModelsList called"
**原因：** 模型没有被加载
**检查点：**
- Assets 分区是否有效
- 是否调用了 `CheckAssetsVersion()`
- 内置模型是否存在

### 问题 2：Wake word object is NULL
**原因：** 模型列表中没有找到唤醒词模型
**检查点：**
- 模型名称是否包含 "wn" 前缀
- `esp_srmodel_filter` 是否找到了模型

### 问题 3：Failed to initialize wake word
**原因：** AFE 初始化失败
**检查点：**
- PSRAM 是否可用
- 音频编解码器配置是否正确
- 输入通道数是否正确（Korvo-2 V3 应该是 2 个通道：麦克风+参考）

### 问题 4：看到初始化成功，但没有 "Wake word detection running"
**原因：** 音频输入任务没有运行或音频数据读取失败
**检查点：**
- 音频编解码器是否正常工作
- 麦克风是否有信号
- `ReadAudioData` 是否失败（会看到 "Failed to read audio data for wake word!"）

### 问题 5：看到 "Wake word detection running"，但说话不触发
**可能原因：**
- 唤醒词发音不标准
- 麦克风硬件问题
- 音频增益太低
- 背景噪音太大
- 唤醒词模型不匹配

## 下一步操作

1. **重新编译并烧录** 带有调试日志的固件
2. **连接串口监视器**，查看完整的启动日志
3. **在日志中查找**上述关键日志点
4. **尝试说出唤醒词**，观察是否有检测日志
5. **将完整日志**发送给技术支持或社区

## 进一步调试

如果以上日志仍无法定位问题，可以：

1. **测试麦克风**：使用音频测试模式（配置 WiFi 时按 BOOT 按钮）
2. **降低唤醒阈值**：修改 `DET_MODE_95` 为 `DET_MODE_90` (在 afe_wake_word.cc 第 38 行附近)
3. **增加调试输出**：在 `Feed()` 函数中打印音频数据统计
4. **检查硬件**：
   - 麦克风是否正常供电
   - I2S 引脚连接是否正确
   - ES7210/ES8311 芯片是否正常

## 联系支持

- **QQ 群：** 1011329060
- **GitHub Issues：** https://github.com/78/xiaozhi-esp32/issues
- **飞书文档：** https://ccnphfhqs21z.feishu.cn/wiki/F5krwD16viZoF0kKkvDcrZNYnhb

---

**生成日期：** 2025-10-10
**适用固件版本：** v2.0.3+
**开发板型号：** ESP32-S3-Korvo-2 V3.0

