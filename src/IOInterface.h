#ifndef _IO_INTERFACE_
#define _IO_INTERFACE_

#include <vector>
#include <stddef.h>
#include <memory>
#include "Device.h"

template <typename T>
class IOInterface {
public:
    IOInterface(size_t chans = 1);
    ~IOInterface() = default;

    IOInterface(const IOInterface &) = delete;
    IOInterface &operator=(const IOInterface &) = delete;

    bool open(unsigned rbs);
    bool open(unsigned rbs, int ref, const std::string &args);
    void start();
    void stop();
    void reset();

    void setFreq(double freq);
    double setGain(double gain);

    double getFreq();
    double getGain();

    void shiftFreq(double offset);
    void resetFreq();

    int getBuffer(std::vector<std::vector<T>> &bufs,
                  unsigned frameNum, int coarse, int fine, int state);
    int comp_timing_offset(int coarse, int fine, int state);

protected:
    const unsigned _chans;
    unsigned _rbs;

private:
    std::shared_ptr<Device<T>> _device;
    unsigned _prevFrameNum, _frameSize, _frameMod = 10;
    int _ref, _pssTimingAdjust;
    std::string _args;
    int64_t _ts0;
    double _freq, _gain;
};

#endif /* _IO_INTERFACE_ */
