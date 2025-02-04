/*************************************************************************/ /*!
@File
@Title          RGX device node header file
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the RGX device node
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

#if !defined(__RGXDEVICE_H__)
#define __RGXDEVICE_H__

#include "img_types.h"
#include "pvrsrv_device_types.h"
#include "mmu_common.h"
#include "rgx_fwif_km.h"
#include "rgx_fwif.h"
#include "rgxscript.h"
#include "cache_external.h"
#include "device.h"


typedef struct _RGX_SERVER_COMMON_CONTEXT_ RGX_SERVER_COMMON_CONTEXT;

typedef struct {
	DEVMEM_MEMDESC		*psFWFrameworkMemDesc;
	IMG_DEV_VIRTADDR	*psMCUFenceAddr;
} RGX_COMMON_CONTEXT_INFO;


/*!
 ******************************************************************************
 * Device state flags
 *****************************************************************************/
#define RGXKM_DEVICE_STATE_ZERO_FREELIST		(0x1 << 0)		/*!< Zeroing the physical pages of reconstructed free lists */
#define RGXKM_DEVICE_STATE_FTRACE_EN			(0x1 << 1)		/*!< Used to enable device FTrace thread to consume HWPerf data */
#define RGXKM_DEVICE_STATE_DISABLE_FED_LOGGING_EN (0x1 << 2)	/*!< Used to disable the Fatal Error Detection logging */

#define RGXFWIF_GPU_STATS_WINDOW_SIZE_US					1000000
#define RGXFWIF_GPU_STATS_MAX_VALUE_OF_STATE				10000

/*!
 ******************************************************************************
 * GPU DVFS History CB
 *****************************************************************************/

#define RGX_GPU_DVFS_HIST_SIZE 100  /* History size must NOT be greater than 16384 (2^14) */

typedef struct _RGX_GPU_DVFS_HIST_
{
	IMG_UINT32               ui32CurrentDVFSId;              /*!< Current history entry index */
	IMG_UINT32				 aui32DVFSClockCB[RGX_GPU_DVFS_HIST_SIZE];   /*!< Circular buffer of DVFS clock history in Hz */
} RGX_GPU_DVFS_HIST;

typedef struct _RGXFWIF_GPU_UTIL_STATS_
{
	IMG_BOOL				bPoweredOn;			/* if TRUE, device is powered on and statistic are valid.
													It might be FALSE if DVFS frequency is not provided by system layer (see RGX_TIMING_INFORMATION::ui32CoreClockSpeed) */
	IMG_UINT32				ui32GpuStatActive;	/* GPU active  ratio expressed in 0,01% units */
	IMG_UINT32				ui32GpuStatBlocked; /* GPU blocked ratio expressed in 0,01% units */
	IMG_UINT32				ui32GpuStatIdle;    /* GPU idle    ratio expressed in 0,01% units */
} RGXFWIF_GPU_UTIL_STATS;

typedef struct _RGX_REG_CONFIG_
{
	IMG_BOOL		bEnabled;
	RGXFWIF_PWR_EVT		ePowerIslandToPush;
	IMG_UINT32      	ui32NumRegRecords;
} RGX_REG_CONFIG;

typedef struct _PVRSRV_STUB_PBDESC_ PVRSRV_STUB_PBDESC;

/*!
 ******************************************************************************
 * RGX Device info
 *****************************************************************************/

typedef struct _PVRSRV_RGXDEV_INFO_
{
	PVRSRV_DEVICE_TYPE		eDeviceType;
	PVRSRV_DEVICE_CLASS		eDeviceClass;
	PVRSRV_DEVICE_NODE		*psDeviceNode;

	IMG_UINT8				ui8VersionMajor;
	IMG_UINT8				ui8VersionMinor;
	IMG_UINT32				ui32CoreConfig;
	IMG_UINT32				ui32CoreFlags;

	IMG_BOOL                bFirmwareInitialised;
	IMG_BOOL				bPDPEnabled;

	/* Kernel mode linear address of device registers */
	IMG_PVOID				pvRegsBaseKM;

	/* FIXME: The alloc for this should go through OSAllocMem in future */
	IMG_HANDLE				hRegMapping;

	/* System physical address of device registers*/
	IMG_CPU_PHYADDR			sRegsPhysBase;
	/*  Register region size in bytes */
	IMG_UINT32				ui32RegSize;

	PVRSRV_STUB_PBDESC		*psStubPBDescListKM;


	/* Firmware memory context info */
	DEVMEM_CONTEXT			*psKernelDevmemCtx;
	DEVMEM_HEAP				*psFirmwareHeap;
	MMU_CONTEXT				*psKernelMMUCtx;
	IMG_UINT32				ui32KernelCatBaseIdReg;
	IMG_UINT32				ui32KernelCatBaseId;
	IMG_UINT32				ui32KernelCatBaseReg;
	IMG_UINT32				ui32KernelCatBaseWordSize;
	IMG_UINT32				ui32KernelCatBaseAlignShift;
	IMG_UINT32				ui32KernelCatBaseShift;
	IMG_UINT64				ui64KernelCatBaseMask;

	IMG_VOID				*pvDeviceMemoryHeap;

	/* Kernel CCBs */
	DEVMEM_MEMDESC			*apsKernelCCBCtlMemDesc[RGXFWIF_DM_MAX];	/*!< memdesc for kernel CCB control */
	RGXFWIF_CCB_CTL			*apsKernelCCBCtl[RGXFWIF_DM_MAX];			/*!< kernel CCB control kernel mapping */
	DEVMEM_MEMDESC			*apsKernelCCBMemDesc[RGXFWIF_DM_MAX];		/*!< memdesc for kernel CCB */
	IMG_UINT8				*apsKernelCCB[RGXFWIF_DM_MAX];				/*!< kernel CCB kernel mapping */

	/* Firmware CCBs */
	DEVMEM_MEMDESC			*apsFirmwareCCBCtlMemDesc[RGXFWIF_DM_MAX];	/*!< memdesc for Firmware CCB control */
	RGXFWIF_CCB_CTL			*apsFirmwareCCBCtl[RGXFWIF_DM_MAX];			/*!< kernel CCB control Firmware mapping */
	DEVMEM_MEMDESC			*apsFirmwareCCBMemDesc[RGXFWIF_DM_MAX];		/*!< memdesc for Firmware CCB */
	IMG_UINT8				*apsFirmwareCCB[RGXFWIF_DM_MAX];				/*!< kernel CCB Firmware mapping */

	/*
		if we don't preallocate the pagetables we must
		insert newly allocated page tables dynamically
	*/
	IMG_VOID				*pvMMUContextList;

	IMG_UINT32				ui32ClkGateStatusReg;
	IMG_UINT32				ui32ClkGateStatusMask;
	RGX_SCRIPTS				*psScripts;

	DEVMEM_MEMDESC			*psRGXFWCodeMemDesc;
	DEVMEM_EXPORTCOOKIE		sRGXFWCodeExportCookie;

	DEVMEM_MEMDESC			*psRGXFWDataMemDesc;
	DEVMEM_EXPORTCOOKIE		sRGXFWDataExportCookie;

	DEVMEM_MEMDESC			*psRGXFWCorememMemDesc;
	DEVMEM_EXPORTCOOKIE		sRGXFWCorememExportCookie;

	DEVMEM_MEMDESC			*psRGXFWIfTraceBufCtlMemDesc;
	RGXFWIF_TRACEBUF		*psRGXFWIfTraceBuf;

	DEVMEM_MEMDESC			*psRGXFWIfHWRInfoBufCtlMemDesc;
	RGXFWIF_HWRINFOBUF		*psRGXFWIfHWRInfoBuf;

	DEVMEM_MEMDESC			*psRGXFWIfGpuUtilFWCbCtlMemDesc;
	RGXFWIF_GPU_UTIL_FWCB	*psRGXFWIfGpuUtilFWCb;

	DEVMEM_MEMDESC			*psRGXFWIfHWPerfBufCtlMemDesc;
	IMG_BYTE				*psRGXFWIfHWPerfBuf;
	IMG_UINT32				ui32RGXFWIfHWPerfBufSize; /* in bytes */

	DEVMEM_MEMDESC			*psRGXFWIfCorememDataStoreMemDesc;

	DEVMEM_MEMDESC			*psRGXFWIfRegCfgMemDesc;

	DEVMEM_MEMDESC			*psRGXFWIfInitMemDesc;

#if defined(RGXFW_ALIGNCHECKS)
	DEVMEM_MEMDESC			*psRGXFWAlignChecksMemDesc;
#endif

	DEVMEM_MEMDESC			*psRGXFWSigTAChecksMemDesc;
	IMG_UINT32				ui32SigTAChecksSize;

	DEVMEM_MEMDESC			*psRGXFWSig3DChecksMemDesc;
	IMG_UINT32				ui32Sig3DChecksSize;

	IMG_VOID				*pvLISRData;
	IMG_VOID				*pvMISRData;

	DEVMEM_MEMDESC			*psRGXBIFFaultAddressMemDesc;

#if defined(FIX_HW_BRN_37200)
	DEVMEM_MEMDESC			*psRGXFWHWBRN37200MemDesc;
#endif

#if defined (PDUMP)
	IMG_BOOL				abDumpedKCCBCtlAlready[RGXFWIF_DM_MAX];

#endif

	/*! Handles to the lock and stream objects used to transport
	 * HWPerf data to user side clients. See RGXHWPerfInit() RGXHWPerfDeinit().
	 * Set during initialisation if the application hint turns bit 7
	 * 'Enable HWPerf' on in the ConfigFlags sent to the FW. FW stores this
	 * bit in the RGXFW_CTL.ui32StateFlags member. They may also get
	 * set by the API RGXCtrlHWPerf(). Thus these members may be 0 if HWPerf is
	 * not enabled as these members are created on demand and destroyed at
	 * driver unload.
	 */
	POS_LOCK 				hLockHWPerfStream;
	IMG_HANDLE				hHWPerfStream;
#if defined(SUPPORT_GPUTRACE_EVENTS)
	IMG_HANDLE				hGPUTraceCmdCompleteHandle;
	IMG_BOOL				bFTraceGPUEventsEnabled;
	IMG_HANDLE				hGPUTraceTLConnection;
	IMG_HANDLE				hGPUTraceTLStream;
#endif

	/* If we do 10 deferred memory allocations per second, then the ID would warp around after 13 years */
	IMG_UINT32				ui32ZSBufferCurrID;	/*!< ID assigned to the next deferred devmem allocation */
	IMG_UINT32				ui32FreelistCurrID;	/*!< ID assigned to the next freelist */

	POS_LOCK 				hLockZSBuffer;		/*!< Lock to protect simultaneous access to ZSBuffers */
	DLLIST_NODE				sZSBufferHead;		/*!< List of on-demand ZSBuffers */
	POS_LOCK 				hLockFreeList;		/*!< Lock to protect simultaneous access to Freelists */
	DLLIST_NODE				sFreeListHead;		/*!< List of growable Freelists */
	PSYNC_PRIM_CONTEXT		hSyncPrimContext;
	PVRSRV_CLIENT_SYNC_PRIM *psPowSyncPrim;

	IMG_VOID				(*pfnActivePowerCheck) (PVRSRV_DEVICE_NODE *psDeviceNode);
	IMG_UINT32				ui32ActivePMReqOk;
	IMG_UINT32				ui32ActivePMReqDenied;
	IMG_UINT32				ui32ActivePMReqTotal;

	IMG_HANDLE				hProcessQueuesMISR;

	IMG_UINT32 				ui32DeviceFlags;	/*!< Flags to track general device state  */

	/* Poll data for detecting firmware fatal errors */
	IMG_UINT32  aui32CrLastPollAddr[RGXFW_THREAD_NUM];
	IMG_UINT32  ui32KCCBLastROff[RGXFWIF_DM_MAX];
	IMG_UINT32  ui32LastGEOTimeouts;

	/* GPU DVFS History and GPU Utilization stats */
	RGX_GPU_DVFS_HIST*      psGpuDVFSHistory;
	RGXFWIF_GPU_UTIL_STATS	(*pfnGetGpuUtilStats) (PVRSRV_DEVICE_NODE *psDeviceNode);

	/* Register configuration */
	RGX_REG_CONFIG		sRegCongfig;

	IMG_BOOL				bIgnoreFurtherIRQs;
	DLLIST_NODE				sMemoryContextList;

	/* Linked lists of contexts on this device */
	DLLIST_NODE 		sRenderCtxtListHead;
	DLLIST_NODE 		sComputeCtxtListHead;
	DLLIST_NODE 		sTransferCtxtListHead;
	DLLIST_NODE 		sRaytraceCtxtListHead;

#if defined(RGXFW_POWMON_TEST)
	IMG_HANDLE			hPowerMonitoringThread;	    /*!< Fatal Error Detection thread */
	IMG_BOOL			bPowMonEnable;
#endif
} PVRSRV_RGXDEV_INFO;



typedef struct _RGX_TIMING_INFORMATION_
{
	/*! GPU default core clock speed in Hz */
	IMG_UINT32			ui32CoreClockSpeed;

	/*! Active Power Management: GPU actively requests the host driver to be powered off */
	IMG_BOOL			bEnableActivePM;

	/*! Enable the GPU to power off internal Power Islands independently from the host driver */
	IMG_BOOL			bEnableRDPowIsland;

	/*! Active Power Management: Delay between the GPU idle and the request to the host */
	IMG_UINT32			ui32ActivePMLatencyms;

} RGX_TIMING_INFORMATION;

typedef struct _RGX_DATA_
{
	/*! Timing information */
	RGX_TIMING_INFORMATION	*psRGXTimingInfo;
	IMG_BOOL bHasTDMetaCodePhysHeap;
	IMG_UINT32 uiTDMetaCodePhysHeapID;
	IMG_BOOL bHasTDSecureBufPhysHeap;
	IMG_UINT32 uiTDSecureBufPhysHeapID;
} RGX_DATA;


/*
	RGX PDUMP register bank name (prefix)
*/
#define RGX_PDUMPREG_NAME		"RGXREG"

#endif /* __RGXDEVICE_H__ */
