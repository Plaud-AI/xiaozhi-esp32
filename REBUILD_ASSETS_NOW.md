# 🚀 重新构建和烧录 Assets 分区

## 问题
新动画文件在 `assets_source/anim/` 中，但没有被打包到设备！

## 原因  
增量构建跳过了 `generated_assets.bin` 的生成。

## 解决步骤（立即执行）

### 1. 设置 ESP-IDF 环境
```bash
cd /Users/xionghao/Documents/GitHub/xiaozhi-esp32
source ~/esp/esp-idf/export.sh
```

或者：
```bash
. $HOME/esp/esp-idf/export.sh
```

### 2. 重新构建 assets
```bash
# 旧的 assets.bin 已删除，现在重新构建
idf.py build
```

**关键日志**（确认 assets 被重新生成）：
```
Building default assets.bin based on configuration
🎭 Lottie animations will be included from: .../assets_source/anim/
...
Generated: build/generated_assets.bin
```

### 3. 烧录 assets 分区
```bash
# 方法 A：只烧录 assets 分区（快速，推荐）
esptool.py --chip esp32s3 -p /dev/ttyUSB0 -b 460800 \
  write_flash 0xa20000 build/generated_assets.bin

# 方法 B：烧录完整固件（慢但保险）
idf.py -p /dev/ttyUSB0 flash
```

**注意**：macOS 用户可能需要改端口：
```bash
# macOS 查找端口
ls /dev/cu.usbserial-*

# 使用找到的端口
esptool.py --chip esp32s3 -p /dev/cu.usbserial-XXXXX -b 460800 \
  write_flash 0xa20000 build/generated_assets.bin
```

### 4. 重启并验证
```bash
idf.py -p /dev/ttyUSB0 monitor
```

**成功标志**（在日志中查找）：
```
I (xxx) EmotionAssetsLoader: Loaded asset: idle (3335 bytes) ← 新文件！
I (xxx) EmotionAssetsLoader: Loaded asset: loading (3260 bytes) ← 新文件！
I (xxx) EmotionAssetsLoader: Loaded asset: speaking (2998 bytes) ← 新文件！  
I (xxx) EmotionAssetsLoader: Loaded asset: listening (5023 bytes) ← 新文件！
I (xxx) EmotionAssetsLoader: Loaded asset: settings (5263 bytes) ← 新文件！
I (xxx) EmotionAssetsLoader: Loaded asset: updating (3340 bytes) ← 新文件！
I (xxx) EmotionAssetsLoader: Loaded asset: success (3810 bytes) ← 新文件！
I (xxx) EmotionAssetsLoader: Loaded asset: error (4306 bytes) ← 新文件！
```

**对比旧日志**（问题日志）：
```
❌ I (1460) EmotionAssetsLoader: Loaded asset: listening (30452 bytes) ← 旧文件！
❌ W (1478) EmotionAssetsLoader: Asset not found: speaking ← 没有！
```

---

## 验证新动画

### 测试 1：Idle 动画
设备启动后进入 IDLE 状态，应该看到：
```
I (xxx) AnimMgr: Fallback: Loaded animation from Assets: idle.json (3335 bytes)
I (xxx) LottieAnimation: Lottie parsed successfully. Total frames: 60.0 ← 新动画
```

### 测试 2：Speaking 动画
语音唤醒并回复时：
```
I (xxx) AnimMgr: Fallback: Loaded animation from Assets: speaking.json (2998 bytes)
I (xxx) LottieAnimation: Lottie parsed successfully. Total frames: XX ← 新动画
```

### 测试 3：Listening 动画
唤醒后倾听时：
```
I (xxx) AnimMgr: Fallback: Loaded animation from Assets: listening.json (5023 bytes)
I (xxx) LottieAnimation: Lottie parsed successfully. Total frames: XX ← 新动画
```

---

## 常见问题

### Q: 构建时看不到 "Building default assets.bin"？
**A**: 可能还有缓存，尝试：
```bash
rm -rf build/generated_assets.bin
idf.py build
```

### Q: 日志还是显示旧文件大小？
**A**: 确认是否重新烧录了 assets：
```bash
# 查看 build 目录中的文件时间
ls -lh build/generated_assets.bin
# 应该是最新的时间戳

# 强制重新烧录
esptool.py --chip esp32s3 -p /dev/ttyUSB0 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0xa20000 build/generated_assets.bin
```

### Q: 如何验证 assets.bin 内容？
**A**: 可以用脚本检查（如果有的话）：
```bash
python scripts/list_assets.py build/generated_assets.bin | grep -E "anim/"
```

---

## 新动画文件列表

| 文件 | 大小 | 用途 |
|------|------|------|
| `idle.json` | 3.3K | 待机呼吸 |
| `loading.json` | 3.2K | 加载动画 |
| `listening.json` | 5.0K | 倾听声波（新） |
| `speaking.json` | 2.9K | 说话张合（新） |
| `settings.json` | 5.1K | 设置/配置 |
| `updating.json` | 3.3K | 升级下载 |
| `success.json` | 3.7K | 成功提示 |
| `error.json` | 4.2K | 错误警告 |

**对比旧文件**：
- 旧 `listening`: 30.4K → 新 `listening`: 5.0K ✅ 简化版
- 旧 `speaking`: 不存在 → 新 `speaking`: 2.9K ✅ 新增

---

## 为什么需要重新构建？

### ESP-IDF 构建系统的行为
```
只修改 assets_source/ 中的文件
         ↓
增量构建检查依赖
         ↓
发现 .cc/.h 文件没变
         ↓
❌ 跳过 generated_assets.bin 生成
```

### 解决方法
```
删除 build/generated_assets.bin
         ↓
idf.py build
         ↓
CMake 检测到文件缺失
         ↓
✅ 强制重新生成 assets.bin
```

---

## 立即执行命令（复制粘贴）

```bash
# 1. 进入项目目录
cd /Users/xionghao/Documents/GitHub/xiaozhi-esp32

# 2. 设置环境
source ~/esp/esp-idf/export.sh

# 3. 重新构建（assets.bin 已删除）
idf.py build

# 4. 烧录 assets 分区（快速）
esptool.py --chip esp32s3 -p /dev/ttyUSB0 -b 460800 \
  write_flash 0xa20000 build/generated_assets.bin

# 5. 查看日志
idf.py -p /dev/ttyUSB0 monitor
```

**预计时间**：
- 构建：30-60 秒
- 烧录 assets：5-10 秒
- 总计：< 2 分钟

---

## 成功后你会看到

### 启动日志
```
✅ I (xxx) EmotionAssetsLoader: Loaded asset: idle (3335 bytes)
✅ I (xxx) EmotionAssetsLoader: Loaded asset: speaking (2998 bytes)
✅ I (xxx) EmotionAssetsLoader: Loaded asset: listening (5023 bytes)
✅ I (xxx) EmotionAssetsLoader: Loaded asset: loading (3260 bytes)
✅ I (xxx) EmotionAssetsLoader: Loaded asset: settings (5263 bytes)
✅ I (xxx) EmotionAssetsLoader: Loaded asset: updating (3340 bytes)
✅ I (xxx) EmotionAssetsLoader: Loaded asset: success (3810 bytes)
✅ I (xxx) EmotionAssetsLoader: Loaded asset: error (4306 bytes)
```

### 动画切换
- 设备启动：显示新的 idle 动画（简洁的圆形角色呼吸）
- 语音唤醒：显示新的 listening 动画（声波扩散效果）
- 语音回复：显示新的 speaking 动画（嘴巴张合）

---

## 完成！

执行完上述步骤后，新动画就会生效了！🎉

