/**
 * @file example_model.h
 * @brief Example wake word model placeholder
 * 
 * This is a placeholder file demonstrating the expected model format.
 * Replace with actual model data for your wake word.
 * 
 * To create a model file:
 * 1. Train or download a TFLite wake word model
 * 2. Convert to C array: xxd -i model.tflite > model.h
 * 3. Add 'const' keyword to make it flash-resident
 * 
 * Compatible models can be found at:
 * - https://github.com/esphome/micro-wake-word-models
 */

#pragma once

// Example model data (placeholder - not a real model!)
// In a real model file, this would be thousands of bytes
// 
// Model header format (TFLite Flatbuffer):
// - First 4 bytes: offset to root table
// - Bytes 4-7: "TFL3" magic number
// - Remaining: Model data

// Uncomment and replace with your actual model data:
/*
const unsigned char example_wake_word_tflite[] = {
  0x20, 0x00, 0x00, 0x00, 0x54, 0x46, 0x4c, 0x33,  // TFL3 header
  // ... thousands more bytes of model data ...
};

const unsigned int example_wake_word_tflite_len = sizeof(example_wake_word_tflite);
*/

// Model configuration (adjust for your specific model):
// 
// Example for "hey jarvis" model:
//   probability_cutoff: 0.5
//   sliding_window_size: 10
//   tensor_arena_size: 30000
//
// Example for "okay nabu" model:
//   probability_cutoff: 0.97
//   sliding_window_size: 5
//   tensor_arena_size: 26080

