/*************************************************************************/ /*!
@Title          RGX Config BVNC 1.V.2.0
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef _RGXCONFIG_KM_1_V_2_0_H_
#define _RGXCONFIG_KM_1_V_2_0_H_

/***** Automatically generated file (1/28/2014 3:39:48 PM): Do not edit manually ********************/
/***** Timestamp:  (1/28/2014 3:39:48 PM)************************************************************/

#define RGX_BNC_KM_B 1
#define RGX_BNC_KM_N 2
#define RGX_BNC_KM_C 0

/******************************************************************************
 * DDK Defines
 *****************************************************************************/
#define RGX_FEATURE_NUM_CLUSTERS (2)
#define RGX_FEATURE_SLC_SIZE_IN_BYTES (128*1024)
#define RGX_FEATURE_PHYS_BUS_WIDTH (40)
#define RGX_FEATURE_AXI_ACELITE
#define RGX_FEATURE_SLC_CACHE_LINE_SIZE_BITS (512)
#define RGX_FEATURE_VIRTUAL_ADDRESS_SPACE_BITS (40)
#define RGX_FEATURE_META (MTP218)


#endif /* _RGXCONFIG_1_V_2_0_H_ */
