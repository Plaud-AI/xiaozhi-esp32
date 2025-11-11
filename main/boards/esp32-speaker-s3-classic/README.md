# ESP32 Speaker S3 Classic

基于 ESP32-S3 的蓝牙音箱开发板 HAL 实现

## 📋 硬件配置

### 🎵 音频系统
- **音频架构**：双独立 I2S 总线
  - **ES8311** (DAC/扬声器) - 使用 I2S0
  - **ES7210** (ADC/麦克风) - 使用 I2S1
- **功放**：NS4150B（硬件自动使能，无需 GPIO 控制）
- **采样率**：16000 Hz（输入/输出）
- **I2C 控制**：GPIO4 (SCL), GPIO5 (SDA)

### 📺 显示屏
- **驱动芯片**：ST7789P3
- **分辨率**：200x320
- **接口**：SPI（GPIO14 CLK, GPIO16 MOSI, GPIO18 CS）
- **背光**：GPIO17

### 🎮 按键
- **Boot 按键**：GPIO0
- **功能按键**：GPIO45
- **音量加**：GPIO48
- **音量减**：GPIO47

### 💡 其他外设
- **RGB LED**：GPIO21（可能是 WS2812）
- **电池检测**：GPIO8 (ADC)
- **PWM 输出**：GPIO1, GPIO2

---

## 🏗️ 架构特点

### 1️⃣ 双 I2S 独立总线
与传统的 BoxAudioCodec（TDM 模式共享 I2S）不同，本开发板使用完全独立的双 I2S 总线：

```
┌─────────────────┐                ┌─────────────────┐
│   ES8311 (DAC)  │                │  ES7210 (ADC)   │
│   扬声器输出     │                │   麦克风输入     │
└────────┬────────┘                └────────┬────────┘
         │ I2S0                             │ I2S1
         │ (GPIO 3/40/46/38/39)             │ (GPIO 13/10/9/11)
         │                                  │
    ┌────┴──────────────────────────────────┴────┐
    │          ESP32-S3-WROOM-1-N16R8            │
    │    (16MB Flash + 8MB PSRAM)                │
    └────────────────────────────────────────────┘
```

**优势**：
- ✅ 完全独立的时钟和数据通路
- ✅ 无需 TDM 时序协调
- ✅ 更灵活的采样率配置

**劣势**：
- ⚠️ 占用两个 I2S 外设
- ⚠️ GPIO 引脚使用较多

### 2️⃣ DualI2sAudioCodec 实现
为了支持双独立 I2S 总线，我们实现了新的 `DualI2sAudioCodec` 类：

```cpp
class DualI2sAudioCodec : public AudioCodec {
private:
    // ES8311 (I2S0) 相关
    i2s_chan_handle_t tx_handle_i2s0_;
    esp_codec_dev_handle_t output_dev_;
    
    // ES7210 (I2S1) 相关
    i2s_chan_handle_t rx_handle_i2s1_;
    esp_codec_dev_handle_t input_dev_;
    
    void CreateEs8311Channel(...);  // 创建 I2S0 TX
    void CreateEs7210Channel(...);  // 创建 I2S1 RX
};
```

---

## 🛠️ 编译和烧录

### 1️⃣ 进入项目目录
```bash
cd /Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32
```

### 2️⃣ 设置目标芯片
```bash
idf.py set-target esp32s3
```

### 3️⃣ 选择开发板配置
```bash
idf.py menuconfig
```
在菜单中选择：
```
Board Configuration
  └─ Board Type: esp32-speaker-s3-classic
```

### 4️⃣ 编译
```bash
idf.py build -Desp32-speaker-s3-classic
```

### 5️⃣ 烧录
```bash
idf.py flash monitor
```

---

## 📝 文件说明

### 配置文件
- **config.h** - 硬件引脚和参数配置
- **config.json** - 编译目标和 sdkconfig 配置

### 代码文件
- **dual_i2s_audio_codec.h** - 双 I2S 音频编解码器头文件
- **dual_i2s_audio_codec.cc** - 双 I2S 音频编解码器实现
- **esp32_speaker_s3_classic.cc** - 开发板主实现文件

---

## 🐛 调试技巧

### I2C 设备扫描
开发板启动时会自动扫描 I2C 总线上的设备，正常情况下应该检测到：
- **0x18** - ES8311 (DAC)
- **0x40** - ES7210 (ADC)

如果扫描不到设备，检查：
1. I2C 上拉电阻是否正常
2. 电源供电是否正常
3. 引脚连接是否正确

### 音频调试
如果没有声音输出：
1. 检查 NS4150B 功放电源是否正常
2. 检查 ES8311 的 I2S 时钟信号（用示波器）
3. 查看日志中的 `DualI2sAudioCodec` 初始化信息
4. 确认音量设置不为 0

### 显示屏调试
如果显示屏无显示：
1. 检查 SPI 时钟频率（降低到 10MHz 测试）
2. 检查 CS/DC/RST 引脚电平
3. 确认背光 GPIO17 是否拉高
4. 用示波器检查 SPI 时序

---

## 📊 与其他板子的对比

| 特性 | esp32-speaker-s3-classic | esp32s3-korvo2-v3 |
|------|--------------------------|-------------------|
| **I2S 架构** | 双独立 I2S（I2S0 + I2S1） | 单共享 I2S（TDM 模式） |
| **ES8311 + ES7210** | ✅ 独立时钟 | ✅ 共享时钟 |
| **采样率** | 16000 Hz | 24000 Hz |
| **显示屏** | ST7789P3 (200x320) | ST7789/ILI9341 (240x280/320x240) |
| **摄像头** | ❌ | ✅ OV2640 |
| **IO 扩展器** | ❌ | ✅ TCA9554 |
| **ADC 按键** | ❌ (GPIO 按键) | ✅ 6 个 ADC 按键 |

---

## ✅ 功能特性

- [x] 双 I2S 音频编解码
- [x] ST7789P3 LCD 显示
- [x] GPIO 按键（Boot + 功能 + 音量调节）
- [x] WiFi 连接
- [x] OTA 固件升级
- [x] 电池电量检测（ADC）
- [ ] RGB LED 控制（待实现）
- [ ] PWM 输出（待实现）
- [ ] NFC 模块（待实现）

---

## 📚 参考资料

- [ESP32-S3 技术参考手册](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_cn.pdf)
- [ES8311 数据手册](https://www.everest-semi.com/pdf/ES8311%20PB.pdf)
- [ES7210 数据手册](https://www.everest-semi.com/pdf/ES7210%20PB.pdf)
- [ST7789 数据手册](https://www.displayfuture.com/Display/datasheet/controller/ST7789.pdf)

---

## 📞 技术支持

如有问题，请提供：
1. 完整的启动日志
2. I2C 扫描结果
3. 硬件连接照片（如有）
4. 使用的 ESP-IDF 版本

**Created by**: PLAUD AI Team  
**Date**: 2025-11-11  
**Version**: 1.0.0

