# CMakeLists.txt for Micro Wake Word standalone module
#
# This CMakeLists.txt supports two build modes:
# 1. ESP-IDF component build (when used as IDF component)
# 2. Standalone CMake build (for Linux/desktop testing)
#
# ESP-IDF Usage:
#   Copy this directory to your project's components/ folder
#   The component will be automatically picked up by ESP-IDF build system
#
# Standalone Usage:
#   mkdir build && cd build
#   cmake .. -DTFLITE_MICRO_PATH=/path/to/tflite-micro \
#            -DGOOGLE_AUDIO_FRONTEND_PATH=/path/to/audio-frontend
#   make

# Check if we're building as an ESP-IDF component
if(DEFINED ENV{IDF_PATH} OR IDF_VERSION)
    # =========================================================================
    # ESP-IDF Component Build
    # =========================================================================
    
    idf_component_register(
        SRCS
            "core/micro_wake_word.cc"
            "core/streaming_model.cc"
        INCLUDE_DIRS
            "."
            "core"
            "platform"
        REQUIRES
            esp_timer
        PRIV_REQUIRES
            esp_psram
    )
    
    # Note: You need to add dependencies on:
    # - TFLite Micro component (e.g., esp-tflite-micro)
    # - Google Audio Frontend component (e.g., esp_micro_speech_features)
    
else()
    # =========================================================================
    # Standalone CMake Build (Linux/Desktop)
    # =========================================================================
    
    cmake_minimum_required(VERSION 3.10)
    project(micro_wake_word VERSION 1.0.0)
    
    set(CMAKE_CXX_STANDARD 14)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    
    # Find TFLite Micro
    if(NOT DEFINED TFLITE_MICRO_PATH)
        message(WARNING "TFLITE_MICRO_PATH not set. Please provide path to TFLite Micro.")
        set(TFLITE_MICRO_PATH "" CACHE PATH "Path to TFLite Micro library")
    endif()
    
    # Find Google Audio Frontend
    if(NOT DEFINED GOOGLE_AUDIO_FRONTEND_PATH)
        message(WARNING "GOOGLE_AUDIO_FRONTEND_PATH not set. Please provide path to Google Audio Frontend.")
        set(GOOGLE_AUDIO_FRONTEND_PATH "" CACHE PATH "Path to Google Audio Frontend library")
    endif()
    
    # Source files
    set(MWW_SOURCES
        core/micro_wake_word.cc
        core/streaming_model.cc
    )
    
    # Create library
    add_library(micro_wake_word STATIC ${MWW_SOURCES})
    
    # Include directories
    target_include_directories(micro_wake_word PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
    )
    
    # Add TFLite Micro includes if available
    if(TFLITE_MICRO_PATH)
        target_include_directories(micro_wake_word PUBLIC
            ${TFLITE_MICRO_PATH}
            ${TFLITE_MICRO_PATH}/tensorflow/lite/micro
        )
    endif()
    
    # Add Google Audio Frontend includes if available
    if(GOOGLE_AUDIO_FRONTEND_PATH)
        target_include_directories(micro_wake_word PUBLIC
            ${GOOGLE_AUDIO_FRONTEND_PATH}/include
        )
    endif()
    
    # Build examples (optional)
    option(BUILD_EXAMPLES "Build example applications" OFF)
    
    if(BUILD_EXAMPLES)
        add_executable(basic_example examples/basic_example.cc)
        target_link_libraries(basic_example micro_wake_word)
        
        # Link TFLite Micro and Audio Frontend libraries
        # target_link_libraries(basic_example tensorflow-lite-micro audio-frontend)
    endif()
    
    # Install targets
    install(TARGETS micro_wake_word
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
    )
    
    install(DIRECTORY core/ platform/
        DESTINATION include/micro_wake_word
        FILES_MATCHING PATTERN "*.h"
    )
    
    # Print configuration summary
    message(STATUS "")
    message(STATUS "Micro Wake Word Configuration:")
    message(STATUS "  TFLite Micro Path: ${TFLITE_MICRO_PATH}")
    message(STATUS "  Audio Frontend Path: ${GOOGLE_AUDIO_FRONTEND_PATH}")
    message(STATUS "  Build Examples: ${BUILD_EXAMPLES}")
    message(STATUS "")
    
endif()

