# 🎯 方案 A：复用 Assets 分区存储 Lottie 动画

## ✅ 已完成的配置

1. ✅ 创建了 `assets_source/anim/` 目录，包含 11 个动画文件
2. ✅ 添加了 `mount_assets_spiffs.h` 挂载工具
3. ✅ 修改了板子初始化代码，自动挂载 assets 为 SPIFFS
4. ✅ 情感系统路径已更新为 `/assets/anim/`
5. ✅ 创建了自动打包烧录脚本 `flash_assets_animations.sh`

## 🚀 现在只需 3 步！

### 步骤 1：编译固件

```bash
cd /Users/xionghao/Documents/GitHub/xiaozhi-esp32

# 清理编译
idf.py fullclean

# 编译
idf.py build
```

**注意：** 这次编译会包含新的文件系统挂载代码。

### 步骤 2：烧录固件

```bash
# 烧录固件
idf.py flash
```

### 步骤 3：打包并烧录动画到 assets 分区

```bash
# 运行自动脚本
./flash_assets_animations.sh

# 如果串口不是 /dev/ttyUSB0，指定串口：
# ./flash_assets_animations.sh /dev/ttyACM0
```

**脚本会自动完成：**
- ✅ 检查动画文件
- ✅ 读取分区表信息
- ✅ 生成 SPIFFS 镜像（5.6MB）
- ✅ 烧录到 assets 分区（地址 0xa20000）

### 步骤 4：查看效果

```bash
idf.py monitor
```

## 📋 预期日志输出

如果一切正常，你应该看到：

```
I (1234) esp32s3_korvo2_v3: Mounting assets as SPIFFS for animations...
I (1235) AssetsSPIFFS: Mounting assets partition as SPIFFS...
I (1236) AssetsSPIFFS: Found assets partition: size=5760 KB, address=0xa20000
I (1237) AssetsSPIFFS: SPIFFS: Total=5760 KB, Used=200 KB, Free=5560 KB
I (1238) AssetsSPIFFS: ✅ Test file access successful: /assets/anim/happy.json
I (1239) AssetsSPIFFS: Assets SPIFFS mounted at: /assets
I (1240) esp32s3_korvo2_v3: Initializing emotion system...
I (1241) EmotionInit: Initializing emotion system (integrated mode)
I (1242) EmotionCoord: Initializing Emotion System...
I (1243) EmotionCoord:   Animation path: /assets/anim/
I (1244) EmotionAnimMapper: Registered: happy -> /assets/anim/happy.json
I (1245) EmotionAnimMapper: Registered: sad -> /assets/anim/sad.json
...
I (1250) EmotionCoord: Emotion System initialized successfully
I (1251) esp32s3_korvo2_v3: ✅ Emotion system ready!
```

## 🎨 文件系统布局

```
/assets/                          ← 挂载点
└── anim/                         ← 动画目录
    ├── happy.json               (16 KB)
    ├── sad.json                 (13 KB)
    ├── excited.json             (17 KB)
    ├── calm.json                (11 KB)
    ├── sleepy.json              (10 KB)
    ├── surprised.json           (13 KB)
    ├── listening.json           (30 KB)
    ├── singing.json             (25 KB)
    ├── champion.json            (18 KB)
    ├── disdain.json             (8 KB)
    └── disgust.json             (21 KB)

总计：约 200 KB
```

## 🎯 使用方法

### 在代码中控制表情

```cpp
#include "display/xiaozhi_emotion_integration.h"

// 方法 1：通过全局宏
XIAOZHI_EMOTION.ShowHappy();
XIAOZHI_EMOTION.OnWakeup();
XIAOZHI_EMOTION.OnStartListening();

// 方法 2：通过单例
auto& emotion = xiaozhi::EmotionSystemIntegration::Instance();
emotion.ShowExcited();
emotion.OnProcessing();
emotion.PlayCustomAnimation("champion.json");
```

### 添加新动画

1. 将新的 `.json` 文件放到 `assets_source/anim/`
2. 运行 `./flash_assets_animations.sh` 重新烧录
3. 无需重新编译固件！

## 🔧 维护和更新

### 查看文件系统使用情况

在代码中添加：

```cpp
size_t total = 0, used = 0;
esp_spiffs_info("assets", &total, &used);
ESP_LOGI("TEST", "Assets SPIFFS: Total=%lu KB, Used=%lu KB, Free=%lu KB",
         total / 1024, used / 1024, (total - used) / 1024);
```

### 只更新动画（不重新编译固件）

```bash
# 1. 修改 assets_source/anim/ 中的文件
# 2. 运行烧录脚本
./flash_assets_animations.sh

# 3. 重启设备
# 无需重新编译固件！
```

### 添加其他资源文件

你可以在 `assets_source/` 中添加其他目录：

```
assets_source/
├── anim/          # 动画
├── images/        # 图片
├── sounds/        # 音效
└── config/        # 配置文件
```

然后运行 `./flash_assets_animations.sh` 即可。

## 🐛 故障排查

### 问题 1：挂载失败 - Failed to mount SPIFFS

**原因：** assets 分区还没有格式化为 SPIFFS

**解决方法：**

选项 A - 使用脚本烧录（推荐）：
```bash
./flash_assets_animations.sh
```

选项 B - 首次使用时自动格式化：
修改 `mount_assets_spiffs.h`：
```cpp
.format_if_mount_failed = true  // 改为 true
```

重新编译烧录固件。

### 问题 2：Test file access failed

**原因：** SPIFFS 是空的，没有动画文件

**解决方法：**
```bash
./flash_assets_animations.sh
```

### 问题 3：动画不显示

**检查步骤：**

1. 验证文件是否存在：
```cpp
FILE* f = fopen("/assets/anim/happy.json", "r");
if (f) {
    ESP_LOGI("TEST", "File exists");
    fclose(f);
} else {
    ESP_LOGE("TEST", "File not found!");
}
```

2. 打印系统信息：
```cpp
XIAOZHI_EMOTION.PrintInfo();
```

3. 检查 ThorVG 配置：
```bash
grep "CONFIG_LV_USE_THORVG" sdkconfig
# 应该输出: CONFIG_LV_USE_THORVG=y
```

### 问题 4：烧录脚本失败

**串口被占用：**
```bash
# 先停止 monitor
# Ctrl+]

# 然后运行脚本
./flash_assets_animations.sh
```

**串口路径错误：**
```bash
# 查看可用串口
ls -l /dev/tty* | grep -E "USB|ACM"

# 指定串口
./flash_assets_animations.sh /dev/ttyACM0
```

## 📊 与原方案对比

| 特性 | 方案 A (Assets SPIFFS) | 原方案 (独立SPIFFS) |
|------|----------------------|-------------------|
| 修改分区表 | ❌ 不需要 | ✅ 需要 |
| 烧录步骤 | 2 次（固件 + assets） | 2 次（固件 + spiffs） |
| 可用空间 | 5.6 MB | 需自定义 |
| OTA 影响 | ❌ 不影响 | ❌ 不影响 |
| 更新动画 | 独立烧录，无需编译 | 独立烧录，无需编译 |
| 风险 | ⚠️ 低（不改分区） | ⚠️ 中（改分区） |

## 🎉 优势总结

✅ **安全** - 不修改分区表  
✅ **灵活** - 5.6MB 空间，够用很久  
✅ **方便** - 一键脚本自动化  
✅ **可维护** - 动画独立更新  
✅ **兼容** - 不影响现有 OTA 功能  

---

**现在就试试吧！** 🚀

如果遇到任何问题，查看上面的故障排查部分，或者直接问我！


