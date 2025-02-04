/*************************************************************************/ /*!
@File
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

#include <linux/version.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/fb.h>

#if defined(SUPPORT_DRM)
#include <drm/drmP.h>
#endif

#include "kerneldisplay.h"
#include "imgpixfmts_km.h"
#include "pvrmodule.h" /* for MODULE_LICENSE() */

#if defined(SUPPORT_DRM)
#include "pvr_drm.h"
#endif

#ifndef CONFIG_FB
#error dc_fbdev needs Linux framebuffer support. Enable it in your kernel.
#endif

#define DRVNAME					"dc_fbdev"
#define DC_PHYS_HEAP_ID			0
#define MAX_COMMANDS_IN_FLIGHT	2
#define NUM_PREFERRED_BUFFERS	2

#define FALLBACK_REFRESH_RATE		60
#define FALLBACK_PHYSICAL_WIDTH		400
#define FALLBACK_PHYSICAL_HEIGHT	300

typedef struct DC_FBDEV_CONTEXT_TAG DC_FBDEV_CONTEXT;
typedef struct DC_FBDEV_BUFFER_TAG DC_FBDEV_BUFFER;
typedef struct DC_FBDEV_FLIP_CONFIG_TAG DC_FBDEV_FLIP_CONFIG;


typedef enum DC_FBDEV_FLIP_CONFIG_STATUS_TAG
{
	DC_FBDEV_FLIP_CONFIG_INACTIVE = 0,
	DC_FBDEV_FLIP_CONFIG_PENDING,
	DC_FBDEV_FLIP_CONFIG_ACTIVE,
	DC_FBDEV_FLIP_CONFIG_RETIRED,
} DC_FBDEV_FLIP_CONFIG_STATUS;


typedef struct
{
	IMG_HANDLE			hSrvHandle;
	IMG_UINT32			ePixFormat;
	struct fb_info		*psLINFBInfo;
	bool				bCanFlip;
	DC_FBDEV_CONTEXT      *psDeviceContext;
}
DC_FBDEV_DEVICE;

/* Flip config structure used for queuing of flips */
struct DC_FBDEV_FLIP_CONFIG_TAG
{
	/* Context with which this config is associated */
	DC_FBDEV_CONTEXT	*psDeviceContext;

	/* Services config data */
	IMG_HANDLE		hConfigData;

	/* Current status of the config */
	DC_FBDEV_FLIP_CONFIG_STATUS eStatus;

	/* Buffer to be displayed when the config is made active */
	DC_FBDEV_BUFFER		*psBuffer;

	/* Number of frames for which this config should remain active */
	IMG_UINT32		ui32DisplayPeriod;
};


struct DC_FBDEV_CONTEXT_TAG
{
	DC_FBDEV_DEVICE		*psDeviceData;
	IMG_HANDLE			hLastConfigData;
	IMG_UINT32			ui32AllocCount;
	/* Queue of flip configs */
	DC_FBDEV_FLIP_CONFIG	asFlipConfigQueue[MAX_COMMANDS_IN_FLIGHT];
	IMG_UINT32		ui32FlipConfigInsertIndex;
	IMG_UINT32		ui32FlipConfigRemoveIndex;
//	spinlock_t		sFlipConfigQueueLock;

};

struct DC_FBDEV_BUFFER_TAG
{
	DC_FBDEV_CONTEXT	*psDeviceContext;
	IMG_UINT32			ui32Width;
	IMG_UINT32			ui32Height;
	IMG_UINT32			ui32ByteStride;
	IMG_UINT32			ui32BufferID;
};

MODULE_SUPPORTED_DEVICE(DEVNAME);

static DC_FBDEV_DEVICE *gpsDeviceData;

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,29))
static inline void console_lock(void)
{
	acquire_console_sem();
}

static inline void console_unlock(void)
{
	release_console_sem();
}
#endif

static inline void Flip(DC_FBDEV_CONTEXT *psDeviceContext,DC_FBDEV_BUFFER *psBuffer)
{

	DC_FBDEV_DEVICE *psDeviceData = psDeviceContext->psDeviceData;
	struct fb_var_screeninfo sVar = psDeviceData->psLINFBInfo->var;
	int err;

	sVar.yoffset = 0;

	if (psBuffer)
	{
		sVar.yoffset = sVar.yres * psBuffer->ui32BufferID;
	}

	if(lock_fb_info(psDeviceData->psLINFBInfo))
	{
		console_lock();

		{
			err = fb_pan_display(psDeviceData->psLINFBInfo, &sVar);
			if(err)
				pr_err("fb_pan_display failed (err=%d)\n", err);
		}

		console_unlock();
		unlock_fb_info(psDeviceData->psLINFBInfo);
	}
}


static inline void FlipConfigQueueAdvanceIndex(DC_FBDEV_CONTEXT *psContext, IMG_UINT32 *pui32Index)
{
	(*pui32Index)++;

	if (*pui32Index >= 2)
	{
		*pui32Index = 0;
	}
}

static IMG_BOOL FlipConfigQueueHavePending(DC_FBDEV_CONTEXT *psContext)
{
	IMG_UINT32 i;

	for (i = 0; i < MAX_COMMANDS_IN_FLIGHT; i++)
	{
		if (psContext->asFlipConfigQueue[i].eStatus == DC_FBDEV_FLIP_CONFIG_PENDING)
		{
			return IMG_TRUE;
		}
	}

	return IMG_FALSE;
}


static void FlipConfigQueueAdd(DC_FBDEV_CONTEXT *psContext, IMG_HANDLE hConfigData, IMG_HANDLE *psBuffer, IMG_UINT32 ui32DisplayPeriod)
{
	DC_FBDEV_FLIP_CONFIG *psFlipConfig;

	/* Get the next inactive flip config and update the insert index */
	psFlipConfig = &psContext->asFlipConfigQueue[psContext->ui32FlipConfigInsertIndex];
	FlipConfigQueueAdvanceIndex(psContext, &psContext->ui32FlipConfigInsertIndex);

	if (psFlipConfig->eStatus != DC_FBDEV_FLIP_CONFIG_INACTIVE)
	{
		pr_err(" 1111 FlipConfigQueueAdd psFlipConfig->eStatus error!!!!! \n");
	}

	/* Fill out the flip config */
	psFlipConfig->psDeviceContext = psContext;
	psFlipConfig->hConfigData	= hConfigData;
	if (psBuffer)
	{
		psFlipConfig->psBuffer		= (DC_FBDEV_BUFFER*)psBuffer[0];
	}
	else
	{
		psFlipConfig->psBuffer		= NULL;
	}
	psFlipConfig->ui32DisplayPeriod	= ui32DisplayPeriod;
	psFlipConfig->eStatus		= DC_FBDEV_FLIP_CONFIG_PENDING;
}


static void FlipConfigQueueProcess(DC_FBDEV_CONTEXT *psContext, IMG_BOOL bDeferRetire, IMG_BOOL bFlush)
{
	IMG_HANDLE asRetiredConfigs[MAX_COMMANDS_IN_FLIGHT] = {0};
	IMG_UINT32 ui32RetireConfigIndex = 0;
	DC_FBDEV_FLIP_CONFIG *psFlipConfig;
	//unsigned long ulFlipConfigQueueLockFlags;
	IMG_UINT32 i;

	//spin_lock_irqsave(&psContext->sFlipConfigQueueLock, ulFlipConfigQueueLockFlags);

	psFlipConfig = &psContext->asFlipConfigQueue[psContext->ui32FlipConfigRemoveIndex];
	do
	{
		switch (psFlipConfig->eStatus)
		{
			case DC_FBDEV_FLIP_CONFIG_PENDING:
				/* Make the flip config active */
				Flip(psContext,psFlipConfig->psBuffer);
				psFlipConfig->eStatus = DC_FBDEV_FLIP_CONFIG_ACTIVE;

				/* Fall through */
			case DC_FBDEV_FLIP_CONFIG_ACTIVE:
				/* We can retire this config only when it's buffer has been display
				   for at least the same number of frames as the display period and
				   when there are more flip configs queued up */
				if (psFlipConfig->ui32DisplayPeriod != 0 && bFlush == IMG_FALSE)
				{
					psFlipConfig->ui32DisplayPeriod--;
				}
				else if (FlipConfigQueueHavePending(psContext))
				{
					{
						asRetiredConfigs[ui32RetireConfigIndex++] = psFlipConfig->hConfigData;

						/* Reset the flip config data */
						psFlipConfig->psDeviceContext = NULL;
						psFlipConfig->hConfigData	= 0;
						psFlipConfig->psBuffer		= NULL;
						psFlipConfig->ui32DisplayPeriod	= 0;
						psFlipConfig->eStatus		= DC_FBDEV_FLIP_CONFIG_INACTIVE;
					}

					/* Consider the flip retired and move on the queue remove index */
					FlipConfigQueueAdvanceIndex(psContext, &psContext->ui32FlipConfigRemoveIndex);
					psFlipConfig = &psContext->asFlipConfigQueue[psContext->ui32FlipConfigRemoveIndex];
				}
				break;
			case DC_FBDEV_FLIP_CONFIG_INACTIVE:
				if (psContext->ui32FlipConfigRemoveIndex != psContext->ui32FlipConfigInsertIndex)
				{
					printk(KERN_ERR DRVNAME " - Tried to process an inactive config\n");
				}
				else
				{
					/* A context has been created but no configs have been queued yet. This is expected */
					break;
				}
			case DC_FBDEV_FLIP_CONFIG_RETIRED:
			default:
				pr_err(" psFlipConfig->eStatus error !! \n ");
		}
	} while (psFlipConfig->eStatus == DC_FBDEV_FLIP_CONFIG_PENDING);

//	spin_unlock_irqrestore(&psContext->sFlipConfigQueueLock, ulFlipConfigQueueLockFlags);

	for (i = 0; i < ui32RetireConfigIndex; i++)
	{
		DCDisplayConfigurationRetired(asRetiredConfigs[i]);
	}
}



static
IMG_VOID DC_FBDEV_GetInfo(IMG_HANDLE hDeviceData,
						  DC_DISPLAY_INFO *psDisplayInfo)
{
	PVR_UNREFERENCED_PARAMETER(hDeviceData);

	strncpy(psDisplayInfo->szDisplayName, DRVNAME " 1", DC_NAME_SIZE);

	psDisplayInfo->ui32MinDisplayPeriod	= 0;
	psDisplayInfo->ui32MaxDisplayPeriod	= 1;
	psDisplayInfo->ui32MaxPipes			= 1;
}

static
PVRSRV_ERROR DC_FBDEV_PanelQueryCount(IMG_HANDLE hDeviceData,
									  IMG_UINT32 *pui32NumPanels)
{
	PVR_UNREFERENCED_PARAMETER(hDeviceData);
	*pui32NumPanels = 1;
	return PVRSRV_OK;
}

static
PVRSRV_ERROR DC_FBDEV_PanelQuery(IMG_HANDLE hDeviceData,
								 IMG_UINT32 ui32PanelsArraySize,
								 IMG_UINT32 *pui32NumPanels,
								 PVRSRV_PANEL_INFO *psPanelInfo)
{
	DC_FBDEV_DEVICE *psDeviceData = hDeviceData;
	struct fb_var_screeninfo *psVar = &psDeviceData->psLINFBInfo->var;
	struct fb_var_screeninfo sVar = { .pixclock = 0 };

	if(!lock_fb_info(psDeviceData->psLINFBInfo))
		return PVRSRV_ERROR_RETRY;

	*pui32NumPanels = 1;

	psPanelInfo[0].sSurfaceInfo.sFormat.ePixFormat = psDeviceData->ePixFormat;
	psPanelInfo[0].sSurfaceInfo.sDims.ui32Width    = psVar->xres;
	psPanelInfo[0].sSurfaceInfo.sDims.ui32Height   = psVar->yres;
	psPanelInfo[0].sSurfaceInfo.sFormat.eMemLayout = PVRSRV_SURFACE_MEMLAYOUT_STRIDED;
	psPanelInfo[0].sSurfaceInfo.sFormat.u.sFBCLayout.eFBCompressionMode = FB_COMPRESSION_NONE;

	/* Conformant fbdev drivers should have `var' and mode in sync by now,
	 * but some don't (like drmfb), so try a couple of different ways to
	 * get the info before falling back to the default.
	 */
	if((psVar->xres > 0) && (psVar->yres > 0) && (psVar->pixclock > 0))
		sVar = *psVar;
	else if(psDeviceData->psLINFBInfo->mode)
		fb_videomode_to_var(&sVar, psDeviceData->psLINFBInfo->mode);

	if((sVar.xres > 0) && (sVar.yres > 0) && (sVar.pixclock > 0))
	{
		psPanelInfo[0].ui32RefreshRate = 1000000000LU /
			((sVar.upper_margin + sVar.lower_margin +
			  sVar.yres + sVar.vsync_len) *
			 (sVar.left_margin  + sVar.right_margin +
			  sVar.xres + sVar.hsync_len) *
			 (sVar.pixclock / 1000));
	}
	else
		psPanelInfo[0].ui32RefreshRate = FALLBACK_REFRESH_RATE;

	if(psPanelInfo[0].ui32RefreshRate == 0)
	{
		psPanelInfo[0].ui32RefreshRate = FALLBACK_REFRESH_RATE;
	}
	pr_err("DC_FBDEV_PanelQuery  psPanelInfo[0].ui32RefreshRate = %d\n",psPanelInfo[0].ui32RefreshRate);
	psPanelInfo[0].ui32XDpi=
		sVar.width ? sVar.width : FALLBACK_PHYSICAL_WIDTH;

	psPanelInfo[0].ui32YDpi=
		sVar.height ? sVar.height : FALLBACK_PHYSICAL_HEIGHT;

	unlock_fb_info(psDeviceData->psLINFBInfo);
	return PVRSRV_OK;
}

static
PVRSRV_ERROR DC_FBDEV_FormatQuery(IMG_HANDLE hDeviceData,
								  IMG_UINT32 ui32NumFormats,
								  PVRSRV_SURFACE_FORMAT *pasFormat,
								  IMG_UINT32 *pui32Supported)
{
	DC_FBDEV_DEVICE *psDeviceData = hDeviceData;
	int i;

	for(i = 0; i < ui32NumFormats; i++)
	{
		pui32Supported[i] = 0;

		if(pasFormat[i].ePixFormat == psDeviceData->ePixFormat)
			pui32Supported[i]++;
	}

	return PVRSRV_OK;
}

static
PVRSRV_ERROR DC_FBDEV_DimQuery(IMG_HANDLE hDeviceData,
							   IMG_UINT32 ui32NumDims,
							   PVRSRV_SURFACE_DIMS *psDim,
							   IMG_UINT32 *pui32Supported)
{
	DC_FBDEV_DEVICE *psDeviceData = hDeviceData;
	struct fb_var_screeninfo *psVar = &psDeviceData->psLINFBInfo->var;
	int i;

	if(!lock_fb_info(psDeviceData->psLINFBInfo))
		return PVRSRV_ERROR_RETRY;

	for(i = 0; i < ui32NumDims; i++)
	{
		pui32Supported[i] = 0;

		if(psDim[i].ui32Width  == psVar->xres &&
		   psDim[i].ui32Height == psVar->yres)
			pui32Supported[i]++;
	}

	unlock_fb_info(psDeviceData->psLINFBInfo);
	return PVRSRV_OK;
}

static
PVRSRV_ERROR DC_FBDEV_ContextCreate(IMG_HANDLE hDeviceData,
									IMG_HANDLE *hDisplayContext)
{
	DC_FBDEV_DEVICE *psDeviceData = (DC_FBDEV_DEVICE *)hDeviceData;
	DC_FBDEV_CONTEXT *psDeviceContext;
	PVRSRV_ERROR eError = PVRSRV_OK;

	psDeviceContext = kzalloc(sizeof(DC_FBDEV_CONTEXT), GFP_KERNEL);
	if(!psDeviceContext)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_out;
	}

//	spin_lock_init(&psDeviceContext->sFlipConfigQueueLock);

	psDeviceContext->psDeviceData = hDeviceData;
	psDeviceData->psDeviceContext= psDeviceContext;
	*hDisplayContext = psDeviceContext;

err_out:
	return eError;
}

static PVRSRV_ERROR
DC_FBDEV_ContextConfigureCheck(IMG_HANDLE hDisplayContext,
							   IMG_UINT32 ui32PipeCount,
							   PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib,
							   IMG_HANDLE *ahBuffers)
{
	DC_FBDEV_CONTEXT *psDeviceContext = hDisplayContext;
	DC_FBDEV_DEVICE *psDeviceData = psDeviceContext->psDeviceData;
	struct fb_var_screeninfo *psVar = &psDeviceData->psLINFBInfo->var;
	DC_FBDEV_BUFFER *psBuffer;
	PVRSRV_ERROR eError;

	if(ui32PipeCount != 1)
	{
		eError = PVRSRV_ERROR_DC_TOO_MANY_PIPES;
		goto err_out;
	}

	if(!ahBuffers)
	{
		eError = PVRSRV_ERROR_DC_INVALID_CONFIG;
		goto err_out;
	}

	if(!lock_fb_info(psDeviceData->psLINFBInfo))
	{
		eError = PVRSRV_ERROR_RETRY;
		goto err_out;
	}

	psBuffer = ahBuffers[0];

	if(pasSurfAttrib[0].sCrop.sDims.ui32Width  != psVar->xres ||
	   pasSurfAttrib[0].sCrop.sDims.ui32Height != psVar->yres ||
	   pasSurfAttrib[0].sCrop.i32XOffset != 0 ||
	   pasSurfAttrib[0].sCrop.i32YOffset != 0)
	{
		eError = PVRSRV_ERROR_DC_INVALID_CROP_RECT;
		goto err_unlock;
	}

	if(pasSurfAttrib[0].sDisplay.sDims.ui32Width !=
	   pasSurfAttrib[0].sCrop.sDims.ui32Width ||
	   pasSurfAttrib[0].sDisplay.sDims.ui32Height !=
	   pasSurfAttrib[0].sCrop.sDims.ui32Height ||
	   pasSurfAttrib[0].sDisplay.i32XOffset !=
	   pasSurfAttrib[0].sCrop.i32XOffset ||
	   pasSurfAttrib[0].sDisplay.i32YOffset !=
	   pasSurfAttrib[0].sCrop.i32YOffset)
	{
		eError = PVRSRV_ERROR_DC_INVALID_DISPLAY_RECT;
		goto err_unlock;
	}

	if(psBuffer->ui32Width  != psVar->xres &&
	   psBuffer->ui32Height != psVar->yres)
	{
		eError = PVRSRV_ERROR_DC_INVALID_BUFFER_DIMS;
		goto err_unlock;
	}

	eError = PVRSRV_OK;
err_unlock:
	unlock_fb_info(psDeviceData->psLINFBInfo);
err_out:
	return eError;
}

static
IMG_VOID DC_FBDEV_ContextConfigure(IMG_HANDLE hDisplayContext,
								   IMG_UINT32 ui32PipeCount,
								   PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib,
								   IMG_HANDLE *ahBuffers,
								   IMG_UINT32 ui32DisplayPeriod,
								   IMG_HANDLE hConfigData)
{
	DC_FBDEV_CONTEXT *psDeviceContext = hDisplayContext;

	PVR_UNREFERENCED_PARAMETER(ui32PipeCount);
	PVR_UNREFERENCED_PARAMETER(pasSurfAttrib);
	PVR_UNREFERENCED_PARAMETER(ui32DisplayPeriod);

	FlipConfigQueueAdd(psDeviceContext, hConfigData, ahBuffers, ui32DisplayPeriod);

	FlipConfigQueueProcess(psDeviceContext, IMG_FALSE, IMG_FALSE);

#if 0
	if(psDeviceContext->hLastConfigData)
		DCDisplayConfigurationRetired(psDeviceContext->hLastConfigData);

	sVar.yoffset = 0;

	if(ui32PipeCount == 0)
	{
		/* If the pipe count is zero, we're tearing down. Don't record
		 * any new configurations, but still allow the display to pan
		 * back to buffer 0.
		 */
		psDeviceContext->hLastConfigData = IMG_NULL;

		/*
			We still need to "retire" this NULL flip as that signals back to
			the DC core that we've finished doing what we need to do
			and it can destroy the display context
		*/
		DCDisplayConfigurationRetired(hConfigData);
	}
	else
	{
		BUG_ON(ahBuffers == IMG_NULL);

		if(psDeviceData->bCanFlip)
		{
			DC_FBDEV_BUFFER *psBuffer = ahBuffers[0];
			sVar.yoffset = sVar.yres * psBuffer->ui32BufferID;
		}

		psDeviceContext->hLastConfigData = hConfigData;
	}

//    pr_err("fb_pan_display sVar.yoffset = %d,sVar.yres = %d  sVar.yres_virtual = %d\n", sVar.yoffset,sVar.yres,sVar.yres_virtual);

	if(lock_fb_info(psDeviceData->psLINFBInfo))
	{
		console_lock();

		/* If we're supposed to be able to flip, but the yres_virtual
		 * has been changed to an unsupported (smaller) value, we need
		 * to change it back (this is a workaround for some Linux fbdev
		 * drivers that seem to lose any modifications to yres_virtual
		 * after a blank.)
		 */
		if(psDeviceData->bCanFlip &&
		   sVar.yres_virtual < sVar.yres * NUM_PREFERRED_BUFFERS)
		{
			sVar.activate = FB_ACTIVATE_NOW;
			sVar.yres_virtual = sVar.yres * NUM_PREFERRED_BUFFERS;

			err = fb_set_var(psDeviceData->psLINFBInfo, &sVar);
			if(err)
				pr_err("fb_set_var failed (err=%d)\n", err);
		}
		else
		{
			err = fb_pan_display(psDeviceData->psLINFBInfo, &sVar);
			if(err)
				pr_err("fb_pan_display failed (err=%d)\n", err);
		}

		console_unlock();
		unlock_fb_info(psDeviceData->psLINFBInfo);
	}
#endif
}

static
IMG_VOID DC_FBDEV_ContextDestroy(IMG_HANDLE hDisplayContext)
{
	DC_FBDEV_CONTEXT *psDeviceContext = hDisplayContext;

	BUG_ON(psDeviceContext->hLastConfigData != IMG_NULL);
	kfree(psDeviceContext);
}

#define BYTE_TO_PAGES(range) (((range) + (PAGE_SIZE - 1)) >> PAGE_SHIFT)

static
PVRSRV_ERROR DC_FBDEV_BufferAlloc(IMG_HANDLE hDisplayContext,
								  DC_BUFFER_CREATE_INFO *psCreateInfo,
								  IMG_DEVMEM_LOG2ALIGN_T *puiLog2PageSize,
								  IMG_UINT32 *pui32PageCount,
								  IMG_UINT32 *pui32PhysHeapID,
								  IMG_UINT32 *pui32ByteStride,
								  IMG_HANDLE *phBuffer)
{
	DC_FBDEV_CONTEXT *psDeviceContext = hDisplayContext;
	DC_FBDEV_DEVICE *psDeviceData = psDeviceContext->psDeviceData;
	PVRSRV_SURFACE_INFO *psSurfInfo = &psCreateInfo->sSurface;
	PVRSRV_ERROR eError = PVRSRV_OK;
	DC_FBDEV_BUFFER *psBuffer;
	IMG_UINT32 ui32ByteSize;

	if(psDeviceContext->ui32AllocCount == NUM_PREFERRED_BUFFERS)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_out;
	}

	if(psSurfInfo->sFormat.ePixFormat != psDeviceData->ePixFormat)
	{
		eError = PVRSRV_ERROR_UNSUPPORTED_PIXEL_FORMAT;
		goto err_out;
	}

	psBuffer = kmalloc(sizeof(DC_FBDEV_BUFFER), GFP_KERNEL);
	if(!psBuffer)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_out;
	}

	psBuffer->psDeviceContext = psDeviceContext;
	psBuffer->ui32ByteStride =
		psSurfInfo->sDims.ui32Width * psCreateInfo->ui32BPP;

	psBuffer->ui32Width = psSurfInfo->sDims.ui32Width;
	psBuffer->ui32Height = psSurfInfo->sDims.ui32Height;
	psBuffer->ui32BufferID = psDeviceContext->ui32AllocCount;

	ui32ByteSize = psBuffer->ui32ByteStride * psBuffer->ui32Height;

	*puiLog2PageSize = PAGE_SHIFT;
	*pui32PageCount	 = BYTE_TO_PAGES(ui32ByteSize);
	*pui32PhysHeapID = DC_PHYS_HEAP_ID;
	*pui32ByteStride = psBuffer->ui32ByteStride;
	*phBuffer		 = psBuffer;

	/* If we don't support flipping, allow this code to give every
	 * allocated buffer the same ID. This means that the display
	 * won't be panned, and the same page list will be used for
	 * every allocation.
	 */
	if(psDeviceContext->psDeviceData->bCanFlip)
		psDeviceContext->ui32AllocCount++;

err_out:
	return eError;
}

static
PVRSRV_ERROR DC_FBDEV_BufferAcquire(IMG_HANDLE hBuffer,
									IMG_DEV_PHYADDR *pasDevPAddr,
									IMG_PVOID *ppvLinAddr)
{
	DC_FBDEV_BUFFER *psBuffer = hBuffer;
	DC_FBDEV_DEVICE *psDeviceData = psBuffer->psDeviceContext->psDeviceData;
	IMG_UINT32 ui32ByteSize = psBuffer->ui32ByteStride * psBuffer->ui32Height;
	IMG_UINTPTR_T uiStartAddr;
	IMG_UINT32 i, ui32MaxLen;

	uiStartAddr = psDeviceData->psLINFBInfo->fix.smem_start +
				  psBuffer->ui32BufferID * ui32ByteSize;

	ui32MaxLen = psDeviceData->psLINFBInfo->fix.smem_len -
				 psBuffer->ui32BufferID * ui32ByteSize;

	for (i = 0; i < BYTE_TO_PAGES(ui32ByteSize); i++)
	{
		BUG_ON(i * PAGE_SIZE >= ui32MaxLen);
		pasDevPAddr[i].uiAddr = uiStartAddr + (i * PAGE_SIZE);
	}

	/* We're UMA, so services will do the right thing and make
	 * its own CPU virtual address mapping for the buffer.
	 */
	*ppvLinAddr = IMG_NULL;

	return PVRSRV_OK;
}

static IMG_VOID DC_FBDEV_BufferRelease(IMG_HANDLE hBuffer)
{
	PVR_UNREFERENCED_PARAMETER(hBuffer);
}

static IMG_VOID DC_FBDEV_BufferFree(IMG_HANDLE hBuffer)
{
	kfree(hBuffer);
}

/* If we can flip, we need to make sure we have the memory to do so.
 *
 * We'll assume that the fbdev device provides extra space in
 * yres_virtual for panning; xres_virtual is theoretically supported,
 * but it involves more work.
 *
 * If the fbdev device doesn't have yres_virtual > yres, we'll try
 * requesting it before bailing. Userspace applications commonly do
 * this with an FBIOPUT_VSCREENINFO ioctl().
 *
 * Another problem is with a limitation in the services DC -- it
 * needs framebuffers to be page aligned (this is a SW limitation,
 * the HW can support non-page-aligned buffers). So we have to
 * check that stride * height for a single buffer is page aligned.
 */

static bool DC_FBDEV_FlipPossible(struct fb_info *psLINFBInfo)
{
	struct fb_var_screeninfo sVar = psLINFBInfo->var;
	int err;

	if(!psLINFBInfo->fix.xpanstep && !psLINFBInfo->fix.ypanstep &&
	   !psLINFBInfo->fix.ywrapstep)
	{
		pr_err("The fbdev device detected does not support ypan/ywrap. "
			   "Flipping disabled.\n");
		return false;
	}

	if((psLINFBInfo->fix.line_length * sVar.yres) % PAGE_SIZE != 0)
	{
		pr_err("Line length (in bytes) x yres is not a multiple of "
			   "page size. Flipping disabled.\n");
		return false;
	}

	/* We might already have enough space */
	if(sVar.yres * NUM_PREFERRED_BUFFERS <= sVar.yres_virtual)
		return true;

	pr_err("No buffer space for flipping; asking for more.\n");

	sVar.activate = FB_ACTIVATE_NOW;
	sVar.yres_virtual = sVar.yres * NUM_PREFERRED_BUFFERS;

	err = fb_set_var(psLINFBInfo, &sVar);
	if(err)
	{
		pr_err("fb_set_var failed (err=%d). Flipping disabled.\n", err);
		return false;
	}

	if(sVar.yres * NUM_PREFERRED_BUFFERS > sVar.yres_virtual)
	{
		pr_err("Failed to obtain additional buffer space. "
			   "Flipping disabled.\n");
		return false;
	}

	/* Some fbdev drivers allow the yres_virtual modification through,
	 * but don't actually update the fix. We need the fix to be updated
	 * and more memory allocated, so we can actually take advantage of
	 * the increased yres_virtual.
	 */
	if(psLINFBInfo->fix.smem_len < psLINFBInfo->fix.line_length * sVar.yres_virtual)
	{
		pr_err("'fix' not re-allocated with sufficient buffer space. "
			   "Flipping disabled.\n");
		return false;
	}

	return true;
}

#if defined(SUPPORT_DRM)
int PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Init)(struct drm_device unref__ *dev)
#else
static int __init DC_FBDEV_init(void)
#endif
{
	static DC_DEVICE_FUNCTIONS sDCFunctions =
	{
		.pfnGetInfo					= DC_FBDEV_GetInfo,
		.pfnPanelQueryCount			= DC_FBDEV_PanelQueryCount,
		.pfnPanelQuery				= DC_FBDEV_PanelQuery,
		.pfnFormatQuery				= DC_FBDEV_FormatQuery,
		.pfnDimQuery				= DC_FBDEV_DimQuery,
		.pfnContextCreate			= DC_FBDEV_ContextCreate,
		.pfnContextDestroy			= DC_FBDEV_ContextDestroy,
		.pfnContextConfigure		= DC_FBDEV_ContextConfigure,
		.pfnContextConfigureCheck	= DC_FBDEV_ContextConfigureCheck,
		.pfnBufferAlloc				= DC_FBDEV_BufferAlloc,
		.pfnBufferAcquire			= DC_FBDEV_BufferAcquire,
		.pfnBufferRelease			= DC_FBDEV_BufferRelease,
		.pfnBufferFree				= DC_FBDEV_BufferFree,
	};

	struct fb_info *psLINFBInfo;
	IMG_PIXFMT ePixFormat;
	int err = -ENODEV;

	psLINFBInfo = registered_fb[0];
	if(!psLINFBInfo)
	{
		pr_err("No Linux framebuffer (fbdev) device is registered!\n"
			   "Check you have a framebuffer driver compiled into your "
			   "kernel\nand that it is enabled on the cmdline.\n");
		goto err_out;
	}

	if(!lock_fb_info(psLINFBInfo))
		goto err_out;

	console_lock();

	/* Filter out broken FB devices */
	if(!psLINFBInfo->fix.smem_len || !psLINFBInfo->fix.line_length)
	{
		pr_err("The fbdev device detected had a zero smem_len or "
			   "line_length,\nwhich suggests it is a broken driver.\n");
		goto err_unlock;
	}

	if(psLINFBInfo->fix.type != FB_TYPE_PACKED_PIXELS ||
	   psLINFBInfo->fix.visual != FB_VISUAL_TRUECOLOR)
	{
		pr_err("The fbdev device detected is not truecolor with packed "
			   "pixels.\n");
		goto err_unlock;
	}

	if(psLINFBInfo->var.bits_per_pixel == 32)
	{
		if(psLINFBInfo->var.red.length   != 8  ||
		   psLINFBInfo->var.green.length != 8  ||
		   psLINFBInfo->var.blue.length  != 8  ||
		   psLINFBInfo->var.red.offset   != 16 ||
		   psLINFBInfo->var.green.offset != 8  ||
		   psLINFBInfo->var.blue.offset  != 0)
		{
			pr_err("The fbdev device detected uses an unrecognized "
				   "32bit pixel format (%u/%u/%u, %u/%u/%u)\n",
				   psLINFBInfo->var.red.length,
				   psLINFBInfo->var.green.length,
				   psLINFBInfo->var.blue.length,
				   psLINFBInfo->var.red.offset,
				   psLINFBInfo->var.green.offset,
				   psLINFBInfo->var.blue.offset);
			goto err_unlock;
		}
		ePixFormat = IMG_PIXFMT_B8G8R8A8_UNORM;
	}
	else if(psLINFBInfo->var.bits_per_pixel == 16)
	{
		if(psLINFBInfo->var.red.length   != 5  ||
		   psLINFBInfo->var.green.length != 6  ||
		   psLINFBInfo->var.blue.length  != 5  ||
		   psLINFBInfo->var.red.offset   != 11 ||
		   psLINFBInfo->var.green.offset != 5  ||
		   psLINFBInfo->var.blue.offset  != 0)
		{
			pr_err("The fbdev device detected uses an unrecognized "
				   "16bit pixel format (%u/%u/%u, %u/%u/%u)\n",
				   psLINFBInfo->var.red.length,
				   psLINFBInfo->var.green.length,
				   psLINFBInfo->var.blue.length,
				   psLINFBInfo->var.red.offset,
				   psLINFBInfo->var.green.offset,
				   psLINFBInfo->var.blue.offset);
			goto err_unlock;
		}
		ePixFormat = IMG_PIXFMT_B5G6R5_UNORM;
	}
	else
	{
		pr_err("The fbdev device detected uses an unsupported "
			   "bpp (%u).\n", psLINFBInfo->var.bits_per_pixel);
		goto err_unlock;
	}

	if(!try_module_get(psLINFBInfo->fbops->owner))
	{
		pr_err("try_module_get() failed");
		goto err_unlock;
	}

	if(psLINFBInfo->fbops->fb_open &&
	   psLINFBInfo->fbops->fb_open(psLINFBInfo, 0) != 0)
	{
		pr_err("fb_open() failed");
		goto err_module_put;
	}

	gpsDeviceData = kmalloc(sizeof(DC_FBDEV_DEVICE), GFP_KERNEL);
	if(!gpsDeviceData)
		goto err_module_put;

	gpsDeviceData->psLINFBInfo = psLINFBInfo;
	gpsDeviceData->ePixFormat = ePixFormat;

	if(DCRegisterDevice(&sDCFunctions,
						MAX_COMMANDS_IN_FLIGHT,
						gpsDeviceData,
						&gpsDeviceData->hSrvHandle) != PVRSRV_OK)
		goto err_kfree;

	gpsDeviceData->bCanFlip = DC_FBDEV_FlipPossible(psLINFBInfo);

	pr_err("Found usable fbdev device (%s):\n"
			"range (physical) = 0x%lx-0x%lx\n"
			"size (bytes)     = 0x%x\n"
			"xres x yres      = %ux%u\n"
			"xres x yres (v)  = %ux%u\n"
			"img pix fmt      = %u\n"
			"flipping?        = %d\n",
			psLINFBInfo->fix.id,
			psLINFBInfo->fix.smem_start,
			psLINFBInfo->fix.smem_start + psLINFBInfo->fix.smem_len,
			psLINFBInfo->fix.smem_len,
			psLINFBInfo->var.xres, psLINFBInfo->var.yres,
			psLINFBInfo->var.xres_virtual, psLINFBInfo->var.yres_virtual,
			ePixFormat, gpsDeviceData->bCanFlip);
	err = 0;
err_unlock:
	console_unlock();
	unlock_fb_info(psLINFBInfo);
err_out:
	return err;
err_kfree:
	kfree(gpsDeviceData);
err_module_put:
	module_put(psLINFBInfo->fbops->owner);
	goto err_unlock;
}

#if defined(SUPPORT_DRM)
void PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Cleanup)(struct drm_device unref__ *dev)
#else
static void __exit DC_FBDEV_exit(void)
#endif
{
	DC_FBDEV_DEVICE *psDeviceData = gpsDeviceData;
	struct fb_info *psLINFBInfo = psDeviceData->psLINFBInfo;

	lock_fb_info(psLINFBInfo);
	console_lock();

	if(psLINFBInfo->fbops->fb_release)
	   psLINFBInfo->fbops->fb_release(psLINFBInfo, 0);

	module_put(psLINFBInfo->fbops->owner);

	console_unlock();
	unlock_fb_info(psLINFBInfo);

	DCUnregisterDevice(psDeviceData->hSrvHandle);
	kfree(psDeviceData);
}

#if !defined(SUPPORT_DRM)
module_init(DC_FBDEV_init);
module_exit(DC_FBDEV_exit);
#endif
