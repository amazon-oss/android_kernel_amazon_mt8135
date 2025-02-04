/*************************************************************************/ /*!
@File
@Title          Functions for creating and reading proc filesystem entries.
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

#ifndef __PROCESS_STATS_H__
#define __PROCESS_STATS_H__

#include "pvrsrv_error.h"


/*
 *  The publishing of Process Stats is controlled by the
 *  PVRSRV_ENABLE_PROCESS_STATS build option. The recording of all Memory
 *  allocations is controlled by the PVRSRV_ENABLE_MEMORY_STATS build option.
 *
 *  Note: There will be a performance degradation with memory allocation
 *        recording enabled!
 */


/*
 *  Memory types which can be tracked...
 */
typedef enum {
	PVRSRV_MEM_ALLOC_TYPE_KMALLOC,
	PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES,
	PVRSRV_MEM_ALLOC_TYPE_IOREMAP,
	PVRSRV_MEM_ALLOC_TYPE_VMAP,

	/* Must be the last enum...*/
	PVRSRV_MEM_ALLOC_TYPE_COUNT
} PVRSRV_MEM_ALLOC_TYPE;


/*
 * Functions for managing the processes recorded...
 */
PVRSRV_ERROR  PVRSRVStatsInitialise(IMG_VOID);

IMG_VOID  PVRSRVStatsDestroy(IMG_VOID);

PVRSRV_ERROR  PVRSRVStatsRegisterProcess(IMG_HANDLE* phProcessStats);

IMG_VOID  PVRSRVStatsDeregisterProcess(IMG_HANDLE hProcessStats);


/*
 * Functions for recording the statistics...
 */
IMG_VOID  PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE eAllocType,
									   IMG_VOID *pvCpuVAddr,
									   IMG_CPU_PHYADDR sCpuPAddr,
									   IMG_SIZE_T uiBytes,
									   IMG_PVOID pvPrivateData);

IMG_VOID  PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE eAllocType,
										  IMG_VOID *pvCpuVAddr);

IMG_VOID  PVRSRVStatsUpdateRenderContextStats(IMG_UINT32 ui32TotalNumPartialRenders,
											  IMG_UINT32 ui32TotalNumOutOfMemory,
											  IMG_UINT32 ui32TotalTAStores,
											  IMG_UINT32 ui32Total3DStores,
											  IMG_UINT32 ui32TotalSHStores);

IMG_VOID  PVRSRVStatsUpdateZSBufferStats(IMG_UINT32 ui32NumReqByApp,
										 IMG_UINT32 ui32NumReqByFW);

IMG_VOID  PVRSRVStatsUpdateFreelistStats(IMG_UINT32 ui32NumGrowReqByApp,
										 IMG_UINT32 ui32NumGrowReqByFW,
										 IMG_UINT32 ui32InitFLPages,
										 IMG_UINT32 ui32NumHighPages);


/*
 *  Functions for obtaining the information stored...
 */
IMG_BOOL  PVRSRVStatsObtainElement(IMG_PVOID pvStatPtr,
								   IMG_UINT32 ui32StatNumber,
								   IMG_INT32* pi32StatData,
								   IMG_CHAR** ppszStatFmtText);

#endif /* __PROCESS_STATS_H__ */
