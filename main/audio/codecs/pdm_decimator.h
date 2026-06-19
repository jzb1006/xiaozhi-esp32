#ifndef _PDM_DECIMATOR_H_
#define _PDM_DECIMATOR_H_

#include <cstddef>
#include <cstdint>

class PdmDecimator {
public:
    explicit PdmDecimator(int decimation_factor);

    int Process(const uint8_t* raw, size_t raw_size, int16_t* dest, int max_samples);
    int Process(const uint8_t* raw, size_t raw_size, int16_t* dest, int max_samples, size_t* bytes_consumed);
    void Reset();

private:
    int decimation_factor_;
    int bit_count_ = 0;
    int ones_count_ = 0;
    int32_t lp1_q8_ = 0;
    int32_t lp2_q8_ = 0;
    int32_t dc_q8_ = 0;
};

#endif // _PDM_DECIMATOR_H_
