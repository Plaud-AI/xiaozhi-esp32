# 🎨 Lottie 动画集成完整指南

**最后更新**: 2025-11-27  
**适用版本**: xiaozhi-esp32 v2.x

## ✅ 已完成的工作

1. ✅ 集成 ThorVG 组件（支持 Lottie 渲染）
2. ✅ 创建 Lottie 动画封装类（`lottie_animation.h/cc`）
3. ✅ 实现情感管理系统（解耦设计）：
   - `EmotionStateManager` - 情感状态管理
   - `EmotionAnimationMapper` - 情感到动画映射
   - `DeviceStateMapper` - 设备状态到情感映射
   - `EmotionCoordinator` - 协调器
4. ✅ 集成到 Assets 分区（Memory-Mapped 方式）
5. ✅ 准备了 11 个 Lottie 动画文件

---

## 🎯 方案说明

### 为什么使用 Memory-Mapped？

你的项目**已有**完整的 Assets 分区机制：

```cpp
// 原有代码（assets.cc）
esp_partition_mmap(partition_, ...);  // 将 Flash 映射到内存
  ↓
Assets::GetAssetData("anim/happy", ptr, size);  // 返回内存指针
  ↓
lv_thorvg_set_src_data(obj, ptr, size);  // ThorVG 从内存加载
```

**优点：**
- ✅ **零拷贝** - 直接从 Flash 读取，不占用 RAM
- ✅ **高性能** - 不需要文件系统开销
- ✅ **兼容性** - 沿用项目原有架构，不冲突
- ✅ **统一管理** - 和字体、图标等资源使用同一机制

**与 SPIFFS 方案的对比：**

| 特性 | Memory-Mapped（本方案）| SPIFFS |
|------|----------------------|--------|
| 与原代码兼容 | ✅ 完美兼容 | ❌ 冲突 |
| RAM 占用 | ⭐⭐⭐⭐⭐ 极低 | ⭐⭐⭐ 需要缓冲 |
| 性能 | ⭐⭐⭐⭐⭐ 零拷贝 | ⭐⭐⭐ 文件读取 |
| 更新灵活性 | ⭐⭐⭐ 需重新打包 | ⭐⭐⭐⭐ 独立更新 |

---

## 🚀 使用步骤（3 步完成）

### 步骤 1：准备动画文件

```bash
cd /Users/xionghao/Documents/GitHub/xiaozhi-esp32
./prepare_assets_animations.sh
```

**做了什么：**
- 从 `/Users/xionghao/Downloads/emojo/` 复制动画
- 转换为 Assets 格式（无 `.json` 后缀）
- 放到 `assets_source/anim/` 目录

### 步骤 2：构建和烧录（一键脚本）

```bash
./build_and_flash_with_animations.sh
```

或者手动指定串口：

```bash
./build_and_flash_with_animations.sh /dev/ttyACM0
```

**脚本会自动完成：**
1. ✅ 检查动画文件
2. ✅ 构建 assets.bin（包含动画）
3. ✅ 编译固件
4. ✅ 烧录固件 + assets

### 步骤 3：查看效果

```bash
idf.py monitor
```

**预期日志：**

```
I (1234) Assets: The partition size is 5760 KB
I (1235) Assets: Mapping partition...
I (1240) EmotionAssetsLoader: Loaded asset: anim/happy (16000 bytes)
I (1241) EmotionAssetsLoader: Loaded asset: anim/sad (13000 bytes)
...
I (1250) EmotionAssetsLoader: Registered 11 animations from assets partition
I (1251) EmotionCoord: Emotion System initialized successfully
I (1252) esp32s3_korvo2_v3: ✅ Emotion system ready!
```

---

## 💡 在代码中使用

### 控制表情

```cpp
#include "display/xiaozhi_emotion_integration.h"

// 方法 1：全局宏（最简单）
XIAOZHI_EMOTION.ShowHappy();
XIAOZHI_EMOTION.OnWakeup();
XIAOZHI_EMOTION.OnStartListening();

// 方法 2：单例
auto& emotion = xiaozhi::EmotionSystemIntegration::Instance();
emotion.ShowExcited();
emotion.OnProcessing();
```

### 自定义动画

```cpp
// 播放自定义动画
emotion.PlayCustomAnimation("champion");  // 无需 .json 后缀
```

### 响应设备状态

```cpp
// 在你的代码中调用（已自动映射到情感）
emotion.OnDeviceState(DeviceState::kDeviceStateListening);
emotion.OnDeviceState(DeviceState::kDeviceStateSpeaking);
emotion.OnDeviceState(DeviceState::kDeviceStateThinking);
```

---

## 📋 文件布局

### 源文件（编译前）

```
assets_source/
└── anim/
    ├── happy          (16 KB) - 无后缀！
    ├── sad            (13 KB)
    ├── excited        (17 KB)
    ├── calm           (11 KB)
    ├── sleepy         (10 KB)
    ├── surprised      (13 KB)
    ├── listening      (30 KB)
    ├── singing        (25 KB)
    ├── champion       (18 KB)
    ├── disdain        (8 KB)
    └── disgust        (21 KB)
```

### Assets 分区（烧录后）

```
Flash (assets partition @ 0xa20000)
├── fonts/...          (字体文件)
├── icons/...          (图标文件)
└── anim/              (动画文件)
    ├── happy
    ├── sad
    ...
```

### 代码访问方式

```cpp
// 在代码中
Assets::GetInstance().GetAssetData("anim/happy", ptr, size);
  ↓
返回 memory-mapped 指针
  ↓
lv_thorvg_set_src_data(obj, ptr, size);
```

---

## 🔧 维护和更新

### 添加新动画

1. 将新动画放到 `assets_source/anim/`（**无 .json 后缀**）
2. 运行构建脚本：`./build_and_flash_with_animations.sh`

### 只更新 Assets（不重新编译固件）

```bash
# 1. 重新构建 assets.bin
python3 scripts/build_default_assets.py \
    --sdkconfig sdkconfig \
    --output build/assets.bin \
    --extra_files assets_source

# 2. 只烧录 assets 分区
esptool.py --chip esp32s3 --port /dev/ttyUSB0 \
    write_flash 0xa20000 build/assets.bin
```

### 修改情感映射

编辑 `main/display/emotion_animation_mapper.cc` 和 `device_state_mapper.cc`：

```cpp
// 修改设备状态到情感的映射
void DeviceStateMapper::RegisterDefaultMappings() {
    RegisterMapping(DeviceState::kDeviceStateListening, EmotionState::LISTENING);
    RegisterMapping(DeviceState::kDeviceStateSpeaking, EmotionState::SPEAKING);
    // 添加你的映射...
}
```

---

## 🐛 故障排查

### 问题 1：动画不显示

**检查步骤：**

1. 查看 assets 是否成功初始化：
```
I (xxx) Assets: The partition size is 5760 KB
```

2. 查看动画是否加载：
```
I (xxx) EmotionAssetsLoader: Loaded asset: anim/happy
```

3. 检查 ThorVG 配置：
```bash
grep "CONFIG_LV_USE_THORVG" sdkconfig
# 应输出: CONFIG_LV_USE_THORVG=y
```

### 问题 2：Assets 分区为空

**原因：** 没有烧录 assets.bin

**解决：**
```bash
./build_and_flash_with_animations.sh
```

### 问题 3：编译错误 - 找不到 Assets

**原因：** assets.cc 和新代码冲突

**检查：**
```bash
grep "InitEmotionSystemFromAssets" main/boards/*/board.cc
```

应该调用 `InitEmotionSystemFromAssets()`，**不是** `InitEmotionSystem()`

### 问题 4：内存不足

**现象：** 设备重启或崩溃

**原因：** PSRAM 未启用或配置错误

**解决：**
```bash
idf.py menuconfig

# 进入：Component config → ESP PSRAM
# 启用: Support for external, SPI-connected RAM
# 模式选择: Quad Mode PSRAM
```

---

## 📚 相关文档

- [ThorVG 官方文档](https://www.thorvg.org/)
- [LVGL ThorVG 集成](https://docs.lvgl.io/master/libs/thorvg.html)
- [ESP32-S3 Memory-Mapped 说明](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/storage/spi_flash.html#memory-mapped-api)
- [项目 Assets 机制](managed_components/espressif__esp_mmap_assets/README.md)

---

## 🎉 总结

现在你可以：

✅ **一键构建和烧录** - 全自动脚本  
✅ **代码控制表情** - 简单 API  
✅ **零拷贝加载** - 高性能  
✅ **统一资源管理** - 和字体/图标一致  
✅ **解耦设计** - 灵活扩展  

需要帮助？查看上面的故障排查部分，或者直接问我！

