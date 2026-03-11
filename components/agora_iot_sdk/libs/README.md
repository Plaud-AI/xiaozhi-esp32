# Agora SDK Libraries

Place the following prebuilt static libraries (ESP32-S3 target) in this directory:

| File | Description |
|------|-------------|
| `librtsa.a` | Agora RTSA core |
| `libahpl.a` | Agora HPL helper |
| `libagora-cjson.a` | Agora bundled cJSON |

## Download

1. Visit https://github.com/AgoraIO/Agora-RTC-Solution-IoT-ESP32
2. Download the SDK package for your target (ESP32 / ESP32-S3).
3. Copy the three `.a` files into this directory.

The project configures and builds without these files present, but
linking will fail unless they are available.
