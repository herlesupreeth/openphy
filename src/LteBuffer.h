#ifndef _LTE_BUFFER_H_
#define _LTE_BUFFER_H_

#include <vector>
#include <complex>

struct LteBuffer {
    LteBuffer(unsigned chans = 1);
    LteBuffer(const LteBuffer &);
    LteBuffer(LteBuffer &&);
    ~LteBuffer() = default;

    LteBuffer &operator=(const LteBuffer &);
    LteBuffer &operator=(LteBuffer &&);

    unsigned cellId, rbs, ng, txAntennas;
    int fn, sfn;
    double freqOffset;
    bool crcValid;

    std::vector<std::vector<std::complex<float>>> buffers;
};
#endif /* _LTE_BUFFER_H_ */
