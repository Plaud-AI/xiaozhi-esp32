小智 ESP32-S3 固件烧录说明
===========================

最后更新: 2026-01-03
版本: v2.0.3
目标板: ESP32S3_KORVO2_V3
芯片: ESP32-S3
Flash: 16MB

此版本已将默认 OTA 地址修改为: http://44.228.155.146:8003/xiaozhi/ota/

烧录地址表:
-----------
0x0       - bootloader.bin
0x8000    - partition-table.bin
0xd000    - ota_data_initial.bin
0x20000   - xiaozhi.bin
0xa20000  - generated_assets.bin

使用 esptool.py 烧录命令:
-------------------------
esptool.py --chip esp32s3 --port [PORT] --baud 921600 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0xd000 ota_data_initial.bin \
  0x20000 xiaozhi.bin \
  0xa20000 generated_assets.bin

提示:
- [PORT]: Windows 为 COMx, macOS 为 /dev/cu.usbserial-xxx, Linux 为 /dev/ttyUSBx
- 也可以使用乐鑫官方的 Flash Download Tool 工具进行烧录


