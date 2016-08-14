#ifndef _RESAMPLER_H_
#define _RESAMPLER_H_

#include "SignalVector.h"

class Resampler {
public:
    Resampler() = default;
    Resampler(const Resampler &r);
    Resampler(const Resampler &&r);
    Resampler(unsigned P, unsigned Q, size_t filterLen);
    ~Resampler() = default;

    Resampler& operator=(const Resampler &r);
    Resampler& operator=(Resampler &&r);

    void rotate(SignalVector &in, SignalVector &out);
    void update(SignalVector &in);

private:
    std::vector<SignalVector> partitions;
    std::vector<std::pair<int, int>> paths;
    SignalVector history;
    size_t filterLen;
    unsigned P, Q;

    void init(unsigned Q);
    void generatePaths(size_t n);
};

#endif /* _RESAMPLER_H_ */
