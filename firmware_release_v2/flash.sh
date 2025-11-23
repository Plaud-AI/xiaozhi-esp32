#!/bin/bash
# ESP32-S3 PLAUD 蓝牙音箱固件烧录脚本
# 版本：v2.0 (修复版)
# 使用方法：./flash.sh [串口] [波特率]

PORT=${1:-/dev/ttyUSB0}
BAUD=${2:-460800}

echo "======================================"
echo "ESP32-S3 PLAUD 固件烧录 v2.0"
echo "======================================"
echo "固件版本: v2.0 (ES8311修复版)"
echo "编译时间: 2025-11-13 17:15"
echo "串口: $PORT"
echo "波特率: $BAUD"
echo "======================================"
echo ""
echo "⚠️  重要提示："
echo "   - Flash模式: DIO (不是QIO)"
echo "   - ES8311 地址已更新为 0x18"
echo "   - ES7210 未焊接时不会崩溃"
echo ""
echo "======================================"

# 检查 Python
if ! command -v python3 &> /dev/null; then
    echo "❌ 错误：未找到 python3"
    exit 1
fi

# 检查文件
FILES=(
    "bootloader.bin"
    "partition-table.bin"
    "ota_data_initial.bin"
    "xiaozhi.bin"
    "generated_assets.bin"
)

for file in "${FILES[@]}"; do
    if [ ! -f "$file" ]; then
        echo "❌ 错误：文件不存在 - $file"
        exit 1
    fi
done

echo "✅ 所有文件检查通过"
echo ""
echo "开始烧录..."
echo ""

# 烧录
python3 -m esptool --chip esp32s3 -p "$PORT" -b "$BAUD" \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0xd000 ota_data_initial.bin \
  0x20000 xiaozhi.bin \
  0xa20000 generated_assets.bin

if [ $? -eq 0 ]; then
    echo ""
    echo "======================================"
    echo "✅ 烧录成功！"
    echo "======================================"
    echo ""
    echo "预期启动日志："
    echo "  ✅ I2C扫描: 0x18 (ES8311)"
    echo "  ✅ ES8311: Codec initialized successfully"
    echo "  ⚠️  ES7210: 未焊接警告（正常）"
    echo "  ✅ Application: STATE: running"
    echo ""
    echo "监控串口："
    echo "  python3 -m esptool -p $PORT monitor"
    echo "======================================"
else
    echo ""
    echo "======================================"
    echo "❌ 烧录失败！"
    echo "======================================"
    echo ""
    echo "请检查："
    echo "  1. 串口是否正确"
    echo "  2. 是否按住Boot键"
    echo "  3. USB线缆是否正常"
    echo "======================================"
    exit 1
fi

