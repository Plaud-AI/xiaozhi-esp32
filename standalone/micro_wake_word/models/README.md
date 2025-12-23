# Wake Word Models

This directory contains TFLite models for wake word detection.

## Model Format

Wake word models should be:
- **Format**: TensorFlow Lite (`.tflite`)
- **Quantization**: INT8 (for best performance on microcontrollers)
- **Input**: [1, stride, 40] int8 features (40 = MFCC channels)
- **Output**: [1, 1] uint8 probability

## Converting Models to C Header

To embed a model in your firmware, convert it to a C header file:

```bash
xxd -i your_model.tflite > your_model.h
```

Then include it in your code:

```cpp
#include "your_model.h"

// Use the model
micro_ww.add_wake_word_model(
    your_model_tflite,      // Model data
    0.5f,                   // Probability cutoff
    10,                     // Sliding window size
    "your wake word",       // Wake word name
    30000                   // Tensor arena size
);
```

## Compatible Models

This implementation is compatible with:

1. **ESPHome microWakeWord models**
   - https://github.com/esphome/micro-wake-word-models
   - Pre-trained models: "okay nabu", "hey jarvis", etc.

2. **Custom trained models**
   - Use the microWakeWord training pipeline
   - https://github.com/kahrendt/microWakeWord

## Model Parameters

When adding a model, you need to specify:

| Parameter | Description | Typical Value |
|-----------|-------------|---------------|
| `probability_cutoff` | Detection threshold (0.0-1.0) | 0.5 - 0.97 |
| `sliding_window_size` | Predictions to average | 5 - 15 |
| `tensor_arena_size` | Memory for inference (bytes) | 20000 - 40000 |

### Tuning Tips

- **Higher `probability_cutoff`**: Fewer false positives, may miss some detections
- **Lower `probability_cutoff`**: More sensitive, more false positives
- **Larger `sliding_window_size`**: More stable, slower response
- **Smaller `sliding_window_size`**: Faster response, less stable

## Example Model Configuration

```cpp
// From ESPHome okay_nabu model
micro_ww.add_wake_word_model(
    okay_nabu_tflite,
    0.97f,    // High threshold for accuracy
    5,        // Small window for fast response
    "okay nabu",
    26080     // Tensor arena size from model docs
);
```

