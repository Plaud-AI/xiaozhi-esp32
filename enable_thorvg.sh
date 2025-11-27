#!/bin/bash

# 自动启用 ThorVG 配置脚本

echo "=== 配置 ThorVG for Lottie 动画 ==="

# 检查 sdkconfig 文件
if [ ! -f "sdkconfig" ]; then
    echo "❌ 错误：sdkconfig 文件不存在"
    echo "请先运行: idf.py menuconfig"
    exit 1
fi

echo "📝 检查当前配置..."

# 检查 LVGL 版本
if grep -q "CONFIG_LVGL_VERSION_MAJOR=9" sdkconfig; then
    echo "✅ LVGL 版本: 9.x (正确)"
else
    echo "⚠️  警告：LVGL 版本可能不是 9.x"
fi

# 检查 ThorVG 配置
if grep -q "CONFIG_LV_USE_THORVG=y" sdkconfig; then
    echo "✅ ThorVG 已启用"
else
    echo "⚙️  正在启用 ThorVG..."
    
    # 备份原配置
    cp sdkconfig sdkconfig.backup
    
    # 添加 ThorVG 配置
    if ! grep -q "CONFIG_LV_USE_THORVG" sdkconfig; then
        echo "" >> sdkconfig
        echo "# ThorVG Configuration (for Lottie animations)" >> sdkconfig
        echo "CONFIG_LV_USE_THORVG=y" >> sdkconfig
        echo "CONFIG_LV_USE_THORVG_INTERNAL=y" >> sdkconfig
        echo "CONFIG_LV_THORVG_ENABLE_LOTTIE=y" >> sdkconfig
        echo "CONFIG_LV_THORVG_ENABLE_SVG=y" >> sdkconfig
    else
        # 更新现有配置
        sed -i.bak 's/# CONFIG_LV_USE_THORVG is not set/CONFIG_LV_USE_THORVG=y/' sdkconfig
        sed -i.bak 's/# CONFIG_LV_USE_THORVG_INTERNAL is not set/CONFIG_LV_USE_THORVG_INTERNAL=y/' sdkconfig
    fi
    
    echo "✅ ThorVG 配置已添加"
fi

# 检查 PSRAM
if grep -q "CONFIG_SPIRAM=y" sdkconfig; then
    echo "✅ PSRAM 已启用"
else
    echo "⚠️  警告：PSRAM 未启用，建议启用以获得更好性能"
fi

echo ""
echo "=== 配置完成 ==="
echo ""
echo "🚀 下一步："
echo "   1. 重新编译: idf.py build"
echo "   2. 烧录固件: idf.py flash"
echo "   3. 烧录动画: python3 \$IDF_PATH/components/spiffs/spiffsgen.py 1048576 spiffs_image spiffs.bin"
echo "   4. 查看日志: idf.py monitor"
echo ""

