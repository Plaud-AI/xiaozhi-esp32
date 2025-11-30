#!/bin/bash
#
# 烧录新 Assets 分区脚本
# 已完成：
#   ✅ 删除旧动画文件（无.json后缀）
#   ✅ 重新生成 generated_assets.bin（包含新动画）
#
# 现在需要：
#   ⏳ 烧录 assets 分区到设备
#

set -e

DEVICE_PORT="/dev/cu.usbserial-21440"
ESPTOOL="/Users/xionghao/.espressif/python_env/idf5.5_py3.9_env/bin/esptool.py"
ASSETS_BIN="build/generated_assets.bin"

echo "=========================================="
echo "🔥 烧录新 Assets 分区"
echo "=========================================="
echo ""

# 1. 检查 assets.bin 是否存在
if [ ! -f "$ASSETS_BIN" ]; then
    echo "❌ 错误：$ASSETS_BIN 不存在"
    echo "   请先运行构建命令"
    exit 1
fi

echo "📦 Assets 文件信息："
ls -lh "$ASSETS_BIN"
echo ""

# 2. 检查端口
echo "🔌 设备端口：$DEVICE_PORT"
if [ ! -e "$DEVICE_PORT" ]; then
    echo "❌ 错误：端口不存在"
    echo "   可用端口："
    ls /dev/cu.usbserial-* 2>/dev/null || echo "   未找到 USB 串口"
    exit 1
fi

# 3. 检查端口是否被占用
if lsof "$DEVICE_PORT" > /dev/null 2>&1; then
    echo ""
    echo "⚠️  警告：端口被占用（可能 monitor 正在运行）"
    echo ""
    echo "请在另一个终端中："
    echo "  1. 按 Ctrl+] 退出 monitor"
    echo "  2. 或关闭运行 monitor 的终端"
    echo "  3. 然后重新运行此脚本"
    echo ""
    echo "占用端口的进程："
    lsof "$DEVICE_PORT"
    exit 1
fi

# 4. 烧录
echo "🔥 开始烧录 assets 分区..."
echo "   地址：0xa20000"
echo "   文件：$ASSETS_BIN"
echo ""

$ESPTOOL --chip esp32s3 -p "$DEVICE_PORT" -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0xa20000 "$ASSETS_BIN"

echo ""
echo "=========================================="
echo "✅ 烧录完成！"
echo "=========================================="
echo ""
echo "🎯 下一步：查看日志验证"
echo ""
echo "运行命令："
echo "  cd /Users/xionghao/Documents/GitHub/xiaozhi-esp32"
echo "  python3 -m serial.tools.miniterm --raw $DEVICE_PORT 115200"
echo ""
echo "或使用 idf.py monitor（需要先设置环境）："
echo "  source ~/esp/esp-idf/export.sh"
echo "  idf.py -p $DEVICE_PORT monitor"
echo ""
echo "预期日志（验证新动画）："
echo "  ✅ I (xxx) EmotionAssetsLoader: Loaded asset: idle (3335 bytes)"
echo "  ✅ I (xxx) EmotionAssetsLoader: Loaded asset: speaking (2998 bytes)"
echo "  ✅ I (xxx) EmotionAssetsLoader: Loaded asset: listening (5023 bytes)"
echo ""

