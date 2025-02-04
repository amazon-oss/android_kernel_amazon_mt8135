/*************************************************************************/ /*!
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

#include "dc_pdp.h"
#include "dc_osfuncs.h"
#include "tcf_pll.h"
#include "tcf_rgbpdp_regs.h"
#include "imgpixfmts_km.h"

#include "pvrsrv.h"

#if defined(LINUX)
#include <linux/spinlock.h>
#endif

#define DCPDP_MAX_COMMANDS_INFLIGHT	(2)
#define DCPDP_PHYS_HEAP_ID		(1)
#define DCPDP_INTERRUPT_ID		(1)

/* This has to be less than or equal to the size of the type
   used for ui32BufferUseMask in the DCPDP_DEVICE structure */
#define DCPDP_MAX_BUFFERS		(32)

#define DCPDP_MIN_DISPLAY_PERIOD	(0)
#define DCPDP_MAX_DISPLAY_PERIOD	(2)

#define DCPDP_PIXEL_FORMAT		IMG_PIXFMT_B8G8R8A8_UNORM
#define DCPDP_PIXEL_FORMAT_BPP		(4)

#ifndef DCPDP_DPI
#define DCPDP_DPI			(160)
#endif

#if defined(DCPDP_DYNAMIC_GTF_TIMING)
#define	fGTF_MARGIN			1.8f	/* Top & bottom margin % */
#define fGTF_HSYNC_MARGIN		8.0f	/* HSync margin % */
#define fGTF_MIN_VSYNCBP		550.0f	/* Min VSync + Back Porch (us) */
#define	fGTF_MIN_PORCH			1.0f	/* Minimum porch line / char cell */
#define fGTF_CELL_GRAN			8.0f	/* assumed pixel granularity (cell) */
#define fGTF_CELL_GRAN2			16.0f	/* assumed pixel granularity * 2 (cell) */

#define fGTF_M				600.0f	/* Blanking Formula Gradient %/KHz */
#define	fGTF_C				40.0f	/* Blanking Formula Offset % */
#define fGTF_K				128.0f	/* Blanking Formula scale factor */
#define fGTF_J				20.0f	/* Blanking Formula weighting % */

#define GTF_VSYNC_WIDTH			7	/* Width of VSync lines */
#define	GTF_MIN_PORCH			1	/* Minimum porch line / char cell */
#define	GTF_MIN_PORCH_PIXELS		8	/* Minimum porch Pixels cell */
#define GTF_CELL_GRAN			8	/* assumed pixel granularity (cell) */
#define GTF_CELL_GRAN2			16	/* assumed pixel granularity * 2 (cell) */
#endif

typedef struct DCPDP_BUFFER_TAG DCPDP_BUFFER;
typedef struct DCPDP_FLIP_CONFIG_TAG DCPDP_FLIP_CONFIG;
typedef struct DCPDP_FLIP_CONTEXT_TAG DCPDP_FLIP_CONTEXT;

typedef struct DCPDP_TIMING_DATA_TAG
{
	IMG_UINT32		ui32HDisplay;
	IMG_UINT32		ui32HBackPorch;
	IMG_UINT32		ui32HTotal;
	IMG_UINT32		ui32HActiveStart;
	IMG_UINT32		ui32HLeftBorder;
	IMG_UINT32		ui32HRightBorder;
	IMG_UINT32		ui32HFrontPorch;

	IMG_UINT32		ui32VDisplay;
	IMG_UINT32		ui32VBackPorch;
	IMG_UINT32		ui32VTotal;
	IMG_UINT32		ui32VActiveStart;
	IMG_UINT32		ui32VTopBorder;
	IMG_UINT32		ui32VBottomBorder;
	IMG_UINT32		ui32VFrontPorch;

	IMG_UINT32		ui32VRefresh;
	IMG_UINT32		ui32ClockFreq;
} DCPDP_TIMING_DATA;

struct DCPDP_BUFFER_TAG
{
	IMG_UINT32		ui32RefCount;

	DCPDP_DEVICE		*psDeviceData;

	IMG_UINT32		ui32Width;
	IMG_UINT32		ui32Height;
	IMG_UINT32		ui32ByteStride;
	IMG_UINT32		ui32SizeInBytes;
	IMG_UINT32		ui32SizeInPages;
	IMG_PIXFMT		ePixelFormat;

	IMG_CPU_PHYADDR		sCpuPAddr;
	IMG_DEV_PHYADDR		sDevPAddr;
};

typedef enum DCPDP_FLIP_CONFIG_STATUS_TAG
{
	DCPDP_FLIP_CONFIG_INACTIVE = 0,
	DCPDP_FLIP_CONFIG_PENDING,
	DCPDP_FLIP_CONFIG_ACTIVE,
} DCPDP_FLIP_CONFIG_STATUS;


/* Flip config structure used for queuing of flips */
struct DCPDP_FLIP_CONFIG_TAG
{
	/* Context with which this config is associated */
	DCPDP_FLIP_CONTEXT	*psContext;

	/* Services config data */
	IMG_HANDLE		hConfigData;

	/* Current status of the config */
	DCPDP_FLIP_CONFIG_STATUS eStatus;

	/* Buffer to be displayed when the config is made active */
	DCPDP_BUFFER		*psBuffer;

	/* Number of frames for which this config should remain active */
	IMG_UINT32		ui32DisplayPeriod;

	/* New mode to possibly switch to after flip */
	PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib;

	/* Handle to a work item that processes the flip config queue */
	IMG_HANDLE		hWorkItem;
};

struct DCPDP_FLIP_CONTEXT_TAG
{
	/* Device on which the context has been created */
	DCPDP_DEVICE		*psDeviceData;

	/* Lock protecting the queue of flip configs */
#if defined(LINUX)
	spinlock_t		lock;
#else
	void			*pvFlipConfigQueueMutex;
#endif

	/* Queue of configs that need processing */
	DCPDP_FLIP_CONFIG	asFlipConfigQueue[DCPDP_MAX_COMMANDS_INFLIGHT];
	IMG_UINT32		ui32FlipConfigInsertIndex;
	IMG_UINT32		ui32FlipConfigRemoveIndex;

	/* Handle to a work queue used to defer the processing of the
	   flip config queue when a vsync interrupt is serviced */
	IMG_HANDLE		hFlipConfigWorkQueue;

	/* Services config data to retire */
	IMG_HANDLE		hConfigDataToRetire;
};

struct DCPDP_DEVICE_TAG
{
	IMG_HANDLE		hPVRServicesConnection;
	IMG_HANDLE		hPVRServicesDevice;
	DC_SERVICES_FUNCS	sPVRServicesFuncs;

	IMG_VOID		*pvDevice;

	IMG_HANDLE		hLISRData;

	IMG_UINT32		ui32PhysHeapID;
	PHYS_HEAP		*psPhysHeap;

	IMG_CPU_PHYADDR		sPDPRegCpuPAddr;
	IMG_CPU_VIRTADDR	pvPDPRegCpuVAddr;

	IMG_CPU_PHYADDR		sDispMemCpuPAddr;
	IMG_UINT64		uiDispMemSize;

	IMG_UINT32		uiTimingDataIndex;
	DCPDP_TIMING_DATA      *pasTimingData;
	IMG_UINT32		uiTimingDataSize;
	IMG_PIXFMT		ePixelFormat;

	IMG_UINT32		ui32BufferSize;
	IMG_UINT32		ui32BufferCount;
	IMG_UINT32		ui32BufferUseMask;

	DCPDP_BUFFER		*psSystemBuffer;

	DCPDP_FLIP_CONTEXT	*psFlipContext;

	IMG_BOOL        	bVSyncEnabled;
	IMG_BOOL        	bVSyncReporting;
	IMG_INT64       	i64LastVSyncTimeStamp;
	IMG_HANDLE		hVSyncWorkQueue;
	IMG_HANDLE		hVSyncWorkItem;
};

#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))

#if defined(DCPDP_DYNAMIC_GTF_TIMING)
static struct VESA_STANDARD_MODES
{
		IMG_UINT32 uiWidth;
		IMG_UINT32 uiHeight;
} gasVesaModes[] =
{
		{ 640, 480},
		{ 800, 600},
		{ 1024, 768},
		{ 1280, 720},
		{ 1280, 768},
		{ 1280, 800},
		{ 1280, 1024},
};
#endif

static IMG_BOOL BufferAcquireMemory(DCPDP_DEVICE *psDeviceData, DCPDP_BUFFER *psBuffer)
{
	IMG_UINT32 i;

	for (i = 0; i < psDeviceData->ui32BufferCount; i++)
	{
		if ((psDeviceData->ui32BufferUseMask & (1UL << i)) == 0)
		{
			psDeviceData->ui32BufferUseMask |= (1UL << i);

			psBuffer->sCpuPAddr.uiAddr = psDeviceData->sDispMemCpuPAddr.uiAddr + (i * psDeviceData->ui32BufferSize);

			psDeviceData->sPVRServicesFuncs.pfnPhysHeapCpuPAddrToDevPAddr(psDeviceData->psPhysHeap,
											  &psBuffer->sDevPAddr,
											  &psBuffer->sCpuPAddr);
			if (psBuffer->sDevPAddr.uiAddr == psBuffer->sCpuPAddr.uiAddr)
			{
				/* Because the device and CPU addresses are the same we assume that we have an
				   incompatible PDP device address. Calculate the correct PDP device address. */
				psBuffer->sDevPAddr.uiAddr -= psDeviceData->sDispMemCpuPAddr.uiAddr;
			}
			return IMG_TRUE;
		}
	}
	return IMG_FALSE;
}

static IMG_VOID BufferReleaseMemory(DCPDP_DEVICE *psDeviceData, DCPDP_BUFFER *psBuffer)
{
	IMG_UINT64 uiOffset;

	DC_ASSERT(psBuffer->sCpuPAddr.uiAddr >= psDeviceData->sDispMemCpuPAddr.uiAddr);
	DC_ASSERT(psBuffer->sCpuPAddr.uiAddr < (psDeviceData->sDispMemCpuPAddr.uiAddr + psDeviceData->uiDispMemSize));

	uiOffset = psBuffer->sCpuPAddr.uiAddr - psDeviceData->sDispMemCpuPAddr.uiAddr;
	uiOffset = DC_OSDiv64(uiOffset, psDeviceData->ui32BufferSize);

	psDeviceData->ui32BufferUseMask &= ~(1UL << uiOffset);

	psBuffer->sDevPAddr.uiAddr = 0;
	psBuffer->sCpuPAddr.uiAddr = 0;
}

static IMG_VOID EnableVSyncInterrupt(DCPDP_DEVICE *psDeviceData)
{
	IMG_UINT32 ui32InterruptEnable;

	ui32InterruptEnable = DC_OSReadReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_INTENAB);
	ui32InterruptEnable |= (0x1 << INTEN_VBLNK1_SHIFT);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_INTENAB, ui32InterruptEnable);
}

static IMG_VOID DisableVSyncInterrupt(DCPDP_DEVICE *psDeviceData)
{
	IMG_UINT32 ui32InterruptEnable;

	ui32InterruptEnable = DC_OSReadReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_INTENAB);
	ui32InterruptEnable &= ~(INTEN_VBLNK1_MASK);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_INTENAB, ui32InterruptEnable);
}

static void SetModeRegisters(DCPDP_DEVICE *psDeviceData);

static INLINE IMG_VOID Flip(DCPDP_BUFFER *psBuffer)
{
	IMG_UINT32 ui32Str1AddrCtrl;
	IMG_UINT32 ui32Stride;
	IMG_UINT32 ui32Value;
	IMG_UINT32 ui32Width = psBuffer->psDeviceData->pasTimingData[psBuffer->psDeviceData->uiTimingDataIndex].ui32HDisplay;
	IMG_UINT32 ui32Height = psBuffer->psDeviceData->pasTimingData[psBuffer->psDeviceData->uiTimingDataIndex].ui32VDisplay;

	ui32Value = (ui32Width - 1) << STR1WIDTH_SHIFT;
	ui32Value |= (ui32Height - 1) << STR1HEIGHT_SHIFT;

	switch (psBuffer->psDeviceData->ePixelFormat)
	{
		case IMG_PIXFMT_B8G8R8A8_UNORM:
		{
			ui32Value |= DCPDP_STR1SURF_FORMAT_ARGB8888 << STR1PIXFMT_SHIFT;
			break;
		}
		default:
		{
			DC_OSDebugPrintf(DBGLVL_ERROR, " - Unrecognised pixel format\n");
			DC_ASSERT(0);
		}
	}

	DC_OSWriteReg32(psBuffer->psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_STR1SURF, ui32Value);

	ui32Stride = ui32Width * DCPDP_PIXEL_FORMAT_BPP;

	DC_OSWriteReg32(psBuffer->psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_PDP_STR1POSN, (ui32Stride >> DCPDP_STR1POSN_STRIDE_SHIFT) - 1);

	DC_ASSERT(psBuffer);

	ui32Str1AddrCtrl = DC_OSReadReg32(psBuffer->psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_STR1ADDRCTRL);
	ui32Str1AddrCtrl &= ~(STR1BASE_MASK);
	ui32Str1AddrCtrl |= ((psBuffer->sDevPAddr.uiAddr >> DCPDP_STR1ADDRCTRL_BASE_ADDR_SHIFT) & STR1BASE_MASK);

	DC_OSWriteReg32(psBuffer->psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_STR1ADDRCTRL, ui32Str1AddrCtrl);
}

static INLINE IMG_VOID FlipConfigQueueAdvanceIndex(DCPDP_FLIP_CONTEXT *psContext, IMG_UINT32 *pui32Index)
{
	(*pui32Index)++;

	if (*pui32Index >= DCPDP_MAX_COMMANDS_INFLIGHT)
	{
		*pui32Index = 0;
	}
}

static INLINE IMG_BOOL FlipConfigQueueEmpty(DCPDP_FLIP_CONTEXT *psContext)
{
	return (IMG_BOOL)(psContext->ui32FlipConfigInsertIndex == psContext->ui32FlipConfigRemoveIndex);
}

static IMG_VOID FlipConfigQueueAdd(DCPDP_FLIP_CONTEXT *psContext,
				   IMG_HANDLE hConfigData,
				   DCPDP_BUFFER *psBuffer,
				   PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib,
				   IMG_UINT32 ui32DisplayPeriod)
{
	DCPDP_FLIP_CONFIG *psFlipConfig;

	/* Get the next inactive flip config and update the insert index */
	psFlipConfig = &psContext->asFlipConfigQueue[psContext->ui32FlipConfigInsertIndex];
	FlipConfigQueueAdvanceIndex(psContext, &psContext->ui32FlipConfigInsertIndex);

	DC_ASSERT(psFlipConfig->eStatus == DCPDP_FLIP_CONFIG_INACTIVE);

	/* Fill out the flip config */
	psFlipConfig->psContext		= psContext;
	psFlipConfig->hConfigData	= hConfigData;
	psFlipConfig->psBuffer		= psBuffer;
	psFlipConfig->ui32DisplayPeriod	= ui32DisplayPeriod;
	psFlipConfig->pasSurfAttrib	= pasSurfAttrib;

	/* Should be updated last */
	psFlipConfig->eStatus		= DCPDP_FLIP_CONFIG_PENDING;
}

static IMG_UINT32 GetTimingIndex(DCPDP_DEVICE *psDeviceData, IMG_UINT32 width, IMG_UINT32 height);

static IMG_VOID FlipConfigQueueProcess(DCPDP_FLIP_CONTEXT *psContext, IMG_BOOL bFlipped, IMG_BOOL bFlush)
{
	DCPDP_DEVICE *psDeviceData = psContext->psDeviceData;
	DCPDP_FLIP_CONFIG *psFlipConfig;
	PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib;
	IMG_HANDLE ahRetiredConfigs[DCPDP_MAX_COMMANDS_INFLIGHT] = {0};
	IMG_UINT32 i, ui32RetireConfigIndex = 0;
#if defined(LINUX)
	unsigned long ulSpinlockFlags;
#endif

#if defined(LINUX)
	spin_lock_irqsave(&psContext->lock, ulSpinlockFlags);
#else
	DC_OSMutexLock(psContext->pvFlipConfigQueueMutex);
#endif

	psFlipConfig = &psContext->asFlipConfigQueue[psContext->ui32FlipConfigRemoveIndex];
	pasSurfAttrib = psFlipConfig->pasSurfAttrib;

	do
	{
		switch (psFlipConfig->eStatus)
		{
			case DCPDP_FLIP_CONFIG_PENDING:
				if (!bFlipped)
				{
					Flip(psFlipConfig->psBuffer);

					bFlipped = IMG_TRUE;
				}

				/* If required, set the mode after flipping */
				if (pasSurfAttrib != NULL)
				{
					if (pasSurfAttrib[0].sDisplay.sDims.ui32Width != psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HDisplay ||
						pasSurfAttrib[0].sDisplay.sDims.ui32Height != psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VDisplay)
					{
						psDeviceData->uiTimingDataIndex = GetTimingIndex(psDeviceData,
												 pasSurfAttrib[0].sDisplay.sDims.ui32Width,
												 pasSurfAttrib[0].sDisplay.sDims.ui32Height);
						SetModeRegisters(psDeviceData);
					}
				}

				psFlipConfig->eStatus = DCPDP_FLIP_CONFIG_ACTIVE;

				/* Now that a new flip config is active we can retire the previous one (if one exists) */
				if (psContext->hConfigDataToRetire != IMG_NULL)
				{
					ahRetiredConfigs[ui32RetireConfigIndex++] = psContext->hConfigDataToRetire;
					psContext->hConfigDataToRetire = IMG_NULL;
				}

				/* Fall through */
			case DCPDP_FLIP_CONFIG_ACTIVE:
				/* We can retire this config only when it's buffer has been
				   displayed for at least the same number of frames as the
				   display period or if we're flushing the queue */
				if (psFlipConfig->ui32DisplayPeriod != 0)
				{
					psFlipConfig->ui32DisplayPeriod--;
				}

				if (psFlipConfig->ui32DisplayPeriod == 0 || bFlush)
				{
					DCPDP_FLIP_CONFIG *psNextFlipConfig;

					/* There should never be an outstanding config to retire at this point */
					DC_ASSERT(psContext->hConfigDataToRetire == IMG_NULL);

					/* Now that this config has been active for the minimum number of frames, i.e. the
					   display period, we can mark the flip config queue item as inactive. However, we
					   can only retire the config once a new config has been made active. Therefore,
					   store the Services config data handle so that we can retire it later. */
					psContext->hConfigDataToRetire = psFlipConfig->hConfigData;

					/* Reset the flip config data */
					psFlipConfig->hConfigData	= 0;
					psFlipConfig->psBuffer		= NULL;
					psFlipConfig->ui32DisplayPeriod	= 0;
					psFlipConfig->eStatus		= DCPDP_FLIP_CONFIG_INACTIVE;

					/* Move on the queue remove index */
					FlipConfigQueueAdvanceIndex(psContext, &psContext->ui32FlipConfigRemoveIndex);

					psNextFlipConfig = &psContext->asFlipConfigQueue[psContext->ui32FlipConfigRemoveIndex];
					if (psNextFlipConfig->ui32DisplayPeriod == 0 || bFlush)
					{
						psFlipConfig = psNextFlipConfig;
						bFlipped = IMG_FALSE;
					}
				}
				break;
			case DCPDP_FLIP_CONFIG_INACTIVE:
				if (FlipConfigQueueEmpty(psContext))
				{
					break;
				}
				DC_OSDebugPrintf(DBGLVL_ERROR, " - Tried to process an inactive config when one wasn't expected\n");
			default:
				DC_ASSERT(0);
		}
	} while (psFlipConfig->eStatus == DCPDP_FLIP_CONFIG_PENDING);

#if defined(LINUX)
	spin_unlock_irqrestore(&psContext->lock, ulSpinlockFlags);
#else
	DC_OSMutexUnlock(psContext->pvFlipConfigQueueMutex);
#endif

	/* Call into Services to retire any configs that were ready to be
	 * retired. */
	for (i = 0; i < ui32RetireConfigIndex; i++)
	{
		psDeviceData->sPVRServicesFuncs.pfnDCDisplayConfigurationRetired(ahRetiredConfigs[i]);
	}
}

static PVRSRV_ERROR VSyncProcessor(IMG_VOID *pvData)
{
	DCPDP_DEVICE *psDeviceData = pvData;

	psDeviceData->sPVRServicesFuncs.pfnCheckStatus(IMG_NULL);

	return PVRSRV_OK;
}

static PVRSRV_ERROR FlipConfigProcessor(IMG_VOID *pvData)
{
	DCPDP_FLIP_CONTEXT *psContext = (DCPDP_FLIP_CONTEXT *)pvData;

	DC_ASSERT(psContext);

	FlipConfigQueueProcess(psContext, IMG_TRUE, IMG_FALSE);

	return PVRSRV_OK;
}

static IMG_VOID DCPDPGetInfo(IMG_HANDLE hDeviceData, DC_DISPLAY_INFO *psDisplayInfo)
{
	PVR_UNREFERENCED_PARAMETER(hDeviceData);

	/* Copy our device name */
	DC_OSStringNCopy(psDisplayInfo->szDisplayName, DRVNAME " 1", DC_NAME_SIZE);

	/* Report what our minimum and maximum display period is. */
	psDisplayInfo->ui32MinDisplayPeriod	= DCPDP_MIN_DISPLAY_PERIOD;
	psDisplayInfo->ui32MaxDisplayPeriod	= DCPDP_MAX_DISPLAY_PERIOD;
	psDisplayInfo->ui32MaxPipes		= 1;
	psDisplayInfo->bUnlatchedSupported	= IMG_FALSE;
}

static PVRSRV_ERROR DCPDPPanelQueryCount(IMG_HANDLE hDeviceData, IMG_UINT32 *pui32NumPanels)
{
	PVR_UNREFERENCED_PARAMETER(hDeviceData);

	/* Don't support hot plug or multiple displays so hard code the value */
	*pui32NumPanels = 1;

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPPanelQuery(IMG_HANDLE hDeviceData,
					IMG_UINT32 ui32PanelsArraySize,
					IMG_UINT32 *pui32NumPanels,
					PVRSRV_PANEL_INFO *psPanelInfo)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)hDeviceData;

	/* Don't support hot plug or multiple displays so hard code the value */
	*pui32NumPanels = 1;

	DC_OSMemSet(psPanelInfo, 0, sizeof *psPanelInfo);

	psPanelInfo[0].sSurfaceInfo.sFormat.ePixFormat = psDeviceData->ePixelFormat;
	psPanelInfo[0].sSurfaceInfo.sFormat.eMemLayout = PVRSRV_SURFACE_MEMLAYOUT_STRIDED;
	psPanelInfo[0].sSurfaceInfo.sFormat.u.sFBCLayout.eFBCompressionMode = FB_COMPRESSION_NONE;

	psPanelInfo[0].sSurfaceInfo.sDims.ui32Width = psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HDisplay;
	psPanelInfo[0].sSurfaceInfo.sDims.ui32Height = psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VDisplay;

	psPanelInfo[0].ui32RefreshRate = psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VRefresh;
	psPanelInfo[0].ui32XDpi = DCPDP_DPI;
	psPanelInfo[0].ui32YDpi = DCPDP_DPI;

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPFormatQuery(IMG_HANDLE hDeviceData,
					 IMG_UINT32 ui32NumFormats,
					 PVRSRV_SURFACE_FORMAT *pasFormat,
					 IMG_UINT32 *pui32Supported)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)hDeviceData;
	IMG_UINT32 i;

	for (i = 0; i < ui32NumFormats; i++)
	{
		pui32Supported[i] = 0;

		if (pasFormat[i].ePixFormat == psDeviceData->ePixelFormat)
		{
			pui32Supported[i]++;
		}
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPDimQuery(IMG_HANDLE hDeviceData,
				  IMG_UINT32 ui32NumDims,
				  PVRSRV_SURFACE_DIMS *psDim,
				  IMG_UINT32 *pui32Supported)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)hDeviceData;
	IMG_UINT32 i;
	IMG_UINT32 j;

	for (i = 0; i < ui32NumDims; i++)
	{
		pui32Supported[i] = 0;

		for (j = 0; j < psDeviceData->uiTimingDataSize; j++)
		{
			if ((psDim[i].ui32Width == psDeviceData->pasTimingData[j].ui32HDisplay) &&
				(psDim[i].ui32Height == psDeviceData->pasTimingData[j].ui32VDisplay))
			{
				pui32Supported[i]++;
			}
		}
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPSetBlank(IMG_HANDLE hDeviceData,
								  IMG_BOOL bEnabled)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)hDeviceData;

	IMG_UINT32 ui32Value;

	ui32Value = DC_OSReadReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_SYNCCTRL);
	if (bEnabled)
	{
		ui32Value |= 0x1 << POWERDN_SHIFT;
		psDeviceData->bVSyncEnabled = IMG_FALSE;
	}
	else
	{
		ui32Value &= ~(POWERDN_MASK);
		psDeviceData->bVSyncEnabled = IMG_TRUE;
	}
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_SYNCCTRL, ui32Value);

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPSetVSyncReporting(IMG_HANDLE hDeviceData,
										   IMG_BOOL bEnabled)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)hDeviceData;

	psDeviceData->bVSyncReporting = bEnabled;

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPLastVSyncQuery(IMG_HANDLE hDeviceData,
										IMG_INT64 *pi64Timestamp)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)hDeviceData;

	*pi64Timestamp = psDeviceData->i64LastVSyncTimeStamp;

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPContextCreate(IMG_HANDLE hDeviceData,
					   IMG_HANDLE *hDisplayContext)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)hDeviceData;
	DCPDP_FLIP_CONTEXT *psContext;
	PVRSRV_ERROR eError;
	IMG_UINT32 i;

	if (psDeviceData->psFlipContext)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - Cannot create a context when one is already active\n");
		return PVRSRV_ERROR_RESOURCE_UNAVAILABLE;
	}

	psContext = DC_OSCallocMem(sizeof *psContext);
	if (psContext == NULL)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - Not enough memory to allocate a display context\n");
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* We don't want to process the flip config work queue in interrupt context so
	   create a work queue which we can use to defer the work */
	eError = DC_OSWorkQueueCreate(&psContext->hFlipConfigWorkQueue, DCPDP_MAX_COMMANDS_INFLIGHT);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - Couldn't create a work queue (%s)\n",
				 psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorPDPOSFreeMemContext;
	}

	/* Create work items to place on the flip config work queue when we service a vsync interrupt */
	for (i = 0; i < DCPDP_MAX_COMMANDS_INFLIGHT; i++)
	{
		eError = DC_OSWorkQueueCreateWorkItem(&psContext->asFlipConfigQueue[i].hWorkItem, FlipConfigProcessor, psContext);
		if (eError != PVRSRV_OK)
		{
			DC_OSDebugPrintf(DBGLVL_ERROR, " - Failed to create a flip config work item (%s)\n",
					 psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
			goto ErrorDC_OSWorkQueueDestroyWorkItems;
		}
	}

#if defined(LINUX)
	spin_lock_init(&psContext->lock);
#else
	/* Create a mutex to protect the flip config queue */
	eError = DC_OSMutexCreate(&psContext->pvFlipConfigQueueMutex);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - Failed to create flip config queue lock (%s)\n",
				 psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorDC_OSWorkQueueDestroyWorkItems;
	}
#endif /* defined(LINUX) */

	psContext->psDeviceData		= psDeviceData;
	psDeviceData->psFlipContext	= psContext;
	*hDisplayContext		= (IMG_HANDLE)psContext;

	EnableVSyncInterrupt(psDeviceData);

	return PVRSRV_OK;

ErrorDC_OSWorkQueueDestroyWorkItems:
	for (i = 0; i < DCPDP_MAX_COMMANDS_INFLIGHT; i++)
	{
		if (psContext->asFlipConfigQueue[i].hWorkItem != (IMG_HANDLE)IMG_NULL)
		{
			DC_OSWorkQueueDestroyWorkItem(psContext->asFlipConfigQueue[i].hWorkItem);
		}
	}

	DC_OSWorkQueueDestroy(psContext->hFlipConfigWorkQueue);

ErrorPDPOSFreeMemContext:
	DC_OSFreeMem(psContext);

	return eError;
}

static IMG_VOID DCPDPContextDestroy(IMG_HANDLE hDisplayContext)
{
	DCPDP_FLIP_CONTEXT *psContext = (DCPDP_FLIP_CONTEXT *)hDisplayContext;
	DCPDP_DEVICE *psDeviceData = psContext->psDeviceData;
	IMG_UINT32 i;

	DisableVSyncInterrupt(psDeviceData);

	/* Make sure there are no work items on the queue before it's destroyed */
	DC_OSWorkQueueFlush(psContext->hFlipConfigWorkQueue);

	for (i = 0; i < DCPDP_MAX_COMMANDS_INFLIGHT; i++)
	{
		if (psContext->asFlipConfigQueue[i].hWorkItem != (IMG_HANDLE)IMG_NULL)
		{
			DC_OSWorkQueueDestroyWorkItem(psContext->asFlipConfigQueue[i].hWorkItem);
		}
	}

	DC_OSWorkQueueDestroy(psContext->hFlipConfigWorkQueue);

#if defined(LINUX)
	/* No need to destroy a spinlock */
#else
	DC_OSMutexDestroy(psContext->pvFlipConfigQueueMutex);
#endif

	psDeviceData->psFlipContext = NULL;
	DC_OSFreeMem(psContext);
}

static IMG_UINT32 GetTimingIndex(DCPDP_DEVICE *psDeviceData, IMG_UINT32 width, IMG_UINT32 height)
{
	IMG_UINT32 i;
	IMG_UINT32 index = -1;
	for (i = 0; i < psDeviceData->uiTimingDataSize; i++)
	{
		if (psDeviceData->pasTimingData[i].ui32HDisplay == width &&
		   psDeviceData->pasTimingData[i].ui32VDisplay == height)
		{
			index = i;
		}
	}

	return index;
}

static IMG_VOID DCPDPContextConfigure(IMG_HANDLE hDisplayContext,
					  IMG_UINT32 ui32PipeCount,
					  PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib,
					  IMG_HANDLE *ahBuffers,
					  IMG_UINT32 ui32DisplayPeriod,
					  IMG_HANDLE hConfigData)
{
	DCPDP_FLIP_CONTEXT *psContext = (DCPDP_FLIP_CONTEXT *)hDisplayContext;
	DCPDP_DEVICE *psDeviceData = psContext->psDeviceData;
	IMG_BOOL bQueueWasEmpty;

	if (ui32DisplayPeriod > DCPDP_MAX_DISPLAY_PERIOD)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - Requested display period of %u is larger than maximum supported display period of %u (clamping)\n",
				 ui32DisplayPeriod, DCPDP_MAX_DISPLAY_PERIOD);
		DC_ASSERT(ui32DisplayPeriod <= DCPDP_MAX_DISPLAY_PERIOD);

		/* Clamp to something sane so that we can continue */
		ui32DisplayPeriod = DCPDP_MAX_DISPLAY_PERIOD;
	}

	/* Add new config to the queue */
	if (ui32PipeCount != 0)
	{
		/* Check if the queue is empty before adding a new flip config */
		bQueueWasEmpty = FlipConfigQueueEmpty(psContext);

		/* We have a regular config so add it as normal */
		FlipConfigQueueAdd(psContext, hConfigData, (DCPDP_BUFFER *)ahBuffers[0], pasSurfAttrib, ui32DisplayPeriod);

		/* Check to see if vsync unlocked flipping should be done. This is determined by the
		   display period being 0 and the flip config queue being empty before we added the
		   new flip config. If it wasn't empty then we need to allow the remaining vsync locked
		   flip configs to be processed before we can start processing the queue here.
		   Also immediately flip if the current vsync interrupt is disabled.
		   This makes sure that even when the PDP is turned off we progress. */
		if (   (ui32DisplayPeriod == 0 && bQueueWasEmpty)
			|| psDeviceData->bVSyncEnabled == IMG_FALSE)
		{
			FlipConfigQueueProcess(psContext, IMG_FALSE, IMG_FALSE);
		}
	}
	else
	{
		/* We have a 'NULL' config, which indicates the display context is
		   about to be destroyed. Queue a flip back to the system buffer */
		FlipConfigQueueAdd(psContext, hConfigData, psDeviceData->psSystemBuffer, pasSurfAttrib, 0);

		/* Flush the remaining configs (this will result in the system buffer being displayed) */
		FlipConfigQueueProcess(psContext, IMG_FALSE, IMG_TRUE);

		psDeviceData->sPVRServicesFuncs.pfnDCDisplayConfigurationRetired(hConfigData);
	}
}

static PVRSRV_ERROR DCPDPContextConfigureCheck(IMG_HANDLE hDisplayContext,
						   IMG_UINT32 ui32PipeCount,
						   PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib,
						   IMG_HANDLE *ahBuffers)
{
	DCPDP_FLIP_CONTEXT *psContext = (DCPDP_FLIP_CONTEXT *)hDisplayContext;
	DCPDP_DEVICE *psDeviceData = psContext->psDeviceData;
	DCPDP_BUFFER *psBuffer;
	IMG_UINT32 ui32NewVESATimingIndex;
	ui32NewVESATimingIndex = GetTimingIndex(psDeviceData,pasSurfAttrib[0].sDisplay.sDims.ui32Width, pasSurfAttrib[0].sDisplay.sDims.ui32Height);

	if (ui32NewVESATimingIndex == -1)
	{
		return PVRSRV_ERROR_DC_INVALID_CONFIG;
	}

	if (ui32PipeCount != 1)
	{
		return PVRSRV_ERROR_DC_INVALID_CONFIG;
	}
	/* new crop dimensions, new mode dimensions */
	else if (pasSurfAttrib[0].sCrop.sDims.ui32Width != psDeviceData->pasTimingData[ui32NewVESATimingIndex].ui32HDisplay ||
		 pasSurfAttrib[0].sCrop.sDims.ui32Height != psDeviceData->pasTimingData[ui32NewVESATimingIndex].ui32VDisplay ||
		 pasSurfAttrib[0].sCrop.i32XOffset != 0 ||
		 pasSurfAttrib[0].sCrop.i32YOffset != 0)
	{
		return PVRSRV_ERROR_DC_INVALID_CROP_RECT;
	}
	/* new mode dimensions, new crop dimensions */
	else if (pasSurfAttrib[0].sDisplay.sDims.ui32Width != pasSurfAttrib[0].sCrop.sDims.ui32Width ||
		 pasSurfAttrib[0].sDisplay.sDims.ui32Height != pasSurfAttrib[0].sCrop.sDims.ui32Height ||
		 pasSurfAttrib[0].sDisplay.i32XOffset != pasSurfAttrib[0].sCrop.i32XOffset ||
		 pasSurfAttrib[0].sDisplay.i32YOffset != pasSurfAttrib[0].sCrop.i32YOffset)
	{
		return PVRSRV_ERROR_DC_INVALID_DISPLAY_RECT;
	}

	psBuffer = ahBuffers[0];

	/* new buffer dimensions, current mode dimensions */
	if (psBuffer->ui32Width <  pasSurfAttrib[0].sDisplay.sDims.ui32Width ||
		psBuffer->ui32Height < pasSurfAttrib[0].sDisplay.sDims.ui32Height)
	{
		return PVRSRV_ERROR_DC_INVALID_BUFFER_DIMS;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPBufferSystemAcquire(IMG_HANDLE hDeviceData,
						 IMG_DEVMEM_LOG2ALIGN_T *puiLog2PageSize,
						 IMG_UINT32 *pui32PageCount,
						 IMG_UINT32 *pui32PhysHeapID,
						 IMG_UINT32 *pui32ByteStride,
						 IMG_HANDLE *phSystemBuffer)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)hDeviceData;

	psDeviceData->psSystemBuffer->ui32RefCount++;

	*puiLog2PageSize	= DC_OSGetPageShift();
	*pui32PageCount		= psDeviceData->psSystemBuffer->ui32SizeInPages;
	*pui32PhysHeapID	= psDeviceData->ui32PhysHeapID;
	*pui32ByteStride	= psDeviceData->psSystemBuffer->ui32ByteStride;
	*phSystemBuffer		= psDeviceData->psSystemBuffer;

	return PVRSRV_OK;
}

static IMG_VOID DCPDPBufferSystemRelease(IMG_HANDLE hSystemBuffer)
{
	DCPDP_BUFFER *psBuffer = (DCPDP_BUFFER *)hSystemBuffer;

	psBuffer->ui32RefCount--;
}

static PVRSRV_ERROR DCPDPBufferAlloc(IMG_HANDLE hDisplayContext,
					 DC_BUFFER_CREATE_INFO *psCreateInfo,
					 IMG_DEVMEM_LOG2ALIGN_T *puiLog2PageSize,
					 IMG_UINT32 *pui32PageCount,
					 IMG_UINT32 *pui32PhysHeapID,
					 IMG_UINT32 *pui32ByteStride,
					 IMG_HANDLE *phBuffer)
{
	DCPDP_FLIP_CONTEXT *psContext = (DCPDP_FLIP_CONTEXT *)hDisplayContext;
	DCPDP_DEVICE *psDeviceData = psContext->psDeviceData;
	PVRSRV_SURFACE_INFO *psSurfInfo = &psCreateInfo->sSurface;
	DCPDP_BUFFER *psBuffer;

	DC_ASSERT(psCreateInfo->ui32BPP == DCPDP_PIXEL_FORMAT_BPP);

	if (psSurfInfo->sFormat.ePixFormat != psDeviceData->ePixelFormat)
	{
		return PVRSRV_ERROR_UNSUPPORTED_PIXEL_FORMAT;
	}

	psBuffer = DC_OSAllocMem(sizeof *psBuffer);
	if (psBuffer == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psBuffer->ui32RefCount		= 0;
	psBuffer->psDeviceData		= psDeviceData;
	psBuffer->ePixelFormat		= psSurfInfo->sFormat.ePixFormat;
	psBuffer->ui32Width		= psSurfInfo->sDims.ui32Width;
	psBuffer->ui32Height		= psSurfInfo->sDims.ui32Height;
	psBuffer->ui32ByteStride	= psBuffer->ui32Width * psCreateInfo->ui32BPP;
	psBuffer->ui32SizeInBytes	= psDeviceData->ui32BufferSize;
	psBuffer->ui32SizeInPages	= psBuffer->ui32SizeInBytes >> DC_OSGetPageShift();

	if (BufferAcquireMemory(psDeviceData, psBuffer) == IMG_FALSE)
	{
		DC_OSFreeMem(psBuffer);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* Make sure we get a buffer with the size that we're expecting (this should be page aligned) */
	DC_ASSERT((IMG_UINT32)DC_ALIGN(psBuffer->ui32ByteStride * psBuffer->ui32Height, DC_OSGetPageSize()) <= psDeviceData->ui32BufferSize);

	*puiLog2PageSize	= DC_OSGetPageShift();
	*pui32PageCount		= psBuffer->ui32SizeInPages;
	*pui32PhysHeapID	= psDeviceData->ui32PhysHeapID;
	*pui32ByteStride	= psBuffer->ui32ByteStride;
	*phBuffer		= psBuffer;

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPBufferAcquire(IMG_HANDLE hBuffer, IMG_DEV_PHYADDR *pasDevPAddr, IMG_PVOID *ppvLinAddr)
{
	DCPDP_BUFFER *psBuffer = (DCPDP_BUFFER *)hBuffer;
	DCPDP_DEVICE *psDeviceData = psBuffer->psDeviceData;
	IMG_CPU_PHYADDR sCpuPAddr = psBuffer->sCpuPAddr;
	IMG_UINT32 i;

	for (i = 0; i < psBuffer->ui32SizeInPages; i++)
	{
		psDeviceData->sPVRServicesFuncs.pfnPhysHeapCpuPAddrToDevPAddr(psDeviceData->psPhysHeap,
										  &pasDevPAddr[i], &sCpuPAddr);
		sCpuPAddr.uiAddr += DC_OSGetPageSize();
	}

	psBuffer->ui32RefCount++;

	*ppvLinAddr = IMG_NULL;

	return PVRSRV_OK;
}

static IMG_VOID DCPDPBufferRelease(IMG_HANDLE hBuffer)
{
	DCPDP_BUFFER *psBuffer = (DCPDP_BUFFER *)hBuffer;

	psBuffer->ui32RefCount--;
}

static IMG_VOID DCPDPBufferFree(IMG_HANDLE hBuffer)
{
	DCPDP_BUFFER *psBuffer = (DCPDP_BUFFER *)hBuffer;

#if defined(DEBUG)
	if (psBuffer->ui32RefCount != 0)
	{
		DC_OSDebugPrintf(DBGLVL_WARNING, " - Freeing buffer that still has %d references\n", psBuffer->ui32RefCount);
	}
#endif

	BufferReleaseMemory(psBuffer->psDeviceData, psBuffer);

	DC_OSFreeMem(psBuffer);
}

static DC_DEVICE_FUNCTIONS g_sDCFunctions =
{
	DCPDPGetInfo,				/* pfnGetInfo */
	DCPDPPanelQueryCount,		/* pfnPanelQueryCount */
	DCPDPPanelQuery,			/* pfnPanelQuery */
	DCPDPFormatQuery,			/* pfnFormatQuery */
	DCPDPDimQuery,				/* pfnDimQuery */
	DCPDPSetBlank,				/* pfnSetBlank */
	DCPDPSetVSyncReporting,		/* pfnSetVSyncReporting */
	DCPDPLastVSyncQuery,		/* pfnLastVSyncQuery */
	DCPDPContextCreate,			/* pfnContextCreate */
	DCPDPContextDestroy,		/* pfnContextDestroy */
	DCPDPContextConfigure,		/* pfnContextConfigure */
	DCPDPContextConfigureCheck,	/* pfnContextConfigureCheck */
	DCPDPBufferAlloc,			/* pfnBufferAlloc */
	DCPDPBufferAcquire,			/* pfnBufferAcquire */
	DCPDPBufferRelease,			/* pfnBufferRelease */
	DCPDPBufferFree,			/* pfnBufferFree */
	IMG_NULL,					/* pfnBufferImport */
	IMG_NULL,					/* pfnBufferMap */
	IMG_NULL,					/* pfnBufferUnmap */
	DCPDPBufferSystemAcquire,	/* pfnBufferSystemAcquire */
	DCPDPBufferSystemRelease,	/* pfnBufferSystemRelease */
};

static IMG_BOOL DisplayLISRHandler(IMG_VOID *pvData)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)pvData;
	IMG_UINT32 ui32InterruptStatus;
	PVRSRV_ERROR eError;

	ui32InterruptStatus = DC_OSReadReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_INTSTAT);
	if (ui32InterruptStatus & INTS_VBLNK1_MASK)
	{
		/* Get the timestamp for this interrupt. */
		psDeviceData->i64LastVSyncTimeStamp = DC_OSClockns();
		if (psDeviceData->bVSyncReporting)
		{
			eError = DC_OSWorkQueueAddWorkItem(psDeviceData->hVSyncWorkQueue, psDeviceData->hVSyncWorkItem);
			if (eError != PVRSRV_OK)
			{
				DC_OSDebugPrintf(DBGLVL_WARNING,
						 " - %s: Couldn't queue work item (%s)\n",
						 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));

			}
		}

		if (psDeviceData->psFlipContext)
		{
			DCPDP_FLIP_CONTEXT *psContext = psDeviceData->psFlipContext;
			DCPDP_FLIP_CONFIG *psFlipConfig;

#if defined(LINUX)
			spin_lock(&psContext->lock);
#endif

			psFlipConfig = &psContext->asFlipConfigQueue[psContext->ui32FlipConfigRemoveIndex];

			if (psFlipConfig->eStatus == DCPDP_FLIP_CONFIG_PENDING)
			{
				Flip(psFlipConfig->psBuffer);
			}

			if (psFlipConfig->eStatus != DCPDP_FLIP_CONFIG_INACTIVE)
			{
				eError = DC_OSWorkQueueAddWorkItem(psContext->hFlipConfigWorkQueue, psFlipConfig->hWorkItem);
				if (eError != PVRSRV_OK)
				{
					DC_OSDebugPrintf(DBGLVL_WARNING,
							 " - %s: Couldn't queue work item (%s)\n",
							 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
				}
			}

#if defined(LINUX)
			spin_unlock(&psContext->lock);
#endif
		}
		/* Clear the vsync status bit to show that the vsync interrupt has been handled */
		DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_INTCLEAR, (0x1UL << INTCLR_VBLNK1_SHIFT));

		return IMG_TRUE;
	}

	return IMG_FALSE;
}

#if defined(DCPDP_DYNAMIC_GTF_TIMING)
static PVRSRV_ERROR SetPDPClocks(DCPDP_DEVICE *psDeviceData, IMG_UINT32 uiClkInHz)
{
	IMG_CPU_PHYADDR sPLLRegCpuPAddr;
	IMG_CPU_VIRTADDR pvPLLRegCpuVAddr;
	IMG_UINT32 uiClkInMHz;

	/* Reserve the PLL register region and map the registers in */
	sPLLRegCpuPAddr.uiAddr = DC_OSAddrRangeStart(psDeviceData->pvDevice, DCPDP_REG_PCI_BASENUM) + DCPDP_PCI_PLL_REG_OFFSET;

	if (DC_OSRequestAddrRegion(sPLLRegCpuPAddr, DCPDP_PCI_PLL_REG_SIZE, DRVNAME) == NULL)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to reserve PLL registers\n", __FUNCTION__);
		return PVRSRV_ERROR_PCI_REGION_UNAVAILABLE;
	}

	pvPLLRegCpuVAddr = DC_OSMapPhysAddr(sPLLRegCpuPAddr, DCPDP_PCI_PLL_REG_SIZE);
	if (pvPLLRegCpuVAddr == NULL)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to map PLL registers\n", __FUNCTION__);

		DC_OSReleaseAddrRegion(sPLLRegCpuPAddr, DCPDP_PCI_PLL_REG_SIZE);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* Calculate the clock in MHz */
	uiClkInMHz = (uiClkInHz + 500000) / 1000000;

	/* Set phase 0, ratio 50:50 and frequency in MHz */
	DC_OSWriteReg32(pvPLLRegCpuVAddr, TCF_PLL_PLL_PDP_CLK0, uiClkInMHz);

	/* setup TCF_CR_PLL_PDP_CLK1TO5 based on the main clock speed */
	if (uiClkInMHz >= 50)
	{
		/* Set clocks 1-5 to be the same frequency as clock 0 */
		DC_OSWriteReg32(pvPLLRegCpuVAddr, TCF_PLL_PLL_PDP_CLK1TO5, 0x0);
	}
	else
	{
		/* Set clocks 1-5 to be double the frequency of clock 0 */
		DC_OSWriteReg32(pvPLLRegCpuVAddr, TCF_PLL_PLL_PDP_CLK1TO5, 0x3);
	}

	/* Now initiate reprogramming of the PLLs */
	DC_OSWriteReg32(pvPLLRegCpuVAddr, TCF_PLL_PLL_PDP_DRP_GO, 1);
	DC_OSDelayus(1000);
	DC_OSWriteReg32(pvPLLRegCpuVAddr, TCF_PLL_PLL_PDP_DRP_GO, 0);

	/* Unmap and release the PLL address region */
	DC_OSUnmapPhysAddr(pvPLLRegCpuVAddr, DCPDP_PCI_PLL_REG_SIZE);
	DC_OSReleaseAddrRegion(sPLLRegCpuPAddr, DCPDP_PCI_PLL_REG_SIZE);

	return PVRSRV_OK;
}
#endif /* defined(DCPDP_DYNAMIC_GTF_TIMING) */

static void SetModeRegisters(DCPDP_DEVICE *psDeviceData)
{
	DCPDP_TIMING_DATA *psTimingData = &psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex];
	IMG_UINT32 ui32Value;

	DC_ASSERT(psDeviceData->psSystemBuffer);

	/* Turn off memory requests */
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_STR1ADDRCTRL, 0x00000000);

	/* Disable sync gen */
	ui32Value = DC_OSReadReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_SYNCCTRL);
	ui32Value &= ~(SYNCACTIVE_MASK);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_SYNCCTRL, ui32Value);

#if defined(DCPDP_DYNAMIC_GTF_TIMING)
	SetPDPClocks(psDeviceData, psTimingData->ui32ClockFreq);
#endif

	if (DC_OSReadReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_STRCTRL) != 0x0000C010)
	{
		/* Buffer request threshold */
		DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_STRCTRL, 0x00001C10);
	}

	/* Border colour */
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_BORDCOL, 0x00005544);

	/* Update control */
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_UPDCTRL, 0x00000000);

	/* Set hsync timings */
	ui32Value = (psTimingData->ui32HBackPorch << HBPS_SHIFT) | (psTimingData->ui32HTotal << HT_SHIFT);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_HSYNC1, ui32Value);

	ui32Value = (psTimingData->ui32HActiveStart << HAS_SHIFT) | (psTimingData->ui32HLeftBorder << HLBS_SHIFT);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_HSYNC2, ui32Value);

	ui32Value = (psTimingData->ui32HFrontPorch << HFPS_SHIFT) | (psTimingData->ui32HRightBorder << HRBS_SHIFT);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_HSYNC3, psTimingData->ui32HFrontPorch << 16 | ui32Value);

	/* Set vsync timings */
	ui32Value = (psTimingData->ui32VBackPorch << VBPS_SHIFT) | (psTimingData->ui32VTotal << VT_SHIFT);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_VSYNC1, ui32Value);

	ui32Value = (psTimingData->ui32VActiveStart << VAS_SHIFT) | (psTimingData->ui32VTopBorder << VTBS_SHIFT);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_VSYNC2, ui32Value);

	ui32Value = (psTimingData->ui32VFrontPorch << VFPS_SHIFT) | (psTimingData->ui32VBottomBorder << VBBS_SHIFT);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_VSYNC3, ui32Value);

	/* Horizontal data enable */
	ui32Value = (psTimingData->ui32HActiveStart << HDES_SHIFT) | (psTimingData->ui32HFrontPorch << HDEF_SHIFT);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_HDECTRL, ui32Value);

	/* Vertical data enable */
	ui32Value = (psTimingData->ui32VActiveStart << VDES_SHIFT) | (psTimingData->ui32VFrontPorch << VDEF_SHIFT);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_VDECTRL, ui32Value);

	/* Vertical event start and vertical fetch start */
	ui32Value = (psTimingData->ui32VBackPorch << VFETCH_SHIFT) | (psTimingData->ui32VFrontPorch << VEVENT_SHIFT);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_VEVENT, ui32Value);

	/* Enable sync gen last and set up polarities of sync/blank */
	ui32Value = 0x1 << SYNCACTIVE_SHIFT |
		0x1 << FIELDPOL_SHIFT |
		0x1 << BLNKPOL_SHIFT |
		0x1 << VSPOL_SHIFT |
		0x1 << HSPOL_SHIFT;
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_SYNCCTRL, ui32Value);

	/* Turn on memory requests */
	DCPDPEnableMemoryRequest(psDeviceData, DCPDPGetModuleParameters()->ui32PDPEnabled);
}

static PVRSRV_ERROR InitSystemBuffer(DCPDP_DEVICE *psDeviceData)
{
	DCPDP_BUFFER *psBuffer;
	IMG_CPU_VIRTADDR pvBufferCpuVAddr;
	IMG_UINT32 *pui32Pixel;
	IMG_UINT32 ui32SizeInPixels;
	IMG_UINT32 uiGray;
	IMG_UINT32 i;

	DC_ASSERT(psDeviceData->psSystemBuffer == NULL);

	psBuffer = DC_OSAllocMem(sizeof *psBuffer);
	if (psBuffer == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psBuffer->ui32RefCount		= 0;
	psBuffer->psDeviceData		= psDeviceData;
	psBuffer->ePixelFormat		= psDeviceData->ePixelFormat;
	psBuffer->ui32Width		= psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HDisplay;
	psBuffer->ui32Height		= psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VDisplay;
	psBuffer->ui32ByteStride	= psBuffer->ui32Width * DCPDP_PIXEL_FORMAT_BPP;
	psBuffer->ui32SizeInBytes	= psDeviceData->ui32BufferSize;
	psBuffer->ui32SizeInPages	= psBuffer->ui32SizeInBytes >> DC_OSGetPageShift();

	if (BufferAcquireMemory(psDeviceData, psBuffer) == IMG_FALSE)
	{
		DC_OSFreeMem(psBuffer);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* Make sure we get a buffer with the size that we're expecting (this should be page aligned) */
	DC_ASSERT((IMG_UINT32)DC_ALIGN(psBuffer->ui32ByteStride * psBuffer->ui32Height, DC_OSGetPageSize()) <= psDeviceData->ui32BufferSize);

	/* Initialise the system buffer to a nice gradient. */
	pvBufferCpuVAddr = DC_OSMapPhysAddr(psBuffer->sCpuPAddr, psBuffer->ui32SizeInBytes);
	if (pvBufferCpuVAddr != NULL)
	{
		pui32Pixel = (IMG_UINT32 *)pvBufferCpuVAddr;
		ui32SizeInPixels = psBuffer->ui32Width * psBuffer->ui32Height;

		for (i = 0; i < ui32SizeInPixels; i++)
		{
			uiGray = (IMG_UINT32)(0x5A * i / ui32SizeInPixels);
			pui32Pixel[i] =  0xFF000000 + uiGray + (uiGray << 8) + (uiGray << 16);
		}

		DC_OSUnmapPhysAddr(pvBufferCpuVAddr, psBuffer->ui32SizeInBytes);
	}

	psDeviceData->psSystemBuffer = psBuffer;

	return PVRSRV_OK;
}

static void DeInitSystemBuffer(DCPDP_DEVICE *psDeviceData)
{
	DC_ASSERT(psDeviceData->psSystemBuffer->ui32RefCount == 0);

	BufferReleaseMemory(psDeviceData, psDeviceData->psSystemBuffer);

	DC_OSFreeMem(psDeviceData->psSystemBuffer);
	psDeviceData->psSystemBuffer = NULL;
}

#if defined(DCPDP_DYNAMIC_GTF_TIMING)
static void GTFCalcFromRefresh(IMG_UINT32 ui32Width, IMG_UINT32 ui32Height,
				   IMG_UINT32 ui32VRefresh, DCPDP_TIMING_DATA *psTimingData)
{
	double fVtFreq, fHzFreq;
	double fFieldPeriod, fHzPeriod, fVtFreqEst, fDutyCycle, fClockFreq;
	double fGTF_Mdash, fGTF_Cdash;
	unsigned long ulVTotal, ulHTotal;
	unsigned long ulVSyncBP, ulVBP;
	unsigned long ulHSyncWidth, ulHFrontPorch, ulHBlank;
	unsigned long ulVSyncMin;

	DC_OSFloatingPointBegin();

	if ((ui32Width * 3UL) == (ui32Height * 4UL))
	{
		ulVSyncMin = 4;
	}
	else if ((ui32Width * 9UL) == (ui32Height * 16UL))
	{
		ulVSyncMin = 5;
	}
	else if ((ui32Width * 10UL) == (ui32Height * 16UL))
	{
		ulVSyncMin = 6;
	}
	else if ((ui32Width * 4UL) == (ui32Height * 5UL))
	{
		ulVSyncMin = 7;
	}
	else if ((ui32Width * 9UL) == (ui32Height * 15UL))
	{
		ulVSyncMin = 7;
	}
	else
	{
		ulVSyncMin = GTF_VSYNC_WIDTH;
	}

	fGTF_Mdash = (fGTF_K * fGTF_M) / 256.0;
	fGTF_Cdash = (((fGTF_C - fGTF_J) * fGTF_K) / 256) + fGTF_J;

	/* Comes in as Hz */
	fVtFreq = (double)ui32VRefresh;

	/* Refresh rates lower than 0 and higher than 250Hz are nonsense */
	if (fVtFreq > 250.0)
	{
		fVtFreq = 250.0;
	}
	else if (fVtFreq <= 0)
	{
		fVtFreq = 60.0;
	}

	/* Calculate Field Vertical Period in uS */
	fFieldPeriod = 1000000.0f / fVtFreq;

	/* Estimate Hz Period (in uS) & hence lines in (VSync + BP) & lines in Vt BP */
	fHzPeriod = (fFieldPeriod - fGTF_MIN_VSYNCBP) / (ui32Height + fGTF_MIN_PORCH);

	ulVSyncBP = (unsigned long)(fGTF_MIN_VSYNCBP / fHzPeriod);

	ulVBP = ulVSyncBP - ulVSyncMin;
	ulVTotal  = ui32Height + ulVSyncBP + GTF_MIN_PORCH;

	/* Estimate Vt frequency (in Hz) */
	fVtFreqEst = 1000000.0f / (fHzPeriod * ulVTotal);

	/* Find Actual Hz Period (in uS) */
	fHzPeriod = (fHzPeriod * fVtFreqEst) / fVtFreq;

	/* Find actual Vt frequency (in Hz) */
	fVtFreq = 1000000.0f / (fHzPeriod * ulVTotal);

	/* Find ideal blanking duty cycle from duty cycle equation and therefore
	   number of pixels in blanking period (to nearest dbl char cell) */
	fDutyCycle = fGTF_Cdash - ((fGTF_Mdash * fHzPeriod) / 1000.0f);

	ulHBlank = (unsigned long)(((ui32Width * fDutyCycle) / (100.0f - fDutyCycle) / fGTF_CELL_GRAN2) + 0.5);
	ulHBlank = ulHBlank * GTF_CELL_GRAN2;

	/* Find HTotal & pixel clock(MHz) & Hz Frequency (KHz) */
	ulHTotal = ui32Width + ulHBlank;
	fClockFreq = (double)ulHTotal / fHzPeriod;
	fHzFreq = 1000.0f / fHzPeriod;

	ulHSyncWidth = (unsigned long)(((ulHTotal * fGTF_HSYNC_MARGIN) / (100.0 * fGTF_CELL_GRAN)) + 0.5);
	ulHSyncWidth *= GTF_CELL_GRAN;

	ulHFrontPorch = (ulHBlank >> 1) - ulHSyncWidth;
	if (ulHFrontPorch < GTF_MIN_PORCH_PIXELS)
	{
		ulHFrontPorch = GTF_MIN_PORCH_PIXELS;
	}

	psTimingData->ui32HDisplay	= ui32Width;
	psTimingData->ui32HBackPorch	= ulHSyncWidth;
	psTimingData->ui32HTotal	= ulHTotal;
	psTimingData->ui32HActiveStart	= ulHBlank - ulHFrontPorch;
	psTimingData->ui32HLeftBorder	= psTimingData->ui32HActiveStart;
	psTimingData->ui32HRightBorder	= psTimingData->ui32HLeftBorder + ui32Width;
	psTimingData->ui32HFrontPorch	= psTimingData->ui32HRightBorder;

	psTimingData->ui32VDisplay	= ui32Height;
	psTimingData->ui32VBackPorch	= GTF_VSYNC_WIDTH;
	psTimingData->ui32VTotal	= ulVTotal;
	psTimingData->ui32VActiveStart	= ulVSyncBP;
	psTimingData->ui32VTopBorder	= psTimingData->ui32VActiveStart;
	psTimingData->ui32VBottomBorder = psTimingData->ui32VTopBorder + ui32Height;
	psTimingData->ui32VFrontPorch	= psTimingData->ui32VBottomBorder;

	psTimingData->ui32VRefresh	= (IMG_UINT32)fVtFreq;

	/* scale, round and then scale */
	psTimingData->ui32ClockFreq = ((unsigned long)(fClockFreq * 1000.0)) * 1000UL;

	DC_OSFloatingPointEnd();
}
#endif /* defined(DCPDP_DYNAMIC_GTF_TIMING) */

PVRSRV_ERROR DCPDPInit(IMG_VOID *pvDevice, DCPDP_DEVICE **ppsDeviceData)
{
	DCPDP_DEVICE *psDeviceData;
	IMG_UINT64 ui64BufferCount;
	PVRSRV_ERROR eError;
#if defined(DCPDP_DYNAMIC_GTF_TIMING)
	IMG_UINT32 i;
#endif

	if (ppsDeviceData == NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	DC_OSSetDrvName(DRVNAME);

	psDeviceData = DC_OSCallocMem(sizeof *psDeviceData);
	if (psDeviceData == NULL)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to allocate device data\n", __FUNCTION__);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psDeviceData->pvDevice = pvDevice;
	psDeviceData->ePixelFormat = DCPDP_PIXEL_FORMAT;

	eError = DC_OSPVRServicesConnectionOpen(&psDeviceData->hPVRServicesConnection);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to open connection to PVR Services (%d)\n",
				 __FUNCTION__, eError);
		goto ErrorFreeDeviceData;
	}

	eError = DC_OSPVRServicesSetupFuncs(psDeviceData->hPVRServicesConnection, &psDeviceData->sPVRServicesFuncs);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to setup PVR Services function table (%d)\n",
				 __FUNCTION__, eError);
		goto ErrorPDPOSPVRServicesConnectionClose;
	}

	/* Services manages the card memory so we need to acquire the display controller
	   heap (which is a carve out of the card memory) so we can allocate our own buffers. */
	psDeviceData->ui32PhysHeapID = DCPDP_PHYS_HEAP_ID;

	eError = psDeviceData->sPVRServicesFuncs.pfnPhysHeapAcquire(psDeviceData->ui32PhysHeapID,
									&psDeviceData->psPhysHeap);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to acquire heap (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorPDPOSPVRServicesConnectionClose;
	}

	/* The PDP can only access local memory so make sure the heap is of the correct type */
	if (psDeviceData->sPVRServicesFuncs.pfnPhysHeapGetType(psDeviceData->psPhysHeap) != PHYS_HEAP_TYPE_LMA)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Heap is of the wrong type\n", __FUNCTION__);
		eError = PVRSRV_ERROR_INVALID_HEAP;
		goto ErrorPhysHeapRelease;
	}

	/* Get the CPU base address for the display heap */
	eError = psDeviceData->sPVRServicesFuncs.pfnPhysHeapGetAddress(psDeviceData->psPhysHeap,
									   &psDeviceData->sDispMemCpuPAddr);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to get display memory base address (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorPhysHeapRelease;
	}

	eError = psDeviceData->sPVRServicesFuncs.pfnPhysHeapGetSize(psDeviceData->psPhysHeap,
									&psDeviceData->uiDispMemSize);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to get the display memory size (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorPhysHeapRelease;
	}

	/* Reserve and map the PDP registers */
	psDeviceData->sPDPRegCpuPAddr.uiAddr =
		DC_OSAddrRangeStart(psDeviceData->pvDevice, DCPDP_REG_PCI_BASENUM) +
		DCPDP_PCI_PDP_REG_OFFSET;

	if (DC_OSRequestAddrRegion(psDeviceData->sPDPRegCpuPAddr, DCPDP_PCI_PDP_REG_SIZE, DRVNAME) == NULL)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to reserve the PDP registers\n", __FUNCTION__);

		psDeviceData->sPDPRegCpuPAddr.uiAddr = 0;
		eError = PVRSRV_ERROR_PCI_REGION_UNAVAILABLE;

		goto ErrorPhysHeapRelease;
	}

	psDeviceData->pvPDPRegCpuVAddr = DC_OSMapPhysAddr(psDeviceData->sPDPRegCpuPAddr, DCPDP_PCI_PDP_REG_SIZE);
	if (psDeviceData->pvPDPRegCpuVAddr == NULL)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to map PDP registers\n", __FUNCTION__);
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;

		goto ErrorReleaseMemRegion;
	}

#if defined(DCPDP_DYNAMIC_GTF_TIMING)
	psDeviceData->uiTimingDataSize = ARRAY_LENGTH(gasVesaModes);
	psDeviceData->pasTimingData = DC_OSAllocMem(sizeof(DCPDP_TIMING_DATA) * psDeviceData->uiTimingDataSize);

	if (psDeviceData->pasTimingData	== NULL)
	{
		goto ErrorPDPOSUnmapPDPRegisters;
	}

	/* fill array with timing data for VESA standard modes */
	for (i = 0; i < psDeviceData->uiTimingDataSize; i++)
	{
		if (gasVesaModes[i].uiWidth == DCPDPGetModuleParameters()->ui32PDPWidth &&
			gasVesaModes[i].uiHeight == DCPDPGetModuleParameters()->ui32PDPHeight)
		{
			psDeviceData->uiTimingDataIndex = i;
		}

		/* Setup timing data */
		GTFCalcFromRefresh(gasVesaModes[i].uiWidth,
				   gasVesaModes[i].uiHeight,
				   60,
				   &psDeviceData->pasTimingData[i]);
	}

#else /* defined(DCPDP_DYNAMIC_GTF_TIMING) */
	/* The default PLL setup is for 1280 x 1024 at 60Hz. The default PLL clock
	   is in flash memory and will be loaded from flash on reset */

	psDeviceData->uiTimingDataSize = 1;
	psDeviceData->pasTimingData = DC_OSAllocMem(sizeof(DCPDP_TIMING_DATA) * psDeviceData->uiTimingDataSize);

	if (psDeviceData->pasTimingData	== NULL)
	{
		goto ErrorPDPOSUnmapPDPRegisters;
	}

	psDeviceData->uiTimingDataIndex = 0;
	psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HDisplay	= 1280;
	psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HTotal		= 1712;
	psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HBackPorch	= 136;
	psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HActiveStart	= 352;
	psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HLeftBorder	= 352;
	psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HFrontPorch	= 1632;
	psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HRightBorder	= 1632;

	psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VDisplay	= 1024;
	psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VTotal		= 1059;
	psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VBackPorch	= 7;
	psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VActiveStart	= 34;
	psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VTopBorder	= 34;
	psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VFrontPorch	= 1058;
	psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VBottomBorder	= 1058;

	psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VRefresh	= 60;
#endif /* defined(DCPDP_DYNAMIC_GTF_TIMING) */

	/* Setup simple buffer allocator */
	psDeviceData->ui32BufferSize = (IMG_UINT32)DC_ALIGN(psDeviceData->pasTimingData[psDeviceData->uiTimingDataSize - 1].ui32HDisplay *
								psDeviceData->pasTimingData[psDeviceData->uiTimingDataSize - 1].ui32VDisplay *
								DCPDP_PIXEL_FORMAT_BPP,
								DC_OSGetPageSize());

	ui64BufferCount = psDeviceData->uiDispMemSize;
	ui64BufferCount = DC_OSDiv64(ui64BufferCount, psDeviceData->ui32BufferSize);

	psDeviceData->ui32BufferCount = (IMG_UINT32)ui64BufferCount;
	if (psDeviceData->ui32BufferCount == 0)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Not enough space for the framebuffer\n", __FUNCTION__);
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorFreeTimingData;
	}
	else if (psDeviceData->ui32BufferCount > DCPDP_MAX_BUFFERS)
	{
		psDeviceData->ui32BufferCount = DCPDP_MAX_BUFFERS;
	}

	psDeviceData->ui32BufferUseMask = 0;

	/* Create a system buffer */
	eError = InitSystemBuffer(psDeviceData);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to initialise the system buffer (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorFreeTimingData;
	}

	SetModeRegisters(psDeviceData);
	psDeviceData->bVSyncEnabled = IMG_TRUE;
	Flip(psDeviceData->psSystemBuffer);

	eError = psDeviceData->sPVRServicesFuncs.pfnDCRegisterDevice(&g_sDCFunctions,
									 DCPDP_MAX_COMMANDS_INFLIGHT,
									 psDeviceData,
									 &psDeviceData->hPVRServicesDevice);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to register the display device (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorDestroySystemBuffer;
	}

	/* Register our handler */
	eError = psDeviceData->sPVRServicesFuncs.pfnSysInstallDeviceLISR(DCPDP_INTERRUPT_ID,
									 IMG_TRUE,
									 DRVNAME,
									 DisplayLISRHandler,
									 psDeviceData,
									 &psDeviceData->hLISRData);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to install the display device interrupt handler (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorDCUnregisterDevice;
	}

	eError = DC_OSWorkQueueCreate(&psDeviceData->hVSyncWorkQueue, 1);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to create a vsync work queue (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorDCUninstallLISR;
	}

	eError = DC_OSWorkQueueCreateWorkItem(&psDeviceData->hVSyncWorkItem, VSyncProcessor, psDeviceData);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to create a vsync work item (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorDCDestroyWorkQueue;
	}

	/* Print some useful information */
	DC_OSDebugPrintf(DBGLVL_INFO, " using mode %ux%u@%uHz (phys: %ux%u)\n",
			 psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HDisplay,
			 psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VDisplay,
			 psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VRefresh,
			 (IMG_UINT32)(((psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HDisplay * 1000) / DCPDP_DPI * 254) / 10000),
			 (IMG_UINT32)(((psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VDisplay * 1000) / DCPDP_DPI * 254) / 10000));
	DC_OSDebugPrintf(DBGLVL_INFO, " register base: 0x%llxK\n", psDeviceData->sPDPRegCpuPAddr.uiAddr);
	DC_OSDebugPrintf(DBGLVL_INFO, " memory base: 0x%llxK\n", psDeviceData->sDispMemCpuPAddr.uiAddr);

	*ppsDeviceData = psDeviceData;

	return PVRSRV_OK;

ErrorDCDestroyWorkQueue:
	DC_OSWorkQueueDestroy(psDeviceData->hVSyncWorkQueue);

ErrorDCUninstallLISR:
	psDeviceData->sPVRServicesFuncs.pfnSysUninstallDeviceLISR(psDeviceData->hLISRData);

ErrorDCUnregisterDevice:
	psDeviceData->sPVRServicesFuncs.pfnDCUnregisterDevice(psDeviceData->hPVRServicesDevice);

ErrorDestroySystemBuffer:
	DeInitSystemBuffer(psDeviceData);

ErrorFreeTimingData:
	DC_OSFreeMem(psDeviceData->pasTimingData);

ErrorPDPOSUnmapPDPRegisters:
	DC_OSUnmapPhysAddr(psDeviceData->pvPDPRegCpuVAddr, DCPDP_PCI_PDP_REG_SIZE);

ErrorReleaseMemRegion:
	DC_OSReleaseAddrRegion(psDeviceData->sPDPRegCpuPAddr, DCPDP_PCI_PDP_REG_SIZE);

ErrorPhysHeapRelease:
	psDeviceData->sPVRServicesFuncs.pfnPhysHeapRelease(psDeviceData->psPhysHeap);

ErrorPDPOSPVRServicesConnectionClose:
	DC_OSPVRServicesConnectionClose(psDeviceData->hPVRServicesConnection);

ErrorFreeDeviceData:
	DC_OSFreeMem(psDeviceData);

	return eError;
}

IMG_VOID DCPDPDeInit(DCPDP_DEVICE *psDeviceData, IMG_VOID **ppvDevice)
{
	if (psDeviceData)
	{
		DC_OSWorkQueueDestroyWorkItem(psDeviceData->hVSyncWorkItem);
		DC_OSWorkQueueDestroy(psDeviceData->hVSyncWorkQueue);

		psDeviceData->sPVRServicesFuncs.pfnDCUnregisterDevice(psDeviceData->hPVRServicesDevice);
		DC_ASSERT(psDeviceData->psFlipContext == NULL);
		(void)psDeviceData->sPVRServicesFuncs.pfnSysUninstallDeviceLISR(psDeviceData->hLISRData);

		DeInitSystemBuffer(psDeviceData);

		/* Reset the register to make sure the PDP doesn't try to access on card memory */
		DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_STR1ADDRCTRL, 0x00000000);

		DC_OSUnmapPhysAddr(psDeviceData->pvPDPRegCpuVAddr, DCPDP_PCI_PDP_REG_SIZE);

		DC_OSReleaseAddrRegion(psDeviceData->sPDPRegCpuPAddr, DCPDP_PCI_PDP_REG_SIZE);

		psDeviceData->sPVRServicesFuncs.pfnPhysHeapRelease(psDeviceData->psPhysHeap);

		DC_OSPVRServicesConnectionClose(psDeviceData->hPVRServicesConnection);

		if (ppvDevice != IMG_NULL)
		{
			*ppvDevice = psDeviceData->pvDevice;
		}

		DC_OSFreeMem(psDeviceData->pasTimingData);
		DC_OSFreeMem(psDeviceData);
	}
}

void DCPDPEnableMemoryRequest(DCPDP_DEVICE *psDeviceData, IMG_BOOL bEnable)
{
	if (psDeviceData)
	{
		IMG_UINT32 ui32Value;

		ui32Value = DC_OSReadReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_STR1ADDRCTRL);
		ui32Value &= ~(STR1STREN_MASK);

		if (bEnable)
		{
			ui32Value |= 0x1 << STR1STREN_SHIFT;
		}

		DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, TCF_RGBPDP_PVR_TCF_RGBPDP_STR1ADDRCTRL, ui32Value);
	}
}

