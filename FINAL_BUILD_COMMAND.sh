#!/bin/bash
# 
# 最终构建和烧录脚本
# 

set -e  # 遇到错误立即退出

echo "=================================="
echo "🚀 重新构建 Assets 分区"
echo "=================================="

# 1. 进入项目目录
cd /Users/xionghao/Documents/GitHub/xiaozhi-esp32

# 2. 设置 ESP-IDF 环境
echo "📦 设置 ESP-IDF 环境..."
if [ -f "$HOME/esp/esp-idf/export.sh" ]; then
    source "$HOME/esp/esp-idf/export.sh"
elif [ -f "$HOME/.espressif/esp-idf/export.sh" ]; then
    source "$HOME/.espressif/esp-idf/export.sh"
else
    echo "❌ 找不到 ESP-IDF，请手动设置环境"
    echo "   通常命令是: source ~/esp/esp-idf/export.sh"
    exit 1
fi

# 3. 确认旧 assets.bin 已删除
echo ""
echo "🗑️  确认旧 assets.bin 已删除..."
if [ -f "build/generated_assets.bin" ]; then
    echo "⚠️  发现旧的 assets.bin，删除中..."
    rm -f build/generated_assets.bin
    echo "✅ 已删除"
else
    echo "✅ 已删除（之前操作已完成）"
fi

# 4. 列出新动画文件
echo ""
echo "📂 新动画文件列表："
ls -lh assets_source/anim/*.json | grep -E "(idle|loading|listening|speaking|settings|updating|success|error)\.json"

# 5. 重新构建
echo ""
echo "🔨 开始构建..."
idf.py build

# 6. 检查 assets.bin 是否生成
echo ""
echo "✅ 构建完成！检查 assets.bin..."
if [ -f "build/generated_assets.bin" ]; then
    ls -lh build/generated_assets.bin
    echo "✅ Assets.bin 已生成"
else
    echo "❌ Assets.bin 未生成，构建可能失败"
    exit 1
fi

echo ""
echo "=================================="
echo "✅ 构建成功！"
echo "=================================="
echo ""
echo "🔥 下一步：烧录 Assets 分区"
echo ""
echo "方法 1（快速，仅 assets）："
echo "  esptool.py --chip esp32s3 -p /dev/ttyUSB0 -b 460800 \\"
echo "    write_flash 0xa20000 build/generated_assets.bin"
echo ""
echo "方法 2（完整固件）："
echo "  idf.py -p /dev/ttyUSB0 flash"
echo ""
echo "方法 3（macOS 用户）："
echo "  # 先查找端口: ls /dev/cu.usbserial-*"
echo "  esptool.py --chip esp32s3 -p /dev/cu.usbserial-XXXXX -b 460800 \\"
echo "    write_flash 0xa20000 build/generated_assets.bin"
echo ""

