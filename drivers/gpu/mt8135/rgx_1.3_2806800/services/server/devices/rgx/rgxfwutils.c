/*************************************************************************/ /*!
@File
@Title          RGX firmware utility routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX firmware utility routines
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

#include <stddef.h>

#include "lists.h"

#include "rgxdefs_km.h"
#include "rgx_fwif_km.h"
#include "pdump_km.h"
#include "osfunc.h"
#include "allocmem.h"
#include "devicemem.h"
#include "devicemem_pdump.h"
#include "devicemem_server.h"
#include "pvr_debug.h"
#include "rgxfwutils.h"
#include "rgx_fwif.h"
#include "rgx_fwif_alignchecks_km.h"
#include "rgx_fwif_resetframework.h"
#include "rgx_pdump_panics.h"
#include "rgxheapconfig.h"
#include "pvrsrv.h"
#include "rgxdebug.h"
#include "rgxhwperf.h"
#include "rgxccb.h"
#include "rgxmem.h"
#include "rgxta3d.h"
#include "rgxutils.h"
#include "sync_internal.h"
#include "tlstream.h"
#if defined(TDMETACODE)
#include "physmem_osmem.h"
#endif

#ifdef __linux__
#include <linux/kernel.h>	// sprintf
#include <linux/string.h>	// strncpy, strlen
#include "trace_events.h"
#else
#include <stdio.h>
#endif

/* Kernel CCB length */
#define RGXFWIF_KCCB_TA_NUMCMDS_LOG2	(6)
#define RGXFWIF_KCCB_3D_NUMCMDS_LOG2	(6)
#define RGXFWIF_KCCB_2D_NUMCMDS_LOG2	(6)
#define RGXFWIF_KCCB_CDM_NUMCMDS_LOG2	(6)
#define RGXFWIF_KCCB_GP_NUMCMDS_LOG2	(6)
#define RGXFWIF_KCCB_RTU_NUMCMDS_LOG2	(6)
#define RGXFWIF_KCCB_SHG_NUMCMDS_LOG2	(6)

/* Firmware CCB length */
#define RGXFWIF_FWCCB_TA_NUMCMDS_LOG2	(4)
#define RGXFWIF_FWCCB_3D_NUMCMDS_LOG2	(4)
#define RGXFWIF_FWCCB_2D_NUMCMDS_LOG2	(4)
#define RGXFWIF_FWCCB_CDM_NUMCMDS_LOG2	(4)
#define RGXFWIF_FWCCB_GP_NUMCMDS_LOG2	(4)
#define RGXFWIF_FWCCB_RTU_NUMCMDS_LOG2	(4)
#define RGXFWIF_FWCCB_SHG_NUMCMDS_LOG2	(4)

static IMG_VOID __MTSScheduleWrite(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32Value)
{
	/* ensure memory is flushed before kicking MTS */
	OSWriteMemoryBarrier();

	OSWriteHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_MTS_SCHEDULE, ui32Value);

	/* ensure the MTS kick goes through before continuing */
	OSMemoryBarrier();
}


/*!
*******************************************************************************
 @Function		RGXFWSetupSignatureChecks
 @Description
 @Input			psDevInfo

 @Return		PVRSRV_ERROR
******************************************************************************/
static PVRSRV_ERROR RGXFWSetupSignatureChecks(PVRSRV_RGXDEV_INFO* psDevInfo,
											  DEVMEM_MEMDESC** ppsSigChecksMemDesc,
											  IMG_UINT32 ui32SigChecksBufSize,
											  RGXFWIF_SIGBUF_CTL *psSigBufCtl)
{
	PVRSRV_ERROR	eError;
	DEVMEM_FLAGS_T	uiMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
									  PVRSRV_MEMALLOCFLAG_GPU_READABLE |
									  PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
									  PVRSRV_MEMALLOCFLAG_CPU_READABLE |
									  PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
									  PVRSRV_MEMALLOCFLAG_UNCACHED |
									  PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	/* Allocate memory for the checks */
	PDUMPCOMMENT("Allocate memory for signature checks");
	eError = DevmemFwAllocate(psDevInfo,
							ui32SigChecksBufSize,
							uiMemAllocFlags,
							"SignatureChecks",
							ppsSigChecksMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate %d bytes for signature checks (%u)",
					ui32SigChecksBufSize,
					eError));
		return eError;
	}

	/* Prepare the pointer for the fw to access that memory */
	RGXSetFirmwareAddress(&psSigBufCtl->psBuffer,
						  *ppsSigChecksMemDesc,
						  0, RFW_FWADDR_NOREF_FLAG);

	DevmemPDumpLoadMem(	*ppsSigChecksMemDesc,
						0,
						ui32SigChecksBufSize,
						PDUMP_FLAGS_CONTINUOUS);

	psSigBufCtl->ui32LeftSizeInRegs = ui32SigChecksBufSize / sizeof(IMG_UINT32);

	return PVRSRV_OK;
}

#if defined(RGXFW_ALIGNCHECKS)
/*!
*******************************************************************************
 @Function		RGXFWSetupAlignChecks
 @Description
 @Input			psDevInfo

 @Return		PVRSRV_ERROR
******************************************************************************/
static PVRSRV_ERROR RGXFWSetupAlignChecks(PVRSRV_RGXDEV_INFO* psDevInfo,
								RGXFWIF_DEV_VIRTADDR	*psAlignChecksDevFW,
								IMG_UINT32				*pui32RGXFWAlignChecks,
								IMG_UINT32				ui32RGXFWAlignChecksSize)
{
	IMG_UINT32		aui32RGXFWAlignChecksKM[] = { RGXFW_ALIGN_CHECKS_INIT_KM };
	IMG_UINT32		ui32RGXFWAlingChecksTotal = sizeof(aui32RGXFWAlignChecksKM) + ui32RGXFWAlignChecksSize;
	IMG_UINT32*		paui32AlignChecks;
	PVRSRV_ERROR	eError;

	/* Allocate memory for the checks */
	PDUMPCOMMENT("Allocate memory for alignment checks");
	eError = DevmemFwAllocate(psDevInfo,
							ui32RGXFWAlingChecksTotal,
							PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE | PVRSRV_MEMALLOCFLAG_UNCACHED,
							"AlignmentChecks",
							&psDevInfo->psRGXFWAlignChecksMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate %d bytes for alignment checks (%u)",
					ui32RGXFWAlingChecksTotal,
					eError));
		goto failAlloc;
	}

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWAlignChecksMemDesc,
									(IMG_VOID **)&paui32AlignChecks);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to acquire kernel addr for alignment checks (%u)",
					eError));
		goto failAqCpuAddr;
	}

	/* Copy the values */
	OSMemCopy(paui32AlignChecks, &aui32RGXFWAlignChecksKM[0], sizeof(aui32RGXFWAlignChecksKM));
	paui32AlignChecks += sizeof(aui32RGXFWAlignChecksKM)/sizeof(IMG_UINT32);

	OSMemCopy(paui32AlignChecks, pui32RGXFWAlignChecks, ui32RGXFWAlignChecksSize);

	DevmemPDumpLoadMem(	psDevInfo->psRGXFWAlignChecksMemDesc,
						0,
						ui32RGXFWAlingChecksTotal,
						PDUMP_FLAGS_CONTINUOUS);

	/* Prepare the pointer for the fw to access that memory */
	RGXSetFirmwareAddress(psAlignChecksDevFW,
						  psDevInfo->psRGXFWAlignChecksMemDesc,
						  0, RFW_FWADDR_NOREF_FLAG);

	return PVRSRV_OK;




failAqCpuAddr:
	DevmemFwFree(psDevInfo->psRGXFWAlignChecksMemDesc);
failAlloc:

	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

static IMG_VOID RGXFWFreeAlignChecks(PVRSRV_RGXDEV_INFO* psDevInfo)
{
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWAlignChecksMemDesc);
	DevmemFwFree(psDevInfo->psRGXFWAlignChecksMemDesc);
}
#endif


IMG_VOID RGXSetFirmwareAddress(RGXFWIF_DEV_VIRTADDR	*ppDest,
							   DEVMEM_MEMDESC		*psSrc,
							   IMG_UINT32			uiExtraOffset,
							   IMG_UINT32			ui32Flags)
{
	PVRSRV_ERROR		eError;
	IMG_DEV_VIRTADDR	psDevVirtAddr;
	IMG_UINT64			ui64Offset;

	eError = DevmemAcquireDevVirtAddr(psSrc, &psDevVirtAddr);
	PVR_ASSERT(eError == PVRSRV_OK);

	/* Convert to an address in META memmap */
	ui64Offset = psDevVirtAddr.uiAddr + uiExtraOffset - RGX_FIRMWARE_HEAP_BASE;

	/* The biggest offset for the Shared region that can be addressed */
	PVR_ASSERT(ui64Offset <= 3*RGXFW_SEGMMU_DMAP_SIZE);

	if (ui32Flags & RFW_FWADDR_METACACHED_FLAG)
	{
		ppDest->ui32Addr = ((IMG_UINT32) ui64Offset) | RGXFW_BOOTLDR_META_ADDR;
	}
	else
	{
		ppDest->ui32Addr = ((IMG_UINT32) ui64Offset) | RGXFW_SEGMMU_DMAP_ADDR_START;
	}

	if (ui32Flags & RFW_FWADDR_NOREF_FLAG)
	{
		DevmemReleaseDevVirtAddr(psSrc);
	}
}


IMG_VOID RGXUnsetFirmwareAddress(DEVMEM_MEMDESC *psSrc)
{
	DevmemReleaseDevVirtAddr(psSrc);
}

struct _RGX_SERVER_COMMON_CONTEXT_ {
	DEVMEM_MEMDESC *psFWCommonContextMemDesc;
	PRGXFWIF_FWCOMMONCONTEXT sFWCommonContextFWAddr;
	DEVMEM_MEMDESC *psFWMemContextMemDesc;
	DEVMEM_MEMDESC *psFWFrameworkMemDesc;
	DEVMEM_MEMDESC *psContextStateMemDesc;
	RGX_CLIENT_CCB *psClientCCB;
	DEVMEM_MEMDESC *psClientCCBMemDesc;
	DEVMEM_MEMDESC *psClientCCBCtrlMemDesc;
	IMG_BOOL bCommonContextMemProvided;
	RGXFWIF_CONTEXT_RESET_REASON eLastResetReason;
};

PVRSRV_ERROR FWCommonContextAllocate(CONNECTION_DATA *psConnection,
									 PVRSRV_DEVICE_NODE *psDeviceNode,
									 const IMG_CHAR *pszContextName,
									 DEVMEM_MEMDESC *psAllocatedMemDesc,
									 IMG_UINT32 ui32AllocatedOffset,
									 DEVMEM_MEMDESC *psFWMemContextMemDesc,
									 DEVMEM_MEMDESC *psContextStateMemDesc,
									 IMG_UINT32 ui32CCBAllocSize,
									 IMG_UINT32 ui32Priority,
									 RGX_COMMON_CONTEXT_INFO *psInfo,
									 RGX_SERVER_COMMON_CONTEXT **ppsServerCommonContext)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	RGX_SERVER_COMMON_CONTEXT *psServerCommonContext;
	RGXFWIF_FWCOMMONCONTEXT *psFWCommonContext;
	IMG_UINT32 ui32FWCommonContextOffset;
	IMG_UINT8 *pui8Ptr;
	PVRSRV_ERROR eError;

	/*
		Allocate all the resources that are required
	*/
	psServerCommonContext = OSAllocMem(sizeof(*psServerCommonContext));
	if (psServerCommonContext == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}

	if (psAllocatedMemDesc)
	{
		PDUMPCOMMENT("Using existing MemDesc for RGX firmware %s context (offset = %d)",
					 pszContextName,
					 ui32AllocatedOffset);
		ui32FWCommonContextOffset = ui32AllocatedOffset;
		psServerCommonContext->psFWCommonContextMemDesc = psAllocatedMemDesc;
		psServerCommonContext->bCommonContextMemProvided = IMG_TRUE;
	}
	else
	{
		/* Allocate device memory for the firmware context */
		PDUMPCOMMENT("Allocate RGX firmware %s context", pszContextName);
		eError = DevmemFwAllocate(psDevInfo,
								sizeof(*psFWCommonContext),
								RGX_FWCOMCTX_ALLOCFLAGS,
								"FirmwareContext",
								&psServerCommonContext->psFWCommonContextMemDesc);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"%s : Failed to allocate firmware %s context (%s)",
									__FUNCTION__,
									pszContextName,
									PVRSRVGetErrorStringKM(eError)));
			goto fail_contextalloc;
		}
		ui32FWCommonContextOffset = 0;
		psServerCommonContext->bCommonContextMemProvided = IMG_FALSE;
	}

	psServerCommonContext->eLastResetReason = RGXFWIF_CONTEXT_RESET_REASON_NONE;

	/* Allocate the client CCB */
	eError = RGXCreateCCB(psDeviceNode,
						  ui32CCBAllocSize,
						  psConnection,
						  pszContextName,
						  psServerCommonContext,
						  &psServerCommonContext->psClientCCB,
						  &psServerCommonContext->psClientCCBMemDesc,
						  &psServerCommonContext->psClientCCBCtrlMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: failed to create CCB for %s context(%s)",
								__FUNCTION__,
								pszContextName,
								PVRSRVGetErrorStringKM(eError)));
		goto fail_allocateccb;
	}

	/*
		Temporarily map the firmware context to the kernel and init it
	*/
	eError = DevmemAcquireCpuVirtAddr(psServerCommonContext->psFWCommonContextMemDesc,
									  (IMG_VOID **)&pui8Ptr);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: Failed to map firmware %s context (%s)to CPU",
								__FUNCTION__,
								pszContextName,
								PVRSRVGetErrorStringKM(eError)));
		goto fail_cpuvirtacquire;
	}

	psFWCommonContext = (RGXFWIF_FWCOMMONCONTEXT *) (pui8Ptr + ui32FWCommonContextOffset);

	/* Set the firmware CCB device addresses in the firmware common context */
	RGXSetFirmwareAddress(&psFWCommonContext->psCCB,
						  psServerCommonContext->psClientCCBMemDesc,
						  0, RFW_FWADDR_METACACHED_FLAG);
	RGXSetFirmwareAddress(&psFWCommonContext->psCCBCtl,
						  psServerCommonContext->psClientCCBCtrlMemDesc,
						  0, RFW_FWADDR_FLAG_NONE);

	/* Set the memory context device address */
	psServerCommonContext->psFWMemContextMemDesc = psFWMemContextMemDesc;
	RGXSetFirmwareAddress(&psFWCommonContext->psFWMemContext,
						  psFWMemContextMemDesc,
						  0, RFW_FWADDR_METACACHED_FLAG);

	/* Set the framework register updates address */
	psServerCommonContext->psFWFrameworkMemDesc = psInfo->psFWFrameworkMemDesc;
	RGXSetFirmwareAddress(&psFWCommonContext->psRFCmd,
						  psInfo->psFWFrameworkMemDesc,
						  0, RFW_FWADDR_METACACHED_FLAG);

	psFWCommonContext->ui32Priority = ui32Priority;

	if(psInfo->psMCUFenceAddr != IMG_NULL)
	{
		psFWCommonContext->ui64MCUFenceAddr = psInfo->psMCUFenceAddr->uiAddr;
	}

	/* Store a reference to Server Common Context for notifications back from the FW. */
	{
		/* This is not the best way to do it, but is the only option left by strict 32/64bit compilers. */
		IMG_PVOID          apvServerCommonContextID[2] = {psServerCommonContext, 0};
		IMG_UINT64*        ui64ServerCommonContextID = (IMG_UINT64*) &(apvServerCommonContextID[0]);

		psFWCommonContext->ui32ServerCommonContextID1 = (IMG_UINT32)(*ui64ServerCommonContextID >> 32);
		psFWCommonContext->ui32ServerCommonContextID2 = (IMG_UINT32)(*ui64ServerCommonContextID & 0xffffffff);
	}

	/* Set the firmware GPU context state buffer */
	psServerCommonContext->psContextStateMemDesc = psContextStateMemDesc;
	if (psContextStateMemDesc)
	{
		RGXSetFirmwareAddress(&psFWCommonContext->psContextState,
							  psContextStateMemDesc,
							  0,
							  RFW_FWADDR_METACACHED_FLAG);
	}

	/*
	 * Dump the created context
	 */
	PDUMPCOMMENT("Dump %s context", pszContextName);
	DevmemPDumpLoadMem(psServerCommonContext->psFWCommonContextMemDesc,
					   ui32FWCommonContextOffset,
					   sizeof(*psFWCommonContext),
					   PDUMP_FLAGS_CONTINUOUS);

	/* We've finished the setup so release the CPU mapping */
	DevmemReleaseCpuVirtAddr(psServerCommonContext->psFWCommonContextMemDesc);

	/* Map this allocation into the FW */
	RGXSetFirmwareAddress(&psServerCommonContext->sFWCommonContextFWAddr,
						  psServerCommonContext->psFWCommonContextMemDesc,
						  ui32FWCommonContextOffset,
						  RFW_FWADDR_METACACHED_FLAG);

#if defined(LINUX)
	trace_pvr_create_fw_context(OSGetCurrentProcessNameKM(),
								pszContextName,
								psServerCommonContext->sFWCommonContextFWAddr.ui32Addr);
#endif

	*ppsServerCommonContext = psServerCommonContext;
	return PVRSRV_OK;

fail_allocateccb:
	DevmemReleaseCpuVirtAddr(psServerCommonContext->psFWCommonContextMemDesc);
fail_cpuvirtacquire:
	RGXUnsetFirmwareAddress(psServerCommonContext->psFWCommonContextMemDesc);
	if (!psServerCommonContext->bCommonContextMemProvided)
	{
		DevmemFwFree(psServerCommonContext->psFWCommonContextMemDesc);
	}
fail_contextalloc:
	OSFreeMem(psServerCommonContext);
fail_alloc:
	return eError;
}

IMG_VOID FWCommonContextFree(RGX_SERVER_COMMON_CONTEXT *psServerCommonContext)
{
	/*
		Unmap the context itself and then all it's resources
	*/

	/* Unmap the FW common context */
	RGXUnsetFirmwareAddress(psServerCommonContext->psFWCommonContextMemDesc);
	/* Umap context state buffer (if there was one) */
	if (psServerCommonContext->psContextStateMemDesc)
	{
		RGXUnsetFirmwareAddress(psServerCommonContext->psContextStateMemDesc);
	}
	/* Unmap the framework buffer */
	RGXUnsetFirmwareAddress(psServerCommonContext->psFWFrameworkMemDesc);
	/* Unmap client CCB and CCB control */
	RGXUnsetFirmwareAddress(psServerCommonContext->psClientCCBCtrlMemDesc);
	RGXUnsetFirmwareAddress(psServerCommonContext->psClientCCBMemDesc);
	/* Unmap the memory context */
	RGXUnsetFirmwareAddress(psServerCommonContext->psFWMemContextMemDesc);

	/* Destroy the client CCB */
	RGXDestroyCCB(psServerCommonContext->psClientCCB);
	/* Free the FW common context (if there was one) */
	if (!psServerCommonContext->bCommonContextMemProvided)
	{
		DevmemFwFree(psServerCommonContext->psFWCommonContextMemDesc);
	}
	/* Free the hosts representation of the common context */
	OSFreeMem(psServerCommonContext);
}

PRGXFWIF_FWCOMMONCONTEXT FWCommonContextGetFWAddress(RGX_SERVER_COMMON_CONTEXT *psServerCommonContext)
{
	return psServerCommonContext->sFWCommonContextFWAddr;
}

RGX_CLIENT_CCB *FWCommonContextGetClientCCB(RGX_SERVER_COMMON_CONTEXT *psServerCommonContext)
{
	return psServerCommonContext->psClientCCB;
}

RGXFWIF_CONTEXT_RESET_REASON FWCommonContextGetLastResetReason(RGX_SERVER_COMMON_CONTEXT *psServerCommonContext)
{
	RGXFWIF_CONTEXT_RESET_REASON  eLastResetReason;

	PVR_ASSERT(psServerCommonContext != IMG_NULL);

	/* Take the most recent reason and reset for next time... */
	eLastResetReason = psServerCommonContext->eLastResetReason;
	psServerCommonContext->eLastResetReason = RGXFWIF_CONTEXT_RESET_REASON_NONE;

	return eLastResetReason;
}


/*!
*******************************************************************************
 @Function		RGXSetupKernelCCB
 @Description	Allocate and initialise a kernel CCB
 @Input			psDevInfo

 @Return		PVRSRV_ERROR
******************************************************************************/
static PVRSRV_ERROR RGXSetupKernelCCB(PVRSRV_RGXDEV_INFO 	*psDevInfo,
									  RGXFWIF_INIT			*psRGXFWInit,
									  RGXFWIF_DM			eKCCBType,
									  IMG_UINT32			ui32NumCmdsLog2,
									  IMG_UINT32			ui32CmdSize)
{
	PVRSRV_ERROR		eError;
	RGXFWIF_CCB_CTL		*psKCCBCtl;
	DEVMEM_FLAGS_T		uiCCBCtlMemAllocFlags, uiCCBMemAllocFlags;
	IMG_UINT32			ui32kCCBSize = (1U << ui32NumCmdsLog2);


	/*
	 * FIXME: the write offset need not be writeable by the firmware, indeed may
	 * not even be needed for reading. Consider moving it to its own data
	 * structure.
	 */
	uiCCBCtlMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
							PVRSRV_MEMALLOCFLAG_UNCACHED |
							 PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	/* Allocation flags for Kernel CCB */
	uiCCBMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
						 PVRSRV_MEMALLOCFLAG_GPU_READABLE |
						 PVRSRV_MEMALLOCFLAG_CPU_READABLE |
						 PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
						 PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
						 PVRSRV_MEMALLOCFLAG_UNCACHED |
						 PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	/*
		Allocate memory for the kernel CCB control.
	*/
	PDUMPCOMMENT("Allocate memory for kernel CCB control %u", eKCCBType);
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_CCB_CTL),
							uiCCBCtlMemAllocFlags,
							"KernelCCBControl",
							&psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType]);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupKernelCCB: Failed to allocate kernel CCB ctl %u (%u)",
				eKCCBType, eError));
		goto failCCBCtlMemDescAlloc;
	}

	/*
		Allocate memory for the kernel CCB.
		(this will reference further command data in non-shared CCBs)
	*/
	PDUMPCOMMENT("Allocate memory for kernel CCB %u", eKCCBType);
	eError = DevmemFwAllocate(psDevInfo,
							ui32kCCBSize * ui32CmdSize,
							uiCCBMemAllocFlags,
							"KernelCCB",
							&psDevInfo->apsKernelCCBMemDesc[eKCCBType]);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupKernelCCB: Failed to allocate kernel CCB %u (%u)",
				eKCCBType, eError));
		goto failCCBMemDescAlloc;
	}

	/*
		Map the kernel CCB control to the kernel.
	*/
	eError = DevmemAcquireCpuVirtAddr(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType],
									  (IMG_VOID **)&psDevInfo->apsKernelCCBCtl[eKCCBType]);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupKernelCCB: Failed to acquire cpu kernel CCB Ctl %u (%u)",
				eKCCBType, eError));
		goto failCCBCtlMemDescAqCpuVirt;
	}

	/*
		Map the kernel CCB to the kernel.
	*/
	eError = DevmemAcquireCpuVirtAddr(psDevInfo->apsKernelCCBMemDesc[eKCCBType],
									  (IMG_VOID **)&psDevInfo->apsKernelCCB[eKCCBType]);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupKernelCCB: Failed to acquire cpu kernel CCB %u (%u)",
				eKCCBType, eError));
		goto failCCBMemDescAqCpuVirt;
	}

	/*
	 * Initialise the kernel CCB control.
	 */
	psKCCBCtl = psDevInfo->apsKernelCCBCtl[eKCCBType];
	psKCCBCtl->ui32WriteOffset = 0;
	psKCCBCtl->ui32ReadOffset = 0;
	psKCCBCtl->ui32WrapMask = ui32kCCBSize - 1;
	psKCCBCtl->ui32CmdSize = ui32CmdSize;

	/*
	 * Set-up RGXFWIfCtl pointers to access the kCCBs
	 */
	RGXSetFirmwareAddress(&psRGXFWInit->psKernelCCBCtl[eKCCBType],
						  psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType],
						  0, RFW_FWADDR_NOREF_FLAG);

	RGXSetFirmwareAddress(&psRGXFWInit->psKernelCCB[eKCCBType],
						  psDevInfo->apsKernelCCBMemDesc[eKCCBType],
						  0, RFW_FWADDR_METACACHED_FLAG | RFW_FWADDR_NOREF_FLAG);

	psRGXFWInit->eDM[eKCCBType] = eKCCBType;

	/*
	 * Pdump the kernel CCB control.
	 */
	PDUMPCOMMENT("Initialise kernel CCB ctl %d", eKCCBType);
	DevmemPDumpLoadMem(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType],
					   0,
					   sizeof(RGXFWIF_CCB_CTL),
					   0);

	return PVRSRV_OK;


failCCBMemDescAqCpuVirt:
	DevmemReleaseCpuVirtAddr(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType]);
failCCBCtlMemDescAqCpuVirt:
	DevmemFwFree(psDevInfo->apsKernelCCBMemDesc[eKCCBType]);
failCCBMemDescAlloc:
	DevmemFwFree(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType]);
failCCBCtlMemDescAlloc:

	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*!
*******************************************************************************
 @Function		RGXSetupFirmwareCCB
 @Description	Allocate and initialise a Firmware CCB
 @Input			psDevInfo

 @Return		PVRSRV_ERROR
******************************************************************************/
static PVRSRV_ERROR RGXSetupFirmwareCCB(PVRSRV_RGXDEV_INFO 	*psDevInfo,
									  RGXFWIF_INIT			*psRGXFWInit,
									  RGXFWIF_DM			eFWCCBType,
									  IMG_UINT32			ui32NumCmdsLog2,
									  IMG_UINT32			ui32CmdSize)
{
	PVRSRV_ERROR		eError;
	RGXFWIF_CCB_CTL		*psFWCCBCtl;
	DEVMEM_FLAGS_T		uiCCBCtlMemAllocFlags, uiCCBMemAllocFlags;
	IMG_UINT32			ui32FWCCBSize = (1U << ui32NumCmdsLog2);

	/*
	 * FIXME: the write offset need not be writeable by the host, indeed may
	 * not even be needed for reading. Consider moving it to its own data
	 * structure.
	 */
	uiCCBCtlMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
							PVRSRV_MEMALLOCFLAG_UNCACHED |
							 PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	/* Allocation flags for Firmware CCB */
	uiCCBMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
						 PVRSRV_MEMALLOCFLAG_GPU_READABLE |
						 PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
						 PVRSRV_MEMALLOCFLAG_CPU_READABLE |
						 PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
						 PVRSRV_MEMALLOCFLAG_UNCACHED |
						 PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	/*
		Allocate memory for the Firmware CCB control.
	*/
	PDUMPCOMMENT("Allocate memory for firmware CCB control %u", eFWCCBType);
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_CCB_CTL),
							uiCCBCtlMemAllocFlags,
							"FirmwareCCBControl",
							&psDevInfo->apsFirmwareCCBCtlMemDesc[eFWCCBType]);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmwareCCB: Failed to allocate Firmware CCB ctl %u (%u)",
				eFWCCBType, eError));
		goto failCCBCtlMemDescAlloc;
	}

	/*
		Allocate memory for the Firmware CCB.
		(this will reference further command data in non-shared CCBs)
	*/
	PDUMPCOMMENT("Allocate memory for firmware CCB %u", eFWCCBType);
	eError = DevmemFwAllocate(psDevInfo,
							ui32FWCCBSize * ui32CmdSize,
							uiCCBMemAllocFlags,
							"FirmwareCCB",
							&psDevInfo->apsFirmwareCCBMemDesc[eFWCCBType]);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmwareCCB: Failed to allocate Firmware CCB %u (%u)",
				eFWCCBType, eError));
		goto failCCBMemDescAlloc;
	}

	/*
		Map the Firmware CCB control to the kernel.
	*/
	eError = DevmemAcquireCpuVirtAddr(psDevInfo->apsFirmwareCCBCtlMemDesc[eFWCCBType],
									  (IMG_VOID **)&psDevInfo->apsFirmwareCCBCtl[eFWCCBType]);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmwareCCB: Failed to acquire cpu firmware CCB Ctl %u (%u)",
				eFWCCBType, eError));
		goto failCCBCtlMemDescAqCpuVirt;
	}

	/*
		Map the firmware CCB to the kernel.
	*/
	eError = DevmemAcquireCpuVirtAddr(psDevInfo->apsFirmwareCCBMemDesc[eFWCCBType],
									  (IMG_VOID **)&psDevInfo->apsFirmwareCCB[eFWCCBType]);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmwareCCB: Failed to acquire cpu firmware CCB %u (%u)",
				eFWCCBType, eError));
		goto failCCBMemDescAqCpuVirt;
	}

	/*
	 * Initialise the firmware CCB control.
	 */
	psFWCCBCtl = psDevInfo->apsFirmwareCCBCtl[eFWCCBType];
	psFWCCBCtl->ui32WriteOffset = 0;
	psFWCCBCtl->ui32ReadOffset = 0;
	psFWCCBCtl->ui32WrapMask = ui32FWCCBSize - 1;
	psFWCCBCtl->ui32CmdSize = ui32CmdSize;

	/*
	 * Set-up RGXFWIfCtl pointers to access the kCCBs
	 */
	RGXSetFirmwareAddress(&psRGXFWInit->psFirmwareCCBCtl[eFWCCBType],
						  psDevInfo->apsFirmwareCCBCtlMemDesc[eFWCCBType],
						  0, RFW_FWADDR_NOREF_FLAG);

	RGXSetFirmwareAddress(&psRGXFWInit->psFirmwareCCB[eFWCCBType],
						  psDevInfo->apsFirmwareCCBMemDesc[eFWCCBType],
						  0, RFW_FWADDR_NOREF_FLAG);

	psRGXFWInit->eDM[eFWCCBType] = eFWCCBType;

	/*
	 * Pdump the kernel CCB control.
	 */
	PDUMPCOMMENT("Initialise firmware CCB ctl %d", eFWCCBType);
	DevmemPDumpLoadMem(psDevInfo->apsFirmwareCCBCtlMemDesc[eFWCCBType],
					   0,
					   sizeof(RGXFWIF_CCB_CTL),
					   0);

	return PVRSRV_OK;


failCCBMemDescAqCpuVirt:
	DevmemReleaseCpuVirtAddr(psDevInfo->apsFirmwareCCBCtlMemDesc[eFWCCBType]);
failCCBCtlMemDescAqCpuVirt:
	DevmemFwFree(psDevInfo->apsFirmwareCCBMemDesc[eFWCCBType]);
failCCBMemDescAlloc:
	DevmemFwFree(psDevInfo->apsFirmwareCCBCtlMemDesc[eFWCCBType]);
failCCBCtlMemDescAlloc:

	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*!
*******************************************************************************
 @Function		RGXFreeKernelCCB
 @Description	Free a kernel CCB
 @Input			psDevInfo
 @Input			eKCCBType

 @Return		PVRSRV_ERROR
******************************************************************************/
static IMG_VOID RGXFreeKernelCCB(PVRSRV_RGXDEV_INFO 	*psDevInfo,
								 RGXFWIF_DM				eKCCBType)
{
	DevmemReleaseCpuVirtAddr(psDevInfo->apsKernelCCBMemDesc[eKCCBType]);
	DevmemReleaseCpuVirtAddr(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType]);
	DevmemFwFree(psDevInfo->apsKernelCCBMemDesc[eKCCBType]);
	DevmemFwFree(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType]);
}

/*!
*******************************************************************************
 @Function		RGXFreeFirmwareCCB
 @Description	Free a firmware CCB
 @Input			psDevInfo
 @Input			eFWCCBType

 @Return		PVRSRV_ERROR
******************************************************************************/
static IMG_VOID RGXFreeFirmwareCCB(PVRSRV_RGXDEV_INFO 	*psDevInfo,
								 RGXFWIF_DM				eFWCCBType)
{
	DevmemReleaseCpuVirtAddr(psDevInfo->apsFirmwareCCBMemDesc[eFWCCBType]);
	DevmemReleaseCpuVirtAddr(psDevInfo->apsFirmwareCCBCtlMemDesc[eFWCCBType]);
	DevmemFwFree(psDevInfo->apsFirmwareCCBMemDesc[eFWCCBType]);
	DevmemFwFree(psDevInfo->apsFirmwareCCBCtlMemDesc[eFWCCBType]);
}

static IMG_VOID RGXSetupBIFFaultReadRegisterRollback(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	PMR *psPMR;

	if (psDevInfo->psRGXBIFFaultAddressMemDesc)
	{
		if (DevmemServerGetImportHandle(psDevInfo->psRGXBIFFaultAddressMemDesc,(IMG_VOID **)&psPMR) == PVRSRV_OK)
		{
			PMRUnlockSysPhysAddresses(psPMR);
		}
		DevmemFwFree(psDevInfo->psRGXBIFFaultAddressMemDesc);
	}
}

static PVRSRV_ERROR RGXSetupBIFFaultReadRegister(PVRSRV_DEVICE_NODE	*psDeviceNode, RGXFWIF_INIT *psRGXFWInit)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	IMG_UINT32			*pui32MemoryVirtAddr;
	IMG_UINT32			i;
	IMG_SIZE_T			ui32PageSize;
	DEVMEM_FLAGS_T		uiMemAllocFlags;
	PVRSRV_RGXDEV_INFO 	*psDevInfo = psDeviceNode->pvDevice;
	PMR					*psPMR;

	ui32PageSize = OSGetPageSize();

	/* Allocate page of memory for use by RGX_CR_BIF_FAULT_READ */
	uiMemAllocFlags =	PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
						PVRSRV_MEMALLOCFLAG_GPU_READABLE |
						PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
						PVRSRV_MEMALLOCFLAG_CPU_READABLE |
						PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
						PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
						PVRSRV_MEMALLOCFLAG_UNCACHED;

	psDevInfo->psRGXBIFFaultAddressMemDesc = IMG_NULL;
	eError = DevmemFwAllocateExportable(psDeviceNode,
										ui32PageSize,
										uiMemAllocFlags,
										"BIFFaultAddress",
										&psDevInfo->psRGXBIFFaultAddressMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"Failed to allocate mem for RGX_CR_BIF_FAULT_READ (%u)",
				eError));
		goto failBIFFaultAddressDescAlloc;
	}

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXBIFFaultAddressMemDesc,
									  (IMG_VOID **)&pui32MemoryVirtAddr);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to acquire mem for RGX_CR_BIF_FAULT_READ (%u)",
				eError));
		goto failBIFFaultAddressDescAqCpuVirt;
	}

	for (i=0; i<(RGX_CR_BIF_FAULT_READ_ADDRESS_ALIGNSIZE/sizeof(IMG_UINT32)); i++)
	{
		*(pui32MemoryVirtAddr + i) = 0xDEADBEEF;
	}

	eError = DevmemServerGetImportHandle(psDevInfo->psRGXBIFFaultAddressMemDesc,(IMG_VOID **)&psPMR);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Error getting PMR for RGX_CR_BIF_FAULT_READ MemDesc (%u)",
				eError));

		goto failBIFFaultAddressDescGetPMR;
	}
	else
	{
		IMG_BOOL bValid;

		eError = PMRLockSysPhysAddresses(psPMR,OSGetPageShift());

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Error locking physical address for RGX_CR_BIF_FAULT_READ MemDesc (%u)",
					eError));

			goto failBIFFaultAddressDescLockPhys;
		}

		eError = PMR_DevPhysAddr(psPMR,0,&(psRGXFWInit->sBifFaultPhysAddr),&bValid);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Error getting physical address for RGX_CR_BIF_FAULT_READ MemDesc (%u)",
					eError));

			goto failBIFFaultAddressDescGetPhys;
		}

		if (bValid)
		{
			//PVR_DPF((PVR_DBG_MESSAGE,"RGXSetupFirmware: Got physical address for RGX_CR_BIF_FAULT_READ MemDesc (0x%llX)",
			//		psRGXFWInit->sBifFaultPhysAddr.uiAddr));
		}
		else
		{
			psRGXFWInit->sBifFaultPhysAddr.uiAddr = 0;
			PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed getting physical address for RGX_CR_BIF_FAULT_READ MemDesc - invalid page (0x%llX)",
					psRGXFWInit->sBifFaultPhysAddr.uiAddr));

			goto failBIFFaultAddressDescGetPhys;
		}
	}

	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXBIFFaultAddressMemDesc);

	return PVRSRV_OK;

failBIFFaultAddressDescGetPhys:
	PMRUnlockSysPhysAddresses(psPMR);

failBIFFaultAddressDescLockPhys:

failBIFFaultAddressDescGetPMR:
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXBIFFaultAddressMemDesc);

failBIFFaultAddressDescAqCpuVirt:
	DevmemFwFree(psDevInfo->psRGXBIFFaultAddressMemDesc);

failBIFFaultAddressDescAlloc:

	return eError;
}

static IMG_VOID RGXHwBrn37200Rollback(PVRSRV_RGXDEV_INFO *psDevInfo)
{
#if defined(FIX_HW_BRN_37200)
	DevmemFwFree(psDevInfo->psRGXFWHWBRN37200MemDesc);
#endif
}

static PVRSRV_ERROR RGXHwBrn37200(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	PVRSRV_ERROR			eError = PVRSRV_OK;

#if defined(FIX_HW_BRN_37200)
	struct _DEVMEM_HEAP_	*psBRNHeap;
	DEVMEM_FLAGS_T			uiFlags;
	IMG_DEV_VIRTADDR		sTmpDevVAddr;
	IMG_SIZE_T				uiPageSize;

	uiPageSize = OSGetPageSize();

	uiFlags =	PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
				PVRSRV_MEMALLOCFLAG_GPU_READABLE |
				PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
				PVRSRV_MEMALLOCFLAG_CPU_READABLE |
				PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
				PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
				PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
				PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE |
				PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	eError = DevmemFindHeapByName(psDevInfo->psKernelDevmemCtx,
							  "HWBRN37200", /* FIXME: We need to create an IDENT macro for this string.
											 Make sure the IDENT macro is not accessible to userland */
							  &psBRNHeap);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXHwBrn37200: HWBRN37200 Failed DevmemFindHeapByName (%u)", eError));
		goto failFWHWBRN37200FindHeapByName;
	}

	psDevInfo->psRGXFWHWBRN37200MemDesc = IMG_NULL;
	eError = DevmemAllocate(psBRNHeap,
						uiPageSize,
						ROGUE_CACHE_LINE_SIZE,
						uiFlags,
						"HWBRN37200",
						&psDevInfo->psRGXFWHWBRN37200MemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXHwBrn37200: Failed to allocate %u bytes for HWBRN37200 (%u)",
				(IMG_UINT32)uiPageSize,
				eError));
		goto failFWHWBRN37200MemDescAlloc;
	}

	/*
		We need to map it so the heap for this allocation
		is set
	*/
	eError = DevmemMapToDevice(psDevInfo->psRGXFWHWBRN37200MemDesc,
						   psBRNHeap,
						   &sTmpDevVAddr);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXHwBrn37200: Failed to allocate %u bytes for HWBRN37200 (%u)",
				(IMG_UINT32)uiPageSize,
				eError));
		goto failFWHWBRN37200DevmemMapToDevice;
	}

	return PVRSRV_OK;

failFWHWBRN37200DevmemMapToDevice:

failFWHWBRN37200MemDescAlloc:
	RGXHwBrn37200Rollback(psDevInfo);

failFWHWBRN37200FindHeapByName:
#endif

	return eError;
}

/*!
*******************************************************************************

 @Function	RGXSetupFirmware

 @Description

 Setups all the firmware related data

 @Input psDevInfo

 @Return PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RGXSetupFirmware(PVRSRV_DEVICE_NODE	*psDeviceNode,
								 IMG_BOOL			bEnableSignatureChecks,
								 IMG_UINT32			ui32SignatureChecksBufSize,
								 IMG_UINT32			ui32HWPerfFWBufSizeKB,
								 IMG_UINT64		 	ui64HWPerfFilter,
								 IMG_UINT32			ui32RGXFWAlignChecksSize,
								 IMG_UINT32			*pui32RGXFWAlignChecks,
								 IMG_UINT32			ui32ConfigFlags,
								 IMG_UINT32			ui32LogType,
								 IMG_UINT32            ui32NumTilingCfgs,
								 IMG_UINT32            *pui32BIFTilingXStrides,
								 IMG_UINT32			ui32FilterFlags,
								 RGXFWIF_DEV_VIRTADDR	*psRGXFWInitFWAddr)
{
	PVRSRV_ERROR		eError;
	DEVMEM_FLAGS_T		uiMemAllocFlags;
	RGXFWIF_INIT		*psRGXFWInit;
	PVRSRV_RGXDEV_INFO 	*psDevInfo = psDeviceNode->pvDevice;
	IMG_UINT32			dm;

	/* Fw init data */
	uiMemAllocFlags =	PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
						PVRSRV_MEMALLOCFLAG_GPU_READABLE |
						PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
						PVRSRV_MEMALLOCFLAG_CPU_READABLE |
						PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
						PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
						PVRSRV_MEMALLOCFLAG_UNCACHED |
						PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;
						/* FIXME: Change to Cached */

	PDUMPCOMMENT("Allocate RGXFWIF_INIT structure");
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_INIT),
							uiMemAllocFlags,
							"FirmwareInitStructure",
							&psDevInfo->psRGXFWIfInitMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate %u bytes for fw if ctl (%u)",
				(IMG_UINT32)sizeof(RGXFWIF_INIT),
				eError));
		goto failFWIfInitMemDescAlloc;
	}

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWIfInitMemDesc,
									  (IMG_VOID **)&psRGXFWInit);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to acquire kernel fw if ctl (%u)",
				eError));
		goto failFWIfInitMemDescAqCpuVirt;
	}

	RGXSetFirmwareAddress(psRGXFWInitFWAddr,
						psDevInfo->psRGXFWIfInitMemDesc,
						0, RFW_FWADDR_METACACHED_FLAG | RFW_FWADDR_NOREF_FLAG);

	/* FW Trace buffer */
	uiMemAllocFlags =	PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
						PVRSRV_MEMALLOCFLAG_GPU_READABLE |
						PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
						PVRSRV_MEMALLOCFLAG_CPU_READABLE |
						PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
						PVRSRV_MEMALLOCFLAG_UNCACHED |
						PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	PDUMPCOMMENT("Allocate rgxfw trace structure");
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_TRACEBUF),
							uiMemAllocFlags,
							"FirmwareTraceStructure",
							&psDevInfo->psRGXFWIfTraceBufCtlMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate %u bytes for fw trace (%u)",
				(IMG_UINT32)sizeof(RGXFWIF_TRACEBUF),
				eError));
		goto failFWIfTraceBufCtlMemDescAlloc;
	}

	RGXSetFirmwareAddress(&psRGXFWInit->psTraceBufCtl,
						psDevInfo->psRGXFWIfTraceBufCtlMemDesc,
						0, RFW_FWADDR_NOREF_FLAG);

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWIfTraceBufCtlMemDesc,
									  (IMG_VOID **)&psDevInfo->psRGXFWIfTraceBuf);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to acquire kernel tracebuf ctl (%u)",
				eError));
		goto failFWIfTraceBufCtlMemDescAqCpuVirt;
	}

	/* Determine the size of the HWPerf FW buffer */
	if (ui32HWPerfFWBufSizeKB > (RGXFW_HWPERF_L1_SIZE_MIN>>10))
	{
		/* Size specified as in AppHint HWPerfFWBufSizeInKB */
		PVR_DPF((PVR_DBG_MESSAGE,"RGXSetupFirmware: Using HWPerf FW buffer size of %u KB",
				ui32HWPerfFWBufSizeKB));
		psDevInfo->ui32RGXFWIfHWPerfBufSize = ui32HWPerfFWBufSizeKB<<10;
	}
	else if (ui32HWPerfFWBufSizeKB > 0)
	{
		/* Size specified as a AppHint but it is too small */
		PVR_DPF((PVR_DBG_WARNING,"RGXSetupFirmware: HWPerfFWBufSizeInKB value (%u) too small, using minimum (%u)",
				ui32HWPerfFWBufSizeKB, RGXFW_HWPERF_L1_SIZE_MIN>>10));
		psDevInfo->ui32RGXFWIfHWPerfBufSize = RGXFW_HWPERF_L1_SIZE_MIN;
	}
	else
	{
		/* 0 size implies AppHint not set or is set to zero,
		 * use default size from driver constant. */
		psDevInfo->ui32RGXFWIfHWPerfBufSize = RGXFW_HWPERF_L1_SIZE_DEFAULT;
	}

	/* Allocate HWPerf FW L1 buffer */
	eError = DevmemFwAllocate(psDevInfo,
							  psDevInfo->ui32RGXFWIfHWPerfBufSize+RGXFW_HWPERF_L1_PADDING_DEFAULT,
							  uiMemAllocFlags,
							  "FirmwareHWPerfControl",
							  &psDevInfo->psRGXFWIfHWPerfBufCtlMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXSetupFirmware: Failed to allocate kernel fw hwperf ctl (%u)",
				 eError));
		goto failFWIfInitHWPerfInfoMemDescAlloc;
	}

	/* Meta cached flag removed from this allocation as it was found
	 * FW performance was better without it. */
	RGXSetFirmwareAddress(&psRGXFWInit->psHWPerfInfoCtl,
						  psDevInfo->psRGXFWIfHWPerfBufCtlMemDesc,
						  0, RFW_FWADDR_NOREF_FLAG);

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWIfHWPerfBufCtlMemDesc,
									  (IMG_VOID**)&psDevInfo->psRGXFWIfHWPerfBuf);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXSetupFirmware: Failed to acquire kernel hwperf ctl (%u)",
				 eError));
		goto failFWIfInitHWPerfInfoMemDescAqCpuVirt;
	}


	uiMemAllocFlags =	PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
						PVRSRV_MEMALLOCFLAG_GPU_READABLE |
						PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
						PVRSRV_MEMALLOCFLAG_CPU_READABLE |
						PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
						PVRSRV_MEMALLOCFLAG_UNCACHED |
						PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	/* Allocate buffer to store FW data */
	eError = DevmemFwAllocate(psDevInfo,
							  RGX_META_COREMEM_DATA_SIZE,
							  uiMemAllocFlags,
							  "FirmwareCorememDataStore",
							  &psDevInfo->psRGXFWIfCorememDataStoreMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXSetupFirmware: Failed to allocate coremem data store (%u)",
				 eError));
		goto failFWIfCorememDataStoreMemDescAlloc;
	}

	RGXSetFirmwareAddress(&psRGXFWInit->pbyCorememDataStore,
						  psDevInfo->psRGXFWIfCorememDataStoreMemDesc,
						  0, RFW_FWADDR_NOREF_FLAG);

	/* init HW frame info */
	PDUMPCOMMENT("Allocate rgxfw HW info buffer");
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_HWRINFOBUF),
							uiMemAllocFlags,
							"FirmwareHWInfoBuffer",
							&psDevInfo->psRGXFWIfHWRInfoBufCtlMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate %d bytes for HW info (%u)",
				(IMG_UINT32)sizeof(RGXFWIF_HWRINFOBUF),
				eError));
		goto failFWIfHWRInfoBufCtlMemDescAlloc;
	}

	RGXSetFirmwareAddress(&psRGXFWInit->psRGXFWIfHWRInfoBufCtl,
						psDevInfo->psRGXFWIfHWRInfoBufCtlMemDesc,
						0, RFW_FWADDR_NOREF_FLAG);

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWIfHWRInfoBufCtlMemDesc,
									  (IMG_VOID **)&psDevInfo->psRGXFWIfHWRInfoBuf);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to acquire kernel tracebuf ctl (%u)",
				eError));
		goto failFWIfHWRInfoBufCtlMemDescAqCpuVirt;
	}
	OSMemSet(psDevInfo->psRGXFWIfHWRInfoBuf, 0, sizeof(RGXFWIF_HWRINFOBUF));

	/* init HWPERF data */
	psDevInfo->psRGXFWIfTraceBuf->ui32HWPerfRIdx = 0;
	psDevInfo->psRGXFWIfTraceBuf->ui32HWPerfWIdx = 0;
	psDevInfo->psRGXFWIfTraceBuf->ui32HWPerfWrapCount = 0;
	psDevInfo->psRGXFWIfTraceBuf->ui32HWPerfSize = psDevInfo->ui32RGXFWIfHWPerfBufSize;
	psRGXFWInit->ui64HWPerfFilter = ui64HWPerfFilter;
	psDevInfo->psRGXFWIfTraceBuf->ui32HWPerfUt = 0;
	psDevInfo->psRGXFWIfTraceBuf->ui32HWPerfDropCount = 0;
	psDevInfo->psRGXFWIfTraceBuf->ui32FirstDropOrdinal = 0;
	psDevInfo->psRGXFWIfTraceBuf->ui32LastDropOrdinal = 0;


	/* Initialise the HWPerf module in the RGX device driver.
	 * May allocate host buffer if HWPerf enabled at driver load time.
	 */
	eError = RGXHWPerfInit(psDeviceNode, (ui32ConfigFlags & RGXFWIF_INICFG_HWPERF_EN));
	PVR_LOGG_IF_ERROR(eError, "RGXHWPerfInit", failHWPerfInit);

	/* Set initial log type */
	if (ui32LogType & ~RGXFWIF_LOG_TYPE_MASK)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Invalid initial log type (0x%X)",ui32LogType));
		goto failInvalidLogType;
	}
	psDevInfo->psRGXFWIfTraceBuf->ui32LogType = ui32LogType;

	/* Allocate FW CB for GPU utilization */
	uiMemAllocFlags =	PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
						PVRSRV_MEMALLOCFLAG_GPU_READABLE |
						PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
						PVRSRV_MEMALLOCFLAG_CPU_READABLE |
						PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
						PVRSRV_MEMALLOCFLAG_UNCACHED |
						PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	PDUMPCOMMENT("Allocate rgxfw GPU utilization CB (FW)");
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_GPU_UTIL_FWCB),
							uiMemAllocFlags,
							"FirmwareGPUUtilizationCB",
							&psDevInfo->psRGXFWIfGpuUtilFWCbCtlMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate %u bytes for GPU utilization FW CB ctl (%u)",
				(IMG_UINT32)sizeof(RGXFWIF_GPU_UTIL_FWCB),
				eError));
		goto failFWIfGpuUtilFWCbCtlMemDescAlloc;
	}

	RGXSetFirmwareAddress(&psRGXFWInit->psGpuUtilFWCbCtl,
						psDevInfo->psRGXFWIfGpuUtilFWCbCtlMemDesc,
						0, RFW_FWADDR_NOREF_FLAG);

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWIfGpuUtilFWCbCtlMemDesc,
									  (IMG_VOID **)&psDevInfo->psRGXFWIfGpuUtilFWCb);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to acquire kernel GPU utilization FW CB ctl (%u)",
				eError));
		goto failFWIfGpuUtilFWCbCtlMemDescAqCpuVirt;
	}

#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	PDUMPCOMMENT("Allocate rgxfw register configuration structure");
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_REG_CFG),
							uiMemAllocFlags,
							"Firmware register configuration structure",
							&psDevInfo->psRGXFWIfRegCfgMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate %ld bytes for fw register configurations (%u)",
				sizeof(RGXFWIF_REG_CFG),
				eError));
		goto failFWIfTraceBufCtlMemDescAlloc;
	}

	RGXSetFirmwareAddress(&psRGXFWInit->psRegCfg,
						psDevInfo->psRGXFWIfRegCfgMemDesc,
						0, RFW_FWADDR_NOREF_FLAG);
#endif
	/* Allocate a sync for power management */
	eError = SyncPrimContextCreate(IMG_NULL,
									psDevInfo->psDeviceNode,
									&psDevInfo->hSyncPrimContext);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate sync primitive context with error (%u)", eError));
		goto failed_sync_ctx_alloc;
	}

	eError = SyncPrimAlloc(psDevInfo->hSyncPrimContext, &psDevInfo->psPowSyncPrim);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate sync primitive with error (%u)", eError));
		goto failed_sync_alloc;
	}

	psRGXFWInit->uiPowerSync = SyncPrimGetFirmwareAddr(psDevInfo->psPowSyncPrim);

	/* Required info by FW to calculate the ActivePM idle timer latency */
	{
		RGX_DATA *psRGXData = (RGX_DATA*) psDeviceNode->psDevConfig->hDevData;

		psRGXFWInit->ui32InitialCoreClockSpeed = psRGXData->psRGXTimingInfo->ui32CoreClockSpeed;
		psRGXFWInit->ui32ActivePMLatencyms = psRGXData->psRGXTimingInfo->ui32ActivePMLatencyms;
	}

	/* Setup BIF Fault read register */
	eError = RGXSetupBIFFaultReadRegister(psDeviceNode, psRGXFWInit);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to setup BIF fault read register"));
		goto failFWSetupBIFFaultReadRegister;
	}

	/* Apply FIX_HW_BRN_37200 */
	eError = RGXHwBrn37200(psDevInfo);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to apply HWBRN37200"));
		goto failFWHWBRN37200;
	}

	/*
	 * Set up kernel TA CCB.
	 */
	eError = RGXSetupKernelCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_TA, RGXFWIF_KCCB_TA_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_KCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate kernel TA CCB"));
		goto failTAKCCB;
	}

	/*
	 * Set up firmware TA CCB.
	 */
	eError = RGXSetupFirmwareCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_TA, RGXFWIF_FWCCB_TA_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_FWCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate Firmware TA CCB"));
		goto failTAFWCCB;
	}

	/*
	 * Set up kernel 3D CCB.
	 */
	eError = RGXSetupKernelCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_3D, RGXFWIF_KCCB_3D_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_KCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate kernel 3D CCB"));
		goto fail3DKCCB;
	}

	/*
	 * Set up Firmware 3D CCB.
	 */
	eError = RGXSetupFirmwareCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_3D, RGXFWIF_FWCCB_3D_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_FWCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate Firmware 3D CCB"));
		goto fail3DFWCCB;
	}

	/*
	 * Set up kernel 2D CCB.
	 */
	eError = RGXSetupKernelCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_2D, RGXFWIF_KCCB_2D_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_KCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate kernel 2D CCB"));
		goto fail2DKCCB;
	}
	/*
	 * Set up Firmware 2D CCB.
	 */
	eError = RGXSetupFirmwareCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_2D, RGXFWIF_FWCCB_2D_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_FWCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate Firmware 2D CCB"));
		goto fail2DFWCCB;
	}

	/*
	 * Set up kernel compute CCB.
	 */
	eError = RGXSetupKernelCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_CDM, RGXFWIF_KCCB_CDM_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_KCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate kernel Compute CCB"));
		goto failCompKCCB;
	}

	/*
	 * Set up Firmware Compute CCB.
	 */
	eError = RGXSetupFirmwareCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_CDM, RGXFWIF_FWCCB_CDM_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_FWCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate Firmware Compute CCB"));
		goto failCompFWCCB;
	}

	/*
	 * Set up kernel general purpose CCB.
	 */
	eError = RGXSetupKernelCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_GP, RGXFWIF_KCCB_GP_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_KCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate kernel General Purpose CCB"));
		goto failGPKCCB;
	}

	/*
	 * Set up Firmware general purpose CCB.
	 */
	eError = RGXSetupFirmwareCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_GP, RGXFWIF_FWCCB_GP_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_FWCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate Firmware General Purpose CCB"));
		goto failGPFWCCB;
	}
#if defined(RGX_FEATURE_RAY_TRACING)
	/*
	 * Set up kernel SHG CCB.
	 */
	eError = RGXSetupKernelCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_SHG, RGXFWIF_KCCB_SHG_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_KCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate kernel SHG CCB"));
		goto failSHGCCB;
	}

	/*
	 * Set up Firmware SHG CCB.
	 */
	eError = RGXSetupFirmwareCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_SHG, RGXFWIF_FWCCB_SHG_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_FWCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate Firmware SHG CCB"));
		goto failSHGFWCCB;
	}

	/*
	 * Set up kernel RTU CCB.
	 */
	eError = RGXSetupKernelCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_RTU, RGXFWIF_KCCB_RTU_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_KCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate kernel RTU CCB"));
		goto failRTUCCB;
	}

	/*
	 * Set up Firmware RTU CCB.
	 */
	eError = RGXSetupFirmwareCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_RTU, RGXFWIF_FWCCB_RTU_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_FWCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate Firmware SHG CCB"));
		goto failRTUFWCCB;
	}
#endif

	/* Setup Signature and Checksum Buffers for TA and 3D */
	eError = RGXFWSetupSignatureChecks(psDevInfo,
									   &psDevInfo->psRGXFWSigTAChecksMemDesc,
									   ui32SignatureChecksBufSize,
									   &psRGXFWInit->asSigBufCtl[RGXFWIF_DM_TA]);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to setup TA signature checks"));
		goto failTASigCheck;
	}
	psDevInfo->ui32SigTAChecksSize = ui32SignatureChecksBufSize;

	eError = RGXFWSetupSignatureChecks(psDevInfo,
									   &psDevInfo->psRGXFWSig3DChecksMemDesc,
									   ui32SignatureChecksBufSize,
									   &psRGXFWInit->asSigBufCtl[RGXFWIF_DM_3D]);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to setup 3D signature checks"));
		goto fail3DSigCheck;
	}
	psDevInfo->ui32Sig3DChecksSize = ui32SignatureChecksBufSize;

#if defined(RGXFW_ALIGNCHECKS)
	eError = RGXFWSetupAlignChecks(psDevInfo,
								&psRGXFWInit->paui32AlignChecks,
								pui32RGXFWAlignChecks,
								ui32RGXFWAlignChecksSize);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to setup alignment checks"));
		goto failAlignCheck;
	}
#endif

	/* Fill the remaining bits of fw the init data */
	psRGXFWInit->sPDSExecBase.uiAddr = RGX_PDSCODEDATA_HEAP_BASE;
	psRGXFWInit->sUSCExecBase.uiAddr = RGX_USCCODE_HEAP_BASE;
	psRGXFWInit->sDPXControlStreamBase.uiAddr = RGX_DOPPLER_HEAP_BASE;
	psRGXFWInit->sResultDumpBase.uiAddr = RGX_DOPPLER_HEAP_BASE;
	psRGXFWInit->sRTUHeapBase.uiAddr = RGX_DOPPLER_HEAP_BASE;

	/* RD Power Island */
	{
		RGX_DATA *psRGXData = (RGX_DATA*) psDeviceNode->psDevConfig->hDevData;
		IMG_BOOL bEnableRDPowIsland = psRGXData->psRGXTimingInfo->bEnableRDPowIsland;

		ui32ConfigFlags |= bEnableRDPowIsland? RGXFWIF_INICFG_POW_RASCALDUST : 0;

	}

	psRGXFWInit->ui32ConfigFlags = ui32ConfigFlags & RGXFWIF_INICFG_ALL;
	psRGXFWInit->ui32FilterFlags = ui32FilterFlags;

	psDevInfo->bPDPEnabled = (ui32ConfigFlags & RGXFWIF_SRVCFG_DISABLE_PDP_EN)
			? IMG_FALSE : IMG_TRUE;

	/* Initialise the compatibility check data */
	RGXFWIF_COMPCHECKS_BVNC_INIT(psRGXFWInit->sRGXCompChecks.sFWBVNC);
	RGXFWIF_COMPCHECKS_BVNC_INIT(psRGXFWInit->sRGXCompChecks.sHWBVNC);

	{
		/* Below line is to make sure (compilation time check) that
				RGX_BVNC_KM_V_ST fits into RGXFWIF_COMPCHECKS_BVNC structure */
		IMG_CHAR _tmp_[RGXFWIF_COMPCHECKS_BVNC_V_LEN_MAX] = RGX_BVNC_KM_V_ST;
		_tmp_[0] = '\0';
	}

	PDUMPCOMMENT("Dump RGXFW Init data");
	if (!bEnableSignatureChecks)
	{
#if defined(PDUMP)
		PDUMPCOMMENT("(to enable rgxfw signatures place the following line after the RTCONF line)");
		DevmemPDumpLoadMem(	psDevInfo->psRGXFWIfInitMemDesc,
							offsetof(RGXFWIF_INIT, asSigBufCtl),
							sizeof(RGXFWIF_SIGBUF_CTL)*RGXFWIF_DM_MAX,
							PDUMP_FLAGS_CONTINUOUS);
#endif
		psRGXFWInit->asSigBufCtl[RGXFWIF_DM_3D].psBuffer.ui32Addr = 0x0;
		psRGXFWInit->asSigBufCtl[RGXFWIF_DM_TA].psBuffer.ui32Addr = 0x0;
	}

	for (dm = 0; dm < RGXFWIF_DM_MAX; dm++)
	{
		psDevInfo->psRGXFWIfTraceBuf->aui16HwrDmLockedUpCount[dm] = 0;
		psDevInfo->psRGXFWIfTraceBuf->aui16HwrDmOverranCount[dm] = 0;
		psDevInfo->psRGXFWIfTraceBuf->aui16HwrDmRecoveredCount[dm] = 0;
		psDevInfo->psRGXFWIfTraceBuf->aui16HwrDmFalseDetectCount[dm] = 0;
		psDevInfo->psRGXFWIfTraceBuf->apsHwrDmFWCommonContext[dm].ui32Addr = 0;
	}

	/*
	 * BIF Tiling configuration
	 */

	psRGXFWInit->sBifTilingCfg[0].uiBase = RGX_BIF_TILING_HEAP_1_BASE;
	psRGXFWInit->sBifTilingCfg[0].uiLen = RGX_BIF_TILING_HEAP_SIZE;
	psRGXFWInit->sBifTilingCfg[0].uiXStride = pui32BIFTilingXStrides[0];
	psRGXFWInit->sBifTilingCfg[1].uiBase = RGX_BIF_TILING_HEAP_2_BASE;
	psRGXFWInit->sBifTilingCfg[1].uiLen = RGX_BIF_TILING_HEAP_SIZE;
	psRGXFWInit->sBifTilingCfg[1].uiXStride = pui32BIFTilingXStrides[1];
	psRGXFWInit->sBifTilingCfg[2].uiBase = RGX_BIF_TILING_HEAP_3_BASE;
	psRGXFWInit->sBifTilingCfg[2].uiLen = RGX_BIF_TILING_HEAP_SIZE;
	psRGXFWInit->sBifTilingCfg[2].uiXStride = pui32BIFTilingXStrides[2];
	psRGXFWInit->sBifTilingCfg[3].uiBase = RGX_BIF_TILING_HEAP_4_BASE;
	psRGXFWInit->sBifTilingCfg[3].uiLen = RGX_BIF_TILING_HEAP_SIZE;
	psRGXFWInit->sBifTilingCfg[3].uiXStride = pui32BIFTilingXStrides[3];

#if defined(PDUMP)
	PDUMPCOMMENT("Dump rgxfw HW Perf Info structure");
	DevmemPDumpLoadMem (psDevInfo->psRGXFWIfHWPerfBufCtlMemDesc,
						0,
						psDevInfo->ui32RGXFWIfHWPerfBufSize,
						PDUMP_FLAGS_CONTINUOUS);
	PDUMPCOMMENT("Dump rgxfw trace structure");
	DevmemPDumpLoadMem(	psDevInfo->psRGXFWIfTraceBufCtlMemDesc,
						0,
						sizeof(RGXFWIF_TRACEBUF),
						PDUMP_FLAGS_CONTINUOUS);
#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	PDUMPCOMMENT("Dump rgxfw register configuration buffer");
	DevmemPDumpLoadMem(	psDevInfo->psRGXFWIfRegCfgMemDesc,
						0,
						sizeof(RGXFWIF_REG_CFG),
						PDUMP_FLAGS_CONTINUOUS);
#endif
	DevmemPDumpLoadMem(	psDevInfo->psRGXFWIfInitMemDesc,
						0,
						sizeof(RGXFWIF_INIT),
						PDUMP_FLAGS_CONTINUOUS);

	PDUMPCOMMENT("RTCONF: run-time configuration");


	/* Dump the config options so they can be edited.
	 *
	 * FIXME: Need new DevmemPDumpWRW API which writes a WRW to load ui32ConfigFlags
	 */
	PDUMPCOMMENT("(Set the FW config options here)");
	PDUMPCOMMENT("( bit 0: Ctx Switch TA Enable)");
	PDUMPCOMMENT("( bit 1: Ctx Switch 3D Enable)");
	PDUMPCOMMENT("( bit 2: Ctx Switch CDM Enable)");
	PDUMPCOMMENT("( bit 3: Ctx Switch Rand mode)");
	PDUMPCOMMENT("( bit 4: Ctx Switch Soft Reset Enable)");
	PDUMPCOMMENT("( bit 5: Reserved (do not set)");
	PDUMPCOMMENT("( bit 6: Rascal+Dust Power Island)");
	PDUMPCOMMENT("( bit 7: Enable HWPerf)");
	PDUMPCOMMENT("( bit 8: Enable HWR)");
	PDUMPCOMMENT("( bit 9: Check MList)");
	PDUMPCOMMENT("( bit 10: Disable Auto Clock Gating)");
	PDUMPCOMMENT("( bit 11: Enable HWPerf Polling Perf Counter)");
	PDUMPCOMMENT("( bit 12-13: Ctx Switch Object mode)");
	PDUMPCOMMENT("( bit 14: Enable SHG Bypass mode)");
	PDUMPCOMMENT("( bit 15: Enable RTU Bypass mode)");
	PDUMPCOMMENT("( bit 16: Enable register configuration)");

	DevmemPDumpLoadMemValue32(psDevInfo->psRGXFWIfInitMemDesc,
							offsetof(RGXFWIF_INIT, ui32ConfigFlags),
							psRGXFWInit->ui32ConfigFlags,
							PDUMP_FLAGS_CONTINUOUS);

	/*
	 * Dump the log config so it can be edited.
	 */
	PDUMPCOMMENT("(Set the log config here)");
	PDUMPCOMMENT("( bit 0: Log Type: set for TRACE, reset for TBI)");
	PDUMPCOMMENT("( bit 1: MAIN Group Enable)");
	PDUMPCOMMENT("( bit 2: MTS Group Enable)");
	PDUMPCOMMENT("( bit 3: CLEANUP Group Enable)");
	PDUMPCOMMENT("( bit 4: CSW Group Enable)");
	PDUMPCOMMENT("( bit 5: BIF Group Enable)");
	PDUMPCOMMENT("( bit 6: PM Group Enable)");
	PDUMPCOMMENT("( bit 7: RTD Group Enable)");
	PDUMPCOMMENT("( bit 8: SPM Group Enable)");
	PDUMPCOMMENT("( bit 9: POW Group Enable)");
	PDUMPCOMMENT("( bit 10: HWR Group Enable)");
	PDUMPCOMMENT("( bit 11: DEBUG Group Enable)");
	DevmemPDumpLoadMemValue32(psDevInfo->psRGXFWIfTraceBufCtlMemDesc,
							offsetof(RGXFWIF_TRACEBUF, ui32LogType),
							psDevInfo->psRGXFWIfTraceBuf->ui32LogType,
							PDUMP_FLAGS_CONTINUOUS);
#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	PDUMPCOMMENT("(Number of registers configurations in sidekick)");
	DevmemPDumpLoadMemValue32(psDevInfo->psRGXFWIfRegCfgMemDesc,
							offsetof(RGXFWIF_REG_CFG, ui32NumRegsSidekick),
							0,
							PDUMP_FLAGS_CONTINUOUS);
	PDUMPCOMMENT("(Number of registers configurations in rascal/dust)");
	DevmemPDumpLoadMemValue32(psDevInfo->psRGXFWIfRegCfgMemDesc,
							offsetof(RGXFWIF_REG_CFG, ui32NumRegsRascalDust),
							0,
							PDUMP_FLAGS_CONTINUOUS);
	PDUMPCOMMENT("(Set registers here, address, value)");
	DevmemPDumpLoadMemValue64(psDevInfo->psRGXFWIfRegCfgMemDesc,
							offsetof(RGXFWIF_REG_CFG, asRegConfigs[0].ui64Addr),
							0,
							PDUMP_FLAGS_CONTINUOUS);
	DevmemPDumpLoadMemValue64(psDevInfo->psRGXFWIfRegCfgMemDesc,
							offsetof(RGXFWIF_REG_CFG, asRegConfigs[0].ui64Value),
							0,
							PDUMP_FLAGS_CONTINUOUS);
#endif /* SUPPORT_USER_REGISTER_CONFIGURATION */
#endif

	/* Initialize FW started flag */
	psRGXFWInit->bFirmwareStarted = IMG_FALSE;

	/* We don't need access to the fw init data structure anymore */
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfInitMemDesc);
	psRGXFWInit = IMG_NULL;

	psDevInfo->bFirmwareInitialised = IMG_TRUE;

	return PVRSRV_OK;

#if defined(RGXFW_ALIGNCHECKS)
failAlignCheck:
	DevmemFwFree(psDevInfo->psRGXFWSig3DChecksMemDesc);
#endif
fail3DSigCheck:
	DevmemFwFree(psDevInfo->psRGXFWSigTAChecksMemDesc);
failTASigCheck:
#if defined(RGX_FEATURE_RAY_TRACING)
	RGXFreeFirmwareCCB(psDevInfo, RGXFWIF_DM_RTU);
failRTUFWCCB:
	RGXFreeKernelCCB(psDevInfo, RGXFWIF_DM_RTU);
failRTUCCB:
	RGXFreeFirmwareCCB(psDevInfo, RGXFWIF_DM_SHG);
failSHGFWCCB:
	RGXFreeKernelCCB(psDevInfo, RGXFWIF_DM_SHG);
failSHGCCB:
#endif /* RGX_FEATURE_RAY_TRACING */
	RGXFreeFirmwareCCB(psDevInfo, RGXFWIF_DM_GP);
failGPFWCCB:
	RGXFreeKernelCCB(psDevInfo, RGXFWIF_DM_GP);
failGPKCCB:
	RGXFreeFirmwareCCB(psDevInfo, RGXFWIF_DM_CDM);
failCompFWCCB:
	RGXFreeKernelCCB(psDevInfo, RGXFWIF_DM_CDM);
failCompKCCB:
	RGXFreeFirmwareCCB(psDevInfo, RGXFWIF_DM_2D);
fail2DFWCCB:
	RGXFreeKernelCCB(psDevInfo, RGXFWIF_DM_2D);
fail2DKCCB:
	RGXFreeFirmwareCCB(psDevInfo, RGXFWIF_DM_3D);
fail3DFWCCB:
	RGXFreeKernelCCB(psDevInfo, RGXFWIF_DM_3D);
fail3DKCCB:
	RGXFreeFirmwareCCB(psDevInfo, RGXFWIF_DM_TA);
failTAFWCCB:
	RGXFreeKernelCCB(psDevInfo, RGXFWIF_DM_TA);

failTAKCCB:
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfTraceBufCtlMemDesc);

	RGXHwBrn37200Rollback(psDevInfo);
failFWHWBRN37200:

	RGXSetupBIFFaultReadRegisterRollback(psDevInfo);
failFWSetupBIFFaultReadRegister:
	SyncPrimFree(psDevInfo->psPowSyncPrim);

failed_sync_alloc:
	SyncPrimContextDestroy(psDevInfo->hSyncPrimContext);

failed_sync_ctx_alloc:
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfGpuUtilFWCbCtlMemDesc);

failFWIfGpuUtilFWCbCtlMemDescAqCpuVirt:
	DevmemFwFree(psDevInfo->psRGXFWIfGpuUtilFWCbCtlMemDesc);

failFWIfGpuUtilFWCbCtlMemDescAlloc:
	/* Nothing to do for invalid log type check */

failInvalidLogType:
	RGXHWPerfDeinit();

failHWPerfInit:
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfHWRInfoBufCtlMemDesc);

failFWIfHWRInfoBufCtlMemDescAqCpuVirt:
	DevmemFwFree(psDevInfo->psRGXFWIfHWRInfoBufCtlMemDesc);

failFWIfHWRInfoBufCtlMemDescAlloc:
	DevmemFwFree(psDevInfo->psRGXFWIfCorememDataStoreMemDesc);

failFWIfCorememDataStoreMemDescAlloc:
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfHWPerfBufCtlMemDesc);
failFWIfInitHWPerfInfoMemDescAqCpuVirt:
	PVR_DPF((PVR_DBG_WARNING, "failFWIfInitHWRPerfInfoMemDescAqCpuVirt"));
	DevmemFwFree(psDevInfo->psRGXFWIfHWPerfBufCtlMemDesc);

failFWIfInitHWPerfInfoMemDescAlloc:
	PVR_DPF((PVR_DBG_WARNING, "failFWIfInitHWPerfInfoMemDescAlloc"));
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfTraceBufCtlMemDesc);
failFWIfTraceBufCtlMemDescAqCpuVirt:
	DevmemFwFree(psDevInfo->psRGXFWIfTraceBufCtlMemDesc);

failFWIfTraceBufCtlMemDescAlloc:
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfInitMemDesc);
failFWIfInitMemDescAqCpuVirt:
	DevmemFwFree(psDevInfo->psRGXFWIfInitMemDesc);

failFWIfInitMemDescAlloc:

	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*!
*******************************************************************************

 @Function	RGXFreeFirmware

 @Description

 Frees all the firmware-related allocations

 @Input psDevInfo

 @Return PVRSRV_ERROR

******************************************************************************/
IMG_VOID RGXFreeFirmware(PVRSRV_RGXDEV_INFO 	*psDevInfo)
{
	RGXFWIF_DM	eCCBType;

	psDevInfo->bFirmwareInitialised = IMG_FALSE;

	for (eCCBType = 0; eCCBType < RGXFWIF_DM_MAX; eCCBType++)
	{
		if (psDevInfo->apsKernelCCBMemDesc[eCCBType])
		{
			RGXFreeKernelCCB(psDevInfo, eCCBType);
		}
		if (psDevInfo->apsFirmwareCCBMemDesc[eCCBType])
		{
			RGXFreeFirmwareCCB(psDevInfo, eCCBType);
		}
	}

	if (psDevInfo->psRGXFWCodeMemDesc)
	{
		/* Free fw code */
		PDUMPCOMMENT("Freeing FW code memory");
		DevmemReleaseDevVirtAddr(psDevInfo->psRGXFWCodeMemDesc);
		DevmemFwFree(psDevInfo->psRGXFWCodeMemDesc);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,"No firmware code memory to free!"));
	}

	if (psDevInfo->psRGXFWDataMemDesc)
	{
		/* Free fw data */
		PDUMPCOMMENT("Freeing FW data memory");
		DevmemReleaseDevVirtAddr(psDevInfo->psRGXFWDataMemDesc);
		DevmemFwFree(psDevInfo->psRGXFWDataMemDesc);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,"No firmware data memory to free!"));
	}

	if (psDevInfo->psRGXFWCorememMemDesc)
	{
		/* Free fw data */
		PDUMPCOMMENT("Freeing FW coremem memory");
		DevmemReleaseDevVirtAddr(psDevInfo->psRGXFWCorememMemDesc);
		DevmemFwFree(psDevInfo->psRGXFWCorememMemDesc);
	}

#if defined(RGXFW_ALIGNCHECKS)
	if (psDevInfo->psRGXFWAlignChecksMemDesc)
	{
		RGXFWFreeAlignChecks(psDevInfo);
	}
#endif

	if (psDevInfo->psRGXFWSigTAChecksMemDesc)
	{
		DevmemFwFree(psDevInfo->psRGXFWSigTAChecksMemDesc);
	}

	if (psDevInfo->psRGXFWSig3DChecksMemDesc)
	{
		DevmemFwFree(psDevInfo->psRGXFWSig3DChecksMemDesc);
	}

#if defined(FIX_HW_BRN_37200)
	if (psDevInfo->psRGXFWHWBRN37200MemDesc)
	{
		DevmemReleaseDevVirtAddr(psDevInfo->psRGXFWHWBRN37200MemDesc);
		DevmemFree(psDevInfo->psRGXFWHWBRN37200MemDesc);
	}
#endif

	RGXSetupBIFFaultReadRegisterRollback(psDevInfo);

	if (psDevInfo->psPowSyncPrim != IMG_NULL)
	{
		SyncPrimFree(psDevInfo->psPowSyncPrim);
		psDevInfo->psPowSyncPrim = IMG_NULL;
	}

	if (psDevInfo->hSyncPrimContext != 0)
	{
		SyncPrimContextDestroy(psDevInfo->hSyncPrimContext);
		psDevInfo->hSyncPrimContext = 0;
	}

	if (psDevInfo->psRGXFWIfGpuUtilFWCbCtlMemDesc)
	{
		DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfGpuUtilFWCbCtlMemDesc);
		DevmemFwFree(psDevInfo->psRGXFWIfGpuUtilFWCbCtlMemDesc);
	}

	if (psDevInfo->psRGXFWIfHWRInfoBufCtlMemDesc)
	{
		DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfHWRInfoBufCtlMemDesc);
		DevmemFwFree(psDevInfo->psRGXFWIfHWRInfoBufCtlMemDesc);
	}

	RGXHWPerfDeinit();

	if (psDevInfo->psRGXFWIfHWPerfBufCtlMemDesc)
	{
		psDevInfo->psRGXFWIfHWPerfBuf = IMG_NULL;
		DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfHWPerfBufCtlMemDesc);
		DevmemFwFree(psDevInfo->psRGXFWIfHWPerfBufCtlMemDesc);
		psDevInfo->psRGXFWIfHWPerfBufCtlMemDesc = IMG_NULL;
	}

	if (psDevInfo->psRGXFWIfCorememDataStoreMemDesc)
	{
		DevmemFwFree(psDevInfo->psRGXFWIfCorememDataStoreMemDesc);
		psDevInfo->psRGXFWIfCorememDataStoreMemDesc = IMG_NULL;
	}

	if (psDevInfo->psRGXFWIfTraceBufCtlMemDesc)
	{
		psDevInfo->psRGXFWIfTraceBuf = IMG_NULL;
		DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfTraceBufCtlMemDesc);
		DevmemFwFree(psDevInfo->psRGXFWIfTraceBufCtlMemDesc);
		psDevInfo->psRGXFWIfTraceBufCtlMemDesc = IMG_NULL;
	}
#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	if (psDevInfo->psRGXFWIfRegCfgMemDesc)
	{
		DevmemFwFree(psDevInfo->psRGXFWIfRegCfgMemDesc);
	}
#endif
	if (psDevInfo->psRGXFWIfInitMemDesc)
	{
		DevmemFwFree(psDevInfo->psRGXFWIfInitMemDesc);
		psDevInfo->psRGXFWIfInitMemDesc = IMG_NULL;
	}
}


/******************************************************************************
 FUNCTION	: RGXStartFirmware

 PURPOSE	: Attempts to obtain a slot in the Kernel CCB

 PARAMETERS	: psDevInfo

 RETURNS	: PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR RGXStartFirmware(PVRSRV_RGXDEV_INFO 	*psDevInfo)
{
	PVRSRV_ERROR	eError = PVRSRV_OK;

	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_POWERTRANS|PDUMP_FLAGS_CONTINUOUS, "RGXStart: RGX Firmware Slave boot Start");
	/*
	 * Run init script.
	 */
	eError = RGXRunScript(psDevInfo, psDevInfo->psScripts->asInitCommands, RGX_MAX_INIT_COMMANDS, PDUMP_FLAGS_POWERTRANS|PDUMP_FLAGS_CONTINUOUS, IMG_NULL);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXStart: RGXRunScript failed (%d)", eError));
		return eError;
	}
	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_POWERTRANS|PDUMP_FLAGS_CONTINUOUS, "RGXStart: RGX Firmware startup complete\n");

	return eError;
}


/******************************************************************************
 FUNCTION	: RGXAcquireKernelCCBSlot

 PURPOSE	: Attempts to obtain a slot in the Kernel CCB

 PARAMETERS	: psCCB - the CCB
			: Address of space if available, IMG_NULL otherwise

 RETURNS	: PVRSRV_ERROR
******************************************************************************/
static PVRSRV_ERROR RGXAcquireKernelCCBSlot(RGXFWIF_CCB_CTL	*psKCCBCtl,
											IMG_UINT32			*pui32Offset)
{
	IMG_UINT32	ui32OldWriteOffset, ui32NextWriteOffset;

	ui32OldWriteOffset = psKCCBCtl->ui32WriteOffset;
	ui32NextWriteOffset = (ui32OldWriteOffset + 1) & psKCCBCtl->ui32WrapMask;

	/* Note: The MTS can queue up to 255 kicks (254 pending kicks and 1 executing kick)
	 * Hence the kernel CCB should not queue more 254 commands
	 */
	PVR_ASSERT(psKCCBCtl->ui32WrapMask < 255);

	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{

		if (ui32NextWriteOffset != psKCCBCtl->ui32ReadOffset)
		{
			*pui32Offset = ui32NextWriteOffset;
			return PVRSRV_OK;
		}
		{
			/*
			 * The following sanity check doesn't impact performance,
			 * since the CPU has to wait for the GPU anyway (full kernel CCB).
			 */
			if (PVRSRVGetPVRSRVData()->eServicesState != PVRSRV_SERVICES_STATE_OK)
			{
				return PVRSRV_ERROR_KERNEL_CCB_FULL;
			}
		}

		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

	/* Time out on waiting for CCB space */
	return PVRSRV_ERROR_KERNEL_CCB_FULL;
}


PVRSRV_ERROR RGXSendCommandWithPowLock(PVRSRV_RGXDEV_INFO 	*psDevInfo,
										 RGXFWIF_DM			eKCCBType,
										 RGXFWIF_KCCB_CMD	*psKCCBCmd,
										 IMG_UINT32			ui32CmdSize,
										 IMG_BOOL			bPDumpContinuous)
{
	PVRSRV_ERROR		eError;
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevInfo->psDeviceNode;

	/* Ensure RGX is powered up before kicking MTS */
	eError = PVRSRVPowerLock();

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "RGXSendCommandWithPowLock: failed to acquire powerlock (%s)",
					PVRSRVGetErrorStringKM(eError)));

		goto _PVRSRVPowerLock_Exit;
	}

	PDUMPPOWCMDSTART();

	eError = PVRSRVSetDevicePowerStateKM(psDeviceNode->sDevId.ui32DeviceIndex,
										 PVRSRV_DEV_POWER_STATE_ON,
										 IMG_FALSE);
	PDUMPPOWCMDEND();

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "RGXSendCommandWithPowLock: failed to transition RGX to ON (%s)",
					PVRSRVGetErrorStringKM(eError)));

		goto _PVRSRVSetDevicePowerStateKM_Exit;
	}

	RGXSendCommandRaw(psDevInfo, eKCCBType,  psKCCBCmd, ui32CmdSize, bPDumpContinuous?PDUMP_FLAGS_CONTINUOUS:0);

_PVRSRVSetDevicePowerStateKM_Exit:
	PVRSRVPowerUnlock();

_PVRSRVPowerLock_Exit:
	return eError;
}

PVRSRV_ERROR RGXSendCommandRaw(PVRSRV_RGXDEV_INFO 	*psDevInfo,
								 RGXFWIF_DM			eKCCBType,
								 RGXFWIF_KCCB_CMD	*psKCCBCmd,
								 IMG_UINT32			ui32CmdSize,
								 PDUMP_FLAGS_T		uiPdumpFlags)
{
	PVRSRV_ERROR		eError;
	RGXFWIF_CCB_CTL		*psKCCBCtl = psDevInfo->apsKernelCCBCtl[eKCCBType];
	IMG_UINT8			*pui8KCCB = psDevInfo->apsKernelCCB[eKCCBType];
	IMG_UINT32			ui32NewWriteOffset;
	IMG_UINT32			ui32OldWriteOffset = psKCCBCtl->ui32WriteOffset;
#if !defined(PDUMP)
	PVR_UNREFERENCED_PARAMETER(uiPdumpFlags);
#endif

	PVR_ASSERT(ui32CmdSize == psKCCBCtl->ui32CmdSize);
	if (!OSLockIsLocked(PVRSRVGetPVRSRVData()->hPowerLock))
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXSendCommandRaw called without power lock held!"));
		PVR_ASSERT(OSLockIsLocked(PVRSRVGetPVRSRVData()->hPowerLock));
	}

	/*
	 * Acquire a slot in the CCB.
	 */
	eError = RGXAcquireKernelCCBSlot(psKCCBCtl, &ui32NewWriteOffset);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXSendCommandRaw failed to acquire CCB slot. Type:%u Error:%u",
				eKCCBType, eError));
#if defined(DEBUG)
		PVRSRVDebugRequest(DEBUG_REQUEST_VERBOSITY_MAX);
#endif
		goto _RGXSendCommandRaw_Exit;
	}

	/*
	 * Copy the command into the CCB.
	 */
	OSMemCopy(&pui8KCCB[ui32OldWriteOffset * psKCCBCtl->ui32CmdSize],
			  psKCCBCmd, psKCCBCtl->ui32CmdSize);

	/* ensure kCCB data is written before the offsets */
	OSWriteMemoryBarrier();

	/* Move past the current command */
	psKCCBCtl->ui32WriteOffset = ui32NewWriteOffset;


#if defined(PDUMP)
	{
		IMG_BOOL bIsInCaptureRange;
		IMG_BOOL bPdumpEnabled;
		IMG_BOOL bPDumpContinuous = (uiPdumpFlags & PDUMP_FLAGS_CONTINUOUS) != 0;
		IMG_BOOL bPDumpPowTrans = (uiPdumpFlags & PDUMP_FLAGS_POWERTRANS) != 0;

		PDumpIsCaptureFrameKM(&bIsInCaptureRange);
		bPdumpEnabled = (bIsInCaptureRange || bPDumpContinuous) && !bPDumpPowTrans;

		/* in capture range */
		if (bPdumpEnabled)
		{
			/* Dump new Kernel CCB content */
			PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "Dump kCCB(%d) cmd, woff = %d", eKCCBType, ui32OldWriteOffset);
			DevmemPDumpLoadMem(psDevInfo->apsKernelCCBMemDesc[eKCCBType],
					ui32OldWriteOffset * psKCCBCtl->ui32CmdSize,
					psKCCBCtl->ui32CmdSize,
					PDUMP_FLAGS_CONTINUOUS);

			if (!psDevInfo->abDumpedKCCBCtlAlready[eKCCBType])
			{
				/* entering capture range */
				psDevInfo->abDumpedKCCBCtlAlready[eKCCBType] = IMG_TRUE;

				/* wait for firmware to catch up */
				PVR_DPF((PVR_DBG_MESSAGE, "RGXSendCommandRaw: waiting on fw to catch-up. DM: %d, roff: %d, woff: %d",
							eKCCBType, psKCCBCtl->ui32ReadOffset, ui32OldWriteOffset));
				PVRSRVPollForValueKM(&psKCCBCtl->ui32ReadOffset, ui32OldWriteOffset, 0xFFFFFFFF);

				/* Dump Init state of Kernel CCB control (read and write offset) */
				PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "Initial state of kernel CCB Control(%d), roff: %d, woff: %d", eKCCBType, psKCCBCtl->ui32ReadOffset, psKCCBCtl->ui32WriteOffset);
				DevmemPDumpLoadMem(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType],
						0,
						sizeof(RGXFWIF_CCB_CTL),
						PDUMP_FLAGS_CONTINUOUS);
			}
			else
			{
				/* already in capture range */

				/* Dump new kernel CCB write offset */
				PDUMPCOMMENTWITHFLAGS(uiPdumpFlags, "Dump kCCBCtl(%d) woff: %d", eKCCBType, ui32NewWriteOffset);
				DevmemPDumpLoadMem(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType],
								   offsetof(RGXFWIF_CCB_CTL, ui32WriteOffset),
								   sizeof(IMG_UINT32),
								   uiPdumpFlags);
			}
		}

		/* out of capture range */
		if (!bPdumpEnabled)
		{
			if (psDevInfo->abDumpedKCCBCtlAlready[eKCCBType])
			{
				/* exiting capture range */
				psDevInfo->abDumpedKCCBCtlAlready[eKCCBType] = IMG_FALSE;

				/* make sure previous cmds are drained in pdump in case we will 'jump' over some future cmds */
				PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS,
							"kCCB(%p): Draining rgxfw_roff (0x%x) == woff (0x%x)",
								psKCCBCtl,
								ui32OldWriteOffset,
								ui32OldWriteOffset);
				eError = DevmemPDumpDevmemPol32(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType],
												offsetof(RGXFWIF_CCB_CTL, ui32ReadOffset),
												ui32OldWriteOffset,
												0xffffffff,
												PDUMP_POLL_OPERATOR_EQUAL,
												PDUMP_FLAGS_CONTINUOUS);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_WARNING, "RGXSendCommandRaw: problem pdumping POL for cCCBCtl (%d)", eError));
					goto _RGXSendCommandRaw_Exit;
				}
			}
		}
	}
#endif


	PDUMPCOMMENTWITHFLAGS(uiPdumpFlags, "MTS kick for kernel CCB %d", eKCCBType);
	/*
	 * Kick the MTS to schedule the firmware.
	 */
	{
		IMG_UINT32	ui32MTSRegVal = (eKCCBType & ~RGX_CR_MTS_SCHEDULE_DM_CLRMSK) | RGX_CR_MTS_SCHEDULE_TASK_COUNTED;

		__MTSScheduleWrite(psDevInfo, ui32MTSRegVal);

		PDUMPREG32(RGX_PDUMPREG_NAME, RGX_CR_MTS_SCHEDULE, ui32MTSRegVal, uiPdumpFlags);
	}

#if defined (NO_HARDWARE)
	/* keep the roff updated because fw isn't there to update it */
	psKCCBCtl->ui32ReadOffset = psKCCBCtl->ui32WriteOffset;
#endif

_RGXSendCommandRaw_Exit:
	return eError;
}

IMG_VOID RGXScheduleProcessQueuesKM(PVRSRV_CMDCOMP_HANDLE hCmdCompHandle)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE*) hCmdCompHandle;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

	OSScheduleMISR(psDevInfo->hProcessQueuesMISR);
}

/*!
******************************************************************************

 @Function	_RGXScheduleProcessQueuesMISR

 @Description - Sends uncounted kick to all the DMs (the FW will process all
				the queue for all the DMs)
******************************************************************************/
static IMG_VOID _RGXScheduleProcessQueuesMISR(IMG_VOID *pvData)
{
	PVRSRV_DEVICE_NODE     *psDeviceNode = (PVRSRV_DEVICE_NODE*) pvData;
	PVRSRV_RGXDEV_INFO     *psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_DM			   eDM;
	PVRSRV_ERROR		   eError;
	PVRSRV_DEV_POWER_STATE ePowerState;

	/* We don't need to acquire the BridgeLock as this power transition won't
	   send a command to the FW */
	eError = PVRSRVPowerLock();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "RGXScheduleProcessQueuesKM: failed to acquire powerlock (%s)",
					PVRSRVGetErrorStringKM(eError)));

		return;
	}

	/* Check whether it's worth waking up the GPU */
	eError = PVRSRVGetDevicePowerState(psDeviceNode->sDevId.ui32DeviceIndex, &ePowerState);

	if ((eError == PVRSRV_OK) && (ePowerState == PVRSRV_DEV_POWER_STATE_OFF))
	{
		RGXFWIF_GPU_UTIL_FWCB  *psUtilFWCb = psDevInfo->psRGXFWIfGpuUtilFWCb;
		IMG_UINT64             ui64FWCbEntryCurrent;
		IMG_BOOL               bGPUHasWorkWaiting;

		ui64FWCbEntryCurrent = psUtilFWCb->aui64CB[(psUtilFWCb->ui32WriteOffset - 1) & RGXFWIF_GPU_UTIL_FWCB_MASK];
		bGPUHasWorkWaiting = (RGXFWIF_GPU_UTIL_FWCB_ENTRY_STATE(ui64FWCbEntryCurrent) == RGXFWIF_GPU_UTIL_FWCB_STATE_BLOCKED);

		if (!bGPUHasWorkWaiting)
		{
			/* all queues are empty, don't wake up the GPU */
			PVRSRVPowerUnlock();
			return;
		}
	}

	/* wake up the GPU */
	eError = PVRSRVSetDevicePowerStateKM(psDeviceNode->sDevId.ui32DeviceIndex,
										 PVRSRV_DEV_POWER_STATE_ON,
										 IMG_FALSE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "RGXScheduleProcessQueuesKM: failed to transition RGX to ON (%s)",
					PVRSRVGetErrorStringKM(eError)));

		PVRSRVPowerUnlock();
		return;
	}

	/* uncounted kick for all DMs */
	for (eDM = RGXFWIF_HWDM_MIN; eDM < RGXFWIF_HWDM_MAX; eDM++)
	{
		IMG_UINT32	ui32MTSRegVal = (eDM & ~RGX_CR_MTS_SCHEDULE_DM_CLRMSK) | RGX_CR_MTS_SCHEDULE_TASK_NON_COUNTED;

		__MTSScheduleWrite(psDevInfo, ui32MTSRegVal);
	}

	PVRSRVPowerUnlock();
}

PVRSRV_ERROR RGXInstallProcessQueuesMISR(IMG_HANDLE *phMISR, PVRSRV_DEVICE_NODE *psDeviceNode)
{
	return OSInstallMISR(phMISR,
						 _RGXScheduleProcessQueuesMISR,
						 psDeviceNode);
}


/*!
******************************************************************************

 @Function	RGXScheduleCommand

 @Description - Submits a CCB command and kicks the firmware but first schedules
				any commands which have to happen before handle

 @Input psDevInfo - pointer to device info
 @Input eKCCBType - see RGXFWIF_CMD_*
 @Input pvKCCBCmd - kernel CCB command
 @Input ui32CmdSize -
 @Input bPDumpContinuous - TRUE if the pdump flags should be continuous


 @Return ui32Error - success or failure

******************************************************************************/
PVRSRV_ERROR RGXScheduleCommand(PVRSRV_RGXDEV_INFO 	*psDevInfo,
								RGXFWIF_DM			eKCCBType,
								RGXFWIF_KCCB_CMD	*psKCCBCmd,
								IMG_UINT32			ui32CmdSize,
								IMG_BOOL			bPDumpContinuous)
{
	PVRSRV_DATA *psData = PVRSRVGetPVRSRVData();
	PVRSRV_ERROR eError;

	if ((eKCCBType == RGXFWIF_DM_3D) || (eKCCBType == RGXFWIF_DM_2D) || (eKCCBType == RGXFWIF_DM_CDM))
	{
		/* This handles the no operation case */
		OSCPUOperation(psData->uiCacheOp);
		psData->uiCacheOp = PVRSRV_CACHE_OP_NONE;
	}

	eError = RGXPreKickCacheCommand(psDevInfo);
	if (eError != PVRSRV_OK) goto RGXScheduleCommand_exit;

	eError = RGXSendCommandWithPowLock(psDevInfo, eKCCBType, psKCCBCmd, ui32CmdSize, bPDumpContinuous);
	if (eError != PVRSRV_OK) goto RGXScheduleCommand_exit;


RGXScheduleCommand_exit:
	return eError;
}

/*
 * RGXCheckFirmwareCCBs
 */
IMG_VOID RGXCheckFirmwareCCBs(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	RGXFWIF_FWCCB_CMD 	*psFwCCBCmd;
	IMG_UINT32 			ui32DMCount;

	for (ui32DMCount = 0; ui32DMCount < RGXFWIF_DM_MAX; ui32DMCount++)
	{
		RGXFWIF_CCB_CTL *psFWCCBCtl = psDevInfo->apsFirmwareCCBCtl[ui32DMCount];
		IMG_UINT8 		*psFWCCB = psDevInfo->apsFirmwareCCB[ui32DMCount];

		while (psFWCCBCtl->ui32ReadOffset != psFWCCBCtl->ui32WriteOffset)
		{
			/* Point to the next command */
			psFwCCBCmd = ((RGXFWIF_FWCCB_CMD *)psFWCCB) + psFWCCBCtl->ui32ReadOffset;

			switch(psFwCCBCmd->eCmdType)
			{
				case RGXFWIF_FWCCB_CMD_ZSBUFFER_BACKING:
				{
					if (psDevInfo->bPDPEnabled)
					{
						PDUMP_PANIC(RGX, ZSBUFFER_BACKING, "Request to add backing to ZSBuffer");
					}
					RGXProcessRequestZSBufferBacking(psDevInfo,
													psFwCCBCmd->uCmdData.sCmdZSBufferBacking.ui32ZSBufferID);
					break;
				}

				case RGXFWIF_FWCCB_CMD_ZSBUFFER_UNBACKING:
				{
					if (psDevInfo->bPDPEnabled)
					{
						PDUMP_PANIC(RGX, ZSBUFFER_UNBACKING, "Request to remove backing from ZSBuffer");
					}
					RGXProcessRequestZSBufferUnbacking(psDevInfo,
													psFwCCBCmd->uCmdData.sCmdZSBufferBacking.ui32ZSBufferID);
					break;
				}

				case RGXFWIF_FWCCB_CMD_FREELIST_GROW:
				{
					if (psDevInfo->bPDPEnabled)
					{
						PDUMP_PANIC(RGX, FREELIST_GROW, "Request to grow the free list");
					}
					RGXProcessRequestGrow(psDevInfo,
										psFwCCBCmd->uCmdData.sCmdFreeListGS.ui32FreelistID);
					break;
				}

				case RGXFWIF_FWCCB_CMD_FREELISTS_RECONSTRUCTION:
				{
					if (psDevInfo->bPDPEnabled)
					{
						PDUMP_PANIC(RGX, FREELISTS_RECONSTRUCTION, "Request to reconstruct free lists");
					}
					RGXProcessRequestFreelistsReconstruction(psDevInfo, ui32DMCount,
										psFwCCBCmd->uCmdData.sCmdFreeListsReconstruction.ui32FreelistsCount,
										psFwCCBCmd->uCmdData.sCmdFreeListsReconstruction.aui32FreelistIDs);
					break;
				}

				case RGXFWIF_FWCCB_CMD_CONTEXT_RESET_NOTIFICATION:
				{
					/* This is not the best way to do it, but is the only option left by strict 32/64bit compilers. */
					IMG_PVOID    apvServerCommonContextID[2] = {0, 0};
					IMG_UINT64*  ui64ServerCommonContextID = (IMG_UINT64*) &(apvServerCommonContextID[0]);
					RGX_SERVER_COMMON_CONTEXT  *psServerCommonContext;

					*ui64ServerCommonContextID = ((IMG_UINT64)(psFwCCBCmd->uCmdData.sCmdContextResetNotification.ui32ServerCommonContextID1) << 32) |
												  (IMG_UINT64)(psFwCCBCmd->uCmdData.sCmdContextResetNotification.ui32ServerCommonContextID2);

					psServerCommonContext = (RGX_SERVER_COMMON_CONTEXT*)apvServerCommonContextID[0];

					PVR_DPF((PVR_DBG_MESSAGE,"RGXCheckFirmwareCCBs: Context reset 0x%p (%d)",
							psServerCommonContext,
							(IMG_UINT32)(psFwCCBCmd->uCmdData.sCmdContextResetNotification.eResetReason)));

					psServerCommonContext->eLastResetReason = psFwCCBCmd->uCmdData.sCmdContextResetNotification.eResetReason;
					break;
				}

				case RGXFWIF_FWCCB_CMD_DEBUG_DUMP:
				{
					RGXDumpDebugInfo(IMG_NULL,psDevInfo);
					break;
				}

				default:
					PVR_ASSERT(IMG_FALSE);
			}

			/* Update read offset */
			psFWCCBCtl->ui32ReadOffset = (psFWCCBCtl->ui32ReadOffset + 1) & psFWCCBCtl->ui32WrapMask;
		}
	}
}

/*
 * PVRSRVRGXFrameworkCopyCommand
 */
PVRSRV_ERROR PVRSRVRGXFrameworkCopyCommand(DEVMEM_MEMDESC	*psFWFrameworkMemDesc,
										   IMG_PBYTE		pbyGPUFRegisterList,
										   IMG_UINT32		ui32FrameworkRegisterSize)
{
	PVRSRV_ERROR	eError;
	RGXFWIF_RF_REGISTERS	*psRFReg;

	eError = DevmemAcquireCpuVirtAddr(psFWFrameworkMemDesc,
									  (IMG_VOID **)&psRFReg);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXFrameworkCopyCommand: Failed to map firmware render context state (%u)",
				eError));
		return eError;
	}

	OSMemCopy(psRFReg, pbyGPUFRegisterList, ui32FrameworkRegisterSize);

	/* Release the CPU mapping */
	DevmemReleaseCpuVirtAddr(psFWFrameworkMemDesc);

	/*
	 * Dump the FW framework buffer
	 */
	PDUMPCOMMENT("Dump FWFramework buffer");
	DevmemPDumpLoadMem(psFWFrameworkMemDesc, 0, ui32FrameworkRegisterSize, PDUMP_FLAGS_CONTINUOUS);

	return PVRSRV_OK;
}

/*
 * PVRSRVRGXFrameworkCreateKM
 */
PVRSRV_ERROR PVRSRVRGXFrameworkCreateKM(PVRSRV_DEVICE_NODE	*psDeviceNode,
										DEVMEM_MEMDESC		**ppsFWFrameworkMemDesc,
										IMG_UINT32			ui32FrameworkCommandSize)
{
	PVRSRV_ERROR			eError;
	PVRSRV_RGXDEV_INFO 		*psDevInfo = psDeviceNode->pvDevice;

	/*
		Allocate device memory for the firmware GPU framework state.
		Sufficient info to kick one or more DMs should be contained in this buffer
	*/
	PDUMPCOMMENT("Allocate RGX firmware framework state");

	eError = DevmemFwAllocate(psDevInfo,
							  ui32FrameworkCommandSize,
							  RGX_FWCOMCTX_ALLOCFLAGS,
							  "FirmwareGPUFrameworkState",
							  ppsFWFrameworkMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXFrameworkContextKM: Failed to allocate firmware framework state (%u)",
				eError));
		return eError;
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR RGXWaitForFWOp(PVRSRV_RGXDEV_INFO	*psDevInfo,
							RGXFWIF_DM eDM,
							PVRSRV_CLIENT_SYNC_PRIM *psSyncPrim,
							IMG_BOOL bPDumpContinuous)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevInfo->psDeviceNode;
	RGXFWIF_KCCB_CMD	sCmdSyncPrim;

	/* Ensure RGX is powered up before kicking MTS */
	eError = PVRSRVPowerLock();

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXWaitForFWOp: failed to acquire powerlock (%s)",
					PVRSRVGetErrorStringKM(eError)));

		goto _PVRSRVPowerLock_Exit;
	}

	PDUMPPOWCMDSTART();

	eError = PVRSRVSetDevicePowerStateKM(psDeviceNode->sDevId.ui32DeviceIndex,
										 PVRSRV_DEV_POWER_STATE_ON,
										 IMG_FALSE);
	PDUMPPOWCMDEND();

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXWaitForFWOp: failed to transition RGX to ON (%s)",
					PVRSRVGetErrorStringKM(eError)));

		goto _PVRSRVSetDevicePowerStateKM_Exit;
	}




	/* Setup sync primitive */
	SyncPrimSet(psSyncPrim, 0);

	/* prepare a sync command */
	sCmdSyncPrim.eCmdType = RGXFWIF_KCCB_CMD_SYNC;
	sCmdSyncPrim.uCmdData.sSyncData.uiSyncObjDevVAddr = SyncPrimGetFirmwareAddr(psSyncPrim);
	sCmdSyncPrim.uCmdData.sSyncData.uiUpdateVal = 1;

	PDUMPCOMMENT("RGXWaitForFWOp: Submit Kernel SyncPrim [0x%08x] to DM %d ", sCmdSyncPrim.uCmdData.sSyncData.uiSyncObjDevVAddr, eDM);

	/* submit the sync primitive to the kernel CCB */
	eError = RGXSendCommandRaw(psDevInfo,
								eDM,
								&sCmdSyncPrim,
								sizeof(RGXFWIF_KCCB_CMD),
								bPDumpContinuous  ? PDUMP_FLAGS_CONTINUOUS:0);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXScheduleCommandAndWait: Failed to schedule Kernel SyncPrim with error (%u)", eError));
		goto _RGXSendCommandRaw_Exit;
	}

	/* Wait for sync primitive to be updated */
#if defined(PDUMP)
	PDUMPCOMMENT("RGXScheduleCommandAndWait: Poll for Kernel SyncPrim [0x%08x] on DM %d ", sCmdSyncPrim.uCmdData.sSyncData.uiSyncObjDevVAddr, eDM);

	SyncPrimPDumpPol(psSyncPrim,
					1,
					0xffffffff,
					PDUMP_POLL_OPERATOR_EQUAL,
					bPDumpContinuous ? PDUMP_FLAGS_CONTINUOUS:0);
#endif

	{
		RGXFWIF_CCB_CTL  *psKCCBCtl = psDevInfo->apsKernelCCBCtl[eDM];
		IMG_UINT32       ui32CurrentQueueLength = (psKCCBCtl->ui32WrapMask+1 +
												   psKCCBCtl->ui32WriteOffset -
												   psKCCBCtl->ui32ReadOffset) & psKCCBCtl->ui32WrapMask;
		IMG_UINT32       ui32MaxRetries;

		for (ui32MaxRetries = (ui32CurrentQueueLength + 1) * 3;
			 ui32MaxRetries > 0;
			 ui32MaxRetries--)
		{
			/* FIXME: Need to re-think PVRSRVLocking */
			OSSetKeepPVRLock();
			eError = PVRSRVWaitForValueKM(psSyncPrim->pui32LinAddr, 1, 0xffffffff);
			OSSetReleasePVRLock();

			if (eError != PVRSRV_ERROR_TIMEOUT)
			{
				break;
			}
		}

		if (eError == PVRSRV_ERROR_TIMEOUT)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXScheduleCommandAndWait: PVRSRVWaitForValueKM timed out. Dump debug information."));

			PVRSRVDebugRequest(DEBUG_REQUEST_VERBOSITY_MAX);
			PVR_ASSERT(eError != PVRSRV_ERROR_TIMEOUT);
		}
	}

_RGXSendCommandRaw_Exit:
_PVRSRVSetDevicePowerStateKM_Exit:

	PVRSRVPowerUnlock();

_PVRSRVPowerLock_Exit:
	return eError;
}

static
PVRSRV_ERROR RGXScheduleCleanupCommand(PVRSRV_RGXDEV_INFO 	*psDevInfo,
									   RGXFWIF_DM			eDM,
									   RGXFWIF_KCCB_CMD		*psKCCBCmd,
									   IMG_UINT32			ui32CmdSize,
									   RGXFWIF_CLEANUP_TYPE	eCleanupType,
									   PVRSRV_CLIENT_SYNC_PRIM *psSyncPrim,
									   IMG_BOOL				bPDumpContinuous)
{
	PVRSRV_ERROR eError;

	psKCCBCmd->eCmdType = RGXFWIF_KCCB_CMD_CLEANUP;

	psKCCBCmd->uCmdData.sCleanupData.eCleanupType = eCleanupType;
	psKCCBCmd->uCmdData.sCleanupData.uiSyncObjDevVAddr = SyncPrimGetFirmwareAddr(psSyncPrim);

	SyncPrimSet(psSyncPrim, 0);

	/*
		Send the cleanup request to the firmware. If the resource is still busy
		the firmware will tell us and we'll drop out with a retry.
	*/
	eError = RGXScheduleCommand(psDevInfo,
								eDM,
								psKCCBCmd,
								ui32CmdSize,
								bPDumpContinuous);
	if (eError != PVRSRV_OK)
	{
		goto fail_command;
	}

	/* Wait for sync primitive to be updated */
#if defined(PDUMP)
	PDUMPCOMMENT("Wait for the firmware to reply to the cleanup command");
	SyncPrimPDumpPol(psSyncPrim,
					RGXFWIF_CLEANUP_RUN,
					RGXFWIF_CLEANUP_RUN,
					PDUMP_POLL_OPERATOR_EQUAL,
					bPDumpContinuous ? PDUMP_FLAGS_CONTINUOUS:0);

	/*
	 * The cleanup request to the firmware will tell us if a given resource is busy or not.
	 * If the RGXFWIF_CLEANUP_BUSY flag is set, this means that the resource is still in use.
	 * In this case we return a PVRSRV_ERROR_RETRY error to the client drivers and they will
	 * re-issue the cleanup request until it succeed.
	 *
	 * Since this retry mechanism doesn't work for pdumps, client drivers should ensure
	 * that cleanup requests are only submitted if the resource is unused.
	 * If this is not the case, the following poll will block infinitely, making sure
	 * the issue doesn't go unnoticed.
	 */
	PDUMPCOMMENT("Cleanup: If this poll fails, the following resource is still in use (DM=%u, type=%u, address=0x%08x)",
					eDM,
					psKCCBCmd->uCmdData.sCleanupData.eCleanupType,
					psKCCBCmd->uCmdData.sCleanupData.uCleanupData.psContext.ui32Addr);
	SyncPrimPDumpPol(psSyncPrim,
					0,
					RGXFWIF_CLEANUP_BUSY,
					PDUMP_POLL_OPERATOR_EQUAL,
					bPDumpContinuous ? PDUMP_FLAGS_CONTINUOUS:0);
#endif

	{
		RGXFWIF_CCB_CTL  *psKCCBCtl = psDevInfo->apsKernelCCBCtl[eDM];
		IMG_UINT32       ui32CurrentQueueLength = (psKCCBCtl->ui32WrapMask+1 +
												   psKCCBCtl->ui32WriteOffset -
												   psKCCBCtl->ui32ReadOffset) & psKCCBCtl->ui32WrapMask;
		IMG_UINT32       ui32MaxRetries;

		for (ui32MaxRetries = ui32CurrentQueueLength + 1;
			 ui32MaxRetries > 0;
			 ui32MaxRetries--)
		{
			/* FIXME: Need to re-think PVRSRVLocking */
			OSSetKeepPVRLock();
			eError = PVRSRVWaitForValueKM(psSyncPrim->pui32LinAddr, RGXFWIF_CLEANUP_RUN, RGXFWIF_CLEANUP_RUN);
			OSSetReleasePVRLock();

			if (eError != PVRSRV_ERROR_TIMEOUT)
			{
				break;
			}
		}

		/*
			If the firmware hasn't got back to us in a timely manner
			then bail and let the caller retry the command.
		*/
		if (eError == PVRSRV_ERROR_TIMEOUT)
		{
			PVR_DPF((PVR_DBG_WARNING,"RGXScheduleCleanupCommand: PVRSRVWaitForValueKM timed out. Dump debug information."));

			eError = PVRSRV_ERROR_RETRY;
#if defined(DEBUG)
			PVRSRVDebugRequest(DEBUG_REQUEST_VERBOSITY_MAX);
#endif
			goto fail_poll;
		}
		else if (eError != PVRSRV_OK)
		{
			goto fail_poll;
		}
	}

	/*
		If the command has was run but a resource was busy, then the request
		will need to be retried.
	*/
	if (*psSyncPrim->pui32LinAddr & RGXFWIF_CLEANUP_BUSY)
	{
		eError = PVRSRV_ERROR_RETRY;
		goto fail_requestbusy;
	}

	return PVRSRV_OK;

fail_requestbusy:
fail_poll:
fail_command:
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}

/*
	RGXRequestCommonContextCleanUp
*/
PVRSRV_ERROR RGXFWRequestCommonContextCleanUp(PVRSRV_DEVICE_NODE *psDeviceNode,
											  PRGXFWIF_FWCOMMONCONTEXT psFWCommonContextFWAddr,
											  PVRSRV_CLIENT_SYNC_PRIM *psSyncPrim,
											  RGXFWIF_DM eDM)
{
	RGXFWIF_KCCB_CMD			sRCCleanUpCmd = {0};
	PVRSRV_ERROR				eError;

	PDUMPCOMMENT("Common ctx cleanup Request DM%d [context = 0x%08x]", eDM, psFWCommonContextFWAddr.ui32Addr);

	/* Setup our command data, the cleanup call will fill in the rest */
	sRCCleanUpCmd.uCmdData.sCleanupData.uCleanupData.psContext = psFWCommonContextFWAddr;

	/* Request cleanup of the firmware resource */
	eError = RGXScheduleCleanupCommand(psDeviceNode->pvDevice,
									   eDM,
									   &sRCCleanUpCmd,
									   sizeof(RGXFWIF_KCCB_CMD),
									   RGXFWIF_CLEANUP_FWCOMMONCONTEXT,
									   psSyncPrim,
									   IMG_FALSE);

	if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXRequestCommonContextCleanUp: Failed to schedule a memory context cleanup with error (%u)", eError));
	}

	return eError;
}

/*
 * RGXRequestHWRTDataCleanUp
 */

PVRSRV_ERROR RGXFWRequestHWRTDataCleanUp(PVRSRV_DEVICE_NODE *psDeviceNode,
										 PRGXFWIF_HWRTDATA psHWRTData,
										 PVRSRV_CLIENT_SYNC_PRIM *psSync,
										 RGXFWIF_DM eDM)
{
	RGXFWIF_KCCB_CMD			sHWRTDataCleanUpCmd = {0};
	PVRSRV_ERROR				eError;

	PDUMPCOMMENT("HW RTData cleanup Request DM%d [HWRTData = 0x%08x]", eDM, psHWRTData.ui32Addr);

	sHWRTDataCleanUpCmd.uCmdData.sCleanupData.uCleanupData.psHWRTData = psHWRTData;

	eError = RGXScheduleCleanupCommand(psDeviceNode->pvDevice,
									   eDM,
									   &sHWRTDataCleanUpCmd,
									   sizeof(sHWRTDataCleanUpCmd),
									   RGXFWIF_CLEANUP_HWRTDATA,
									   psSync,
									   IMG_FALSE);

	if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXRequestHWRTDataCleanUp: Failed to schedule a HWRTData cleanup with error (%u)", eError));
	}

	return eError;
}

/*
	RGXFWRequestFreeListCleanUp
*/
PVRSRV_ERROR RGXFWRequestFreeListCleanUp(PVRSRV_RGXDEV_INFO *psDevInfo,
										 PRGXFWIF_FREELIST psFWFreeList,
										 PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	RGXFWIF_KCCB_CMD			sFLCleanUpCmd = {0};
	PVRSRV_ERROR 				eError;

	PDUMPCOMMENT("Free list cleanup Request [FreeList = 0x%08x]", psFWFreeList.ui32Addr);

	/* Setup our command data, the cleanup call will fill in the rest */
	sFLCleanUpCmd.uCmdData.sCleanupData.uCleanupData.psFreelist = psFWFreeList;

	/* Request cleanup of the firmware resource */
	eError = RGXScheduleCleanupCommand(psDevInfo,
									   RGXFWIF_DM_GP,
									   &sFLCleanUpCmd,
									   sizeof(RGXFWIF_KCCB_CMD),
									   RGXFWIF_CLEANUP_FREELIST,
									   psSync,
									   IMG_FALSE);

	if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXFWRequestFreeListCleanUp: Failed to schedule a memory context cleanup with error (%u)", eError));
	}

	return eError;
}

/*
	RGXFWRequestZSBufferCleanUp
*/
PVRSRV_ERROR RGXFWRequestZSBufferCleanUp(PVRSRV_RGXDEV_INFO *psDevInfo,
										 PRGXFWIF_ZSBUFFER psFWZSBuffer,
										 PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	RGXFWIF_KCCB_CMD			sZSBufferCleanUpCmd = {0};
	PVRSRV_ERROR 				eError;

	PDUMPCOMMENT("ZS Buffer cleanup Request [ZS Buffer = 0x%08x]", psFWZSBuffer.ui32Addr);

	/* Setup our command data, the cleanup call will fill in the rest */
	sZSBufferCleanUpCmd.uCmdData.sCleanupData.uCleanupData.psZSBuffer = psFWZSBuffer;

	/* Request cleanup of the firmware resource */
	eError = RGXScheduleCleanupCommand(psDevInfo,
									   RGXFWIF_DM_3D,
									   &sZSBufferCleanUpCmd,
									   sizeof(RGXFWIF_KCCB_CMD),
									   RGXFWIF_CLEANUP_ZSBUFFER,
									   psSync,
									   IMG_FALSE);

	if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXFWRequestZSBufferCleanUp: Failed to schedule a memory context cleanup with error (%u)", eError));
	}

	return eError;
}


PVRSRV_ERROR ContextSetPriority(RGX_SERVER_COMMON_CONTEXT *psContext,
								CONNECTION_DATA *psConnection,
								PVRSRV_RGXDEV_INFO *psDevInfo,
								IMG_UINT32 ui32Priority,
								RGXFWIF_DM eDM)
{
	IMG_UINT32				ui32CmdSize;
	IMG_UINT8				*pui8CmdPtr;
	RGXFWIF_KCCB_CMD		sPriorityCmd;
	RGXFWIF_CCB_CMD_HEADER	*psCmdHeader;
	RGXFWIF_CMD_PRIORITY	*psCmd;
	IMG_UINT32				ui32BeforeWOff = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psContext));
	IMG_BOOL				bKickCMD = IMG_TRUE;
	PVRSRV_ERROR			eError;

	/*
		Get space for command
	*/
	ui32CmdSize = RGX_CCB_FWALLOC_ALIGN(sizeof(RGXFWIF_CCB_CMD_HEADER) + sizeof(RGXFWIF_CMD_PRIORITY));

	eError = RGXAcquireCCB(FWCommonContextGetClientCCB(psContext),
						   ui32CmdSize,
						   (IMG_PVOID *) &pui8CmdPtr,
						   IMG_TRUE);
	if (eError != PVRSRV_OK)
	{
		if (ui32BeforeWOff != RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psContext)))
		{
			bKickCMD = IMG_FALSE;
		}
		else
		{
			goto fail_ccbacquire;
		}
	}

	if (bKickCMD)
	{
		/*
			Write the command header and command
		*/
		psCmdHeader = (RGXFWIF_CCB_CMD_HEADER *) pui8CmdPtr;
		psCmdHeader->eCmdType = RGXFWIF_CCB_CMD_TYPE_PRIORITY;
		psCmdHeader->ui32CmdSize = RGX_CCB_FWALLOC_ALIGN(sizeof(RGXFWIF_CMD_PRIORITY));
		pui8CmdPtr += sizeof(*psCmdHeader);

		psCmd = (RGXFWIF_CMD_PRIORITY *) pui8CmdPtr;
		psCmd->ui32Priority = ui32Priority;
		pui8CmdPtr += sizeof(*psCmd);
	}

	/*
		We should reserved space in the kernel CCB here and fill in the command
		directly.
		This is so if there isn't space in the kernel CCB we can return with
		retry back to services client before we take any operations
	*/

	if (bKickCMD)
	{
		/*
			Submit the command
		*/
		RGXReleaseCCB(FWCommonContextGetClientCCB(psContext),
					  ui32CmdSize,
					  IMG_TRUE);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to release space in client CCB", __FUNCTION__));
			return eError;
		}
	}

	/* Construct the priority command. */
	sPriorityCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
	sPriorityCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psContext);
	sPriorityCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psContext));
	sPriorityCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;

	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		eError = RGXScheduleCommand(psDevInfo,
									eDM,
									&sPriorityCmd,
									sizeof(sPriorityCmd),
									IMG_TRUE);
		if (eError != PVRSRV_ERROR_RETRY)
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXFlushComputeDataKM: Failed to schedule SLC flush command with error (%u)", eError));
	}

	return PVRSRV_OK;

fail_ccbacquire:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*
	RGXReadMETAReg
*/
PVRSRV_ERROR RGXReadMETAReg(PVRSRV_RGXDEV_INFO	*psDevInfo, IMG_UINT32 ui32RegAddr, IMG_UINT32 *pui32RegValue)
{

	IMG_UINT8 *pui8RegBase = (IMG_UINT8*)psDevInfo->pvRegsBaseKM;
	IMG_UINT32 ui32RegValue;

	/* Wait for Slave Port to be Ready */
	if (PVRSRVPollForValueKM(
			(IMG_UINT32*) (pui8RegBase + RGX_CR_META_SP_MSLVCTRL1),
			RGX_CR_META_SP_MSLVCTRL1_READY_EN|RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
			RGX_CR_META_SP_MSLVCTRL1_READY_EN|RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN) != PVRSRV_OK)
	{
		return PVRSRV_ERROR_TIMEOUT;
	}

	/* Issue the Read */
	OSWriteHWReg32(
		psDevInfo->pvRegsBaseKM,
		RGX_CR_META_SP_MSLVCTRL0,
		ui32RegAddr | RGX_CR_META_SP_MSLVCTRL0_RD_EN);

	/* Wait for Slave Port to be Ready: read complete */
	if (PVRSRVPollForValueKM(
			(IMG_UINT32*) (pui8RegBase + RGX_CR_META_SP_MSLVCTRL1),
			RGX_CR_META_SP_MSLVCTRL1_READY_EN|RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
			RGX_CR_META_SP_MSLVCTRL1_READY_EN|RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN) != PVRSRV_OK)
	{
		return PVRSRV_ERROR_TIMEOUT;
	}

	/* Read the value */
	ui32RegValue = OSReadHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_META_SP_MSLVDATAX);

	*pui32RegValue = ui32RegValue;

	return PVRSRV_OK;
}


/*
	RGXUpdateHealthStatus
*/
PVRSRV_ERROR RGXUpdateHealthStatus(PVRSRV_DEVICE_NODE* psDevNode,
								   IMG_BOOL bCheckAfterTimePassed)
{
	PVRSRV_DATA*                 psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_DEVICE_HEALTH_STATUS  eNewStatus   = PVRSRV_DEVICE_HEALTH_STATUS_OK;
	PVRSRV_RGXDEV_INFO*  psDevInfo;
	RGXFWIF_TRACEBUF*  psRGXFWIfTraceBufCtl;
	IMG_UINT32  ui32DMCount, ui32ThreadCount;
	IMG_BOOL  bAnyDMProgress, bAllDMsIdle;

	PVR_ASSERT(psDevNode != NULL);
	psDevInfo = psDevNode->pvDevice;
	psRGXFWIfTraceBufCtl = psDevInfo->psRGXFWIfTraceBuf;

	/* If the firmware is not initialised, there is not much point continuing! */
	if (!psDevInfo->bFirmwareInitialised  ||  psDevInfo->pvRegsBaseKM == IMG_NULL  ||
		psDevInfo->psDeviceNode == IMG_NULL  ||
		psDevInfo->psDeviceNode->psSyncPrimPreKick == IMG_NULL)
	{
		return PVRSRV_OK;
	}

	/* If RGX is not powered on, don't continue
	   (there is a race condition where PVRSRVIsDevicePowered returns TRUE when the GPU is actually powering down.
	   That's not a problem as this function does not touch the HW except for the RGXScheduleCommand function,
	   which is already powerlock safe. The worst thing that could happen is that RGX might power back up
	   but the chances of that are very low */
	if (!PVRSRVIsDevicePowered(psDevNode->sDevId.ui32DeviceIndex))
	{
		return PVRSRV_OK;
	}

	/* If this is a quick update, then include the last current value... */
	if (!bCheckAfterTimePassed)
	{
		eNewStatus = psDevNode->eHealthStatus;
	}

	/*
	   Firmware thread checks...
	*/
	for (ui32ThreadCount = 0;  ui32ThreadCount < RGXFW_THREAD_NUM;  ui32ThreadCount++)
	{
		if (psRGXFWIfTraceBufCtl != IMG_NULL)
		{
			IMG_CHAR*  pszTraceAssertInfo = psRGXFWIfTraceBufCtl->sTraceBuf[ui32ThreadCount].sAssertBuf.szInfo;

			/*
			Check if the FW has hit an assert...
			*/
			if (*pszTraceAssertInfo != '\0')
			{
				PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: Firmware thread %d has asserted: %s (%s:%d)",
						ui32ThreadCount, pszTraceAssertInfo,
						psRGXFWIfTraceBufCtl->sTraceBuf[ui32ThreadCount].sAssertBuf.szPath,
						psRGXFWIfTraceBufCtl->sTraceBuf[ui32ThreadCount].sAssertBuf.ui32LineNum));
				eNewStatus = PVRSRV_DEVICE_HEALTH_STATUS_DEAD;
			}

			/*
			   Check the threads to see if they are in the same poll locations as last time...
			*/
			if (bCheckAfterTimePassed)
			{
				if (psRGXFWIfTraceBufCtl->aui32CrPollAddr[ui32ThreadCount] != 0  &&
					psRGXFWIfTraceBufCtl->aui32CrPollAddr[ui32ThreadCount] == psDevInfo->aui32CrLastPollAddr[ui32ThreadCount])
				{
					PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: Firmware stuck on CR poll: T%u polling %s (reg:0x%08X val:0x%08X)",
							ui32ThreadCount,
							((psRGXFWIfTraceBufCtl->aui32CrPollAddr[ui32ThreadCount] & RGXFW_POLL_TYPE_SET)?("set"):("unset")),
							psRGXFWIfTraceBufCtl->aui32CrPollAddr[ui32ThreadCount] & ~RGXFW_POLL_TYPE_SET,
							psRGXFWIfTraceBufCtl->aui32CrPollValue[ui32ThreadCount]));
					eNewStatus = PVRSRV_DEVICE_HEALTH_STATUS_DEAD;
				}
				psDevInfo->aui32CrLastPollAddr[ui32ThreadCount]    = psRGXFWIfTraceBufCtl->aui32CrPollAddr[ui32ThreadCount];
			}
		}
	}

	/*
	   Event Object Timeouts check...
	*/
	if (psDevInfo->ui32LastGEOTimeouts > 1  &&  psPVRSRVData->ui32GEOConsecutiveTimeouts > psDevInfo->ui32LastGEOTimeouts)
	{
		PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: Global Event Object Timeouts have risen (from %d to %d)",
				psDevInfo->ui32LastGEOTimeouts, psPVRSRVData->ui32GEOConsecutiveTimeouts));
		eNewStatus = PVRSRV_DEVICE_HEALTH_STATUS_DEAD;
	}
	psDevInfo->ui32LastGEOTimeouts = psPVRSRVData->ui32GEOConsecutiveTimeouts;

	/*
	   Check Kernel CCB for each DM. We need to see progress on at least one DM...
	*/
	bAnyDMProgress = IMG_FALSE;
	bAllDMsIdle    = IMG_TRUE;

	for (ui32DMCount = 0; ui32DMCount < RGXFWIF_DM_MAX; ui32DMCount++)
	{
		RGXFWIF_CCB_CTL *psKCCBCtl = ((PVRSRV_RGXDEV_INFO*)psDevNode->pvDevice)->apsKernelCCBCtl[ui32DMCount];

		/* Check the values of the CCB pointers are valid... */
		if (psKCCBCtl != IMG_NULL)
		{
			if (psKCCBCtl->ui32ReadOffset > psKCCBCtl->ui32WrapMask  ||
				psKCCBCtl->ui32WriteOffset > psKCCBCtl->ui32WrapMask)
			{
				PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: KCCB for DM%d has invalid offset (ROFF=%d WOFF=%d)",
						ui32DMCount, psKCCBCtl->ui32ReadOffset, psKCCBCtl->ui32WriteOffset));
				eNewStatus = PVRSRV_DEVICE_HEALTH_STATUS_DEAD;
			}

			/* Check that the Read Offset has updated since last time (or is empty)... */
			if (bCheckAfterTimePassed)
			{
				if (psKCCBCtl->ui32ReadOffset != psKCCBCtl->ui32WriteOffset)
				{
					bAllDMsIdle = IMG_FALSE;
				}

				if (psKCCBCtl->ui32ReadOffset != psDevInfo->ui32KCCBLastROff[ui32DMCount])
				{
					bAnyDMProgress = IMG_TRUE;
				}
				else if (psKCCBCtl->ui32ReadOffset != psKCCBCtl->ui32WriteOffset)
				{
					PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: KCCB for DM%d has not progressed (ROFF=%d WOFF=%d)",
							ui32DMCount, psKCCBCtl->ui32ReadOffset, psKCCBCtl->ui32WriteOffset));
				}
				psDevInfo->ui32KCCBLastROff[ui32DMCount] = psKCCBCtl->ui32ReadOffset;
			}
		}
	}

	if (bCheckAfterTimePassed  &&  !bAllDMsIdle  &&  !bAnyDMProgress)
	{
		PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: No progress on KCCBs for any DM"));
		eNewStatus = PVRSRV_DEVICE_HEALTH_STATUS_DEAD;
	}

	/*
	   If no commands are currently pending and nothing happened since the last poll, then
	   schedule a dummy command to ping the firmware so we know it is alive and processing.
	*/
	if (bCheckAfterTimePassed  &&  bAllDMsIdle  &&  !bAnyDMProgress)
	{
		PVRSRV_ERROR      eError;
		RGXFWIF_KCCB_CMD  sCmpKCCBCmd;

		sCmpKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_HEALTH_CHECK;

		eError = RGXScheduleCommand(psDevNode->pvDevice,
									RGXFWIF_DM_GP,
									&sCmpKCCBCmd,
									sizeof(sCmpKCCBCmd),
									IMG_TRUE);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: Cannot schedule Health Check command! (0x%x)", eError));
		}
	}

	/*
	   Finished, save the new status...
	*/
	psDevNode->eHealthStatus = eNewStatus;

	return PVRSRV_OK;
} /* RGXUpdateHealthStatus */

IMG_VOID DumpStalledFWCommonContext(RGX_SERVER_COMMON_CONTEXT *psCurrentServerCommonContext)
{
	RGX_CLIENT_CCB 	*psCurrentClientCCB = psCurrentServerCommonContext->psClientCCB;
	PRGXFWIF_FWCOMMONCONTEXT sFWCommonContext = psCurrentServerCommonContext->sFWCommonContextFWAddr;

	DumpStalledCCBCommand(sFWCommonContext, psCurrentClientCCB);
}

IMG_VOID AttachKickResourcesCleanupCtls(PRGXFWIF_CLEANUP_CTL *apsCleanupCtl,
									IMG_UINT32 *pui32NumCleanupCtl,
									RGXFWIF_DM eDM,
									IMG_BOOL bKick,
									RGX_RTDATA_CLEANUP_DATA        *psRTDataCleanup,
									RGX_ZSBUFFER_DATA              *psZBuffer,
									RGX_ZSBUFFER_DATA              *psSBuffer)
{
	PRGXFWIF_CLEANUP_CTL *psCleanupCtlWrite = apsCleanupCtl;

	PVR_ASSERT((eDM == RGXFWIF_DM_TA) || (eDM == RGXFWIF_DM_3D));

	if(bKick)
	{
		if(eDM == RGXFWIF_DM_TA)
		{
			if(psRTDataCleanup)
			{
				PRGXFWIF_CLEANUP_CTL psCleanupCtl;

				RGXSetFirmwareAddress(&psCleanupCtl, psRTDataCleanup->psFWHWRTDataMemDesc,
									offsetof(RGXFWIF_HWRTDATA, sTACleanupState),
								RFW_FWADDR_NOREF_FLAG | RFW_FWADDR_METACACHED_FLAG);

				*(psCleanupCtlWrite++) = psCleanupCtl;
			}
		}
		else
		{
			if(psRTDataCleanup)
			{
				PRGXFWIF_CLEANUP_CTL psCleanupCtl;

				RGXSetFirmwareAddress(&psCleanupCtl, psRTDataCleanup->psFWHWRTDataMemDesc,
									offsetof(RGXFWIF_HWRTDATA, s3DCleanupState),
								RFW_FWADDR_NOREF_FLAG | RFW_FWADDR_METACACHED_FLAG);

				*(psCleanupCtlWrite++) = psCleanupCtl;
			}

			if(psZBuffer)
			{
				(psCleanupCtlWrite++)->ui32Addr = psZBuffer->sZSBufferFWDevVAddr.ui32Addr +
								offsetof(RGXFWIF_FWZSBUFFER, sCleanupState);
			}

			if(psSBuffer)
			{
				(psCleanupCtlWrite++)->ui32Addr = psSBuffer->sZSBufferFWDevVAddr.ui32Addr +
								offsetof(RGXFWIF_FWZSBUFFER, sCleanupState);
			}
		}
	}

	*pui32NumCleanupCtl = psCleanupCtlWrite - apsCleanupCtl;

	PVR_ASSERT(*pui32NumCleanupCtl <= RGXFWIF_KCCB_CMD_KICK_DATA_MAX_NUM_CLEANUP_CTLS);
}

PVRSRV_ERROR RGXResetHWRLogs(PVRSRV_DEVICE_NODE *psDevNode)
{
	PVRSRV_RGXDEV_INFO	*psDevInfo;
	RGXFWIF_HWRINFOBUF	*psHWRInfoBuf;
	RGXFWIF_TRACEBUF 	*psRGXFWIfTraceBufCtl;
	IMG_UINT32 			i;

	if(psDevNode->pvDevice == IMG_NULL)
	{
		return PVRSRV_ERROR_INVALID_DEVINFO;
	}
	psDevInfo = psDevNode->pvDevice;

	psHWRInfoBuf = psDevInfo->psRGXFWIfHWRInfoBuf;
	psRGXFWIfTraceBufCtl = psDevInfo->psRGXFWIfTraceBuf;

	for(i = 0 ; i < RGXFWIF_DM_MAX ; i++)
	{
		/* Reset the HWR numbers */
		psRGXFWIfTraceBufCtl->aui16HwrDmLockedUpCount[i] = 0;
		psRGXFWIfTraceBufCtl->aui16HwrDmFalseDetectCount[i] = 0;
		psRGXFWIfTraceBufCtl->aui16HwrDmRecoveredCount[i] = 0;
		psRGXFWIfTraceBufCtl->aui16HwrDmOverranCount[i] = 0;
	}

	for(i = 0 ; i < RGXFWIF_HWINFO_MAX ; i++)
	{
		psHWRInfoBuf->sHWRInfo[i].ui32HWRNumber = 0;
	}

	for(i = 0 ; i < RGXFW_THREAD_NUM ; i++)
	{
		psHWRInfoBuf->ui32FirstCrPollAddr[i] = 0;
		psHWRInfoBuf->ui32FirstCrPollValue[i] = 0;
	}

	psHWRInfoBuf->ui32WriteIndex = 0;
	psHWRInfoBuf->bDDReqIssued = IMG_FALSE;

	return PVRSRV_OK;
}


/******************************************************************************
 End of file (rgxfwutils.c)
******************************************************************************/
