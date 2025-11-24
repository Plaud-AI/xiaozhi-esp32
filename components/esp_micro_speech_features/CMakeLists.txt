# ESPMicroSpeechFeatures - ESP-IDF Component
# Ported from https://github.com/kahrendt/ESPMicroSpeechFeatures v1.1.0

set(COMPONENT_SRCS
    "src/fft.c"
    "src/fft_util.c"
    "src/filterbank.c"
    "src/filterbank_util.c"
    "src/frontend.c"
    "src/frontend_util.c"
    "src/kiss_fft.c"
    "src/kiss_fftr.c"
    "src/log_lut.c"
    "src/log_scale.c"
    "src/log_scale_util.c"
    "src/noise_reduction.c"
    "src/noise_reduction_util.c"
    "src/pcan_gain_control.c"
    "src/pcan_gain_control_util.c"
    "src/window.c"
    "src/window_util.c"
)

set(COMPONENT_ADD_INCLUDEDIRS
    "include"
    "src"
)

set(COMPONENT_REQUIRES)
set(COMPONENT_PRIV_REQUIRES)

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS ${COMPONENT_ADD_INCLUDEDIRS}
    REQUIRES ${COMPONENT_REQUIRES}
    PRIV_REQUIRES ${COMPONENT_PRIV_REQUIRES}
)

# Suppress warnings for this third-party library
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-unused-variable
)

