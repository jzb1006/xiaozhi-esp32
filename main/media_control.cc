#include "media_control.h"

#include <cstring>
#ifndef MEDIA_CONTROL_HOST_TEST
#include <cJSON.h>
#endif

MediaControl ParseMediaControlFields(const char* type, const char* state, const char* kind) {
    MediaControl control;
    if (type == nullptr || state == nullptr || kind == nullptr) {
        return control;
    }
    if (std::strcmp(type, "media") != 0 || std::strcmp(kind, "music") != 0) {
        return control;
    }
    control.valid = true;
    control.music = true;
    if (std::strcmp(state, "start") == 0) {
        control.start = true;
    } else if (std::strcmp(state, "stop") == 0) {
        control.stop = true;
    } else {
        control.valid = false;
        control.music = false;
    }
    return control;
}

MediaControl ParseMediaControl(const cJSON* root) {
#ifdef MEDIA_CONTROL_HOST_TEST
    (void)root;
    return MediaControl{};
#else
    auto type = cJSON_GetObjectItem(root, "type");
    auto state = cJSON_GetObjectItem(root, "state");
    auto kind = cJSON_GetObjectItem(root, "kind");
    return ParseMediaControlFields(
            cJSON_IsString(type) ? type->valuestring : nullptr,
            cJSON_IsString(state) ? state->valuestring : nullptr,
            cJSON_IsString(kind) ? kind->valuestring : nullptr
    );
#endif
}
