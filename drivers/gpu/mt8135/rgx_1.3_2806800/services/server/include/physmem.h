/*************************************************************************/ /*!
@File
@Title          Physmem header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for common entry point for creation of RAM backed PMR's
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

#ifndef _SRVSRV_PHYSMEM_H_
#define _SRVSRV_PHYSMEM_H_

/* include/ */
#include "img_types.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"

/* services/server/include/ */
#include "pmr.h"
#include "pmr_impl.h"

/*
 * PhysmemNewRamBackedPMR
 *
 * This function will create a RAM backed PMR using the device specific
 * callback, this allows control at a per-devicenode level to select the
 * memory source thus supporting mixed UMA/LMA systems.
 *
 * The size must be a multiple of page size.  The page size is
 * specified in log2.  It should be regarded as a minimum contiguity
 * of which the that the resulting memory must be a multiple.  It may
 * be that this should be a fixed number.  It may be that the
 * allocation size needs to be a multiple of some coarser "page size"
 * than that specified in the page size argument.  For example, take
 * an OS whose page granularity is a fixed 16kB, but the caller
 * requests memory in page sizes of 4kB.  The request can be satisfied
 * if and only if the SIZE requested is a multiple of 16kB.  If the
 * arguments supplied are such that this OS cannot grant the request,
 * PVRSRV_ERROR_INVALID_PARAMS will be returned.
 *
 * The caller should supply storage of a pointer.  Upon successful
 * return a PMR object will have been created and a pointer to it
 * returned in the PMROut argument.
 *
 * A PMR thusly created should be destroyed with PhysmemUnrefPMR.
 *
 * Note that this function may cause memory allocations and on some
 * OSes this may cause scheduling events, so it is important that this
 * function be called with interrupts enabled and in a context where
 * scheduling events and memory allocations are permitted.
 *
 * The flags may be used by the implementation to change its behaviour
 * if required.  The flags will also be stored in the PMR as immutable
 * metadata and returned to mmu_common when it asks for it.
 *
 */
extern PVRSRV_ERROR
PhysmemNewRamBackedPMR(PVRSRV_DEVICE_NODE *psDevNode,
					   IMG_DEVMEM_SIZE_T uiSize,
					   IMG_DEVMEM_SIZE_T uiChunkSize,
					   IMG_UINT32 ui32NumPhysChunks,
					   IMG_UINT32 ui32NumVirtChunks,
					   IMG_BOOL *pabMappingTable,
					   IMG_UINT32 uiLog2PageSize,
					   PVRSRV_MEMALLOCFLAGS_T uiFlags,
					   PMR **ppsPMROut);

#endif /* _SRVSRV_PHYSMEM_H_ */
