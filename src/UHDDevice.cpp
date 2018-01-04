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

#include <map>
#include <iostream>
#include <iomanip>
#include <stdint.h>

#include "UHDDevice.h"

extern "C" {
#include "lte/log.h"
}

#define RX_BUFLEN        (1 << 22)

using namespace std;

template <typename T>
void UHDDevice<T>::init(int64_t &ts, size_t rbs,
                        int ref, const string &args)
{
    uhd::device_addr_t addr(args);
    uhd::device_addrs_t addrs = uhd::device::find(addr);
    if (!addrs.size())
        throw runtime_error("No UHD device found");

    ostringstream ost;
    ost << "DEV   : " << "Opening device " << addrs.front().to_string();
    LOG_DEV(ost.str().c_str());

    auto parseDeviceType = [](const auto addr)->DeviceType {
        const map<string, DeviceType> devMap {
            { "B200", DEV_B200 },
            { "B210", DEV_B210 },
            { "X300", DEV_X300 },
            { "X310", DEV_X310 },
            { "N300", DEV_N300 },
            { "N310", DEV_N310 },
        };

        for (auto dev : devMap) {
            if (addr.to_string().find(dev.first) != string::npos)
                return dev.second;
        }
        return DEV_UNKNOWN;
    };

    _type = parseDeviceType(addrs.front());
    if (_type == DEV_UNKNOWN) {
        LOG_DEV_ERR("Unknown or unsupported device");
    }

    const map<DeviceType, string> argsMap {
        { DEV_B200, "" },
        { DEV_B210, "" },
        { DEV_X300, "master_clock_rate=184.32e6" },
        { DEV_X310, "master_clock_rate=184.32e6" },
        { DEV_N300, "master_clock_rate=122.88e6" },
        { DEV_N310, "master_clock_rate=122.88e6" },
    };

    try {
        _dev = uhd::usrp::multi_usrp::make(uhd::device_addr_t(args + argsMap.at(_type)));
    } catch (const exception &ex) {
        throw runtime_error("UHD device construction failed");
    }

    if (_chans > 1)
        _dev->set_time_unknown_pps(uhd::time_spec_t());

    switch (ref) {
    case REF_EXTERNAL:
        _dev->set_clock_source("external");
        break;
    case REF_GPS:
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
    _prev_ts = 0;
}

template <typename T>
int UHDDevice<T>::reload()
{
    uhd::rx_metadata_t md;
    size_t total = 0;

    vector<vector<T>> pkt_bufs(_chans, vector<T>(_spp));
    vector<T *> pkt_ptrs;
    for (auto &p : pkt_bufs) pkt_ptrs.push_back(p.data());

    for (;;) {
        size_t num = _stream->recv(pkt_ptrs, _spp, md, 1.0, true);
        if (num <= 0) {
            LOG_DEV_ERR("Receive timed out");
            _prev_ts = 0;
            continue;
        } else if (num < _spp) {
            LOG_DEV_ERR("Received short packet");
            _prev_ts = 0;
        }

        total += num;
        int64_t ts = md.time_spec.to_ticks(_rate);

        if (_prev_ts) {
            if (ts < _prev_ts)
                throw runtime_error("Non-monotonic timestamps detected");

            if ((size_t) (ts - _prev_ts) == _spp - 1) {
                ostringstream ost;
                ost << "DEV   : " << "Correcting UHD timestamp slip - "
                                  << "Expected " << _spp << " samples, "
                                  << "but read " << ts - _prev_ts;
                LOG_ERR(ost.str().c_str());
                ts++;
            }

            auto b = begin(_rx_bufs);
            for (auto &p : pkt_ptrs) {
                int rc = (*b++)->write(p, num, ts);
                if (rc == -TimestampBuffer<T>::ERR_OVERFLOW) {
                    ostringstream ost;
                    ost << "DEV   : " << "Internal buffer overflow";
                    LOG_ERR(ost.str().c_str());
                    continue;
                }
            }
        }

        _prev_ts = ts;

        if (total >= _spp)
            break;
    }

    return 0;
}

template <typename T>
int UHDDevice<T>::pull(vector<vector<T>> &bufs, size_t len, int64_t ts)
{
    int err;

    if (bufs.size() != _chans)
        throw out_of_range("");

    if (_rx_bufs.front()->avail_smpls(ts) < len)
        throw length_error("");

    auto d = begin(_rx_bufs);
    for (auto &b : bufs)
        (*d++)->read(b, ts);

    return len;
}

template <typename T>
double UHDDevice<T>::setGain(double gain)
{
    ostringstream ost;
    ost << "DEV   : " << "Setting gain to " << gain << " dB";
    LOG_DEV(ost.str().c_str());
    try {
        for (size_t i = 0; i < _chans; i++)
            _dev->set_rx_gain(gain, i);
    } catch (const exception &ex) {
        ostringstream ost;
        ost << "DEV   : " << "Gain setting failed " << ex.what();
        LOG_ERR(ost.str().c_str());
    }

    return _dev->get_rx_gain();
}

template <typename T>
bool UHDDevice<T>::initRates(int rbs)
{
    const map<int, double> rateMapRadix2 {
        { 6,   1.92e6 },
        { 15,  3.84e6 },
        { 25,  7.68e6 },
        { 50,  15.36e6 },
        { 75,  15.36e6 },
        { 100, 30.72e6 },
    };
    const map<int, double> rateMapRadix3 {
        { 6,   1.92e6 },
        { 15,  3.84e6 },
        { 25,  5.76e6 },
        { 50,  11.52e6 },
        { 75,  15.36e6 },
        { 100, 23.04e6 },
    };

    double targetRate;
    try {
        if (_type == DEV_N300 || _type == DEV_N310)
            targetRate = rateMapRadix2.at(rbs);
        else
            targetRate = rateMapRadix3.at(rbs);
    } catch (const exception &ex) {
        ostringstream ost;
        ost << "DEV   : " << "Invalid resource block selection";
        LOG_ERR(ost.str().c_str());
        return false;
    }

    ostringstream ost;
    ost << "DEV   : " << "Setting rate to " << targetRate/1e6 << " MHz";
    LOG_DEV(ost.str().c_str());
    try {
        if (_type == DEV_B200 || _type == DEV_B210) {
             double mcr = targetRate;
             if (mcr < 5e6)
                 while (mcr < 30.72e6 / _chans) mcr *= 2.0;
             _dev->set_master_clock_rate(mcr);
        }
        _dev->set_rx_rate(targetRate);
        _rate = _dev->get_rx_rate();
    } catch (const exception &ex) {
        ostringstream ost;
        ost << "DEV   : " << "Rate setting failed " << ex.what();
        LOG_ERR(ost.str().c_str());
        return false;
    }
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
        throw runtime_error("Unsupported sample type");

    _rx_bufs.resize(_chans);

    for (size_t i = 0; i < _chans; i++) {
        stream_args.channels.push_back(i);
        _rx_bufs[i] = make_shared<TSBuffer>(RX_BUFLEN);
    }

    _stream = _dev->get_rx_stream(stream_args);
    _spp = _stream->get_max_num_samps();

    ostringstream ost;
    ost << "DEV   : " << "Setting samples per packet to " << _spp;
    LOG_DEV(ost.str().c_str());

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

    ostringstream ost;
    ost << "DEV   : " << "Setting RF frequency to " << freq/1e6 << " MHz";
    LOG_DEV(ost.str().c_str());

    try {
        for (size_t i = 0; i < _chans; i++)
            tres = _dev->set_rx_freq(treq, i);

        _base_freq = tres.actual_rf_freq;

        treq.rf_freq_policy = uhd::tune_request_t::POLICY_MANUAL;
        treq.rf_freq = _base_freq;

        for (size_t i = 0; i < _chans; i++)
            _dev->set_rx_freq(treq, i);
    } catch (const exception &ex) {
        ost << "DEV   : " << "RF frequency setting failed " << ex.what();
        LOG_ERR(ost.str().c_str());
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
    _prev_ts = 0;
}

template <typename T>
bool UHDDevice<T>::supportRadix3() const
{
    switch (_type) {
    case DEV_N300:
    case DEV_N310:
    case DEV_UNKNOWN:
        return false;
    default:
        return true;
    };
}

template <typename T>
UHDDevice<T>::UHDDevice(size_t chans)
  : _type(DEV_UNKNOWN), _chans(chans)
{
}

template <typename T>
UHDDevice<T>::~UHDDevice()
{
    stop();
}

template class UHDDevice<complex<short>>;
template class UHDDevice<complex<float>>;
