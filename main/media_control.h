#ifndef _MEDIA_CONTROL_H_
#define _MEDIA_CONTROL_H_

struct cJSON;

struct MediaControl {
    bool valid = false;
    bool music = false;
    bool start = false;
    bool stop = false;
};

MediaControl ParseMediaControlFields(const char* type, const char* state, const char* kind);
MediaControl ParseMediaControl(const cJSON* root);

#endif // _MEDIA_CONTROL_H_
