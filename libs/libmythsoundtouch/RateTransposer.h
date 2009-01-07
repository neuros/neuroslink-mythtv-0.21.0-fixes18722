////////////////////////////////////////////////////////////////////////////////
/// 
/// Sample rate transposer. Changes sample rate by using linear interpolation 
/// together with anti-alias filtering (first order interpolation with anti-
/// alias filtering should be quite adequate for this application).
///
/// Use either of the derived classes of 'RateTransposerInteger' or 
/// 'RateTransposerFloat' for corresponding integer/floating point tranposing
/// algorithm implementation.
///
/// Author        : Copyright (c) Olli Parviainen
/// Author e-mail : oparviai @ iki.fi
/// SoundTouch WWW: http://www.iki.fi/oparviai/soundtouch
///
////////////////////////////////////////////////////////////////////////////////
//
// Last changed  : $Date: 2004-11-13 16:29:45 -0600 (Sat, 13 Nov 2004) $
// File revision : $Revision: 4714 $
//
// $Id: RateTransposer.h 4714 2004-11-13 22:29:45Z ijr $
//
////////////////////////////////////////////////////////////////////////////////
//
// License :
//
//  SoundTouch audio processing library
//  Copyright (c) Olli Parviainen
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2.1 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
////////////////////////////////////////////////////////////////////////////////

#ifndef RateTransposer_H
#define RateTransposer_H

#include "AAFilter.h"
#include "FIFOSamplePipe.h"
#include "FIFOSampleBuffer.h"

#include "STTypes.h"

namespace soundtouch
{

/// A common linear samplerate transposer class.
///
/// Note: Use function "RateTransposer::newInstance()" to create a new class 
/// instance instead of the "new" operator; that function automatically 
/// chooses a correct implementation depending on if integer or floating 
/// arithmetics are to be used.
class RateTransposer : public FIFOProcessor
{
protected:
    /// Anti-alias filter object
    AAFilter *pAAFilter;

    float fRate;

    uint uChannels;

    /// Buffer for collecting samples to feed the anti-alias filter between
    /// two batches
    FIFOSampleBuffer storeBuffer;

    /// Buffer for keeping samples between transposing & anti-alias filter
    FIFOSampleBuffer tempBuffer;

    /// Output sample buffer
    FIFOSampleBuffer outputBuffer;

    BOOL bUseAAFilter;

    void init();

    virtual void resetRegisters() = 0;

    virtual uint transposeStereo(soundtouch::SAMPLETYPE *dest, 
                         const soundtouch::SAMPLETYPE *src, 
                         uint numSamples) = 0;
    virtual uint transposeMono(soundtouch::SAMPLETYPE *dest, 
                       const soundtouch::SAMPLETYPE *src, 
                       uint numSamples) = 0;
    uint transpose(soundtouch::SAMPLETYPE *dest, 
                   const soundtouch::SAMPLETYPE *src, 
                   uint numSamples);

    void flushStoreBuffer();

    void downsample(const soundtouch::SAMPLETYPE *src, 
                    uint numSamples);
    void upsample(const soundtouch::SAMPLETYPE *src, 
                 uint numSamples);

    /// Transposes sample rate by applying anti-alias filter to prevent folding. 
    /// Returns amount of samples returned in the "dest" buffer.
    /// The maximum amount of samples that can be returned at a time is set by
    /// the 'set_returnBuffer_size' function.
    void processSamples(const soundtouch::SAMPLETYPE *src, 
                        uint numSamples);


public:
    RateTransposer();
    virtual ~RateTransposer();

    /// Operator 'new' is overloaded so that it automatically creates a suitable instance 
    /// depending on if we're to use integer or floating point arithmetics.
    void *operator new(size_t s);

    /// Use this function instead of "new" operator to create a new instance of this class. 
    /// This function automatically chooses a correct implementation, depending on if 
    /// integer ot floating point arithmetics are to be used.
    static RateTransposer *newInstance();

    /// Returns the output buffer object
    FIFOSamplePipe *getOutput() { return &outputBuffer; };

    /// Returns the store buffer object
    FIFOSamplePipe *getStore() { return &storeBuffer; };

    /// Return anti-alias filter object
    AAFilter *getAAFilter() const;

    /// Enables/disables the anti-alias filter. Zero to disable, nonzero to enable
    void enableAAFilter(BOOL newMode);

    /// Returns nonzero if anti-alias filter is enabled.
    BOOL isAAFilterEnabled() const;

    /// Sets new target rate. Normal rate = 1.0, smaller values represent slower 
    /// rate, larger faster rates.
    virtual void setRate(float newRate);

    /// Sets the number of channels, 1 = mono, 2 = stereo
    void setChannels(uint channels);

    /// Adds 'numSamples' pcs of samples from the 'samples' memory position into
    /// the input of the object.
    void putSamples(const soundtouch::SAMPLETYPE *samples, uint numSamples);

    /// Clears all the samples in the object
    void clear();

    /// Returns nonzero if there aren't any samples available for outputting.
    uint isEmpty();
};

}

#endif
