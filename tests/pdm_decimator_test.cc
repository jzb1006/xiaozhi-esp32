#include "pdm_decimator.h"

#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <vector>

static int max_abs_sample(const std::vector<int16_t>& pcm) {
    int max_abs = 0;
    for (int16_t sample : pcm) {
        max_abs = std::max(max_abs, std::abs((int)sample));
    }
    return max_abs;
}

static int tail_rms(const std::vector<int16_t>& pcm, int start) {
    int64_t sum_squares = 0;
    int count = 0;
    for (size_t i = start; i < pcm.size(); ++i) {
        sum_squares += (int32_t)pcm[i] * pcm[i];
        ++count;
    }
    return (int)std::sqrt((double)sum_squares / count);
}

static void append_density_sample(std::vector<uint8_t>& raw, int decimation_factor, int ones) {
    for (int bit = 0; bit < decimation_factor; ++bit) {
        if (raw.empty() || (bit % 8) == 0) {
            raw.push_back(0);
        }
        if (bit < ones) {
            raw.back() |= 1 << (bit % 8);
        }
    }
}

static void append_pdm_bit(std::vector<uint8_t>& raw, int bit_index, bool value) {
    if ((bit_index % 8) == 0) {
        raw.push_back(0);
    }
    if (value) {
        raw.back() |= 1 << (bit_index % 8);
    }
}

static void test_all_ones_dc_is_removed_after_warmup() {
    PdmDecimator decimator(128);
    std::vector<uint8_t> raw(16 * 160, 0xff);
    std::vector<int16_t> pcm(160);

    int produced = decimator.Process(raw.data(), raw.size(), pcm.data(), pcm.size());

    assert(produced == 160);
    int tail_max = 0;
    for (int i = produced - 40; i < produced; ++i) {
        tail_max = std::max(tail_max, std::abs((int)pcm[i]));
    }
    assert(tail_max < 6000);
}

static void test_all_zeros_dc_is_removed_after_warmup() {
    PdmDecimator decimator(128);
    std::vector<uint8_t> raw(16 * 160, 0x00);
    std::vector<int16_t> pcm(160);

    int produced = decimator.Process(raw.data(), raw.size(), pcm.data(), pcm.size());

    assert(produced == 160);
    int tail_max = 0;
    for (int i = produced - 40; i < produced; ++i) {
        tail_max = std::max(tail_max, std::abs((int)pcm[i]));
    }
    assert(tail_max < 6000);
}

static void test_equal_ones_and_zeros_decodes_to_zero() {
    PdmDecimator decimator(128);
    std::vector<uint8_t> raw(16 * 80, 0x0f);
    std::vector<int16_t> pcm(80);

    int produced = decimator.Process(raw.data(), raw.size(), pcm.data(), pcm.size());

    assert(produced == 80);
    assert(tail_rms(pcm, 40) < 100);
}

static void test_alternating_full_bytes_are_low_passed() {
    PdmDecimator decimator(128);
    std::vector<uint8_t> raw;
    raw.reserve(16 * 80);
    for (int i = 0; i < 80; ++i) {
        for (int j = 0; j < 8; ++j) {
            raw.push_back(0x00);
        }
        for (int j = 0; j < 8; ++j) {
            raw.push_back(0xff);
        }
    }
    std::vector<int16_t> pcm(80);

    int produced = decimator.Process(raw.data(), raw.size(), pcm.data(), pcm.size());

    assert(produced == 80);
    assert(max_abs_sample(pcm) < 6000);
}

static void test_alternating_full_scale_windows_are_low_passed() {
    PdmDecimator decimator(128);
    constexpr int kDecimationFactor = 128;
    constexpr int kSamples = 640;
    std::vector<uint8_t> raw;
    raw.reserve(kSamples * kDecimationFactor / 8);

    for (int i = 0; i < kSamples; ++i) {
        append_density_sample(raw, kDecimationFactor, (i % 2) == 0 ? 0 : kDecimationFactor);
    }

    std::vector<int16_t> pcm(kSamples);
    int produced = decimator.Process(raw.data(), raw.size(), pcm.data(), pcm.size());

    assert(produced == kSamples);
    int steady_state_max = 0;
    for (int i = 160; i < produced; ++i) {
        steady_state_max = std::max(steady_state_max, std::abs((int)pcm[i]));
    }
    assert(steady_state_max < 10000);
}

static void test_constant_density_offset_is_removed() {
    PdmDecimator decimator(128);
    std::vector<uint8_t> raw(16 * 160, 0x0f);
    for (size_t i = 0; i < raw.size(); i += 5) {
        raw[i] = 0x01; // About 40 percent density, matching the C6 idle trace.
    }
    std::vector<int16_t> pcm(160);

    int produced = decimator.Process(raw.data(), raw.size(), pcm.data(), pcm.size());

    assert(produced == 160);
    int tail_max = 0;
    for (int i = produced - 40; i < produced; ++i) {
        tail_max = std::max(tail_max, std::abs((int)pcm[i]));
    }
    assert(tail_max < 2000);
}

static void test_preserves_speech_band_density_modulation() {
    PdmDecimator decimator(128);
    constexpr int kDecimationFactor = 128;
    constexpr int kSampleRate = 16000;
    constexpr int kToneHz = 1000;
    constexpr int kSamples = 1600;
    constexpr double kPi = 3.14159265358979323846;
    std::vector<uint8_t> raw;
    raw.reserve(kSamples * kDecimationFactor / 8);

    for (int i = 0; i < kSamples; ++i) {
        double wave = std::sin(2.0 * kPi * kToneHz * i / kSampleRate);
        int ones = (int)std::lround((0.50 + 0.24 * wave) * kDecimationFactor);
        append_density_sample(raw, kDecimationFactor, ones);
    }

    std::vector<int16_t> pcm(kSamples);
    int produced = decimator.Process(raw.data(), raw.size(), pcm.data(), pcm.size());

    assert(produced == kSamples);
    int steady_state_max = 0;
    for (int i = 320; i < produced; ++i) {
        steady_state_max = std::max(steady_state_max, std::abs((int)pcm[i]));
    }
    assert(steady_state_max > 5000);
}

static void test_preserves_speech_band_sigma_delta_modulation() {
    PdmDecimator decimator(128);
    constexpr int kDecimationFactor = 128;
    constexpr int kSampleRate = 16000;
    constexpr int kToneHz = 3000;
    constexpr int kSamples = 3200;
    constexpr double kPi = 3.14159265358979323846;
    std::vector<uint8_t> raw;
    raw.reserve(kSamples * kDecimationFactor / 8);

    double integrator = 0.0;
    for (int i = 0; i < kSamples * kDecimationFactor; ++i) {
        double target = 0.24 * std::sin(2.0 * kPi * kToneHz * i / (kSampleRate * kDecimationFactor));
        bool bit = integrator >= 0.0;
        integrator += target - (bit ? 1.0 : -1.0);
        append_pdm_bit(raw, i, bit);
    }

    std::vector<int16_t> pcm(kSamples);
    int produced = decimator.Process(raw.data(), raw.size(), pcm.data(), pcm.size());

    assert(produced == kSamples);
    assert(tail_rms(pcm, 800) > 250);
}

static void test_sigma_delta_idle_bias_is_suppressed() {
    PdmDecimator decimator(128);
    constexpr int kDecimationFactor = 128;
    constexpr int kSamples = 3200;
    std::vector<uint8_t> raw;
    raw.reserve(kSamples * kDecimationFactor / 8);

    double integrator = 0.0;
    for (int i = 0; i < kSamples * kDecimationFactor; ++i) {
        bool bit = integrator >= 0.0;
        integrator += -0.20 - (bit ? 1.0 : -1.0);
        append_pdm_bit(raw, i, bit);
    }

    std::vector<int16_t> pcm(kSamples);
    int produced = decimator.Process(raw.data(), raw.size(), pcm.data(), pcm.size());

    assert(produced == kSamples);
    assert(tail_rms(pcm, 800) < 600);
}

static void test_keeps_partial_bits_between_calls() {
    PdmDecimator decimator(128);
    std::vector<uint8_t> first_half(8, 0x0f);
    std::vector<uint8_t> second_half(8, 0x0f);
    std::vector<int16_t> pcm(80, 1234);

    int produced = decimator.Process(first_half.data(), first_half.size(), pcm.data(), 1);
    assert(produced == 0);
    assert(pcm[0] == 1234);

    produced = decimator.Process(second_half.data(), second_half.size(), pcm.data(), 1);
    assert(produced == 1);

    for (int i = 1; i < 80; ++i) {
        produced += decimator.Process(first_half.data(), first_half.size(), pcm.data() + i, 1);
        produced += decimator.Process(second_half.data(), second_half.size(), pcm.data() + i, 1);
    }
    assert(produced == 80);
    assert(tail_rms(pcm, 40) < 100);
}

static void test_reports_consumed_raw_bytes_when_output_buffer_fills() {
    PdmDecimator decimator(128);
    std::vector<uint8_t> raw(16 * 10, 0x0f);
    std::vector<int16_t> pcm(5);
    size_t bytes_consumed = 0;

    int produced = decimator.Process(raw.data(), raw.size(), pcm.data(), pcm.size(), &bytes_consumed);

    assert(produced == 5);
    assert(bytes_consumed == 16 * 5);
}

int main() {
    test_all_ones_dc_is_removed_after_warmup();
    test_all_zeros_dc_is_removed_after_warmup();
    test_equal_ones_and_zeros_decodes_to_zero();
    test_alternating_full_bytes_are_low_passed();
    test_alternating_full_scale_windows_are_low_passed();
    test_constant_density_offset_is_removed();
    test_preserves_speech_band_density_modulation();
    test_preserves_speech_band_sigma_delta_modulation();
    test_sigma_delta_idle_bias_is_suppressed();
    test_keeps_partial_bits_between_calls();
    test_reports_consumed_raw_bytes_when_output_buffer_fills();
    return EXIT_SUCCESS;
}
