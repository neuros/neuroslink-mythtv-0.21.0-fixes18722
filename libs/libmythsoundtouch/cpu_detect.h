////////////////////////////////////////////////////////////////////////////////
///
/// A header file for detecting the Intel MMX instructions set extension.
///
/// Please see 'mmx_win.cpp', 'mmx_cpp.cpp' and 'mmx_non_x86.cpp' for the 
/// routine implementations for x86 Windows, x86 gnu version and non-x86 
/// platforms, respectively.
///
/// Author        : Copyright (c) Olli Parviainen
/// Author e-mail : oparviai @ iki.fi
/// SoundTouch WWW: http://www.iki.fi/oparviai/soundtouch
///
////////////////////////////////////////////////////////////////////////////////
//
// Last changed  : $Date: 2006-03-16 04:00:28 -0600 (Thu, 16 Mar 2006) $
// File revision : $Revision: 9376 $
//
// $Id: cpu_detect.h 9376 2006-03-16 10:00:28Z ijr $
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

#ifndef _CPU_DETECT_H_
#define _CPU_DETECT_H_

#include "STTypes.h"

#define MM_MMX    0x0001 /* standard MMX */
#define MM_3DNOW  0x0004 /* AMD 3DNOW */
#define MM_MMXEXT 0x0002 /* SSE integer functions or AMD MMX ext */
#define MM_SSE    0x0008 /* SSE functions */
#define MM_SSE2   0x0010 /* PIV SSE2 functions */
#define MM_3DNOWEXT  0x0020 /* AMD 3DNowExt */

/// Checks which instruction set extensions are supported by the CPU.
///
/// \return A bitmask of supported extensions, see SUPPORT_... defines.
uint detectCPUextensions(void);

/// Disables given set of instruction extensions. See SUPPORT_... defines.
void disableExtensions(uint wDisableMask);

#endif  // _CPU_DETECT_H_
