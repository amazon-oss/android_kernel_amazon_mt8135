/*************************************************************************/ /*!
@File
@Title          Host memory management implementation for Linux
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

#include <linux/slab.h>

#include "img_defs.h"
#include "allocmem.h"
#include "process_stats.h"


IMG_INTERNAL IMG_PVOID OSAllocMem(IMG_UINT32 ui32Size)
{
	IMG_PVOID pvRet = kmalloc(ui32Size, GFP_KERNEL);

#if defined(PVRSRV_ENABLE_PROCESS_STATS) && defined(PVRSRV_ENABLE_MEMORY_STATS)
	if (pvRet != IMG_NULL)
	{
		IMG_CPU_PHYADDR sCpuPAddr;
		sCpuPAddr.uiAddr = 0;

		PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_KMALLOC,
									 pvRet,
									 sCpuPAddr,
									 ui32Size,
									 IMG_NULL);
   }
#endif

	return pvRet;
}

IMG_INTERNAL IMG_PVOID OSReAllocMem(IMG_PVOID pvPrev, IMG_UINT32 ui32Size)
{
	IMG_PVOID pvRet = krealloc(pvPrev, ui32Size, GFP_KERNEL);
	return pvRet;
}

IMG_INTERNAL IMG_PVOID OSAllocZMem(IMG_UINT32 ui32Size)
{
	IMG_PVOID pvRet = kzalloc(ui32Size, GFP_KERNEL);

#if defined(PVRSRV_ENABLE_PROCESS_STATS) && defined(PVRSRV_ENABLE_MEMORY_STATS)
	if (pvRet != IMG_NULL)
	{
		IMG_CPU_PHYADDR sCpuPAddr;
		sCpuPAddr.uiAddr = 0;

		PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_KMALLOC,
									 pvRet,
									 sCpuPAddr,
									 ui32Size,
									 IMG_NULL);
	}
#endif

	return pvRet;
}

IMG_INTERNAL IMG_VOID OSFreeMem(IMG_PVOID pvMem)
{
#if defined(PVRSRV_ENABLE_PROCESS_STATS) && defined(PVRSRV_ENABLE_MEMORY_STATS)
	if (pvMem != IMG_NULL)
	{
		PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_KMALLOC, pvMem);
	}
#endif

	kfree(pvMem);
}
