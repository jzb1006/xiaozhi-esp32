#include "pdm_decimator.h"

#include <algorithm>

PdmDecimator::PdmDecimator(int decimation_factor) : decimation_factor_(decimation_factor) {
}

int PdmDecimator::Process(const uint8_t* raw, size_t raw_size, int16_t* dest, int max_samples) {
    return Process(raw, raw_size, dest, max_samples, nullptr);
}

int PdmDecimator::Process(const uint8_t* raw, size_t raw_size, int16_t* dest, int max_samples, size_t* bytes_consumed) {
    int produced = 0;
    size_t consumed = 0;
    for (size_t i = 0; i < raw_size && produced < max_samples; ++i) {
        uint8_t byte = raw[i];
        consumed = i + 1;
        for (int bit = 0; bit < 8 && produced < max_samples; ++bit) {
            ones_count_ += (byte >> bit) & 0x01;
            bit_count_++;
            if (bit_count_ == decimation_factor_) {
                int centered = ones_count_ * 2 - decimation_factor_;
                int32_t sample_q8 = centered * 32768 * 256 / decimation_factor_;
                lp1_q8_ += (sample_q8 - lp1_q8_) >> 2;
                lp2_q8_ += (lp1_q8_ - lp2_q8_) >> 2;
                dc_q8_ += (lp2_q8_ - dc_q8_) >> 6;
                int32_t pcm = (lp2_q8_ - dc_q8_) >> 8;
                dest[produced++] = static_cast<int16_t>(std::clamp<int32_t>(pcm, -32767, 32767));
                bit_count_ = 0;
                ones_count_ = 0;
            }
        }
    }
    if (bytes_consumed != nullptr) {
        *bytes_consumed = consumed;
    }
    return produced;
}

void PdmDecimator::Reset() {
    bit_count_ = 0;
    ones_count_ = 0;
    lp1_q8_ = 0;
    lp2_q8_ = 0;
    dc_q8_ = 0;
}
