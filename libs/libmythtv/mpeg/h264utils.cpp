// -*- Mode: c++ -*-
/*******************************************************************
 * h264utils
 *
 * Copyright (C) Her Majesty the Queen in Right of Canada, 2006
 * Communications Research Centre (CRC)
 *
 * Distributed as part of MythTV (www.mythtv.org)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Contact:
 * Francois Lefebvre <francois [dot] lefebvre [at] crc [dot] ca>
 * Web: http://www.crc.ca
 *
 * 2006/04 Jean-Francois Roy for CRC
 *    Initial release
 *
 ********************************************************************/

// C headers
#include <cstring>

// C++ headers
#include <iostream>

// MythTV headers
#include "h264utils.h"

extern "C" {
// from libavcodec
extern const uint8_t *ff_find_start_code(const uint8_t * p, const uint8_t *end, uint32_t * state);
}

namespace H264
{

KeyframeSequencer::KeyframeSequencer() /* throw() */
{
    Reset();
}

void KeyframeSequencer::Reset(void) /* throw() */
{
    errored = false;
    state_changed = false;

    sync_accumulator = 0xffffffff;
    sync_stream_offset = 0;

    first_NAL_byte = H264::NALUnitType::UNKNOWN;

    saw_AU_delimiter = false;
    saw_first_VCL_NAL_unit = false;
    saw_sps = false;

    did_evaluate_once = false;
    keyframe = false;
    keyframe_sync_stream_offset = 0;
}

void KeyframeSequencer::KeyframePredicate(
    const uint8_t new_first_NAL_byte) /* throw() */
{
    // new_NAL_type will be current after this method executes
    uint8_t new_NAL_type = new_first_NAL_byte & 0x1f;
    uint8_t current_NAL_type = first_NAL_byte & 0x1f;

    // stage 1: if we're starting a new AU, save the offset
    //          in case it's an IDR AU
    if ((saw_first_VCL_NAL_unit || !did_evaluate_once) &&
        !saw_AU_delimiter)
    {
        did_evaluate_once = true;

        // stage 1.1: this particular check follows
        //            ITU-T Rec. H.264 (03/2005)
        if (new_NAL_type     == NALUnitType::AU_DELIMITER ||
            current_NAL_type == NALUnitType::END_SEQUENCE)
        {
            saw_first_VCL_NAL_unit = false;
            saw_AU_delimiter = true;
            keyframe_sync_stream_offset = sync_stream_offset;
        }

        /* stage 1.2 is a hack. The correct method would be to
         * write down when see those types (and others, such as
         * a VCL NAL unit), wait until the next VCL NAL unit,
         * and compare it to the last seen VCL NAL unit according
         * to ITU-T Rec. H.264 (03/2005) 7.4.1.2.4
         */

        // stage 1.2: HACK: this is a fugly guesstimate based on
        //            Figure 7-1 in 7.4.1.2.3
        if (new_NAL_type == NALUnitType::SEI || 
            new_NAL_type == NALUnitType::SPS ||
            new_NAL_type == NALUnitType::PPS || 
            (new_NAL_type > NALUnitType::SPS_EXT &&
             new_NAL_type < NALUnitType::AUXILIARY_SLICE))
        {
            saw_first_VCL_NAL_unit = false;
            saw_AU_delimiter = true;
            keyframe_sync_stream_offset = sync_stream_offset;
        }
    }

    // stage 2: determine if it's an IDR AU
    if (!saw_first_VCL_NAL_unit && !saw_sps && new_NAL_type == NALUnitType::SPS)
    {
        saw_sps = true;
        state_changed = true;
        keyframe = false;
    }

    // stage 3: did we see the AU's first VCL NAL unit yet?
    if (!saw_first_VCL_NAL_unit && NALUnitType::IsVCLType(new_NAL_type))
    {
        saw_first_VCL_NAL_unit = true;
        saw_AU_delimiter = false;
        state_changed = true;
        if (saw_sps)
            keyframe = true;
        saw_sps = false;
    }
}

uint32_t KeyframeSequencer::AddBytes(
    const uint8_t  *bytes,
    const uint32_t  byte_count,
    const int64_t   stream_offset) /* throw() */
{
    const uint8_t *local_bytes = bytes;
    const uint8_t *local_bytes_end = bytes + byte_count;

    state_changed = false;

    while (local_bytes < local_bytes_end)
    {
        local_bytes = ff_find_start_code(local_bytes, local_bytes_end,
                                         &sync_accumulator);

        if ((sync_accumulator & 0xffffff00) == 0x00000100)
        {
            uint8_t k = *(local_bytes-1);
            sync_stream_offset = stream_offset;
            keyframe = false;

            KeyframePredicate(k);
            first_NAL_byte = k;

            return local_bytes - bytes;
        }
    }

    return local_bytes - bytes;
}

bool KeyframeSequencer::IsOnFrame(void) const /* throw() */
{
    // HACK: This could be improved, but it does work if you're
    //       interested in knowing when you just hit a frame
    //       (as opposed to still being on one).
    return saw_first_VCL_NAL_unit;
}

} // namespace H264
