#ifndef _SYNCHRONIZER_H_
#define _SYNCHRONIZER_H_

#include <map>
#include "IOInterface.h"
#include "Converter.h"

extern "C" {
#include "openphy/lte.h"
#include "openphy/ref.h"
}

using namespace std;

enum {
    SYNC_ERR_NONE,
    SYNC_ERR_PSS_TIME,
    SYNC_ERR_PSS_FREQ,
    SYNC_ERR_SSS,
};

typedef complex<short> SampleType;

class Synchronizer : protected IOInterface<SampleType> {
public:
    Synchronizer(size_t chans = 1);
    virtual ~Synchronizer();

    Synchronizer(const Synchronizer &) = delete;
    Synchronizer &operator=(const Synchronizer &) = delete;

    bool open(size_t rbs, int ref, const std::string &args);
    bool reopen(size_t rbs);
    void reset();
    void stop();

    void setFreq(double freq);
    void setGain(double gain);

    static bool timePSS(struct lte_time *t);
    static bool timeSSS(struct lte_time *t);
    static bool timePBCH(struct lte_time *t);
    static bool timePDSCH(struct lte_time *t);

protected:
    bool syncPSS1();
    bool syncPSS2();
    bool syncPSS3();
    bool syncPSS4();

    int syncSSS();
    int drive(struct lte_time *ltime, int adjust);

    void resetState(bool freq = true);
    void setCellId(int cellId);
    void generateReferences();
    bool decodePBCH(struct lte_time *ltime, struct lte_mib *mib);

    void changeState(auto newState);
    static void logPSS(float mag, int offset);
    static void logSSS(float offset);

    Converter<SampleType> _converter;
    unsigned _pssMisses = 0, _sssMisses = 0;
    int _cellId;
    double _freq, _gain;
    bool _reset, _stop;

    map<int, string> _stateStrings;

    vector<struct lte_ref_map *[4]> _pbchRefMaps;
    struct lte_rx *_rx;
    struct lte_sync _sync;
};
#endif /* _SYNCHRONIZER_H_ */
