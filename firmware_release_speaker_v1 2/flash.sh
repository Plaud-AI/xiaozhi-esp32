#!/bin/bash
# ESP32-S3 PLAUD 蓝牙音箱固件烧录脚本
# 使用方法：./flash.sh [串口] [波特率]
# 示例：./flash.sh /dev/ttyUSB0 460800

# 默认参数
PORT=${1:-/dev/ttyUSB0}
BAUD=${2:-460800}

echo "======================================"
echo "ESP32-S3 PLAUD 蓝牙音箱固件烧录"
echo "======================================"
echo "串口: $PORT"
echo "波特率: $BAUD"
echo "======================================"

# 检查是否安装了 esptool
if ! command -v python3 &> /dev/null; then
    echo "❌ 错误：未找到 python3"
    echo "请先安装 Python 3"
    exit 1
fi

# 检查文件是否存在
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

# 执行烧录
python3 -m esptool --chip esp32s3 -p "$PORT" -b "$BAUD" \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0xd000 ota_data_initial.bin \
  0x20000 xiaozhi.bin \
  0xa20000 generated_assets.bin

# 检查烧录结果
if [ $? -eq 0 ]; then
    echo ""
    echo "======================================"
    echo "✅ 烧录成功！"
    echo "======================================"
    echo ""
    echo "下一步："
    echo "1. 设备将自动重启"
    echo "2. 可使用串口监控查看启动日志："
    echo "   python3 -m esptool -p $PORT monitor"
    echo ""
    echo "预期看到："
    echo "  - I2C设备: 0x18/0x30 (ES8311), 0x40 (ES7210)"
    echo "  - ES8311: Codec initialized successfully"
    echo "  - ES7210: Codec initialized successfully"
    echo "  - 蓝牙: ESP32-PLAUD"
    echo "======================================"
else
    echo ""
    echo "======================================"
    echo "❌ 烧录失败！"
    echo "======================================"
    echo ""
    echo "常见问题："
    echo "1. 串口被占用："
    echo "   - 关闭其他串口监控程序"
    echo "   - 检查串口路径是否正确"
    echo ""
    echo "2. 无法连接设备："
    echo "   - 按住 Boot 键，然后插入 USB"
    echo "   - 检查 USB 线缆是否正常"
    echo "   - 尝试降低波特率：./flash.sh $PORT 115200"
    echo ""
    echo "3. 权限问题（Linux/Mac）："
    echo "   - sudo ./flash.sh $PORT $BAUD"
    echo "   - 或添加用户到 dialout 组"
    echo "======================================"
    exit 1
fi

