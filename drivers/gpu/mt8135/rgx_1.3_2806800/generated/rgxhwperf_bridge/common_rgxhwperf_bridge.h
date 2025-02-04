/*************************************************************************/ /*!
@File
@Title          Common bridge header for rgxhwperf
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures that are used by both
				the client and sever side of the bridge for rgxhwperf
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

#ifndef COMMON_RGXHWPERF_BRIDGE_H
#define COMMON_RGXHWPERF_BRIDGE_H

#include "rgx_bridge.h"
#include "rgx_hwperf_km.h"


#include "pvr_bridge.h"

#define PVRSRV_BRIDGE_RGXHWPERF_CMD_FIRST			(PVRSRV_BRIDGE_RGXHWPERF_START)
#define PVRSRV_BRIDGE_RGXHWPERF_RGXCTRLHWPERF			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXHWPERF_CMD_FIRST+0)
#define PVRSRV_BRIDGE_RGXHWPERF_RGXCONFIGENABLEHWPERFCOUNTERS			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXHWPERF_CMD_FIRST+1)
#define PVRSRV_BRIDGE_RGXHWPERF_RGXCTRLHWPERFCOUNTERS			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXHWPERF_CMD_FIRST+2)
#define PVRSRV_BRIDGE_RGXHWPERF_CMD_LAST			(PVRSRV_BRIDGE_RGXHWPERF_CMD_FIRST+2)


/*******************************************
			RGXCtrlHWPerf
 *******************************************/

/* Bridge in structure for RGXCtrlHWPerf */
typedef struct PVRSRV_BRIDGE_IN_RGXCTRLHWPERF_TAG
{
	IMG_HANDLE hDevNode;
	IMG_BOOL bEnable;
	IMG_UINT64 ui64Mask;
} PVRSRV_BRIDGE_IN_RGXCTRLHWPERF;


/* Bridge out structure for RGXCtrlHWPerf */
typedef struct PVRSRV_BRIDGE_OUT_RGXCTRLHWPERF_TAG
{
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXCTRLHWPERF;

/*******************************************
			RGXConfigEnableHWPerfCounters
 *******************************************/

/* Bridge in structure for RGXConfigEnableHWPerfCounters */
typedef struct PVRSRV_BRIDGE_IN_RGXCONFIGENABLEHWPERFCOUNTERS_TAG
{
	IMG_HANDLE hDevNode;
	IMG_UINT32 ui32ArrayLen;
	RGX_HWPERF_CONFIG_CNTBLK * psBlockConfigs;
} PVRSRV_BRIDGE_IN_RGXCONFIGENABLEHWPERFCOUNTERS;


/* Bridge out structure for RGXConfigEnableHWPerfCounters */
typedef struct PVRSRV_BRIDGE_OUT_RGXCONFIGENABLEHWPERFCOUNTERS_TAG
{
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXCONFIGENABLEHWPERFCOUNTERS;

/*******************************************
			RGXCtrlHWPerfCounters
 *******************************************/

/* Bridge in structure for RGXCtrlHWPerfCounters */
typedef struct PVRSRV_BRIDGE_IN_RGXCTRLHWPERFCOUNTERS_TAG
{
	IMG_HANDLE hDevNode;
	IMG_BOOL bEnable;
	IMG_UINT32 ui32ArrayLen;
	IMG_UINT8 * pui8BlockIDs;
} PVRSRV_BRIDGE_IN_RGXCTRLHWPERFCOUNTERS;


/* Bridge out structure for RGXCtrlHWPerfCounters */
typedef struct PVRSRV_BRIDGE_OUT_RGXCTRLHWPERFCOUNTERS_TAG
{
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXCTRLHWPERFCOUNTERS;

#endif /* COMMON_RGXHWPERF_BRIDGE_H */
