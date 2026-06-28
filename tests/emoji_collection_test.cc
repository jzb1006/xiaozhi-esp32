#include "display/lvgl_display/emoji_collection.h"

#include <cassert>
#include <cstddef>

class CountingImage : public LvglImage {
public:
    explicit CountingImage(int* live_count) : live_count_(live_count) {
        ++(*live_count_);
    }

    ~CountingImage() override {
        --(*live_count_);
    }

    const lv_img_dsc_t* image_dsc() const override {
        return nullptr;
    }

private:
    int* live_count_;
};

static void test_replaces_existing_emoji_without_leaking_old_image() {
    int live_count = 0;

    {
        EmojiCollection collection;
        collection.AddEmoji("neutral", new CountingImage(&live_count));
        collection.AddEmoji("neutral", new CountingImage(&live_count));

        assert(live_count == 1);
    }

    assert(live_count == 0);
}

int main() {
    test_replaces_existing_emoji_without_leaking_old_image();
    return 0;
}
