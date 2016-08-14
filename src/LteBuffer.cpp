/*
 * Interthread Buffer Object
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

#include "LteBuffer.h"

LteBuffer::LteBuffer(unsigned chans)
 : buffers(chans), freqOffset(0.0), crcValid(false)
{
}

LteBuffer::LteBuffer(const LteBuffer &b)
  : cellId(b.cellId), rbs(b.rbs), ng(b.ng), txAntennas(b.txAntennas),
    fn(b.fn), sfn(b.sfn), freqOffset(b.freqOffset),
    crcValid(b.crcValid), buffers(b.buffers)
{
}

LteBuffer::LteBuffer(LteBuffer &&b)
  : cellId(b.cellId), rbs(b.rbs), ng(b.ng), txAntennas(b.txAntennas),
    fn(b.fn), sfn(b.sfn), freqOffset(b.freqOffset),
    crcValid(b.crcValid), buffers(move(b.buffers))
{
}

LteBuffer &LteBuffer::operator=(const LteBuffer &b)
{
    if (this != &b) {
        cellId  = b.cellId;
        rbs = b.rbs;
        ng = b.ng;
        txAntennas = b.txAntennas;
        fn = b.fn;
        sfn = b.sfn;
        freqOffset = b.freqOffset;
        crcValid = b.crcValid;
        buffers = b.buffers;
    }
}

LteBuffer &LteBuffer::operator=(LteBuffer &&b)
{
    if (this != &b) {
        cellId  = b.cellId;
        rbs = b.rbs;
        ng = b.ng;
        txAntennas = b.txAntennas;
        fn = b.fn;
        sfn = b.sfn;
        freqOffset = b.freqOffset;
        crcValid = b.crcValid;
        buffers = move(b.buffers);
    }
}
