# Agora IoT SDK component for ESP32.
#
# The prebuilt .a libraries are NOT included in this repository.
# Download the Agora RTSA SDK for ESP32 from:
#   https://github.com/AgoraIO/Agora-RTC-Solution-IoT-ESP32
# then copy the following files into components/agora_iot_sdk/libs/:
#   librtsa.a        - Agora RTSA core
#   libahpl.a        - Agora HPL helper library
#   libagora-cjson.a - cJSON bundled with Agora SDK
#
# Without the .a files the project will link but the agora_iot_sdk
# component will produce linker errors; omit it from the build by
# removing agora_iot_sdk from PRIV_REQUIRES in main/CMakeLists.txt.

cmake_minimum_required(VERSION 3.16)

idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES     lwip pthread
)

set(AGORA_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs")

# Only link if the libraries are present (allows the project to configure
# without them).
if(EXISTS "${AGORA_LIB_DIR}/librtsa.a")
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-L${AGORA_LIB_DIR}"
        "${AGORA_LIB_DIR}/librtsa.a"
        "${AGORA_LIB_DIR}/libahpl.a"
        "${AGORA_LIB_DIR}/libagora-cjson.a"
    )
else()
    message(WARNING
        "Agora SDK libraries not found in ${AGORA_LIB_DIR}. "
        "See components/agora_iot_sdk/CMakeLists.txt for download instructions.")
endif()
