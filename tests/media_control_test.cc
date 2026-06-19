#include "media_control.h"

#include <cassert>

static void test_parses_music_start() {
    auto control = ParseMediaControlFields("media", "start", "music");

    assert(control.valid);
    assert(control.music);
    assert(control.start);
    assert(!control.stop);
}

static void test_parses_music_stop() {
    auto control = ParseMediaControlFields("media", "stop", "music");

    assert(control.valid);
    assert(control.music);
    assert(!control.start);
    assert(control.stop);
}

static void test_ignores_tts_event() {
    auto control = ParseMediaControlFields("tts", "start", nullptr);

    assert(!control.valid);
}

static void test_ignores_non_music_media_event() {
    auto control = ParseMediaControlFields("media", "start", "video");

    assert(!control.valid);
}

int main() {
    test_parses_music_start();
    test_parses_music_stop();
    test_ignores_tts_event();
    test_ignores_non_music_media_event();
    return 0;
}
