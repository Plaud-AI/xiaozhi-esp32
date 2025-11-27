# Lottie 表情动画资源

## 动画列表

| 文件名 | 中文名 | 情感类型 | 文件大小 | 用途 |
|--------|--------|---------|---------|------|
| `happy.json` | 开心 | HAPPY | 16KB | 表达开心、愉快的情绪 |
| `sad.json` | 悲伤 | SAD | 13KB | 表达悲伤、难过的情绪 |
| `surprised.json` | 好奇 | SURPRISED | 13KB | 表达惊讶、好奇的情绪 |
| `sleepy.json` | 困倦 | SLEEPY | 10KB | 表达困倦、疲惫的情绪 |
| `calm.json` | 满足 | CALM | 11KB | 表达平静、满足的情绪 |
| `excited.json` | 小骄傲 | EXCITED | 17KB | 表达兴奋、骄傲的情绪 |
| `listening.json` | 听音乐 | - | 30KB | 聆听、专注状态 |
| `singing.json` | 唱歌 | - | 25KB | 表演、唱歌状态 |
| `disdain.json` | 不屑 | - | 8KB | 表达不屑、轻视的情绪 |
| `disgust.json` | 吐 | - | 21KB | 表达厌恶、吐槽的情绪 |
| `champion.json` | NBA冠军戒指 | - | 18KB | 表达成功、庆祝 |

## 使用说明

### 1. 注册动画（推荐使用自动注册）

```cpp
#include "emotion_display.h"

// 方法 1: 自动从目录注册
emotion_display->InitEmotionSystem("/spiffs/anim/");

// 方法 2: 手动注册
auto& mgr = lottie::EmotionAnimationManager::Instance();
mgr.RegisterEmotion(lottie::EmotionType::HAPPY, "/spiffs/anim/happy.json");
mgr.RegisterEmotion(lottie::EmotionType::SAD, "/spiffs/anim/sad.json");
// ... 注册其他情感
```

### 2. 显示情感

```cpp
// 通过情感类型
emotion_display->ShowEmotion(lottie::EmotionType::HAPPY);

// 通过字符串
emotion_display->ShowEmotion("happy");
```

### 3. 播放序列

```cpp
std::vector<lottie::EmotionSequenceItem> sequence = {
    {lottie::EmotionType::HAPPY, 2000},    // 开心 2 秒
    {lottie::EmotionType::EXCITED, 2000},  // 兴奋 2 秒
    {lottie::EmotionType::CALM, 2000}      // 平静 2 秒
};
emotion_display->PlayEmotionSequence(sequence, false);
```

## 文件命名规范

建议使用以下命名规范，与 `EmotionType` 枚举对应：

- `happy.json` - 开心 (HAPPY)
- `sad.json` - 悲伤 (SAD)
- `surprised.json` - 惊讶 (SURPRISED)
- `angry.json` - 生气 (ANGRY)
- `confused.json` - 疑惑 (CONFUSED)
- `sleepy.json` - 困倦 (SLEEPY)
- `neutral.json` - 中性 (NEUTRAL)
- `excited.json` - 兴奋 (EXCITED)
- `calm.json` - 平静 (CALM)
- `thinking.json` - 思考 (THINKING)
- `love.json` - 喜爱 (LOVE)

## 动画规格建议

- **分辨率**: 建议 ≤ 512×512（会自动缩放到屏幕大小）
- **帧率**: 20-30 FPS
- **时长**: 1-3 秒
- **文件大小**: 尽量 ≤ 30KB
- **图层数**: ≤ 20 层
- **格式**: Lottie JSON (导出自 After Effects + Bodymovin)

## 烧录到 ESP32

```bash
# 1. 确保 partitions.csv 中包含 spiffs 分区
# spiffs, data, spiffs, , 1M

# 2. 生成 SPIFFS 镜像
python3 $IDF_PATH/components/spiffs/spiffsgen.py \
    1048576 spiffs_image spiffs.bin

# 3. 烧录 SPIFFS（地址根据分区表调整）
python3 $IDF_PATH/components/esptool_py/esptool/esptool.py \
    --chip esp32s3 --port /dev/ttyUSB0 \
    write_flash 0x310000 spiffs.bin
```

## 添加新动画

1. 将 Lottie JSON 文件复制到此目录
2. 使用标准命名（对应 EmotionType）
3. 重新生成并烧录 SPIFFS 镜像
4. 或者使用 OTA 动态更新

## 故障排查

### 动画无法加载
- 检查文件路径是否正确
- 确认 SPIFFS 已正确烧录
- 查看日志输出 `idf.py monitor`

### 动画播放卡顿
- 减少动画复杂度（图层、路径）
- 降低帧率到 20 FPS
- 确保 PSRAM 已启用

### 文件过大
- 使用在线工具压缩 Lottie JSON
- 移除不必要的图层和效果
- 考虑降低分辨率

