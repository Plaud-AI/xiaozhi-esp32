/* Copyright 2023 The TensorFlow Authors. All Rights Reserved.
 * 
 * 唤醒词识别模型数据
 * 
 * 注意：这是一个占位符模型数据文件。
 * 在实际使用时，您需要：
 * 1. 训练您自己的唤醒词识别模型（TensorFlow/Keras）
 * 2. 转换为 TFLite 格式（使用 TFLiteConverter）
 * 3. 量化为 INT8 格式（使用 Representative Dataset）
 * 4. 使用 xxd 或类似工具转换为 C 数组
 * 5. 替换下面的模型数据
 * 
 * 模型输入：int8_t[49][40] - 音频特征（MFCC）
 * 模型输出：int8_t[4] - 类别概率（silence, unknown, yes, no）
 * 
 * 参考：
 * - TensorFlow Lite Micro: https://www.tensorflow.org/lite/microcontrollers
 * - 模型训练教程: https://www.tensorflow.org/lite/microcontrollers/get_started_low_level
 */

#ifndef WAKE_WORD_MODEL_DATA_H_
#define WAKE_WORD_MODEL_DATA_H_

extern const unsigned char g_wake_word_model_data[];
extern const unsigned int g_wake_word_model_data_len;

#endif  // WAKE_WORD_MODEL_DATA_H_

