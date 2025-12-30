小智 ESP32-S3 固件烧录说明
===========================

目标板: ESP32S3_KORVO2_V3
芯片: ESP32-S3
Flash: 16MB

烧录地址表:
-----------
0x0       - bootloader.bin
0x8000    - partition-table.bin
0xd000    - ota_data_initial.bin
0x20000   - xiaozhi.bin
0xa20000  - generated_assets.bin

使用 esptool.py 烧录命令:
-------------------------
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 921600 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0xd000 ota_data_initial.bin \
  0x20000 xiaozhi.bin \
  0xa20000 generated_assets.bin

Windows 用户请将 /dev/ttyUSB0 改为对应的 COM 端口（如 COM3）
macOS 用户请使用 /dev/cu.usbserial-xxx 端口

或使用 Flash Download Tool (Windows) 进行烧录
