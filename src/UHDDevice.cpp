/*
 * UHD Device Access
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

#include <iostream>
#include <iomanip>
#include <stdint.h>

#include "UHDDevice.h"

extern "C" {
#include "openphy/log.h"
}

#define RX_BUFLEN        (1 << 20)

#define DEV_ARGS_X300       "master_clock_rate=184.32e6"
#define DEV_ARGS_DEFAULT    ""

using namespace std;

static int get_dev_type(const uhd::device_addr_t &addr)
{
    size_t b200_str, b210_str, x300_str, x310_str;

    b200_str = addr.to_string().find("B200");
    b210_str = addr.to_string().find("B210");
    x300_str = addr.to_string().find("X300");
    x310_str = addr.to_string().find("X310");

    if (b200_str != string::npos)
        return DEV_TYPE_B200;
    else if (b210_str != string::npos)
        return DEV_TYPE_B210;
    else if ((x300_str != string::npos) ||
         (x310_str != string::npos))
        return DEV_TYPE_X300;

    return DEV_TYPE_UNKNOWN;
}

static string get_dev_args(int type)
{
    switch (type) {
    case DEV_TYPE_X300:
        return DEV_ARGS_X300;
    case DEV_TYPE_B200:
    case DEV_TYPE_B210:
    default:
        return DEV_ARGS_DEFAULT;
    }
}

template <typename T>
void UHDDevice<T>::init(int64_t &ts, size_t rbs,
                        int ref, const string &args)
{
    uhd::device_addr_t addr(args);
    uhd::device_addrs_t addrs = uhd::device::find(addr);
    if (!addrs.size()) {
        throw invalid_argument("** No UHD device found");
    }

    cout << "-- Opening device " << addrs.front().to_string() << endl;
    _type = get_dev_type(addrs.front());
    if (_type == DEV_TYPE_UNKNOWN)
        cerr << "** Unsupported or unknown device" << endl;

    addr = uhd::device_addr_t(args + get_dev_args(_type));
    try {
        _dev = uhd::usrp::multi_usrp::make(addr);
    } catch (const exception &ex) {
        cerr << "** UHD make failed" << endl;
        cerr << ex.what() << endl;
    }

    if (_chans > 1)
        _dev->set_time_unknown_pps(uhd::time_spec_t());

    switch (ref) {
    case REF_EXTERNAL:
        _dev->set_clock_source("external");
        break;
    case REF_GPSDO:
        _dev->set_clock_source("gpsdo");
        break;
    case REF_INTERNAL:
    default:
        break;
    }

    initRates(rbs);
    initRx(ts);
}

template <typename T>
void UHDDevice<T>::resetFreq()
{
    uhd::tune_request_t treq(_base_freq);
    treq.rf_freq_policy = uhd::tune_request_t::POLICY_MANUAL;
    treq.rf_freq = _base_freq;

    ostringstream ost;

    try {
        for (size_t i = 0; i < _chans; i++)
            _dev->set_rx_freq(treq, i);

        _offset_freq = _base_freq;
    } catch (const exception &ex) {
        ost << "DEV   : Frequency setting failed - " << ex.what();
        LOG_ERR(ost.str().c_str());
    }

    ost << "DEV   : Resetting RF frequency to "
        << _base_freq / 1e6 << " MHz";

    LOG_DEV(ost.str().c_str());
}

static int64_t last = 0;

static double get_rate(int rbs)
{
    switch (rbs) {
    case 6:
        return 1.92e6;
    case 15:
        return 3.84e6;
    case 25:
        return 5.76e6;
    case 50:
        return 11.52e6;
    case 75:
        return 15.36e6;
    case 100:
        return 23.04e6;
    default:
        cerr << "** Invalid sample rate selection" << endl;
    }

    return 0.0;
}

template <typename T>
void UHDDevice<T>::shiftFreq(double offset)
{
    uhd::tune_request_t treq(_offset_freq + offset);
    treq.rf_freq_policy = uhd::tune_request_t::POLICY_MANUAL;
    treq.rf_freq = _base_freq;

    ostringstream ost;

    try {
        for (size_t i = 0; i < _chans; i++)
            _dev->set_rx_freq(treq, i);

        _offset_freq = _dev->get_rx_freq();
    } catch (const exception &ex) {
        ost << "DEV   : Frequency setting failed - " << ex.what();
        LOG_ERR(ost.str().c_str());
    }

    ost << "DEV   : Adjusting DDC " << offset << " Hz"
        << ", DDC offset " << _base_freq - _offset_freq  << " Hz";
    LOG_DEV(ost.str().c_str());
}

template <typename T>
int64_t UHDDevice<T>::get_ts_high()
{
    return _rx_bufs.front()->get_last_time();
}

template <typename T>
int64_t UHDDevice<T>::get_ts_low()
{
    return _rx_bufs.front()->get_first_time();
}

template <typename T>
void UHDDevice<T>::start()
{
    uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    cmd.stream_now = true;
    _dev->issue_stream_cmd(cmd);
    last = 0;
}

template <typename T>
int UHDDevice<T>::reload()
{
    int rc;
    uhd::rx_metadata_t md;
    size_t total = 0;

    vector<vector<T>> pkt_bufs(_chans, vector<T>(_spp));
    vector<T *> pkt_ptrs;
    for (auto &p : pkt_bufs) pkt_ptrs.push_back(p.data());

    for (;;) {
        size_t num = _stream->recv(pkt_ptrs, _spp, md, 1.0, true);
        if (num <= 0) {
            cout << "Receive timed out " <<  endl;
            continue;
        } else if (num < _spp) {
            cout << "Short packet" <<  endl;
        }

        total += num;
        int64_t ts = md.time_spec.to_ticks(_rate);

        if (last) {

        if (ts < last) {
            cout << "ts   : " << ts << endl;
            cout << "last : " << last << endl;
            cout << "Non-monotonic TIME" << endl;
            exit(1);
            continue;
        }

        if ((size_t) (ts - last) != _spp) {
            cout << "UHD Timestamp Jump " << ts << endl;
            cout << "last     : " << last << endl;
            cout << "expected : " << _spp << endl;
            cout << "got      : " << ts - last << endl;
        }

        auto b = begin(_rx_bufs);
        for (auto &p : pkt_ptrs) {
            rc = (*b++)->write(p, num, ts);
            if (rc < 0) {
                if (rc == -2) {
                    cout << "Internal overflow" << endl;
                    continue;
                }

                cout << "Fatal buffer reload error " << rc << endl;
                cout << "ts   : " << ts << endl;
                cout << "last : " << last << endl;
                exit(1);
            }
        }
        }

        last = ts;

        if (total >= _spp)
            break;
    }

    return 0;
}

template <typename T>
int UHDDevice<T>::pull(vector<vector<T>> &bufs,
                    size_t len, int64_t ts)
{
    int err;

    if (bufs.size() != _chans) {
        cerr << "UHD: Invalid buffer " << bufs.size() << endl;
        return -1;
    }

    if (_rx_bufs.front()->avail_smpls(ts) < len) {
        cerr << "Insufficient samples in buffer " << endl;
        return -1;
    }

    auto d = begin(_rx_bufs);
    for (auto &b : bufs)
        (*d++)->read(b, ts);

    return len;
}

template <typename T>
double UHDDevice<T>::setGain(double gain)
{
    cout << "-- Setting gain to " << gain << " dB" << endl;
    try {
        for (size_t i = 0; i < _chans; i++)
            _dev->set_rx_gain(gain, i);
    } catch (const exception &ex) {
        cerr << "** Gain setting failed" << endl;
        cerr << ex.what() << endl;
    }

    return _dev->get_rx_gain();
}

template <typename T>
bool UHDDevice<T>::initRates(int rbs)
{
    double mcr, rate = get_rate(rbs);
    if (rate == 0.0)
        return false;

    cout << "-- Setting rates to " << rate << " Hz" << endl;
    try {
        if (_type != DEV_TYPE_X300) {
            mcr = 32 * rate;
            while (mcr > 61.44e6 / _chans) mcr /= 2.0;

            _dev->set_master_clock_rate(mcr);
        }
        _dev->set_rx_rate(rate);
    } catch (const exception &ex) {
        cerr << "** Sample rate setting failed" << endl;
        cerr << ex.what() << endl;
    }

    _rate = _dev->get_rx_rate();
    return true;
}

template <typename T>
void UHDDevice<T>::initRx(int64_t &ts)
{
    uhd::stream_args_t stream_args;

    if (sizeof(T) == sizeof(complex<short>))
        stream_args = uhd::stream_args_t("sc16", "sc16");
    else if (sizeof(T) == sizeof(complex<float>))
        stream_args = uhd::stream_args_t("fc32", "sc16");
    else
        throw invalid_argument("");

    _rx_bufs.resize(_chans);

    for (size_t i = 0; i < _chans; i++) {
        stream_args.channels.push_back(i);
        _rx_bufs[i] = make_shared<TSBuffer>(RX_BUFLEN);
    }

    _stream = _dev->get_rx_stream(stream_args);
    _spp = _stream->get_max_num_samps();
    cout << "-- Samples per packet " << _spp << endl;

    vector<vector<T>> pkt_bufs(_chans, vector<T>(_spp));
    vector<T *> pkt_ptrs;
    for (auto &p : pkt_bufs) pkt_ptrs.push_back(p.data());

    uhd::rx_metadata_t md;
    _stream->recv(pkt_ptrs, _spp, md, 0.1, true);

    ts = _dev->get_time_now().to_ticks(_rate);
    for (auto &r : _rx_bufs)
        r->write(ts);
}

template <typename T>
void UHDDevice<T>::setFreq(double freq)
{
    uhd::tune_request_t treq(freq);
    uhd::tune_result_t tres;

    cout << "-- Setting frequency to " << freq << " Hz" << endl;
    try {
        for (size_t i = 0; i < _chans; i++)
            tres = _dev->set_rx_freq(treq, i);

        _base_freq = tres.actual_rf_freq;

        treq.rf_freq_policy = uhd::tune_request_t::POLICY_MANUAL;
        treq.rf_freq = _base_freq;

        for (size_t i = 0; i < _chans; i++)
            _dev->set_rx_freq(treq, i);
    } catch (const exception &ex) {
        cerr << "** Frequency setting failed" << endl;
        cerr << ex.what() << endl;
    }

    _offset_freq = _base_freq;
}

template <typename T>
void UHDDevice<T>::stop()
{
    uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
    _dev->issue_stream_cmd(cmd);

    vector<vector<T>> pkt_bufs(_chans, vector<T>(_spp));
    vector<T *> pkt_ptrs;
    for (auto &p : pkt_bufs) pkt_ptrs.push_back(p.data());

    uhd::rx_metadata_t md;
    while (_stream->recv(pkt_ptrs, _spp, md, 0.1, true) > 0);
}

template <typename T>
void UHDDevice<T>::reset()
{
    stop();
    last = 0;
}

template <typename T>
UHDDevice<T>::UHDDevice(size_t chans)
  : _type(DEV_TYPE_UNKNOWN), _chans(chans)
{
}

template <typename T>
UHDDevice<T>::~UHDDevice()
{
    stop();
}

template class UHDDevice<complex<short>>;
template class UHDDevice<complex<float>>;
