/*
 * LTE Synchronizer
 *
 * Copyright (C) 2016 Ettus Research LLC
 * Author Tom Tsou <tom.tsou@ettus.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "SynchronizerPDSCH.h"

extern "C" {
#include "openphy/pbch.h"
#include "openphy/slot.h"
#include "openphy/si.h"
#include "openphy/subframe.h"
#include "openphy/ofdm.h"
#include "openphy/log.h"
}

void SynchronizerPDSCH::handleFreqOffset(double offset)
{
    _freqOffsets.push(offset);

    auto log = [](float offset) {
        char sbuf[80];
        snprintf(sbuf, 80, "REF   : Frequency offset %f Hz", offset);
        LOG_SYNC(sbuf);
    };

    if (_freqOffsets.full()) {
        auto average = _freqOffsets.average();
        log(average);
        shiftFreq(average);
    }
}

/*
 * PDSCH drive sequence
 */
int SynchronizerPDSCH::drive(int adjust)
{
    struct lte_time *ltime = &_rx->time;
    static struct lte_mib mib;

    ltime->subframe = (ltime->subframe + 1) % 10;
    if (!ltime->subframe)
        ltime->frame = (ltime->frame + 1) % 1024;

    Synchronizer::drive(ltime, adjust);

    switch (_rx->state) {
    case LTE_STATE_PBCH:
        if (timePBCH(ltime)) {
            if (decodePBCH(ltime, &mib)) {
                lte_log_time(ltime);
                if (mib.rbs != _rbs) {
                    _rbs = mib.rbs;
                    reopen(_rbs);
                    changeState(LTE_STATE_PSS_SYNC);
                } else {
                    changeState(LTE_STATE_PDSCH_SYNC);
                }
                _pssMisses = 0;
            } else if (++_pssMisses > 10) {
                resetState();
            }
        }
        break;
    case LTE_STATE_PDSCH_SYNC:
        /* SSS must match so we only check timing/frequency on 0 */
        if (ltime->subframe == 5) {
            if (syncPSS4() == StatePSS::NotFound && _pssMisses > 100) {
                resetState();
                break;
            }
        }
    case LTE_STATE_PDSCH:
        if (timePDSCH(ltime)) {
            auto lbuf = _inboundQueue->read();
            if (!lbuf) {
                LOG_ERR("SYNC  : Dropped frame");
                break;
            }

            handleFreqOffset(lbuf->freqOffset);

            if (lbuf->crcValid) {
                _pssMisses = 0;
                _sssMisses = 0;
                lbuf->crcValid = false;
            }

            lbuf->rbs = mib.rbs;
            lbuf->cellId = _cellId;
            lbuf->ng = mib.phich_ng;
            lbuf->txAntennas = mib.ant;
            lbuf->sfn = ltime->subframe;
            lbuf->fn = ltime->frame;

            _converter.delayPDSCH(lbuf->buffers, adjust);
            _outboundQueue->write(lbuf);
        }
    }

    _converter.update();

    return 0;
}

/*
 * PDSCH synchronizer loop 
 */
void SynchronizerPDSCH::start()
{
    _stop = false;
    IOInterface<complex<short>>::start();

    for (int counter = 0;; counter++) {
        int shift = getBuffer(_converter.raw(), counter,
                              _rx->sync.coarse,
                              _rx->sync.fine,
                              _rx->state == LTE_STATE_PDSCH_SYNC);
        _rx->sync.coarse = 0;
        _rx->sync.fine = 0;

        if (drive(shift) < 0) {
            fprintf(stderr, "Drive: Fatal error\n");
            break;
        }

        _converter.reset();
        if (_reset) resetState();
        if (_stop) break;
    }
}

void SynchronizerPDSCH::attachInboundQueue(shared_ptr<BufferQueue> q)
{
    _inboundQueue = q;
}

void SynchronizerPDSCH::attachOutboundQueue(shared_ptr<BufferQueue> q)
{
    _outboundQueue = q;
}

SynchronizerPDSCH::SynchronizerPDSCH(size_t chans)
  : Synchronizer(chans), _freqOffsets(200)
{
}
