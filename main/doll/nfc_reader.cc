#include "nfc_reader.h"

#include <sdkconfig.h>

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <esp_log.h>

#define TAG "NfcReader"

bool NfcReader::Initialize() {
    return true;
}

bool NfcReader::ReadTag(std::string& tag_id) {
    return false;
}

bool NfcReader::IsTagPresent() {
    return false;
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION
