/*************************************************************************/ /*!
@File
@Title          RGX TA/3D routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX TA/3D routines
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
/* for the offsetof macro */
#include <stddef.h>

#include "pdump_km.h"
#include "pvr_debug.h"
#include "rgxutils.h"
#include "rgxfwutils.h"
#include "rgxta3d.h"
#include "rgxmem.h"
#include "allocmem.h"
#include "devicemem.h"
#include "devicemem_pdump.h"
#include "osfunc.h"
#include "pvrsrv.h"
#include "rgx_memallocflags.h"
#include "rgxccb.h"
#include "rgxhwperf.h"

#include "rgxdefs_km.h"
#include "rgx_fwif_km.h"
#include "physmem.h"
#include "sync_server.h"
#include "sync_internal.h"
#include "process_stats.h"

#if defined(PVR_ANDROID_NATIVE_WINDOW_HAS_SYNC)
#include "pvr_sync.h"
#endif /* defined(PVR_ANDROID_NATIVE_WINDOW_HAS_SYNC) */

typedef struct _DEVMEM_REF_LOOKUP_
{
	IMG_UINT32 ui32ZSBufferID;
	RGX_ZSBUFFER_DATA *psZSBuffer;
} DEVMEM_REF_LOOKUP;

typedef struct _DEVMEM_FREELIST_LOOKUP_
{
	IMG_UINT32 ui32FreeListID;
	RGX_FREELIST *psFreeList;
} DEVMEM_FREELIST_LOOKUP;

typedef struct {
	DEVMEM_MEMDESC				*psContextStateMemDesc;
	RGX_SERVER_COMMON_CONTEXT	*psServerCommonContext;
	IMG_UINT32					ui32Priority;
} RGX_SERVER_RC_TA_DATA;

typedef struct {
	DEVMEM_MEMDESC				*psContextStateMemDesc;
	RGX_SERVER_COMMON_CONTEXT	*psServerCommonContext;
	IMG_UINT32					ui32Priority;
} RGX_SERVER_RC_3D_DATA;

struct _RGX_SERVER_RENDER_CONTEXT_ {
	PVRSRV_DEVICE_NODE			*psDeviceNode;
	DEVMEM_MEMDESC				*psFWRenderContextMemDesc;
	DEVMEM_MEMDESC				*psFWFrameworkMemDesc;
	RGX_SERVER_RC_TA_DATA		sTAData;
	RGX_SERVER_RC_3D_DATA		s3DData;
	IMG_UINT32					ui32CleanupStatus;
#define RC_CLEANUP_TA_COMPLETE		(1 << 0)
#define RC_CLEANUP_3D_COMPLETE		(1 << 1)
	PVRSRV_CLIENT_SYNC_PRIM		*psCleanupSync;
	DLLIST_NODE					sListNode;
};


static
#ifdef __GNUC__
	__attribute__((noreturn))
#endif
void sleep_for_ever(void)
{
#if defined(__KLOCWORK__) // klocworks would report an infinite loop because of while(1).
	PVR_ASSERT(0);
#else
	while(1)
	{
		OSSleepms(~0); // sleep the maximum amount of time possible
	}
#endif
}


/*
	Static functions used by render context code
*/

static
PVRSRV_ERROR _DestroyTAContext(RGX_SERVER_RC_TA_DATA *psTAData,
							   PVRSRV_DEVICE_NODE *psDeviceNode,
							   PVRSRV_CLIENT_SYNC_PRIM *psCleanupSync)
{
	PVRSRV_ERROR eError;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psDeviceNode,
											  FWCommonContextGetFWAddress(psTAData->psServerCommonContext),
											  psCleanupSync,
											  RGXFWIF_DM_TA);
	if (eError == PVRSRV_ERROR_RETRY)
	{
		return eError;
	}
	else if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Unexpected error from RGXFWRequestCommonContextCleanUp (%s)",
				__FUNCTION__,
				PVRSRVGetErrorStringKM(eError)));
	}

	/* ... it has so we can free it's resources */
#if defined(DEBUG)
	/* Log the number of TA context stores which occurred */
	{
		RGXFWIF_TACTX_STATE	*psFWTAState;

		eError = DevmemAcquireCpuVirtAddr(psTAData->psContextStateMemDesc,
										  (IMG_VOID**)&psFWTAState);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"%s: Failed to map firmware render context state (%u)",
					__FUNCTION__, eError));
		}
		else
		{
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
			PVRSRVStatsUpdateRenderContextStats(0, 0, psFWTAState->ui32NumStores, 0, 0);
#endif

			/* Release the CPU virt addr */
			DevmemReleaseCpuVirtAddr(psTAData->psContextStateMemDesc);
		}
	}
#endif
	FWCommonContextFree(psTAData->psServerCommonContext);
	DevmemFwFree(psTAData->psContextStateMemDesc);
	return PVRSRV_OK;
}

static
PVRSRV_ERROR _Destroy3DContext(RGX_SERVER_RC_3D_DATA *ps3DData,
							   PVRSRV_DEVICE_NODE *psDeviceNode,
							   PVRSRV_CLIENT_SYNC_PRIM *psCleanupSync)
{
	PVRSRV_ERROR eError;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psDeviceNode,
											  FWCommonContextGetFWAddress(ps3DData->psServerCommonContext),
											  psCleanupSync,
											  RGXFWIF_DM_3D);
	if (eError == PVRSRV_ERROR_RETRY)
	{
		return eError;
	}
	else if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Unexpected error from RGXFWRequestCommonContextCleanUp (%s)",
				 __FUNCTION__,
				 PVRSRVGetErrorStringKM(eError)));
	}

	/* ... it has so we can free it's resources */
#if defined(DEBUG)
	/* Log the number of 3D context stores which occurred */
	{
		RGXFWIF_3DCTX_STATE	*psFW3DState;

		eError = DevmemAcquireCpuVirtAddr(ps3DData->psContextStateMemDesc,
										  (IMG_VOID**)&psFW3DState);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"%s: Failed to map firmware render context state (%u)",
					__FUNCTION__, eError));
		}
		else
		{
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
			PVRSRVStatsUpdateRenderContextStats(0, 0, 0, psFW3DState->ui32NumStores, 0);
#endif

			/* Release the CPU virt addr */
			DevmemReleaseCpuVirtAddr(ps3DData->psContextStateMemDesc);
		}
	}
#endif

	FWCommonContextFree(ps3DData->psServerCommonContext);
	DevmemFwFree(ps3DData->psContextStateMemDesc);
	return PVRSRV_OK;
}

static IMG_BOOL _RGXDumpPMRPageList(PDLLIST_NODE psNode, IMG_PVOID pvCallbackData)
{
	RGX_PMR_NODE *psPMRNode = IMG_CONTAINER_OF(psNode, RGX_PMR_NODE, sMemoryBlock);
	PVRSRV_ERROR			eError;

	eError = PMRDumpPageList(psPMRNode->psPMR,
							RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"Error (%u) printing pmr %p", eError, psPMRNode->psPMR));
	}

	return IMG_TRUE;
}

IMG_BOOL RGXDumpFreeListPageList(RGX_FREELIST *psFreeList)
{
	PVR_LOG(("Freelist FWAddr 0x%08x, ID = %d, CheckSum 0x%016llx",
				psFreeList->sFreeListFWDevVAddr.ui32Addr,
				psFreeList->ui32FreelistID,
				psFreeList->ui64FreelistChecksum));

	/* Dump Init FreeList page list */
	PVR_LOG(("  Initial Memory block"));
	dllist_foreach_node(&psFreeList->sMemoryBlockInitHead,
					_RGXDumpPMRPageList,
					IMG_NULL);

	/* Dump Grow FreeList page list */
	PVR_LOG(("  Grow Memory blocks"));
	dllist_foreach_node(&psFreeList->sMemoryBlockHead,
					_RGXDumpPMRPageList,
					IMG_NULL);

	return IMG_TRUE;
}

static PVRSRV_ERROR _UpdateFwFreelistSize(RGX_FREELIST *psFreeList,
										IMG_BOOL bGrow,
										IMG_UINT32 ui32DeltaSize)
{
	PVRSRV_ERROR			eError;
	RGXFWIF_KCCB_CMD		sGPCCBCmd;

	sGPCCBCmd.eCmdType = (bGrow) ? RGXFWIF_KCCB_CMD_FREELIST_GROW_UPDATE : RGXFWIF_KCCB_CMD_FREELIST_SHRINK_UPDATE;
	sGPCCBCmd.uCmdData.sFreeListGSData.psFreeListFWDevVAddr = psFreeList->sFreeListFWDevVAddr.ui32Addr;
	sGPCCBCmd.uCmdData.sFreeListGSData.ui32DeltaSize = ui32DeltaSize;
	sGPCCBCmd.uCmdData.sFreeListGSData.ui32NewSize = psFreeList->ui32CurrentFLPages;

	PVR_DPF((PVR_DBG_MESSAGE, "Send FW update: freelist [FWAddr=0x%08x] has 0x%08x pages",
								psFreeList->sFreeListFWDevVAddr.ui32Addr,
								psFreeList->ui32CurrentFLPages));

	/* Submit command to the firmware.  */
	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		eError = RGXScheduleCommand(psFreeList->psDevInfo,
									RGXFWIF_DM_GP,
									&sGPCCBCmd,
									sizeof(sGPCCBCmd),
									IMG_TRUE);
		if (eError != PVRSRV_ERROR_RETRY)
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "_UpdateFwFreelistSize: failed to update FW freelist size. (error = %u)", eError));
		return eError;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR _FreeListCheckSum(RGX_FREELIST *psFreeList,
									   IMG_UINT64 *pui64CheckSum)
{
#if defined(NO_HARDWARE)
	/* No checksum needed as we have all information in the pdumps */
	PVR_UNREFERENCED_PARAMETER(psFreeList);
	*pui64CheckSum = 0;
	return PVRSRV_OK;
#else
	PVRSRV_ERROR eError;
	IMG_SIZE_T uiNumBytes;
	IMG_UINT8* pui8Buffer;
	IMG_UINT32* pui32Buffer;
	IMG_UINT32 ui32CheckSumAdd = 0;
	IMG_UINT32 ui32CheckSumXor = 0;
	IMG_UINT32 ui32Entry;
	IMG_UINT32 ui32Entry2;

	/* Allocate Buffer of the size of the freelist */
	pui8Buffer = OSAllocMem(psFreeList->ui32CurrentFLPages * sizeof(IMG_UINT32));
	if (pui8Buffer == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto _OSAllocMem_Exit;
	}

	/* Copy freelist content into Buffer */
	eError = PMR_ReadBytes(psFreeList->psFreeListPMR,
					psFreeList->uiFreeListPMROffset + (psFreeList->ui32MaxFLPages - psFreeList->ui32CurrentFLPages) * sizeof(IMG_UINT32),
					pui8Buffer,
					psFreeList->ui32CurrentFLPages * sizeof(IMG_UINT32),
					&uiNumBytes);
	if (eError != PVRSRV_OK)
	{
		goto _PMR_ReadBytes_Exit;
	}

	PVR_ASSERT(uiNumBytes == psFreeList->ui32CurrentFLPages * sizeof(IMG_UINT32));

	/* Generate checksum */
	pui32Buffer = (IMG_UINT32 *)pui8Buffer;
	for(ui32Entry = 0; ui32Entry < psFreeList->ui32CurrentFLPages; ui32Entry++)
	{
		ui32CheckSumAdd += pui32Buffer[ui32Entry];
		ui32CheckSumXor ^= pui32Buffer[ui32Entry];

		/* Check for double entries */
		for (ui32Entry2 = 0; ui32Entry2 < psFreeList->ui32CurrentFLPages; ui32Entry2++)
		{
			if ((ui32Entry != ui32Entry2) &&
				(pui32Buffer[ui32Entry] == pui32Buffer[ui32Entry2]))
			{
				PVR_DPF((PVR_DBG_ERROR, "Freelist consistency failure: FW addr: 0x%08X, Double entry found 0x%08x on idx: %d and %d",
											psFreeList->sFreeListFWDevVAddr.ui32Addr,
											pui32Buffer[ui32Entry2],
											ui32Entry,
											ui32Entry2));
				sleep_for_ever();
//				PVR_ASSERT(0);
			}
		}
	}

	OSFreeMem(pui8Buffer);

	/* Set return value */
	*pui64CheckSum = ((IMG_UINT64)ui32CheckSumXor << 32) | ui32CheckSumAdd;
	PVR_ASSERT(eError == PVRSRV_OK);
	return PVRSRV_OK;

	/*
	  error exit paths follow
	*/

_PMR_ReadBytes_Exit:
	OSFreeMem(pui8Buffer);

_OSAllocMem_Exit:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
#endif
}

PVRSRV_ERROR RGXGrowFreeList(RGX_FREELIST *psFreeList,
							IMG_UINT32 ui32NumPages,
							PDLLIST_NODE pListHeader)
{
	RGX_PMR_NODE	*psPMRNode;
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_BOOL bMappingTable = IMG_TRUE;
	IMG_DEVMEM_OFFSET_T uiOffset;
	IMG_DEVMEM_SIZE_T uiLength;
	IMG_DEVMEM_SIZE_T uistartPage;
	PVRSRV_ERROR eError;
	IMG_UINT64 ui64CheckSum;
	IMG_UINT32 ui32CheckSumXor;
	IMG_UINT32 ui32CheckSumAdd;

	/* Are we allowed to grow ? */
	if ((psFreeList->ui32MaxFLPages - psFreeList->ui32CurrentFLPages) < ui32NumPages)
	{
		PVR_DPF((PVR_DBG_WARNING,"Freelist [0x%p]: grow by %u pages denied. Max PB size reached (current pages %u/%u)",
				psFreeList,
				ui32NumPages,
				psFreeList->ui32CurrentFLPages,
				psFreeList->ui32MaxFLPages));
		return PVRSRV_ERROR_PBSIZE_ALREADY_MAX;
	}

	/* Allocate kernel memory block structure */
	psPMRNode = OSAllocMem(sizeof(*psPMRNode));
	if (psPMRNode == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXGrowFreeList: failed to allocate host data structure"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorAllocHost;
	}

	/*
	 * Lock protects simultaneous manipulation of:
	 * - the memory block list
	 * - the freelist's ui32CurrentFLPages
	 */
	OSLockAcquire(psFreeList->psDevInfo->hLockFreeList);


	psPMRNode->ui32NumPages = ui32NumPages;
	psPMRNode->psFreeList = psFreeList;

	/* Allocate Memory Block */
	PDUMPCOMMENT("Allocate PB Block (Pages %08X)", ui32NumPages);
	uiSize = (IMG_DEVMEM_SIZE_T)ui32NumPages * RGX_BIF_PM_PHYSICAL_PAGE_SIZE;
	eError = PhysmemNewRamBackedPMR(psFreeList->psDevInfo->psDeviceNode,
									uiSize,
									uiSize,
									1,
									1,
									&bMappingTable,
									RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT,
									PVRSRV_MEMALLOCFLAG_GPU_READABLE,
									&psPMRNode->psPMR);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "RGXGrowFreeList: Failed to allocate PB block of size: 0x%016llX",
				 (IMG_UINT64)uiSize));
		goto ErrorBlockAlloc;
	}

	/* Zeroing physical pages pointed by the PMR */
	if (psFreeList->psDevInfo->ui32DeviceFlags & RGXKM_DEVICE_STATE_ZERO_FREELIST)
	{
		eError = PMRZeroingPMR(psPMRNode->psPMR, RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXGrowFreeList: Failed to zero PMR %p of freelist %p with Error %d",
									psPMRNode->psPMR,
									psFreeList,
									eError));
			PVR_ASSERT(0);
		}
	}

	uiLength = psPMRNode->ui32NumPages * sizeof(IMG_UINT32);
	uistartPage = (psFreeList->ui32MaxFLPages - psFreeList->ui32CurrentFLPages - psPMRNode->ui32NumPages);
	uiOffset = psFreeList->uiFreeListPMROffset + (uistartPage * sizeof(IMG_UINT32));

	/* write Freelist with Memory Block physical addresses */
	eError = PMRWritePMPageList(
						/* Target PMR, offset, and length */
						psFreeList->psFreeListPMR,
						uiOffset,
						uiLength,
						/* Referenced PMR, and "page" granularity */
						psPMRNode->psPMR,
						RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT,
						&psPMRNode->psPageList,
						&ui64CheckSum);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "RGXGrowFreeList: Failed to write pages of Node %p",
				 psPMRNode));
		goto ErrorPopulateFreelist;
	}

	/* We add It must be added to the tail, otherwise the freelist population won't work */
	dllist_add_to_head(pListHeader, &psPMRNode->sMemoryBlock);

	/* Update number of available pages */
	psFreeList->ui32CurrentFLPages += ui32NumPages;

	/* Update statistics */
	if (psFreeList->ui32NumHighPages < psFreeList->ui32CurrentFLPages)
	{
		psFreeList->ui32NumHighPages = psFreeList->ui32CurrentFLPages;
	}

	if (psFreeList->bCheckFreelist)
	{
		/* Update checksum */
		ui32CheckSumAdd = (IMG_UINT32)(psFreeList->ui64FreelistChecksum + ui64CheckSum);
		ui32CheckSumXor = (IMG_UINT32)((psFreeList->ui64FreelistChecksum  ^ ui64CheckSum) >> 32);
		psFreeList->ui64FreelistChecksum = ((IMG_UINT64)ui32CheckSumXor << 32) | ui32CheckSumAdd;
		/* Note: We can't do a freelist check here, because the freelist is probably empty (OOM) */
	}

	OSLockRelease(psFreeList->psDevInfo->hLockFreeList);

	PVR_DPF((PVR_DBG_MESSAGE,"Freelist [%p]: grow by %u pages (current pages %u/%u)",
			psFreeList,
			ui32NumPages,
			psFreeList->ui32CurrentFLPages,
			psFreeList->ui32MaxFLPages));

	return PVRSRV_OK;

	/* Error handling */
ErrorPopulateFreelist:
	PMRUnrefPMR(psPMRNode->psPMR);

ErrorBlockAlloc:
	OSFreeMem(psPMRNode);
	OSLockRelease(psFreeList->psDevInfo->hLockFreeList);

ErrorAllocHost:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;

}

static PVRSRV_ERROR RGXShrinkFreeList(PDLLIST_NODE pListHeader,
										RGX_FREELIST *psFreeList)
{
	DLLIST_NODE *psNode;
	RGX_PMR_NODE *psPMRNode;
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 ui32OldValue;

	/*
	 * Lock protects simultaneous manipulation of:
	 * - the memory block list
	 * - the freelist's ui32CurrentFLPages value
	 */
	PVR_ASSERT(pListHeader);
	PVR_ASSERT(psFreeList);
	PVR_ASSERT(psFreeList->psDevInfo);
	PVR_ASSERT(psFreeList->psDevInfo->hLockFreeList);

	OSLockAcquire(psFreeList->psDevInfo->hLockFreeList);

	/* Get node from head of list and remove it */
	psNode = dllist_get_next_node(pListHeader);
	if (psNode)
	{
		dllist_remove_node(psNode);

		psPMRNode = IMG_CONTAINER_OF(psNode, RGX_PMR_NODE, sMemoryBlock);
		PVR_ASSERT(psPMRNode);
		PVR_ASSERT(psPMRNode->psPMR);
		PVR_ASSERT(psPMRNode->psFreeList);

		/* remove block from freelist list */

		/* Unwrite Freelist with Memory Block physical addresses */
		eError = PMRUnwritePMPageList(psPMRNode->psPageList);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "RGXRemoveBlockFromFreeListKM: Failed to unwrite pages of Node %p",
					 psPMRNode));
			PVR_ASSERT(IMG_FALSE);
		}

		/* Free PMR (We should be the only one that holds a ref on the PMR) */
		eError = PMRUnrefPMR(psPMRNode->psPMR);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "RGXRemoveBlockFromFreeListKM: Failed to free PB block %p (error %u)",
					 psPMRNode->psPMR,
					 eError));
			PVR_ASSERT(IMG_FALSE);
		}

		/* update available pages in freelist */
		ui32OldValue = psFreeList->ui32CurrentFLPages;
		psFreeList->ui32CurrentFLPages -= psPMRNode->ui32NumPages;

		/* check underflow */
		PVR_ASSERT(ui32OldValue > psFreeList->ui32CurrentFLPages);

		PVR_DPF((PVR_DBG_MESSAGE, "Freelist [%p]: shrink by %u pages (current pages %u/%u)",
								psFreeList,
								psPMRNode->ui32NumPages,
								psFreeList->ui32CurrentFLPages,
								psFreeList->ui32MaxFLPages));

		OSFreeMem(psPMRNode);
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING,"Freelist [0x%p]: shrink denied. PB already at initial PB size (%u pages)",
								psFreeList,
								psFreeList->ui32InitFLPages));
		eError = PVRSRV_ERROR_PBSIZE_ALREADY_MIN;
	}

	OSLockRelease(psFreeList->psDevInfo->hLockFreeList);

	return eError;
}

static IMG_BOOL _FindFreeList(PDLLIST_NODE psNode, IMG_PVOID pvCallbackData)
{
	DEVMEM_FREELIST_LOOKUP *psRefLookUp = (DEVMEM_FREELIST_LOOKUP *)pvCallbackData;
	RGX_FREELIST *psFreeList;

	psFreeList = IMG_CONTAINER_OF(psNode, RGX_FREELIST, sNode);

	if (psFreeList->ui32FreelistID == psRefLookUp->ui32FreeListID)
	{
		psRefLookUp->psFreeList = psFreeList;
		return IMG_FALSE;
	}
	else
	{
		return IMG_TRUE;
	}
}

IMG_VOID RGXProcessRequestGrow(PVRSRV_RGXDEV_INFO *psDevInfo,
								IMG_UINT32 ui32FreelistID)
{
	DEVMEM_FREELIST_LOOKUP sLookUp;
	RGXFWIF_KCCB_CMD s3DCCBCmd;
	IMG_UINT32 ui32GrowValue;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psDevInfo);

	/* find the freelist with the corresponding ID */
	sLookUp.ui32FreeListID = ui32FreelistID;
	sLookUp.psFreeList = IMG_NULL;

	OSLockAcquire(psDevInfo->hLockFreeList);
	dllist_foreach_node(&psDevInfo->sFreeListHead, _FindFreeList, (IMG_PVOID)&sLookUp);
	OSLockRelease(psDevInfo->hLockFreeList);

	if (sLookUp.psFreeList)
	{
		RGX_FREELIST *psFreeList = sLookUp.psFreeList;

		/* Try to grow the freelist */
		eError = RGXGrowFreeList(psFreeList,
								psFreeList->ui32GrowFLPages,
								&psFreeList->sMemoryBlockHead);
		if (eError == PVRSRV_OK)
		{
			/* Grow successful, return size of grow size */
			ui32GrowValue = psFreeList->ui32GrowFLPages;

			psFreeList->ui32NumGrowReqByFW++;
		}
		else
		{
			/* Grow failed */
			ui32GrowValue = 0;
			PVR_DPF((PVR_DBG_ERROR,"Grow for FreeList %p failed (error %u)",
									psFreeList,
									eError));
		}

		/* send feedback */
		s3DCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_FREELIST_GROW_UPDATE;
		s3DCCBCmd.uCmdData.sFreeListGSData.psFreeListFWDevVAddr = sLookUp.psFreeList->sFreeListFWDevVAddr.ui32Addr;
		s3DCCBCmd.uCmdData.sFreeListGSData.ui32DeltaSize = ui32GrowValue;
		s3DCCBCmd.uCmdData.sFreeListGSData.ui32NewSize = psFreeList->ui32CurrentFLPages;

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError = RGXScheduleCommand(psDevInfo,
												RGXFWIF_DM_3D,
												&s3DCCBCmd,
												sizeof(s3DCCBCmd),
												IMG_FALSE);
			if (eError != PVRSRV_ERROR_RETRY)
			{
				break;
			}
			OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();
		/* Kernel CCB should never fill up, as the FW is processing them right away  */
		PVR_ASSERT(eError == PVRSRV_OK);
	}
	else
	{
		/* Should never happen */
		PVR_DPF((PVR_DBG_ERROR,"FreeList Lookup for FreeList ID 0x%08x failed (Populate)", sLookUp.ui32FreeListID));
		PVR_ASSERT(IMG_FALSE);
	}
}

static IMG_BOOL _RGXCheckFreeListReconstruction(PDLLIST_NODE psNode, IMG_PVOID pvCallbackData)
{

	PVRSRV_RGXDEV_INFO 		*psDevInfo;
	RGX_FREELIST			*psFreeList;
	RGX_PMR_NODE			*psPMRNode;
	PVRSRV_ERROR			eError;
	IMG_DEVMEM_OFFSET_T		uiOffset;
	IMG_DEVMEM_SIZE_T		uiLength;
	IMG_UINT32				ui32StartPage;
	IMG_UINT64				ui64CheckSum;

	psPMRNode = IMG_CONTAINER_OF(psNode, RGX_PMR_NODE, sMemoryBlock);
	psFreeList = psPMRNode->psFreeList;
	PVR_ASSERT(psFreeList);
	psDevInfo = psFreeList->psDevInfo;
	PVR_ASSERT(psDevInfo);

	uiLength = psPMRNode->ui32NumPages * sizeof(IMG_UINT32);
	ui32StartPage = (psFreeList->ui32MaxFLPages - psFreeList->ui32CurrentFLPages - psPMRNode->ui32NumPages);
	uiOffset = psFreeList->uiFreeListPMROffset + (ui32StartPage * sizeof(IMG_UINT32));

	PMRUnwritePMPageList(psPMRNode->psPageList);
	psPMRNode->psPageList = IMG_NULL;
	eError = PMRWritePMPageList(
						/* Target PMR, offset, and length */
						psFreeList->psFreeListPMR,
						uiOffset,
						uiLength,
						/* Referenced PMR, and "page" granularity */
						psPMRNode->psPMR,
						RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT,
						&psPMRNode->psPageList,
						&ui64CheckSum);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"Error (%u) writing FL 0x%08x", eError, (IMG_UINT32)psFreeList->ui32FreelistID));
	}

	/* Zeroing physical pages pointed by the reconstructed freelist */
	if (psDevInfo->ui32DeviceFlags & RGXKM_DEVICE_STATE_ZERO_FREELIST)
	{
		eError = PMRZeroingPMR(psPMRNode->psPMR, RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"_RGXCheckFreeListReconstruction: Failed to zero PMR %p of freelist %p with Error %d",
									psPMRNode->psPMR,
									psFreeList,
									eError));
			PVR_ASSERT(0);
		}
	}

	psFreeList->ui32CurrentFLPages += psPMRNode->ui32NumPages;

	return IMG_TRUE;
}

IMG_VOID RGXProcessRequestFreelistsReconstruction(PVRSRV_RGXDEV_INFO *psDevInfo,
								RGXFWIF_DM eDM,
								IMG_UINT32 ui32FreelistsCount,
								IMG_UINT32 *paui32Freelists)
{
	PVRSRV_ERROR eError;
	DEVMEM_FREELIST_LOOKUP sLookUp;
	IMG_UINT32 ui32Loop, ui32Loop2;
	RGXFWIF_KCCB_CMD s3DCCBCmd;
	IMG_UINT64 ui64CheckSum;

	PVR_ASSERT(psDevInfo);

	//PVR_DPF((PVR_DBG_ERROR,"FreeList RECONSTRUCTION: Reconstructing %u freelist(s)", ui32FreelistsCount));

	for (ui32Loop = 0; ui32Loop < ui32FreelistsCount; ui32Loop++)
	{
		/* check if there is more than one occurrence of FL on the list */
		for (ui32Loop2 = ui32Loop + 1; ui32Loop2 < ui32FreelistsCount; ui32Loop2++)
		{
			if (paui32Freelists[ui32Loop] == paui32Freelists[ui32Loop2])
			{
				/* There is a duplicate on a list, skip current Freelist */
				break;
			}
		}

		if (ui32Loop2 < ui32FreelistsCount)
		{
			/* There is a duplicate on the list, skip current Freelist */
			continue;
		}

		/* find the freelist with the corresponding ID */
		sLookUp.ui32FreeListID = paui32Freelists[ui32Loop];
		sLookUp.psFreeList = IMG_NULL;

		//PVR_DPF((PVR_DBG_ERROR,"FreeList RECONSTRUCTION: Looking for freelist %08X", (IMG_UINT32)sLookUp.ui32FreeListID));
		OSLockAcquire(psDevInfo->hLockFreeList);
		//PVR_DPF((PVR_DBG_ERROR,"FreeList RECONSTRUCTION: Freelist head %08X", (IMG_UINT32)&psDevInfo->sFreeListHead));
		dllist_foreach_node(&psDevInfo->sFreeListHead, _FindFreeList, (IMG_PVOID)&sLookUp);
		OSLockRelease(psDevInfo->hLockFreeList);

		if (sLookUp.psFreeList)
		{
			RGX_FREELIST *psFreeList = sLookUp.psFreeList;

			//PVR_DPF((PVR_DBG_ERROR,"FreeList RECONSTRUCTION: Reconstructing freelist %08X", (IMG_UINT32)psFreeList));

			/* Do the FreeList Reconstruction */

			psFreeList->ui32CurrentFLPages = 0;

			/* Reconstructing Init FreeList pages */
			dllist_foreach_node(&psFreeList->sMemoryBlockInitHead,
							_RGXCheckFreeListReconstruction,
							IMG_NULL);

			/* Reconstructing Grow FreeList pages */
			dllist_foreach_node(&psFreeList->sMemoryBlockHead,
							_RGXCheckFreeListReconstruction,
							IMG_NULL);

			if (psFreeList->bCheckFreelist)
			{
				/* Get Freelist checksum (as the list is fully populated) */
				eError = _FreeListCheckSum(psFreeList,
											&ui64CheckSum);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_ERROR,
							 "RGXProcessRequestFreelistsReconstruction: Failed to get freelist checksum Node %p",
							 psFreeList));
					sleep_for_ever();
//					PVR_ASSERT(0);
				}

				/* Verify checksum with previous value */
				if (psFreeList->ui64FreelistChecksum != ui64CheckSum)
				{
					PVR_DPF((PVR_DBG_ERROR, "RGXProcessRequestFreelistsReconstruction: Freelist [%p] checksum failed: before reconstruction = 0x%016llx, after reconstruction = 0x%016llx",
											psFreeList,
											psFreeList->ui64FreelistChecksum,
											ui64CheckSum));
					sleep_for_ever();
					//PVR_ASSERT(0);
				}
			}

			eError = PVRSRV_OK;

			if (eError == PVRSRV_OK)
			{
				/* Freelist reconstruction successful */
				s3DCCBCmd.uCmdData.sFreeListsReconstructionData.aui32FreelistIDs[ui32Loop] =
													paui32Freelists[ui32Loop];
			}
			else
			{
				/* Freelist reconstruction failed */
				s3DCCBCmd.uCmdData.sFreeListsReconstructionData.aui32FreelistIDs[ui32Loop] =
													paui32Freelists[ui32Loop] | RGXFWIF_FREELISTS_RECONSTRUCTION_FAILED_FLAG;

				PVR_DPF((PVR_DBG_ERROR,"Reconstructing of FreeList %p failed (error %u)",
										psFreeList,
										eError));
			}
		}
		else
		{
			/* Should never happen */
			PVR_DPF((PVR_DBG_ERROR,"FreeList Lookup for FreeList ID 0x%08x failed (Freelist reconstruction)", sLookUp.ui32FreeListID));
			PVR_ASSERT(IMG_FALSE);
		}
	}

	/* send feedback */
	s3DCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_FREELISTS_RECONSTRUCTION_UPDATE;
	s3DCCBCmd.uCmdData.sFreeListsReconstructionData.ui32FreelistsCount = ui32FreelistsCount;

	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		eError = RGXScheduleCommand(psDevInfo,
											eDM,
											&s3DCCBCmd,
											sizeof(s3DCCBCmd),
											IMG_FALSE);
		if (eError != PVRSRV_ERROR_RETRY)
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

	/* Kernel CCB should never fill up, as the FW is processing them right away  */
	PVR_ASSERT(eError == PVRSRV_OK);
}

/* Create HWRTDataSet */
IMG_EXPORT
PVRSRV_ERROR RGXCreateHWRTData(PVRSRV_DEVICE_NODE	*psDeviceNode,
							   IMG_UINT32			psRenderTarget, /* FIXME this should not be IMG_UINT32 */
							   IMG_DEV_VIRTADDR		psPMMListDevVAddr,
							   IMG_DEV_VIRTADDR		psVFPPageTableAddr,
							   RGX_FREELIST			*apsFreeLists[RGXFW_MAX_FREELISTS],
							   RGX_RTDATA_CLEANUP_DATA	**ppsCleanupData,
							   DEVMEM_MEMDESC		**ppsRTACtlMemDesc,
							   IMG_UINT32           ui32PPPScreen,
							   IMG_UINT32           ui32PPPGridOffset,
							   IMG_UINT64           ui64PPPMultiSampleCtl,
							   IMG_UINT32           ui32TPCStride,
							   IMG_DEV_VIRTADDR		sTailPtrsDevVAddr,
							   IMG_UINT32           ui32TPCSize,
							   IMG_UINT32           ui32TEScreen,
							   IMG_UINT32           ui32TEAA,
							   IMG_UINT32           ui32TEMTILE1,
							   IMG_UINT32           ui32TEMTILE2,
							   IMG_UINT32           ui32MTileStride,
							   IMG_UINT32                 ui32ISPMergeLowerX,
							   IMG_UINT32                 ui32ISPMergeLowerY,
							   IMG_UINT32                 ui32ISPMergeUpperX,
							   IMG_UINT32                 ui32ISPMergeUpperY,
							   IMG_UINT32                 ui32ISPMergeScaleX,
							   IMG_UINT32                 ui32ISPMergeScaleY,
							   IMG_UINT16			ui16MaxRTs,
							   DEVMEM_MEMDESC		**ppsMemDesc,
							   IMG_UINT32			*puiHWRTData)
{
	PVRSRV_ERROR eError;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	RGXFWIF_DEV_VIRTADDR pFirmwareAddr;
	RGXFWIF_HWRTDATA *psHWRTData;
	RGXFWIF_RTA_CTL *psRTACtl;
	IMG_UINT32 ui32Loop;
	RGX_RTDATA_CLEANUP_DATA *psTmpCleanup;

	/* Prepare cleanup struct */
	psTmpCleanup = OSAllocMem(sizeof(*psTmpCleanup));
	if (psTmpCleanup == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto AllocError;
	}

	OSMemSet(psTmpCleanup, 0, sizeof(*psTmpCleanup));
	*ppsCleanupData = psTmpCleanup;

	/* Allocate cleanup sync */
	eError = SyncPrimAlloc(psDeviceNode->hSyncPrimContext,
						   &psTmpCleanup->psCleanupSync);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXCreateHWRTData: Failed to allocate cleanup sync (0x%x)",
				eError));
		goto SyncAlloc;
	}

	psDevInfo = psDeviceNode->pvDevice;

	/*
	 * This FW RT-Data is only mapped into kernel for initialisation.
	 * Otherwise this allocation is only used by the FW.
	 * Therefore the GPU cache doesn't need coherency,
	 * and write-combine is suffice on the CPU side (WC buffer will be flushed at the first TA-kick)
	 */
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_HWRTDATA),
							PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE,
							"FirmwareHWRTData",
							ppsMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXCreateHWRTData: DevmemAllocate for RGX_FWIF_HWRTDATA failed"));
		goto FWRTDataAllocateError;
	}

	psTmpCleanup->psDeviceNode = psDeviceNode;
	psTmpCleanup->psFWHWRTDataMemDesc = *ppsMemDesc;

	RGXSetFirmwareAddress(&pFirmwareAddr, *ppsMemDesc, 0, RFW_FWADDR_METACACHED_FLAG);

	*puiHWRTData = pFirmwareAddr.ui32Addr;

	DevmemAcquireCpuVirtAddr(*ppsMemDesc, (IMG_VOID **)&psHWRTData);

	/* FIXME: MList is something that that PM writes physical addresses to,
	 * so ideally its best allocated in kernel */
	psHWRTData->psPMMListDevVAddr = psPMMListDevVAddr;
	psHWRTData->psParentRenderTarget.ui32Addr = psRenderTarget;
	#if defined(SUPPORT_VFP)
	psHWRTData->sVFPPageTableAddr = psVFPPageTableAddr;
	#endif

	psHWRTData->ui32PPPScreen         = ui32PPPScreen;
	psHWRTData->ui32PPPGridOffset     = ui32PPPGridOffset;
	psHWRTData->ui64PPPMultiSampleCtl = ui64PPPMultiSampleCtl;
	psHWRTData->ui32TPCStride         = ui32TPCStride;
	psHWRTData->sTailPtrsDevVAddr     = sTailPtrsDevVAddr;
	psHWRTData->ui32TPCSize           = ui32TPCSize;
	psHWRTData->ui32TEScreen          = ui32TEScreen;
	psHWRTData->ui32TEAA              = ui32TEAA;
	psHWRTData->ui32TEMTILE1          = ui32TEMTILE1;
	psHWRTData->ui32TEMTILE2          = ui32TEMTILE2;
	psHWRTData->ui32MTileStride       = ui32MTileStride;
	psHWRTData->ui32ISPMergeLowerX = ui32ISPMergeLowerX;
	psHWRTData->ui32ISPMergeLowerY = ui32ISPMergeLowerY;
	psHWRTData->ui32ISPMergeUpperX = ui32ISPMergeUpperX;
	psHWRTData->ui32ISPMergeUpperY = ui32ISPMergeUpperY;
	psHWRTData->ui32ISPMergeScaleX = ui32ISPMergeScaleX;
	psHWRTData->ui32ISPMergeScaleY = ui32ISPMergeScaleY;

	OSLockAcquire(psDevInfo->hLockFreeList);
	for (ui32Loop = 0; ui32Loop < RGXFW_MAX_FREELISTS; ui32Loop++)
	{
		psTmpCleanup->apsFreeLists[ui32Loop] = apsFreeLists[ui32Loop];
		psTmpCleanup->apsFreeLists[ui32Loop]->ui32RefCount++;
		psHWRTData->apsFreeLists[ui32Loop] = *((PRGXFWIF_FREELIST *)&(psTmpCleanup->apsFreeLists[ui32Loop]->sFreeListFWDevVAddr.ui32Addr)); /* FIXME: Fix pointer type casting */
		/* invalid initial snapshot value, the snapshot is always taken during first kick
		 * and hence the value get replaced during the first kick anyway. So its safe to set it 0.
		*/
		psHWRTData->aui32FreeListHWRSnapshot[ui32Loop] = 0;
	}
	OSLockRelease(psDevInfo->hLockFreeList);

	PDUMPCOMMENT("Allocate RGXFW RTA control");
	eError = DevmemFwAllocate(psDevInfo,
										sizeof(RGXFWIF_RTA_CTL),
										PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
										PVRSRV_MEMALLOCFLAG_GPU_READABLE |
										PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
										PVRSRV_MEMALLOCFLAG_UNCACHED |
										PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC,
										"FirmwareRTAControl",
										ppsRTACtlMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXCreateHWRTData: Failed to allocate RGX RTA control (%u)",
				eError));
		goto FWRTAAllocateError;
	}
	psTmpCleanup->psRTACtlMemDesc = *ppsRTACtlMemDesc;
	RGXSetFirmwareAddress(&psHWRTData->psRTACtl,
								   *ppsRTACtlMemDesc,
								   0, RFW_FWADDR_METACACHED_FLAG);

	DevmemAcquireCpuVirtAddr(*ppsRTACtlMemDesc, (IMG_VOID **)&psRTACtl);
	psRTACtl->ui32RenderTargetIndex = 0;
	psRTACtl->ui32ActiveRenderTargets = 0;

	if (ui16MaxRTs > 1)
	{
		/* Allocate memory for the checks */
		PDUMPCOMMENT("Allocate memory for shadow render target cache");
		eError = DevmemFwAllocate(psDevInfo,
								ui16MaxRTs,
								PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
								PVRSRV_MEMALLOCFLAG_GPU_READABLE |
								PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
								PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
								PVRSRV_MEMALLOCFLAG_UNCACHED|
								PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC,
								"FirmwareShadowRTCache",
								&psTmpCleanup->psRTArrayMemDesc);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXCreateHWRTData: Failed to allocate %d bytes for render target array (%u)",
						ui16MaxRTs,
						eError));
			goto FWAllocateRTArryError;
		}

		RGXSetFirmwareAddress(&psRTACtl->paui32ValidRenderTargets,
										psTmpCleanup->psRTArrayMemDesc,
										0, RFW_FWADDR_METACACHED_FLAG);
	}
	else
	{
		psRTACtl->paui32ValidRenderTargets.ui32Addr = 0;
	}

	PDUMPCOMMENT("Dump HWRTData 0x%08X", *puiHWRTData);
	DevmemPDumpLoadMem(*ppsMemDesc, 0, sizeof(*psHWRTData), PDUMP_FLAGS_CONTINUOUS);
	PDUMPCOMMENT("Dump RTACtl");
	DevmemPDumpLoadMem(*ppsRTACtlMemDesc, 0, sizeof(*psRTACtl), PDUMP_FLAGS_CONTINUOUS);

	DevmemReleaseCpuVirtAddr(*ppsMemDesc);
	DevmemReleaseCpuVirtAddr(*ppsRTACtlMemDesc);
	return PVRSRV_OK;

	DevmemFwFree(psTmpCleanup->psRTArrayMemDesc);
FWAllocateRTArryError:
	RGXUnsetFirmwareAddress(*ppsRTACtlMemDesc);
	DevmemFwFree(*ppsRTACtlMemDesc);
FWRTAAllocateError:
	RGXUnsetFirmwareAddress(*ppsMemDesc);
	DevmemFwFree(*ppsMemDesc);
FWRTDataAllocateError:
	OSLockAcquire(psDevInfo->hLockFreeList);
	for (ui32Loop = 0; ui32Loop < RGXFW_MAX_FREELISTS; ui32Loop++)
	{
		PVR_ASSERT(psTmpCleanup->apsFreeLists[ui32Loop]->ui32RefCount > 0);
		psTmpCleanup->apsFreeLists[ui32Loop]->ui32RefCount--;
	}
	OSLockRelease(psDevInfo->hLockFreeList);
	SyncPrimFree(psTmpCleanup->psCleanupSync);
SyncAlloc:
	OSFreeMem(psTmpCleanup);

AllocError:
	return eError;
}

/* Destroy HWRTDataSet */
IMG_EXPORT
PVRSRV_ERROR RGXDestroyHWRTData(RGX_RTDATA_CLEANUP_DATA *psCleanupData)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	PVRSRV_ERROR eError;
	PRGXFWIF_HWRTDATA psHWRTData;
	IMG_UINT32 ui32Loop;

	PVR_ASSERT(psCleanupData);

	RGXSetFirmwareAddress(&psHWRTData, psCleanupData->psFWHWRTDataMemDesc, 0, RFW_FWADDR_NOREF_FLAG | RFW_FWADDR_METACACHED_FLAG);

	/* Cleanup HWRTData in TA */
	eError = RGXFWRequestHWRTDataCleanUp(psCleanupData->psDeviceNode,
										 psHWRTData,
										 psCleanupData->psCleanupSync,
										 RGXFWIF_DM_TA);
	if (eError == PVRSRV_ERROR_RETRY)
	{
		return eError;
	}

	psDevInfo = psCleanupData->psDeviceNode->pvDevice;

	/* Cleanup HWRTData in 3D */
	eError = RGXFWRequestHWRTDataCleanUp(psCleanupData->psDeviceNode,
										 psHWRTData,
										 psCleanupData->psCleanupSync,
										 RGXFWIF_DM_3D);
	if (eError == PVRSRV_ERROR_RETRY)
	{
		return eError;
	}

	/* If we got here then TA and 3D operations on this RTData have finished */
	if(psCleanupData->psRTACtlMemDesc)
	{
		RGXUnsetFirmwareAddress(psCleanupData->psRTACtlMemDesc);
		DevmemFwFree(psCleanupData->psRTACtlMemDesc);
	}

	RGXUnsetFirmwareAddress(psCleanupData->psFWHWRTDataMemDesc);
	DevmemFwFree(psCleanupData->psFWHWRTDataMemDesc);

	if(psCleanupData->psRTArrayMemDesc)
	{
		RGXUnsetFirmwareAddress(psCleanupData->psRTArrayMemDesc);
		DevmemFwFree(psCleanupData->psRTArrayMemDesc);
	}

	SyncPrimFree(psCleanupData->psCleanupSync);

	/* decrease freelist refcount */
	OSLockAcquire(psDevInfo->hLockFreeList);
	for (ui32Loop = 0; ui32Loop < RGXFW_MAX_FREELISTS; ui32Loop++)
	{
		PVR_ASSERT(psCleanupData->apsFreeLists[ui32Loop]->ui32RefCount > 0);
		psCleanupData->apsFreeLists[ui32Loop]->ui32RefCount--;
	}
	OSLockRelease(psDevInfo->hLockFreeList);

	OSFreeMem(psCleanupData);

	return PVRSRV_OK;
}

IMG_EXPORT
PVRSRV_ERROR RGXCreateFreeList(PVRSRV_DEVICE_NODE	*psDeviceNode,
							   IMG_UINT32			ui32MaxFLPages,
							   IMG_UINT32			ui32InitFLPages,
							   IMG_UINT32			ui32GrowFLPages,
							   IMG_BOOL				bCheckFreelist,
							   IMG_DEV_VIRTADDR		sFreeListDevVAddr,
							   PMR					*psFreeListPMR,
							   IMG_DEVMEM_OFFSET_T	uiFreeListPMROffset,
							   RGX_FREELIST			**ppsFreeList)
{
	PVRSRV_ERROR				eError;
	RGXFWIF_FREELIST			*psFWFreeList;
	DEVMEM_MEMDESC				*psFWFreelistMemDesc;
	RGX_FREELIST				*psFreeList;
	PVRSRV_RGXDEV_INFO			*psDevInfo = psDeviceNode->pvDevice;

	/* Allocate kernel freelist struct */
	psFreeList = OSAllocMem(sizeof(*psFreeList));
	if (psFreeList == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXCreateFreeList: failed to allocate host data structure"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorAllocHost;
	}
	OSMemSet(psFreeList, 0, sizeof(*psFreeList));

	/* Allocate cleanup sync */
	eError = SyncPrimAlloc(psDeviceNode->hSyncPrimContext,
						   &psFreeList->psCleanupSync);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXCreateFreeList: Failed to allocate cleanup sync (0x%x)",
				eError));
		goto SyncAlloc;
	}

	/*
	 * This FW FreeList context is only mapped into kernel for initialisation.
	 * Otherwise this allocation is only used by the FW.
	 * Therefore the GPU cache doesn't need coherency,
	 * and write-combine is suffice on the CPU side (WC buffer will be flushed at the first TA-kick)
	 */
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(*psFWFreeList),
							PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE,
							"FirmwareFreeList",
							&psFWFreelistMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXCreateFreeList: DevmemAllocate for RGXFWIF_FREELIST failed"));
		goto FWFreeListAlloc;
	}

	/* Initialise host data structures */
	psFreeList->psDevInfo = psDevInfo;
	psFreeList->psFreeListPMR = psFreeListPMR;
	psFreeList->uiFreeListPMROffset = uiFreeListPMROffset;
	psFreeList->psFWFreelistMemDesc = psFWFreelistMemDesc;
	RGXSetFirmwareAddress(&psFreeList->sFreeListFWDevVAddr, psFWFreelistMemDesc, 0, RFW_FWADDR_FLAG_NONE);
	psFreeList->ui32FreelistID = psDevInfo->ui32FreelistCurrID++;
	psFreeList->ui32MaxFLPages = ui32MaxFLPages;
	psFreeList->ui32InitFLPages = ui32InitFLPages;
	psFreeList->ui32GrowFLPages = ui32GrowFLPages;
	psFreeList->ui32CurrentFLPages = 0;
	psFreeList->ui64FreelistChecksum = 0;
	psFreeList->ui32RefCount = 0;
	psFreeList->bCheckFreelist = bCheckFreelist;
	dllist_init(&psFreeList->sMemoryBlockHead);
	dllist_init(&psFreeList->sMemoryBlockInitHead);


	/* Add to list of freelists */
	OSLockAcquire(psDevInfo->hLockFreeList);
	dllist_add_to_tail(&psDevInfo->sFreeListHead, &psFreeList->sNode);
	OSLockRelease(psDevInfo->hLockFreeList);


	/* Initialise FW data structure */
	DevmemAcquireCpuVirtAddr(psFreeList->psFWFreelistMemDesc, (IMG_VOID **)&psFWFreeList);
	psFWFreeList->ui32MaxPages = ui32MaxFLPages;
	psFWFreeList->ui32CurrentPages = ui32InitFLPages;
	psFWFreeList->ui32GrowPages = ui32GrowFLPages;
	psFWFreeList->ui64CurrentStackTop = ui32InitFLPages - 1;
	psFWFreeList->psFreeListDevVAddr = sFreeListDevVAddr;
	psFWFreeList->ui64CurrentDevVAddr = sFreeListDevVAddr.uiAddr + ((ui32MaxFLPages - ui32InitFLPages) * sizeof(IMG_UINT32));
	psFWFreeList->ui32FreeListID = psFreeList->ui32FreelistID;
	psFWFreeList->bGrowPending = IMG_FALSE;

	PVR_DPF((PVR_DBG_MESSAGE,"Freelist %p created: Max pages 0x%08x, Init pages 0x%08x, Max FL base address 0x%016llx, Init FL base address 0x%016llx",
			psFreeList,
			ui32MaxFLPages,
			ui32InitFLPages,
			sFreeListDevVAddr.uiAddr,
			psFWFreeList->psFreeListDevVAddr.uiAddr));

	PDUMPCOMMENT("Dump FW FreeList");
	DevmemPDumpLoadMem(psFreeList->psFWFreelistMemDesc, 0, sizeof(*psFWFreeList), PDUMP_FLAGS_CONTINUOUS);

	/*
	 * Separate dump of the Freelist's number of Pages and stack pointer.
	 * This allows to easily modify the PB size in the out2.txt files.
	 */
	PDUMPCOMMENT("FreeList TotalPages");
	DevmemPDumpLoadMemValue64(psFreeList->psFWFreelistMemDesc,
							offsetof(RGXFWIF_FREELIST, ui32CurrentPages),
							psFWFreeList->ui32CurrentPages,
							PDUMP_FLAGS_CONTINUOUS);
	PDUMPCOMMENT("FreeList StackPointer");
	DevmemPDumpLoadMemValue64(psFreeList->psFWFreelistMemDesc,
							offsetof(RGXFWIF_FREELIST, ui64CurrentStackTop),
							psFWFreeList->ui64CurrentStackTop,
							PDUMP_FLAGS_CONTINUOUS);

	DevmemReleaseCpuVirtAddr(psFreeList->psFWFreelistMemDesc);


	/* Add initial PB block */
	eError = RGXGrowFreeList(psFreeList,
								ui32InitFLPages,
								&psFreeList->sMemoryBlockInitHead);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"RGXCreateFreeList: failed to allocate initial memory block for free list 0x%016llx (error = %u)",
				sFreeListDevVAddr.uiAddr,
				eError));
		goto ErrorAllocBlock;
	}

	/* return values */
	*ppsFreeList = psFreeList;

	return PVRSRV_OK;

	/* Error handling */

ErrorAllocBlock:
	RGXUnsetFirmwareAddress(psFWFreelistMemDesc);
	DevmemFwFree(psFWFreelistMemDesc);

FWFreeListAlloc:
	SyncPrimFree(psFreeList->psCleanupSync);

SyncAlloc:
	OSFreeMem(psFreeList);

ErrorAllocHost:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


/*
	RGXDestroyFreeList
*/
IMG_EXPORT
PVRSRV_ERROR RGXDestroyFreeList(RGX_FREELIST *psFreeList)
{
	PVRSRV_ERROR eError;
	IMG_UINT64 ui64CheckSum;

	PVR_ASSERT(psFreeList);

	if (psFreeList->ui32RefCount != 0)
	{
		/* Freelist still busy */
		return PVRSRV_ERROR_RETRY;
	}

	/* Freelist is not in use => start firmware cleanup */
	eError = RGXFWRequestFreeListCleanUp(psFreeList->psDevInfo,
										 psFreeList->sFreeListFWDevVAddr,
										 psFreeList->psCleanupSync);
	if(eError != PVRSRV_OK)
	{
		/* Can happen if the firmware took too long to handle the cleanup request,
		 * or if SLC-flushes didn't went through (due to some GPU lockup) */
		return eError;
	}

	if (psFreeList->bCheckFreelist)
	{
		/* Do consistency tests (as the list is fully populated) */
		eError = _FreeListCheckSum(psFreeList,
									&ui64CheckSum);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "RGXDestroyFreeList: Failed to get freelist checksum Node %p",
					 psFreeList));
			sleep_for_ever();
//				PVR_ASSERT(0);
		}

		if (psFreeList->ui64FreelistChecksum != ui64CheckSum)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "RGXDestroyFreeList: Checksum mismatch [%p]! stored 0x%016llx, verified 0x%016llx %p",
					 psFreeList,
					 psFreeList->ui64FreelistChecksum,
					 ui64CheckSum,
					 psFreeList));
			sleep_for_ever();
//			PVR_ASSERT(0);
		}
	}

	/* update the statistics */
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	PVRSRVStatsUpdateFreelistStats(psFreeList->ui32NumGrowReqByApp,
								   psFreeList->ui32NumGrowReqByFW,
								   psFreeList->ui32InitFLPages,
								   psFreeList->ui32NumHighPages);
#endif

	/* Destroy FW structures */
	RGXUnsetFirmwareAddress(psFreeList->psFWFreelistMemDesc);
	DevmemFwFree(psFreeList->psFWFreelistMemDesc);

	/* Remove grow shrink blocks */
	while (!dllist_is_empty(&psFreeList->sMemoryBlockHead))
	{
		eError = RGXShrinkFreeList(&psFreeList->sMemoryBlockHead, psFreeList);
		PVR_ASSERT(eError == PVRSRV_OK);
	}

	/* Remove initial PB block */
	eError = RGXShrinkFreeList(&psFreeList->sMemoryBlockInitHead, psFreeList);
	PVR_ASSERT(eError == PVRSRV_OK);

	/* consistency checks */
	PVR_ASSERT(dllist_is_empty(&psFreeList->sMemoryBlockInitHead));
	PVR_ASSERT(psFreeList->ui32CurrentFLPages == 0);

	/* Remove FreeList from list */
	OSLockAcquire(psFreeList->psDevInfo->hLockFreeList);
	dllist_remove_node(&psFreeList->sNode);
	OSLockRelease(psFreeList->psDevInfo->hLockFreeList);

	SyncPrimFree(psFreeList->psCleanupSync);

	/* free Freelist */
	OSFreeMem(psFreeList);

	return eError;
}



/*
	RGXAddBlockToFreeListKM
*/

IMG_EXPORT
PVRSRV_ERROR RGXAddBlockToFreeListKM(RGX_FREELIST *psFreeList,
										IMG_UINT32 ui32NumPages)
{
	PVRSRV_ERROR eError;

	/* Check if we have reference to freelist's PMR */
	if (psFreeList->psFreeListPMR == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,	"Freelist is not configured for grow"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* grow freelist */
	eError = RGXGrowFreeList(psFreeList,
							ui32NumPages,
							&psFreeList->sMemoryBlockHead);
	if(eError == PVRSRV_OK)
	{
		/* update freelist data in firmware */
		_UpdateFwFreelistSize(psFreeList, IMG_TRUE, ui32NumPages);

		psFreeList->ui32NumGrowReqByApp++;
	}

	return eError;
}

/*
	RGXRemoveBlockFromFreeListKM
*/

IMG_EXPORT
PVRSRV_ERROR RGXRemoveBlockFromFreeListKM(RGX_FREELIST *psFreeList)
{
	PVRSRV_ERROR eError;

	/*
	 * Make sure the pages part of the memory block are not in use anymore.
	 * Instruct the firmware to update the freelist pointers accordingly.
	 */

	eError = RGXShrinkFreeList(&psFreeList->sMemoryBlockHead,
								psFreeList);

	return eError;
}


/*
	RGXCreateRenderTarget
*/
IMG_EXPORT
PVRSRV_ERROR RGXCreateRenderTarget(PVRSRV_DEVICE_NODE	*psDeviceNode,
								   IMG_DEV_VIRTADDR		psVHeapTableDevVAddr,
								   RGX_RT_CLEANUP_DATA 	**ppsCleanupData,
								   IMG_UINT32			*sRenderTargetFWDevVAddr)
{
	PVRSRV_ERROR			eError = PVRSRV_OK;
	RGXFWIF_RENDER_TARGET	*psRenderTarget;
	RGXFWIF_DEV_VIRTADDR	pFirmwareAddr;
	PVRSRV_RGXDEV_INFO 		*psDevInfo = psDeviceNode->pvDevice;
	RGX_RT_CLEANUP_DATA		*psCleanupData;

	psCleanupData = OSAllocMem(sizeof(*psCleanupData));
	if (psCleanupData == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_out;
	}

	OSMemSet(psCleanupData, 0, sizeof(*psCleanupData));
	psCleanupData->psDeviceNode = psDeviceNode;
	/*
	 * This FW render target context is only mapped into kernel for initialisation.
	 * Otherwise this allocation is only used by the FW.
	 * Therefore the GPU cache doesn't need coherency,
	 * and write-combine is suffice on the CPU side (WC buffer will be flushed at the first TA-kick)
	 */
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(*psRenderTarget),
							PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE,
							"FirmwareRenderTarget",
							&psCleanupData->psRenderTargetMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXCreateRenderTarget: DevmemAllocate for Render Target failed"));
		goto err_free;
	}
	RGXSetFirmwareAddress(&pFirmwareAddr, psCleanupData->psRenderTargetMemDesc, 0, RFW_FWADDR_FLAG_NONE);
	*sRenderTargetFWDevVAddr = pFirmwareAddr.ui32Addr;

	DevmemAcquireCpuVirtAddr(psCleanupData->psRenderTargetMemDesc, (IMG_VOID **)&psRenderTarget);
	psRenderTarget->psVHeapTableDevVAddr = psVHeapTableDevVAddr;
	psRenderTarget->bTACachesNeedZeroing = IMG_FALSE;
	PDUMPCOMMENT("Dump RenderTarget");
	DevmemPDumpLoadMem(psCleanupData->psRenderTargetMemDesc, 0, sizeof(*psRenderTarget), PDUMP_FLAGS_CONTINUOUS);
	DevmemReleaseCpuVirtAddr(psCleanupData->psRenderTargetMemDesc);

	*ppsCleanupData = psCleanupData;

err_out:
	return eError;

err_free:
	OSFreeMem(psCleanupData);
	goto err_out;
}


/*
	RGXDestroyRenderTarget
*/
IMG_EXPORT
PVRSRV_ERROR RGXDestroyRenderTarget(RGX_RT_CLEANUP_DATA *psCleanupData)
{
	RGXUnsetFirmwareAddress(psCleanupData->psRenderTargetMemDesc);

	/*
		Note:
		When we get RT cleanup in the FW call that instead
	*/
	/* Flush the the SLC before freeing */
	{
		RGXFWIF_KCCB_CMD sFlushInvalCmd;
		PVRSRV_ERROR eError;
		PVRSRV_DEVICE_NODE *psDeviceNode = psCleanupData->psDeviceNode;

		/* Schedule the SLC flush command ... */
	#if defined(PDUMP)
		PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "Submit SLC flush and invalidate");
	#endif
		sFlushInvalCmd.eCmdType = RGXFWIF_KCCB_CMD_SLCFLUSHINVAL;
		sFlushInvalCmd.uCmdData.sSLCFlushInvalData.bInval = IMG_TRUE;
		sFlushInvalCmd.uCmdData.sSLCFlushInvalData.eDM = RGXFWIF_DM_2D; //Covers all of Sidekick
		sFlushInvalCmd.uCmdData.sSLCFlushInvalData.psContext.ui32Addr = 0;

		eError = RGXSendCommandWithPowLock(psDeviceNode->pvDevice,
											RGXFWIF_DM_GP,
											&sFlushInvalCmd,
											sizeof(sFlushInvalCmd),
											IMG_TRUE);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXFreeUFOBlock: Failed to schedule SLC flush command with error (%u)", eError));
		}
		else
		{
			/* Wait for the SLC flush to complete */
			eError = RGXWaitForFWOp(psDeviceNode->pvDevice, RGXFWIF_DM_GP, psDeviceNode->psSyncPrim, IMG_TRUE);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"RGXFreeUFOBlock: SLC flush and invalidate aborted with error (%u)", eError));
			}
		}
	}

	DevmemFwFree(psCleanupData->psRenderTargetMemDesc);
	OSFreeMem(psCleanupData);
	return PVRSRV_OK;
}

/*
	RGXCreateZSBuffer
*/
IMG_EXPORT
PVRSRV_ERROR RGXCreateZSBufferKM(PVRSRV_DEVICE_NODE	*psDeviceNode,
								DEVMEMINT_RESERVATION 	*psReservation,
								PMR 					*psPMR,
								PVRSRV_MEMALLOCFLAGS_T 	uiMapFlags,
								RGX_ZSBUFFER_DATA **ppsZSBuffer,
								IMG_UINT32 *pui32ZSBufferFWDevVAddr)
{
	PVRSRV_ERROR				eError;
	PVRSRV_RGXDEV_INFO 			*psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_FWZSBUFFER			*psFWZSBuffer;
	RGX_ZSBUFFER_DATA			*psZSBuffer;
	DEVMEM_MEMDESC				*psFWZSBufferMemDesc;
	IMG_BOOL					bOnDemand = ((uiMapFlags & PVRSRV_MEMALLOCFLAG_NO_OSPAGES_ON_ALLOC) > 0);

	/* Allocate host data structure */
	psZSBuffer = OSAllocMem(sizeof(*psZSBuffer));
	if (psZSBuffer == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXCreateZSBufferKM: Failed to allocate cleanup data structure for ZS-Buffer"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorAllocCleanup;
	}
	OSMemSet(psZSBuffer, 0, sizeof(*psZSBuffer));

	eError = SyncPrimAlloc(psDeviceNode->hSyncPrimContext,
						   &psZSBuffer->psCleanupSync);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXCreateZSBufferKM: Failed to allocate cleanup sync (0x%x)",
				eError));
		goto ErrorSyncAlloc;
	}

	/* Populate Host data */
	psZSBuffer->psDevInfo = psDevInfo;
	psZSBuffer->psReservation = psReservation;
	psZSBuffer->psPMR = psPMR;
	psZSBuffer->uiMapFlags = uiMapFlags;
	psZSBuffer->ui32RefCount = 0;
	psZSBuffer->bOnDemand = bOnDemand;
	if (bOnDemand)
	{
		psZSBuffer->ui32ZSBufferID = psDevInfo->ui32ZSBufferCurrID++;
		psZSBuffer->psMapping = IMG_NULL;

		OSLockAcquire(psDevInfo->hLockZSBuffer);
		dllist_add_to_tail(&psDevInfo->sZSBufferHead, &psZSBuffer->sNode);
		OSLockRelease(psDevInfo->hLockZSBuffer);
	}

	/* Allocate firmware memory for ZS-Buffer. */
	PDUMPCOMMENT("Allocate firmware ZS-Buffer data structure");
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(*psFWZSBuffer),
							PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE,
							"FirmwareZSBuffer",
							&psFWZSBufferMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXCreateZSBufferKM: Failed to allocate firmware ZS-Buffer (%u)", eError));
		goto ErrorAllocFWZSBuffer;
	}
	psZSBuffer->psZSBufferMemDesc = psFWZSBufferMemDesc;

	/* Temporarily map the firmware render context to the kernel. */
	eError = DevmemAcquireCpuVirtAddr(psFWZSBufferMemDesc,
									  (IMG_VOID **)&psFWZSBuffer);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXCreateZSBufferKM: Failed to map firmware ZS-Buffer (%u)", eError));
		goto ErrorAcquireFWZSBuffer;
	}

	/* Populate FW ZS-Buffer data structure */
	psFWZSBuffer->bOnDemand = bOnDemand;
	psFWZSBuffer->eState = (bOnDemand) ? RGXFWIF_ZSBUFFER_UNBACKED : RGXFWIF_ZSBUFFER_BACKED;
	psFWZSBuffer->ui32ZSBufferID = psZSBuffer->ui32ZSBufferID;

	/* Get firmware address of ZS-Buffer. */
	RGXSetFirmwareAddress(&psZSBuffer->sZSBufferFWDevVAddr, psFWZSBufferMemDesc, 0, RFW_FWADDR_FLAG_NONE);

	/* Dump the ZS-Buffer and the memory content */
	PDUMPCOMMENT("Dump firmware ZS-Buffer");
	DevmemPDumpLoadMem(psFWZSBufferMemDesc, 0, sizeof(*psFWZSBuffer), PDUMP_FLAGS_CONTINUOUS);

	/* Release address acquired above. */
	DevmemReleaseCpuVirtAddr(psFWZSBufferMemDesc);


	/* define return value */
	*ppsZSBuffer = psZSBuffer;
	*pui32ZSBufferFWDevVAddr = psZSBuffer->sZSBufferFWDevVAddr.ui32Addr;

	PVR_DPF((PVR_DBG_MESSAGE, "ZS-Buffer [%p] created (%s)",
							psZSBuffer,
							(bOnDemand) ? "On-Demand": "Up-front"));

	return PVRSRV_OK;

	/* error handling */

ErrorAcquireFWZSBuffer:
	DevmemFwFree(psFWZSBufferMemDesc);

ErrorAllocFWZSBuffer:
	SyncPrimFree(psZSBuffer->psCleanupSync);

ErrorSyncAlloc:
	OSFreeMem(psZSBuffer);

ErrorAllocCleanup:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


/*
	RGXDestroyZSBuffer
*/
IMG_EXPORT
PVRSRV_ERROR RGXDestroyZSBufferKM(RGX_ZSBUFFER_DATA *psZSBuffer)
{
	POS_LOCK hLockZSBuffer;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psZSBuffer);
	hLockZSBuffer = psZSBuffer->psDevInfo->hLockZSBuffer;

	/* Request ZS Buffer cleanup */
	eError = RGXFWRequestZSBufferCleanUp(psZSBuffer->psDevInfo,
										psZSBuffer->sZSBufferFWDevVAddr,
										psZSBuffer->psCleanupSync);
	if (eError != PVRSRV_ERROR_RETRY)
	{
		/* update the statistics */
		if ((psZSBuffer->bOnDemand) &&
				((psZSBuffer->ui32NumReqByApp > 0) || (psZSBuffer->ui32NumReqByFW > 0)))
		{
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
			PVRSRVStatsUpdateZSBufferStats(psZSBuffer->ui32NumReqByApp,
										   psZSBuffer->ui32NumReqByFW);
#endif
		}

		/* Free the firmware render context. */
		RGXUnsetFirmwareAddress(psZSBuffer->psZSBufferMemDesc);
		DevmemFwFree(psZSBuffer->psZSBufferMemDesc);

		/* Remove Deferred Allocation from list */
		if (psZSBuffer->bOnDemand)
		{
			OSLockAcquire(hLockZSBuffer);
			PVR_ASSERT(dllist_node_is_in_list(&psZSBuffer->sNode));
			dllist_remove_node(&psZSBuffer->sNode);
			OSLockRelease(hLockZSBuffer);
		}

		SyncPrimFree(psZSBuffer->psCleanupSync);

		PVR_ASSERT(psZSBuffer->ui32RefCount == 0);

		PVR_DPF((PVR_DBG_MESSAGE,"ZS-Buffer [%p] destroyed",psZSBuffer));

		/* Free ZS-Buffer host data structure */
		OSFreeMem(psZSBuffer);

	}

	return eError;
}

PVRSRV_ERROR
RGXBackingZSBuffer(RGX_ZSBUFFER_DATA *psZSBuffer)
{
	POS_LOCK hLockZSBuffer;
	PVRSRV_ERROR eError;

	if (!psZSBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if ((psZSBuffer->uiMapFlags & PVRSRV_MEMALLOCFLAG_NO_OSPAGES_ON_ALLOC) == 0)
	{
		/* Only deferred allocations can be populated */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_DPF((PVR_DBG_MESSAGE,"ZS Buffer [%p, ID=0x%08x]: Physical backing requested",
								psZSBuffer,
								psZSBuffer->ui32ZSBufferID));
	hLockZSBuffer = psZSBuffer->psDevInfo->hLockZSBuffer;

	OSLockAcquire(hLockZSBuffer);

	if (psZSBuffer->ui32RefCount == 0)
	{
		if (psZSBuffer->bOnDemand)
		{
			IMG_HANDLE hDevmemHeap;

			PVR_ASSERT(psZSBuffer->psMapping == IMG_NULL);

			/* Get Heap */
			eError = DevmemServerGetHeapHandle(psZSBuffer->psReservation, &hDevmemHeap);
			PVR_ASSERT(psZSBuffer->psMapping == IMG_NULL);

			eError = DevmemIntMapPMR(hDevmemHeap,
									psZSBuffer->psReservation,
									psZSBuffer->psPMR,
									psZSBuffer->uiMapFlags,
									&psZSBuffer->psMapping);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"Unable populate ZS Buffer [%p, ID=0x%08x] with error %u",
										psZSBuffer,
										psZSBuffer->ui32ZSBufferID,
										eError));
				OSLockRelease(hLockZSBuffer);
				return eError;

			}
			PVR_DPF((PVR_DBG_MESSAGE, "ZS Buffer [%p, ID=0x%08x]: Physical backing acquired",
										psZSBuffer,
										psZSBuffer->ui32ZSBufferID));
		}
	}

	/* Increase refcount*/
	psZSBuffer->ui32RefCount++;

	OSLockRelease(hLockZSBuffer);

	return PVRSRV_OK;
}


PVRSRV_ERROR
RGXPopulateZSBufferKM(RGX_ZSBUFFER_DATA *psZSBuffer,
					RGX_POPULATION **ppsPopulation)
{
	RGX_POPULATION *psPopulation;
	PVRSRV_ERROR eError;

	psZSBuffer->ui32NumReqByApp++;

	/* Do the backing */
	eError = RGXBackingZSBuffer(psZSBuffer);
	if (eError != PVRSRV_OK)
	{
		goto OnErrorBacking;
	}

	/* Create the handle to the backing */
	psPopulation = OSAllocMem(sizeof(*psPopulation));
	if (psPopulation == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto OnErrorAlloc;
	}

	psPopulation->psZSBuffer = psZSBuffer;

	/* return value */
	*ppsPopulation = psPopulation;

	return PVRSRV_OK;

OnErrorAlloc:
	RGXUnbackingZSBuffer(psZSBuffer);

OnErrorBacking:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

PVRSRV_ERROR
RGXUnbackingZSBuffer(RGX_ZSBUFFER_DATA *psZSBuffer)
{
	POS_LOCK hLockZSBuffer;
	PVRSRV_ERROR eError;

	if (!psZSBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_ASSERT(psZSBuffer->ui32RefCount);

	PVR_DPF((PVR_DBG_MESSAGE,"ZS Buffer [%p, ID=0x%08x]: Physical backing removal requested",
								psZSBuffer,
								psZSBuffer->ui32ZSBufferID));

	hLockZSBuffer = psZSBuffer->psDevInfo->hLockZSBuffer;

	OSLockAcquire(hLockZSBuffer);

	if (psZSBuffer->bOnDemand)
	{
		if (psZSBuffer->ui32RefCount == 1)
		{
			PVR_ASSERT(psZSBuffer->psMapping);

			eError = DevmemIntUnmapPMR(psZSBuffer->psMapping);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"Unable to unpopulate ZS Buffer [%p, ID=0x%08x] with error %u",
										psZSBuffer,
										psZSBuffer->ui32ZSBufferID,
										eError));
				OSLockRelease(hLockZSBuffer);
				return eError;
			}

			PVR_DPF((PVR_DBG_MESSAGE, "ZS Buffer [%p, ID=0x%08x]: Physical backing removed",
										psZSBuffer,
										psZSBuffer->ui32ZSBufferID));
		}
	}

	/* Decrease refcount*/
	psZSBuffer->ui32RefCount--;

	OSLockRelease(hLockZSBuffer);

	return PVRSRV_OK;
}

PVRSRV_ERROR
RGXUnpopulateZSBufferKM(RGX_POPULATION *psPopulation)
{
	PVRSRV_ERROR eError;

	if (!psPopulation)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = RGXUnbackingZSBuffer(psPopulation->psZSBuffer);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	OSFreeMem(psPopulation);

	return PVRSRV_OK;
}

static IMG_BOOL _FindZSBuffer(PDLLIST_NODE psNode, IMG_PVOID pvCallbackData)
{
	DEVMEM_REF_LOOKUP *psRefLookUp = (DEVMEM_REF_LOOKUP *)pvCallbackData;
	RGX_ZSBUFFER_DATA *psZSBuffer;

	psZSBuffer = IMG_CONTAINER_OF(psNode, RGX_ZSBUFFER_DATA, sNode);

	if (psZSBuffer->ui32ZSBufferID == psRefLookUp->ui32ZSBufferID)
	{
		psRefLookUp->psZSBuffer = psZSBuffer;
		return IMG_FALSE;
	}
	else
	{
		return IMG_TRUE;
	}
}

IMG_VOID RGXProcessRequestZSBufferBacking(PVRSRV_RGXDEV_INFO *psDevInfo,
											IMG_UINT32 ui32ZSBufferID)
{
	DEVMEM_REF_LOOKUP sLookUp;
	RGXFWIF_KCCB_CMD sTACCBCmd;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psDevInfo);

	/* scan all deferred allocations */
	sLookUp.ui32ZSBufferID = ui32ZSBufferID;
	sLookUp.psZSBuffer = IMG_NULL;

	OSLockAcquire(psDevInfo->hLockZSBuffer);
	dllist_foreach_node(&psDevInfo->sZSBufferHead, _FindZSBuffer, (IMG_PVOID)&sLookUp);
	OSLockRelease(psDevInfo->hLockZSBuffer);

	if (sLookUp.psZSBuffer)
	{
		IMG_BOOL bBackingDone = IMG_TRUE;

		/* Populate ZLS */
		eError = RGXBackingZSBuffer(sLookUp.psZSBuffer);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"Populating ZS-Buffer failed failed with error %u (ID = 0x%08x)", eError, ui32ZSBufferID));
			bBackingDone = IMG_FALSE;
		}

		/* send confirmation */
		sTACCBCmd.eCmdType = RGXFWIF_KCCB_CMD_ZSBUFFER_BACKING_UPDATE;
		sTACCBCmd.uCmdData.sZSBufferBackingData.psZSBufferFWDevVAddr = sLookUp.psZSBuffer->sZSBufferFWDevVAddr.ui32Addr;
		sTACCBCmd.uCmdData.sZSBufferBackingData.bDone = bBackingDone;

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError = RGXScheduleCommand(psDevInfo,
												RGXFWIF_DM_TA,
												&sTACCBCmd,
												sizeof(sTACCBCmd),
												IMG_FALSE);
			if (eError != PVRSRV_ERROR_RETRY)
			{
				break;
			}
			OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();

		/* Kernel CCB should never fill up, as the FW is processing them right away  */
		PVR_ASSERT(eError == PVRSRV_OK);

		sLookUp.psZSBuffer->ui32NumReqByFW++;
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,"ZS Buffer Lookup for ZS Buffer ID 0x%08x failed (Populate)", sLookUp.ui32ZSBufferID));
	}
}

IMG_VOID RGXProcessRequestZSBufferUnbacking(PVRSRV_RGXDEV_INFO *psDevInfo,
											IMG_UINT32 ui32ZSBufferID)
{
	DEVMEM_REF_LOOKUP sLookUp;
	RGXFWIF_KCCB_CMD sTACCBCmd;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psDevInfo);

	/* scan all deferred allocations */
	sLookUp.ui32ZSBufferID = ui32ZSBufferID;
	sLookUp.psZSBuffer = IMG_NULL;

	OSLockAcquire(psDevInfo->hLockZSBuffer);
	dllist_foreach_node(&psDevInfo->sZSBufferHead, _FindZSBuffer, (IMG_PVOID)&sLookUp);
	OSLockRelease(psDevInfo->hLockZSBuffer);

	if (sLookUp.psZSBuffer)
	{
		/* Unpopulate ZLS */
		eError = RGXUnbackingZSBuffer(sLookUp.psZSBuffer);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"UnPopulating ZS-Buffer failed failed with error %u (ID = 0x%08x)", eError, ui32ZSBufferID));
			PVR_ASSERT(IMG_FALSE);
		}

		/* send confirmation */
		sTACCBCmd.eCmdType = RGXFWIF_KCCB_CMD_ZSBUFFER_UNBACKING_UPDATE;
		sTACCBCmd.uCmdData.sZSBufferBackingData.psZSBufferFWDevVAddr = sLookUp.psZSBuffer->sZSBufferFWDevVAddr.ui32Addr;
		sTACCBCmd.uCmdData.sZSBufferBackingData.bDone = IMG_TRUE;

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError = RGXScheduleCommand(psDevInfo,
												RGXFWIF_DM_TA,
												&sTACCBCmd,
												sizeof(sTACCBCmd),
												IMG_FALSE);
			if (eError != PVRSRV_ERROR_RETRY)
			{
				break;
			}
			OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();

		/* Kernel CCB should never fill up, as the FW is processing them right away  */
		PVR_ASSERT(eError == PVRSRV_OK);

	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,"ZS Buffer Lookup for ZS Buffer ID 0x%08x failed (UnPopulate)", sLookUp.ui32ZSBufferID));
	}
}

static
PVRSRV_ERROR _CreateTAContext(CONNECTION_DATA *psConnection,
							  PVRSRV_DEVICE_NODE *psDeviceNode,
							  DEVMEM_MEMDESC *psAllocatedMemDesc,
							  IMG_UINT32 ui32AllocatedOffset,
							  DEVMEM_MEMDESC *psFWMemContextMemDesc,
							  IMG_DEV_VIRTADDR sVDMCallStackAddr,
							  IMG_UINT32 ui32Priority,
							  RGX_COMMON_CONTEXT_INFO *psInfo,
							  RGX_SERVER_RC_TA_DATA *psTAData)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_TACTX_STATE *psContextState;
	PVRSRV_ERROR eError;
	/*
		Allocate device memory for the firmware GPU context suspend state.
		Note: the FW reads/writes the state to memory by accessing the GPU register interface.
	*/
	PDUMPCOMMENT("Allocate RGX firmware TA context suspend state");

	eError = DevmemFwAllocate(psDevInfo,
							  sizeof(RGXFWIF_TACTX_STATE),
							  RGX_FWCOMCTX_ALLOCFLAGS,
							  "FirmwareTAContextState",
							  &psTAData->psContextStateMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRenderContextKM: Failed to allocate firmware GPU context suspend state (%u)",
				eError));
		goto fail_tacontextsuspendalloc;
	}

	eError = DevmemAcquireCpuVirtAddr(psTAData->psContextStateMemDesc,
									  (IMG_VOID **)&psContextState);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRenderContextKM: Failed to map firmware render context state (%u)",
				eError));
		goto fail_suspendcpuvirtacquire;
	}
	psContextState->uTAReg_VDM_CALL_STACK_POINTER_Init = sVDMCallStackAddr.uiAddr;
	DevmemReleaseCpuVirtAddr(psTAData->psContextStateMemDesc);

	eError = FWCommonContextAllocate(psConnection,
									 psDeviceNode,
									 "TA",
									 psAllocatedMemDesc,
									 ui32AllocatedOffset,
									 psFWMemContextMemDesc,
									 psTAData->psContextStateMemDesc,
									 RGX_CCB_SIZE_LOG2,
									 ui32Priority,
									 psInfo,
									 &psTAData->psServerCommonContext);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRenderContextKM: Failed to init TA fw common context (%u)",
				eError));
		goto fail_tacommoncontext;
	}

	/*
	 * Dump the FW 3D context suspend state buffer
	 */
	PDUMPCOMMENT("Dump the TA context suspend state buffer");
	DevmemPDumpLoadMem(psTAData->psContextStateMemDesc,
					   0,
					   sizeof(RGXFWIF_TACTX_STATE),
					   PDUMP_FLAGS_CONTINUOUS);

	psTAData->ui32Priority = ui32Priority;
	return PVRSRV_OK;

fail_tacommoncontext:
fail_suspendcpuvirtacquire:
	DevmemFwFree(psTAData->psContextStateMemDesc);
fail_tacontextsuspendalloc:
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}

static
PVRSRV_ERROR _Create3DContext(CONNECTION_DATA *psConnection,
							  PVRSRV_DEVICE_NODE *psDeviceNode,
							  DEVMEM_MEMDESC *psAllocatedMemDesc,
							  IMG_UINT32 ui32AllocatedOffset,
							  DEVMEM_MEMDESC *psFWMemContextMemDesc,
							  IMG_UINT32 ui32Priority,
							  RGX_COMMON_CONTEXT_INFO *psInfo,
							  RGX_SERVER_RC_3D_DATA *ps3DData)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR eError;

	/*
		Allocate device memory for the firmware GPU context suspend state.
		Note: the FW reads/writes the state to memory by accessing the GPU register interface.
	*/
	PDUMPCOMMENT("Allocate RGX firmware 3D context suspend state");

	eError = DevmemFwAllocate(psDevInfo,
							  sizeof(RGXFWIF_3DCTX_STATE),
							  RGX_FWCOMCTX_ALLOCFLAGS,
							  "Firmware3DContextState",
							  &ps3DData->psContextStateMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRenderContextKM: Failed to allocate firmware GPU context suspend state (%u)",
				eError));
		goto fail_3dcontextsuspendalloc;
	}

	eError = FWCommonContextAllocate(psConnection,
									 psDeviceNode,
									 "3D",
									 psAllocatedMemDesc,
									 ui32AllocatedOffset,
									 psFWMemContextMemDesc,
									 ps3DData->psContextStateMemDesc,
									 RGX_CCB_SIZE_LOG2,
									 ui32Priority,
									 psInfo,
									 &ps3DData->psServerCommonContext);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRenderContextKM: Failed to init 3D fw common context (%u)",
				eError));
		goto fail_3dcommoncontext;
	}

	/*
	 * Dump the FW 3D context suspend state buffer
	 */
	PDUMPCOMMENT("Dump the 3D context suspend state buffer");
	DevmemPDumpLoadMem(ps3DData->psContextStateMemDesc,
					   0,
					   sizeof(RGXFWIF_3DCTX_STATE),
					   PDUMP_FLAGS_CONTINUOUS);

	ps3DData->ui32Priority = ui32Priority;
	return PVRSRV_OK;

fail_3dcommoncontext:
	DevmemFwFree(ps3DData->psContextStateMemDesc);
fail_3dcontextsuspendalloc:
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}


/*
 * PVRSRVRGXCreateRenderContextKM
 */
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXCreateRenderContextKM(CONNECTION_DATA				*psConnection,
											PVRSRV_DEVICE_NODE			*psDeviceNode,
											IMG_UINT32					ui32Priority,
											IMG_DEV_VIRTADDR			sMCUFenceAddr,
											IMG_DEV_VIRTADDR			sVDMCallStackAddr,
											IMG_UINT32					ui32FrameworkRegisterSize,
											IMG_PBYTE					pabyFrameworkRegisters,
											IMG_HANDLE					hMemCtxPrivData,
											RGX_SERVER_RENDER_CONTEXT	**ppsRenderContext)
{
	PVRSRV_ERROR				eError;
	PVRSRV_RGXDEV_INFO 			*psDevInfo = psDeviceNode->pvDevice;
	RGX_SERVER_RENDER_CONTEXT	*psRenderContext;
	DEVMEM_MEMDESC				*psFWMemContextMemDesc = RGXGetFWMemDescFromMemoryContextHandle(hMemCtxPrivData);
	RGX_COMMON_CONTEXT_INFO		sInfo;

	/* Prepare cleanup structure */
	psRenderContext = OSAllocMem(sizeof(*psRenderContext));
	if (psRenderContext == IMG_NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	OSMemSet(psRenderContext, 0, sizeof(*psRenderContext));
	psRenderContext->psDeviceNode = psDeviceNode;
	*ppsRenderContext= psRenderContext;

	/*
		Create the FW render context, this has the TA and 3D FW common
		contexts embedded within it
	*/
	eError = DevmemFwAllocate(psDevInfo,
							  sizeof(RGXFWIF_FWRENDERCONTEXT),
							  RGX_FWCOMCTX_ALLOCFLAGS,
							  "FirmwareRenderContext",
							  &psRenderContext->psFWRenderContextMemDesc);
	if (eError != PVRSRV_OK)
	{
		goto fail_fwrendercontext;
	}

	/*
		As the common context alloc will dump the TA and 3D common contexts
		after the've been setup we skip of the 2 common contexts and dump the
		rest of the structure
	*/
	PDUMPCOMMENT("Dump shared part of render context context");
	DevmemPDumpLoadMem(psRenderContext->psFWRenderContextMemDesc,
					   (sizeof(RGXFWIF_FWCOMMONCONTEXT) * 2),
					   sizeof(RGXFWIF_FWRENDERCONTEXT) - (sizeof(RGXFWIF_FWCOMMONCONTEXT) * 2),
					   PDUMP_FLAGS_CONTINUOUS);

	/* Allocate cleanup sync */
	eError = SyncPrimAlloc(psDeviceNode->hSyncPrimContext,
						   &psRenderContext->psCleanupSync);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRenderContextKM: Failed to allocate cleanup sync (0x%x)",
				eError));
		goto fail_syncalloc;
	}

	/*
	 * Create the FW framework buffer
	 */
	eError = PVRSRVRGXFrameworkCreateKM(psDeviceNode,
										&psRenderContext->psFWFrameworkMemDesc,
										ui32FrameworkRegisterSize);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRenderContextKM: Failed to allocate firmware GPU framework state (%u)",
				eError));
		goto fail_frameworkcreate;
	}

	/* Copy the Framework client data into the framework buffer */
	eError = PVRSRVRGXFrameworkCopyCommand(psRenderContext->psFWFrameworkMemDesc,
										   pabyFrameworkRegisters,
										   ui32FrameworkRegisterSize);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRenderContextKM: Failed to populate the framework buffer (%u)",
				eError));
		goto fail_frameworkcopy;
	}

	sInfo.psFWFrameworkMemDesc = psRenderContext->psFWFrameworkMemDesc;
	sInfo.psMCUFenceAddr = &sMCUFenceAddr;

	eError = _CreateTAContext(psConnection,
							  psDeviceNode,
							  psRenderContext->psFWRenderContextMemDesc,
							  offsetof(RGXFWIF_FWRENDERCONTEXT, sTAContext),
							  psFWMemContextMemDesc,
							  sVDMCallStackAddr,
							  ui32Priority,
							  &sInfo,
							  &psRenderContext->sTAData);
	if (eError != PVRSRV_OK)
	{
		goto fail_tacontext;
	}

	eError = _Create3DContext(psConnection,
							  psDeviceNode,
							  psRenderContext->psFWRenderContextMemDesc,
							  offsetof(RGXFWIF_FWRENDERCONTEXT, s3DContext),
							  psFWMemContextMemDesc,
							  ui32Priority,
							  &sInfo,
							  &psRenderContext->s3DData);
	if (eError != PVRSRV_OK)
	{
		goto fail_3dcontext;
	}

	{
		PVRSRV_RGXDEV_INFO			*psDevInfo = psDeviceNode->pvDevice;
		dllist_add_to_tail(&(psDevInfo->sRenderCtxtListHead), &(psRenderContext->sListNode));
	}

	return PVRSRV_OK;

fail_3dcontext:
	_DestroyTAContext(&psRenderContext->sTAData,
					  psDeviceNode,
					  psRenderContext->psCleanupSync);
fail_tacontext:
fail_frameworkcopy:
	DevmemFwFree(psRenderContext->psFWFrameworkMemDesc);
fail_frameworkcreate:
	SyncPrimFree(psRenderContext->psCleanupSync);
fail_syncalloc:
	DevmemFwFree(psRenderContext->psFWRenderContextMemDesc);
fail_fwrendercontext:
	OSFreeMem(psRenderContext);
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}

/*
 * PVRSRVRGXDestroyRenderContextKM
 */
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXDestroyRenderContextKM(RGX_SERVER_RENDER_CONTEXT *psRenderContext)
{
	PVRSRV_ERROR				eError;

	/* Cleanup the TA if we haven't already */
	if ((psRenderContext->ui32CleanupStatus & RC_CLEANUP_TA_COMPLETE) == 0)
	{
		eError = _DestroyTAContext(&psRenderContext->sTAData,
								   psRenderContext->psDeviceNode,
								   psRenderContext->psCleanupSync);
		if (eError != PVRSRV_ERROR_RETRY)
		{
			psRenderContext->ui32CleanupStatus |= RC_CLEANUP_TA_COMPLETE;
		}
		else
		{
			goto e0;
		}
	}

	/* Cleanup the 3D if we haven't already */
	if ((psRenderContext->ui32CleanupStatus & RC_CLEANUP_3D_COMPLETE) == 0)
	{
		eError = _Destroy3DContext(&psRenderContext->s3DData,
								   psRenderContext->psDeviceNode,
								   psRenderContext->psCleanupSync);
		if (eError != PVRSRV_ERROR_RETRY)
		{
			psRenderContext->ui32CleanupStatus |= RC_CLEANUP_3D_COMPLETE;
		}
		else
		{
			goto e0;
		}
	}

	/*
		Only if both TA and 3D contexts have been cleaned up can we
		free the shared resources
	*/
	if (psRenderContext->ui32CleanupStatus == (RC_CLEANUP_3D_COMPLETE | RC_CLEANUP_TA_COMPLETE))
	{
		RGXFWIF_FWRENDERCONTEXT	*psFWRenderContext;

		dllist_remove_node(&(psRenderContext->sListNode));

		/* Update SPM statistics */
		eError = DevmemAcquireCpuVirtAddr(psRenderContext->psFWRenderContextMemDesc,
										  (IMG_VOID **)&psFWRenderContext);
		if (eError == PVRSRV_OK)
		{
			if ((psFWRenderContext->ui32TotalNumPartialRenders > 0) ||
				(psFWRenderContext->ui32TotalNumOutOfMemory > 0))
			{
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
				PVRSRVStatsUpdateRenderContextStats(psFWRenderContext->ui32TotalNumPartialRenders,
													psFWRenderContext->ui32TotalNumOutOfMemory, 0, 0, 0);
#endif
			}

			DevmemReleaseCpuVirtAddr(psRenderContext->psFWRenderContextMemDesc);
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXDestroyRenderContextKM: Failed to map firmware render context (%u)",
					eError));
		}

		/* Free the framework buffer */
		DevmemFwFree(psRenderContext->psFWFrameworkMemDesc);

		/* Free the firmware render context */
		DevmemFwFree(psRenderContext->psFWRenderContextMemDesc);

		/* Free the cleanup sync */
		SyncPrimFree(psRenderContext->psCleanupSync);

		OSFreeMem(psRenderContext);
	}

	return PVRSRV_OK;

e0:
	return eError;
}

/*
 * PVRSRVRGXKickTA3DKM
 */
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXKickTA3DKM(RGX_SERVER_RENDER_CONTEXT	*psRenderContext,
								 IMG_UINT32					ui32ClientTAFenceCount,
								 PRGXFWIF_UFO_ADDR			*pauiClientTAFenceUFOAddress,
								 IMG_UINT32					*paui32ClientTAFenceValue,
								 IMG_UINT32					ui32ClientTAUpdateCount,
								 PRGXFWIF_UFO_ADDR			*pauiClientTAUpdateUFOAddress,
								 IMG_UINT32					*paui32ClientTAUpdateValue,
								 IMG_UINT32					ui32ServerTASyncPrims,
								 IMG_UINT32					*paui32ServerTASyncFlags,
								 SERVER_SYNC_PRIMITIVE 		**pasServerTASyncs,
								 IMG_UINT32					ui32Client3DFenceCount,
								 PRGXFWIF_UFO_ADDR			*pauiClient3DFenceUFOAddress,
								 IMG_UINT32					*paui32Client3DFenceValue,
								 IMG_UINT32					ui32Client3DUpdateCount,
								 PRGXFWIF_UFO_ADDR			*pauiClient3DUpdateUFOAddress,
								 IMG_UINT32					*paui32Client3DUpdateValue,
								 IMG_UINT32					ui32Server3DSyncPrims,
								 IMG_UINT32					*paui32Server3DSyncFlags,
								 SERVER_SYNC_PRIMITIVE 		**pasServer3DSyncs,
								 PRGXFWIF_UFO_ADDR			uiPRFenceUFOAddress,
								 IMG_UINT32					ui32PRFenceValue,
								 IMG_UINT32					ui32NumFenceFds,
								 IMG_INT32					*ai32FenceFds,
								 IMG_UINT32					ui32TACmdSize,
								 IMG_PBYTE					pui8TADMCmd,
								 IMG_UINT32					ui323DPRCmdSize,
								 IMG_PBYTE					pui83DPRDMCmd,
								 IMG_UINT32					ui323DCmdSize,
								 IMG_PBYTE					pui83DDMCmd,
								 IMG_UINT32					ui32TAFrameNum,
								 IMG_UINT32					ui32TARTData,
								 IMG_BOOL					bLastTAInScene,
								 IMG_BOOL					bKickTA,
								 IMG_BOOL					bKickPR,
								 IMG_BOOL					bKick3D,
								 IMG_BOOL					bAbort,
								 IMG_BOOL					bPDumpContinuous,
								 RGX_RTDATA_CLEANUP_DATA	*psRTDataCleanup,
								 RGX_ZSBUFFER_DATA		*psZBuffer,
								 RGX_ZSBUFFER_DATA		*psSBuffer,
								 IMG_BOOL			bCommitRefCountsTA,
								 IMG_BOOL			bCommitRefCounts3D,
								 IMG_BOOL			*pbCommittedRefCountsTA,
								 IMG_BOOL			*pbCommittedRefCounts3D)
{
	/* 1 command for the TA */
	RGX_CCB_CMD_HELPER_DATA sTACmdHelperData;
	/* Up to 3 commands for the 3D (partial render fence, partial reader, and render) */
	RGX_CCB_CMD_HELPER_DATA as3DCmdHelperData[3];
	IMG_UINT32				ui32TACmdCount=0;
	IMG_UINT32				ui323DCmdCount=0;
	IMG_BOOL				bKickTADM = IMG_FALSE;
	IMG_BOOL				bKick3DDM = IMG_FALSE;
	RGXFWIF_UFO				sPRUFO;
	IMG_UINT32				*paui32Server3DSyncFlagsPR = IMG_NULL;
	IMG_UINT32				*paui32Server3DSyncFlags3D = IMG_NULL;
	IMG_UINT32				i;
	PVRSRV_ERROR			eError = PVRSRV_OK;
	PVRSRV_ERROR			eError2;

	/* Internal client sync info, used to help with merging of Android fd syncs */
	IMG_UINT32				ui32IntClientTAFenceCount = 0;
	PRGXFWIF_UFO_ADDR		*pauiIntClientTAFenceUFOAddress = IMG_NULL;
	IMG_UINT32				*paui32IntClientTAFenceValue = IMG_NULL;

#if defined(PVR_ANDROID_NATIVE_WINDOW_HAS_SYNC)
	/* Android fd sync update info */
	IMG_BOOL				bSyncsMerged = IMG_FALSE;
#endif
	IMG_UINT32 				ui32NumUpdateSyncs = 0;
	PRGXFWIF_UFO_ADDR 		*puiUpdateFWAddrs = IMG_NULL;
	IMG_UINT32 				*pui32UpdateValues = IMG_NULL;

	*pbCommittedRefCountsTA = IMG_FALSE;
	*pbCommittedRefCounts3D = IMG_FALSE;

	/* Sanity check the server fences */
	for (i=0;i<ui32ServerTASyncPrims;i++)
	{
		if (!(paui32ServerTASyncFlags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Server fence (on TA) must fence", __FUNCTION__));
			return PVRSRV_ERROR_INVALID_SYNC_PRIM_OP;
		}
	}

	for (i=0;i<ui32Server3DSyncPrims;i++)
	{
		if (!(paui32Server3DSyncFlags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Server fence (on 3D) must fence", __FUNCTION__));
			return PVRSRV_ERROR_INVALID_SYNC_PRIM_OP;
		}
	}

	/*
		Sanity check we have a PR kick if there are client or server fences
	*/
#if defined(UNDER_WDDM)
	/* sanity check only applies to WDDM for a TA kick. 3D kicks don't have a PR kick
	 * but do have fences
	 */
	if(bKickTA)
	{
#endif
		if (!bKickPR & ((ui32Client3DFenceCount != 0) || (ui32Server3DSyncPrims != 0)))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: 3D fence (client or server) passed without a PR kick", __FUNCTION__));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
#if defined(UNDER_WDDM)
	}
#endif

	/* Init and acquire to TA command if required */
	if(bKickTA)
	{
		RGX_SERVER_RC_TA_DATA *psTAData = &psRenderContext->sTAData;

#if defined(PVR_ANDROID_NATIVE_WINDOW_HAS_SYNC)
		if (ui32NumFenceFds)
		{
			/*
				Android fd sync fence info we can declare here as the
				data they contain gets merged into *IntClientTAFence* so we
				don't need to scope them beyond this if statement
			*/
			IMG_UINT32 ui32NumFenceSyncs = 0;
			PRGXFWIF_UFO_ADDR *puiFenceFWAddrs = IMG_NULL;
			IMG_UINT32 *pui32FenceValues = IMG_NULL;

			/*
				This call is only using the Android fd sync to fence the
				TA command. There is an update but this is used to
				indicate that the fence has been finished with and so it
				can happen after the PR as by then we've finished using
				the fd sync
			*/
			eError = PVRFDSyncQueryFencesKM(ui32NumFenceFds,
											ai32FenceFds,
											IMG_FALSE,
											&ui32NumFenceSyncs,
											&puiFenceFWAddrs,
											&pui32FenceValues,
											&ui32NumUpdateSyncs,
											&puiUpdateFWAddrs,
											&pui32UpdateValues);
			if (eError != PVRSRV_OK)
			{
				goto fail_fdsync;
			}

			/*
				Merge the Android syncs and the client fences together
			*/
			ui32IntClientTAFenceCount = ui32ClientTAFenceCount + ui32NumFenceSyncs;
			pauiIntClientTAFenceUFOAddress = OSAllocMem(sizeof(*pauiIntClientTAFenceUFOAddress)* ui32IntClientTAFenceCount);
			if (pauiIntClientTAFenceUFOAddress == IMG_NULL)
			{
				/* Free memory created by PVRFDSyncQueryFencesKM */
				OSFreeMem(puiFenceFWAddrs);
				OSFreeMem(pui32FenceValues);
				OSFreeMem(puiUpdateFWAddrs);
				OSFreeMem(pui32UpdateValues);

				eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto fail_fenceUFOarray;
			}
			paui32IntClientTAFenceValue = OSAllocMem(sizeof(*paui32IntClientTAFenceValue)* ui32IntClientTAFenceCount);
			if (paui32IntClientTAFenceValue == IMG_NULL)
			{
				/* Free memory created by PVRFDSyncQueryFencesKM */
				OSFreeMem(puiFenceFWAddrs);
				OSFreeMem(pui32FenceValues);
				OSFreeMem(puiUpdateFWAddrs);
				OSFreeMem(pui32UpdateValues);

				OSFreeMem(pauiIntClientTAFenceUFOAddress);
				eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto fail_fencevaluearray;
			}

			SYNC_MERGE_CLIENT_FENCES(ui32IntClientTAFenceCount, pauiIntClientTAFenceUFOAddress, paui32IntClientTAFenceValue,
									 ui32NumFenceSyncs, puiFenceFWAddrs, pui32FenceValues,
									 ui32ClientTAFenceCount, pauiClientTAFenceUFOAddress, paui32ClientTAFenceValue);

			/* Free memory created by PVRFDSyncQueryFencesKM */
			OSFreeMem(puiFenceFWAddrs);
			OSFreeMem(pui32FenceValues);

			if (ui32NumFenceSyncs || ui32NumUpdateSyncs)
			{
				PDUMPCOMMENT("(TA) Android native fences in use: %u fence syncs, %u update syncs",
							 ui32NumFenceSyncs, ui32NumUpdateSyncs);
			}
			bSyncsMerged = IMG_TRUE;
		}
		else
#endif /* PVR_ANDROID_NATIVE_WINDOW_HAS_SYNC */
		{
			/* No client sync merging so just copy across the pointers */
			ui32IntClientTAFenceCount = ui32ClientTAFenceCount;
			pauiIntClientTAFenceUFOAddress = pauiClientTAFenceUFOAddress;
			paui32IntClientTAFenceValue = paui32ClientTAFenceValue;
		}

		/* Init the TA command helper */
		eError = RGXCmdHelperInitCmdCCB(FWCommonContextGetClientCCB(psTAData->psServerCommonContext),
										  ui32IntClientTAFenceCount,
										  pauiIntClientTAFenceUFOAddress,
										  paui32IntClientTAFenceValue,
										  ui32ClientTAUpdateCount,
										  pauiClientTAUpdateUFOAddress,
										  paui32ClientTAUpdateValue,
										  ui32ServerTASyncPrims,
										  paui32ServerTASyncFlags,
										  pasServerTASyncs,
										  ui32TACmdSize,
										  pui8TADMCmd,
										  RGXFWIF_CCB_CMD_TYPE_TA,
										  bPDumpContinuous,
										  "TA",
										  &sTACmdHelperData);
		if (eError != PVRSRV_OK)
		{
			goto fail_tacmdinit;
		}

		eError = RGXCmdHelperAcquireCmdCCB(1,
										   &sTACmdHelperData,
										   &bKickTADM);
		if (eError != PVRSRV_OK)
		{
			if (!bKickTADM)
			{
				goto fail_taacquirecmd;
			}
			else
			{
				/* commit the TA ref counting next time, when the CCB space is successfully
				 * acquired
				 */
				bCommitRefCountsTA = IMG_FALSE;
			}
		}
		else
		{
			ui32TACmdCount++;
		}
	}

	/* Only kick the 3D if required */
	if (eError == PVRSRV_OK)
	{
	if (bKickPR)
	{
		RGX_SERVER_RC_3D_DATA *ps3DData = &psRenderContext->s3DData;

		if (ui32Server3DSyncPrims)
		{
			/*
				The fence (and possible update) straddle multiple commands so
				we have to modify the flags to do the right things at the right
				time.
				At this stage we should only fence, any updates will happen with
				the normal 3D command.
			*/
			paui32Server3DSyncFlagsPR = OSAllocMem(sizeof(IMG_UINT32) * ui32Server3DSyncPrims);
			if (paui32Server3DSyncFlagsPR == IMG_NULL)
			{
				eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto fail_prserversyncflagsallocpr;
			}

			/* Copy only the fence flag across */
			for (i=0;i<ui32Server3DSyncPrims;i++)
			{
				paui32Server3DSyncFlagsPR[i] = paui32Server3DSyncFlags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK;
			}
		}

		/*
			The command helper doesn't know about the PR fence so create
			the command with all the fences against it and later create
			the PR command itself which _must_ come after the PR fence.
		*/
		sPRUFO.puiAddrUFO = uiPRFenceUFOAddress;
		sPRUFO.ui32Value = ui32PRFenceValue;

		/* Init the PR fence command helper */
		eError = RGXCmdHelperInitCmdCCB(FWCommonContextGetClientCCB(ps3DData->psServerCommonContext),
										ui32Client3DFenceCount,
										pauiClient3DFenceUFOAddress,
										paui32Client3DFenceValue,
										0,
										IMG_NULL,
										IMG_NULL,
										ui32Server3DSyncPrims,
										paui32Server3DSyncFlagsPR,
										pasServer3DSyncs,
										sizeof(sPRUFO),
										(IMG_UINT8*) &sPRUFO,
										RGXFWIF_CCB_CMD_TYPE_FENCE_PR,
										bPDumpContinuous,
										"3D-PR Fence",
										&as3DCmdHelperData[ui323DCmdCount++]);
		if (eError != PVRSRV_OK)
		{
			goto fail_prfencecmdinit;
		}

		/* Init the 3D PR command helper */
		/*
			See note above PVRFDSyncQueryFencesKM as to why updates for android
			syncs are passed in with the PR
		*/
		eError = RGXCmdHelperInitCmdCCB(FWCommonContextGetClientCCB(ps3DData->psServerCommonContext),
										0,
										IMG_NULL,
										IMG_NULL,
										ui32NumUpdateSyncs,
										puiUpdateFWAddrs,
										pui32UpdateValues,
										0,
										IMG_NULL,
										IMG_NULL,
										ui323DPRCmdSize,
										pui83DPRDMCmd,
										RGXFWIF_CCB_CMD_TYPE_3D_PR,
										bPDumpContinuous,
										"3D-PR",
										&as3DCmdHelperData[ui323DCmdCount++]);
		if (eError != PVRSRV_OK)
		{
			goto fail_prcmdinit;
		}
	}

	if (bKick3D || bAbort)
	{
		RGX_SERVER_RC_3D_DATA *ps3DData = &psRenderContext->s3DData;

		if (ui32Server3DSyncPrims)
		{
			/*
				Copy only the update flags for the 3D as the fences will be in
				the PR command created above
			*/
			paui32Server3DSyncFlags3D = OSAllocMem(sizeof(IMG_UINT32) * ui32Server3DSyncPrims);
			if (paui32Server3DSyncFlags3D == IMG_NULL)
			{
				eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto fail_prserversyncflagsalloc3d;
			}

			/* Copy only the update flag across */
			for (i=0;i<ui32Server3DSyncPrims;i++)
			{
				paui32Server3DSyncFlags3D[i] = paui32Server3DSyncFlags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_UPDATE;
			}
		}

		/* Init the 3D command helper */
		eError = RGXCmdHelperInitCmdCCB(FWCommonContextGetClientCCB(ps3DData->psServerCommonContext),
										  0,
										  IMG_NULL,
										  IMG_NULL,
										  ui32Client3DUpdateCount,
										  pauiClient3DUpdateUFOAddress,
										  paui32Client3DUpdateValue,
										  ui32Server3DSyncPrims,
										  paui32Server3DSyncFlags3D,
										  pasServer3DSyncs,
										  ui323DCmdSize,
										  pui83DDMCmd,
										  RGXFWIF_CCB_CMD_TYPE_3D,
										  bPDumpContinuous,
										  "3D",
										  &as3DCmdHelperData[ui323DCmdCount++]);
		if (eError != PVRSRV_OK)
		{
			goto fail_3dcmdinit;
		}
	}

	if (ui323DCmdCount)
	{
		PVR_ASSERT(bKickPR || bKick3D);

		/* Acquire space for all the 3D command(s) */
		eError = RGXCmdHelperAcquireCmdCCB(ui323DCmdCount,
										   as3DCmdHelperData,
										   &bKick3DDM);
		if (eError != PVRSRV_OK)
		{
			if (!bKick3DDM && !bKickTADM)
			{
				goto fail_3dacquirecmd;
			}
			else
			{
				/*
					There are no DM commands but we still need to kick the
					DM to flush out the padding command.
					Also reset the TA count as we're going to try again
				*/
				ui32TACmdCount = 0;
				ui323DCmdCount = 0;

				/* commit the 3D ref counting next time, when the CCB space is
				 * successfully acquired
				 */
				bCommitRefCounts3D = IMG_FALSE;
			}
		}
	}
	}

	/*
		We should acquire the space in the kernel CCB here as after this point
		we release the commands which will take operations on server syncs
		which can't be undone
	*/

	/*
		Everything is ready to go now, release the commands
	*/
	if (ui32TACmdCount)
	{
		RGXCmdHelperReleaseCmdCCB(ui32TACmdCount,
								  &sTACmdHelperData,
								  "TA",
								  FWCommonContextGetFWAddress(psRenderContext->sTAData.psServerCommonContext).ui32Addr);
	}

	if (ui323DCmdCount)
	{
		RGXCmdHelperReleaseCmdCCB(ui323DCmdCount,
								  as3DCmdHelperData,
								  "3D",
								  FWCommonContextGetFWAddress(psRenderContext->s3DData.psServerCommonContext).ui32Addr);
	}

	if (bKickTADM)
	{
		RGXFWIF_KCCB_CMD sTAKCCBCmd;

		/* Construct the kernel TA CCB command. */
		sTAKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
		sTAKCCBCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psRenderContext->sTAData.psServerCommonContext);
		sTAKCCBCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psRenderContext->sTAData.psServerCommonContext));

		if(bCommitRefCountsTA)
		{
			AttachKickResourcesCleanupCtls((PRGXFWIF_CLEANUP_CTL *) &sTAKCCBCmd.uCmdData.sCmdKickData.apsCleanupCtl,
										&sTAKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl,
										RGXFWIF_DM_TA,
										bKickTA,
										psRTDataCleanup,
										psZBuffer,
										psSBuffer);
			*pbCommittedRefCountsTA = IMG_TRUE;
		}
		else
		{
			sTAKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;
		}

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError2 = RGXScheduleCommand(psRenderContext->psDeviceNode->pvDevice,
										RGXFWIF_DM_TA,
										&sTAKCCBCmd,
										sizeof(sTAKCCBCmd),
										bPDumpContinuous);
			if (eError2 != PVRSRV_ERROR_RETRY)
			{
				break;
			}
			OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();

#if defined(SUPPORT_GPUTRACE_EVENTS)
			RGXHWPerfFTraceGPUEnqueueEvent(psRenderContext->psDeviceNode->pvDevice,
					ui32TAFrameNum, ui32TARTData, "TA3D");
#endif

	}

	if (bKick3DDM)
	{
		RGXFWIF_KCCB_CMD s3DKCCBCmd;

		/* Construct the kernel 3D CCB command. */
		s3DKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
		s3DKCCBCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psRenderContext->s3DData.psServerCommonContext);
		s3DKCCBCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psRenderContext->s3DData.psServerCommonContext));

		if(bCommitRefCounts3D)
		{
			AttachKickResourcesCleanupCtls((PRGXFWIF_CLEANUP_CTL *) &s3DKCCBCmd.uCmdData.sCmdKickData.apsCleanupCtl,
											&s3DKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl,
											RGXFWIF_DM_3D,
											bKick3D,
											psRTDataCleanup,
											psZBuffer,
											psSBuffer);
			*pbCommittedRefCounts3D = IMG_TRUE;
		}
		else
		{
			s3DKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;
		}


		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError2 = RGXScheduleCommand(psRenderContext->psDeviceNode->pvDevice,
										RGXFWIF_DM_3D,
										&s3DKCCBCmd,
										sizeof(s3DKCCBCmd),
										bPDumpContinuous);
			if (eError2 != PVRSRV_ERROR_RETRY)
			{
				break;
			}
			OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();
	}

	/*
	 * Now check eError (which may have returned an error from our earlier calls
	 * to RGXCmdHelperAcquireCmdCCB) - we needed to process any flush command first
	 * so we check it now...
	 */
	if (eError != PVRSRV_OK )
	{
		goto fail_3dacquirecmd;
	}

#if defined(PVR_ANDROID_NATIVE_WINDOW_HAS_SYNC)
	if (bSyncsMerged)
	{
		OSFreeMem(paui32IntClientTAFenceValue);
		OSFreeMem(pauiIntClientTAFenceUFOAddress);
		OSFreeMem(puiUpdateFWAddrs);
		OSFreeMem(pui32UpdateValues);
	}
#if defined(NO_HARDWARE)
	for (i = 0; i < ui32NumFenceFds; i++)
	{
		eError = PVRFDSyncNoHwUpdateFenceKM(ai32FenceFds[i]);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed nohw update on fence fd=%d (%s)",
					 __func__, ai32FenceFds[i], PVRSRVGetErrorStringKM(eError)));
		}
	}
#endif
#endif
	if(paui32Server3DSyncFlags3D)
	{
		OSFreeMem(paui32Server3DSyncFlags3D);
	}

	if(paui32Server3DSyncFlagsPR)
	{
		OSFreeMem(paui32Server3DSyncFlagsPR);
	}

	return PVRSRV_OK;

fail_3dacquirecmd:
fail_3dcmdinit:
	if (paui32Server3DSyncFlags3D)
	{
		OSFreeMem(paui32Server3DSyncFlags3D);
	}
fail_prserversyncflagsalloc3d:
fail_prcmdinit:
fail_prfencecmdinit:
	if (paui32Server3DSyncFlagsPR)
	{
		OSFreeMem(paui32Server3DSyncFlagsPR);
	}
fail_prserversyncflagsallocpr:
#if defined(PVR_ANDROID_NATIVE_WINDOW_HAS_SYNC)
	if (bSyncsMerged)
	{
		OSFreeMem(paui32IntClientTAFenceValue);
		OSFreeMem(pauiIntClientTAFenceUFOAddress);
		OSFreeMem(puiUpdateFWAddrs);
		OSFreeMem(pui32UpdateValues);
	}
fail_fencevaluearray:
fail_fenceUFOarray:
fail_fdsync:
#endif
fail_taacquirecmd:
fail_tacmdinit:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

PVRSRV_ERROR PVRSRVRGXSetRenderContextPriorityKM(CONNECTION_DATA *psConnection,
												 RGX_SERVER_RENDER_CONTEXT *psRenderContext,
												 IMG_UINT32 ui32Priority)
{
	PVRSRV_ERROR eError;

	if (psRenderContext->sTAData.ui32Priority != ui32Priority)
	{
		eError = ContextSetPriority(psRenderContext->sTAData.psServerCommonContext,
									psConnection,
									psRenderContext->psDeviceNode->pvDevice,
									ui32Priority,
									RGXFWIF_DM_TA);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set the priority of the TA part of the rendercontext", __FUNCTION__));
			goto fail_tacontext;
		}
		psRenderContext->sTAData.ui32Priority = ui32Priority;
	}

	if (psRenderContext->s3DData.ui32Priority != ui32Priority)
	{
		eError = ContextSetPriority(psRenderContext->s3DData.psServerCommonContext,
									psConnection,
									psRenderContext->psDeviceNode->pvDevice,
									ui32Priority,
									RGXFWIF_DM_3D);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set the priority of the 3D part of the rendercontext", __FUNCTION__));
			goto fail_3dcontext;
		}
		psRenderContext->s3DData.ui32Priority = ui32Priority;
	}
	return PVRSRV_OK;

fail_3dcontext:
fail_tacontext:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


/*
 * PVRSRVRGXGetLastResetReason
 */
PVRSRV_ERROR PVRSRVRGXGetLastRenderContextResetReasonKM(RGX_SERVER_RENDER_CONTEXT	*psRenderContext,
														IMG_UINT32 *peLastResetReason)
{
	RGX_SERVER_RC_TA_DATA         *psRenderCtxTAData = &(psRenderContext->sTAData);
	RGX_SERVER_COMMON_CONTEXT     *psCurrentServerTACommonCtx = psRenderCtxTAData->psServerCommonContext;
	RGX_SERVER_RC_3D_DATA         *psRenderCtx3DData = &(psRenderContext->s3DData);
	RGX_SERVER_COMMON_CONTEXT     *psCurrentServer3DCommonCtx = psRenderCtx3DData->psServerCommonContext;
	RGXFWIF_CONTEXT_RESET_REASON  eLastTAResetReason, eLast3DResetReason;

	PVR_ASSERT(psRenderContext != IMG_NULL);
	PVR_ASSERT(peLastResetReason != IMG_NULL);

	/* Get the last reset reasons from both the TA and 3D so they are reset... */
	eLastTAResetReason = FWCommonContextGetLastResetReason(psCurrentServerTACommonCtx);
	eLast3DResetReason = FWCommonContextGetLastResetReason(psCurrentServer3DCommonCtx);

	/* Combine the reset reason from TA and 3D into one... */
	*peLastResetReason = (IMG_UINT32) eLast3DResetReason;
	if (eLast3DResetReason == RGXFWIF_CONTEXT_RESET_REASON_NONE  ||
		((eLast3DResetReason == RGXFWIF_CONTEXT_RESET_REASON_INNOCENT_LOCKUP  ||
		  eLast3DResetReason == RGXFWIF_CONTEXT_RESET_REASON_INNOCENT_OVERRUNING)  &&
		 (eLastTAResetReason == RGXFWIF_CONTEXT_RESET_REASON_GUILTY_LOCKUP  ||
		  eLastTAResetReason == RGXFWIF_CONTEXT_RESET_REASON_GUILTY_OVERRUNING)))
	{
		*peLastResetReason = (IMG_UINT32) eLastTAResetReason;
	}

	return PVRSRV_OK;
}


static IMG_BOOL CheckForStalledRenderCtxtCommand(PDLLIST_NODE psNode, IMG_PVOID pvCallbackData)
{
	RGX_SERVER_RENDER_CONTEXT 		*psCurrentServerRenderCtx = IMG_CONTAINER_OF(psNode, RGX_SERVER_RENDER_CONTEXT, sListNode);
	RGX_SERVER_RC_TA_DATA			*psRenderCtxTAData = &(psCurrentServerRenderCtx->sTAData);
	RGX_SERVER_COMMON_CONTEXT		*psCurrentServerTACommonCtx = psRenderCtxTAData->psServerCommonContext;
	RGX_SERVER_RC_3D_DATA			*psRenderCtx3DData = &(psCurrentServerRenderCtx->s3DData);
	RGX_SERVER_COMMON_CONTEXT		*psCurrentServer3DCommonCtx = psRenderCtx3DData->psServerCommonContext;


	DumpStalledFWCommonContext(psCurrentServerTACommonCtx);
	DumpStalledFWCommonContext(psCurrentServer3DCommonCtx);

	return IMG_TRUE;
}
IMG_VOID CheckForStalledRenderCtxt(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	dllist_foreach_node(&(psDevInfo->sRenderCtxtListHead), CheckForStalledRenderCtxtCommand, IMG_NULL);
}

/******************************************************************************
 End of file (rgxta3d.c)
******************************************************************************/
