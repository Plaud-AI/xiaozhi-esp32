// Copyright (c) 2016 HR
// Adapted for xiaozhi-esp32 project

#ifndef AUDIO_WAKE_WORDS_FFT_H_
#define AUDIO_WAKE_WORDS_FFT_H_

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif
#ifndef M_2PI
#define M_2PI 6.283185307179586476925286766559005
#endif

namespace plaud {

// Fast Fourier Transform

void make_sintbl(int n, float* sintbl);

void make_bitrev(int n, int* bitrev);

int fft(const int* bitrev, const float* sintbl, float* x, float* y, int n);

}  // namespace plaud

#endif  // AUDIO_WAKE_WORDS_FFT_H_

