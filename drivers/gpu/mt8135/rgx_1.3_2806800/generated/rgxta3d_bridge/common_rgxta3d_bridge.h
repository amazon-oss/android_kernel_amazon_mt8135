/*************************************************************************/ /*!
@File
@Title          Common bridge header for rgxta3d
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures that are used by both
				the client and sever side of the bridge for rgxta3d
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

#ifndef COMMON_RGXTA3D_BRIDGE_H
#define COMMON_RGXTA3D_BRIDGE_H

#include "rgx_bridge.h"
#include "sync_external.h"
#include "rgx_fwif_shared.h"


#include "pvr_bridge.h"

#define PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST			(PVRSRV_BRIDGE_RGXTA3D_START)
#define PVRSRV_BRIDGE_RGXTA3D_RGXCREATEHWRTDATA			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+0)
#define PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYHWRTDATA			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+1)
#define PVRSRV_BRIDGE_RGXTA3D_RGXCREATERENDERTARGET			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+2)
#define PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYRENDERTARGET			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+3)
#define PVRSRV_BRIDGE_RGXTA3D_RGXCREATEZSBUFFER			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+4)
#define PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYZSBUFFER			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+5)
#define PVRSRV_BRIDGE_RGXTA3D_RGXPOPULATEZSBUFFER			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+6)
#define PVRSRV_BRIDGE_RGXTA3D_RGXUNPOPULATEZSBUFFER			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+7)
#define PVRSRV_BRIDGE_RGXTA3D_RGXCREATEFREELIST			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+8)
#define PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYFREELIST			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+9)
#define PVRSRV_BRIDGE_RGXTA3D_RGXADDBLOCKTOFREELIST			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+10)
#define PVRSRV_BRIDGE_RGXTA3D_RGXREMOVEBLOCKFROMFREELIST			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+11)
#define PVRSRV_BRIDGE_RGXTA3D_RGXCREATERENDERCONTEXT			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+12)
#define PVRSRV_BRIDGE_RGXTA3D_RGXDESTROYRENDERCONTEXT			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+13)
#define PVRSRV_BRIDGE_RGXTA3D_RGXKICKTA3D			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+14)
#define PVRSRV_BRIDGE_RGXTA3D_RGXSETRENDERCONTEXTPRIORITY			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+15)
#define PVRSRV_BRIDGE_RGXTA3D_RGXGETLASTRENDERCONTEXTRESETREASON			PVRSRV_IOWR(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+16)
#define PVRSRV_BRIDGE_RGXTA3D_CMD_LAST			(PVRSRV_BRIDGE_RGXTA3D_CMD_FIRST+16)


/*******************************************
			RGXCreateHWRTData
 *******************************************/

/* Bridge in structure for RGXCreateHWRTData */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATEHWRTDATA_TAG
{
	IMG_HANDLE hDevNode;
	IMG_UINT32 ui32RenderTarget;
	IMG_DEV_VIRTADDR sPMMlistDevVAddr;
	IMG_DEV_VIRTADDR sVFPPageTableAddr;
	IMG_HANDLE * phapsFreeLists;
	IMG_UINT32 ui32PPPScreen;
	IMG_UINT32 ui32PPPGridOffset;
	IMG_UINT64 ui64PPPMultiSampleCtl;
	IMG_UINT32 ui32TPCStride;
	IMG_DEV_VIRTADDR sTailPtrsDevVAddr;
	IMG_UINT32 ui32TPCSize;
	IMG_UINT32 ui32TEScreen;
	IMG_UINT32 ui32TEAA;
	IMG_UINT32 ui32TEMTILE1;
	IMG_UINT32 ui32TEMTILE2;
	IMG_UINT32 ui32MTileStride;
	IMG_UINT32 ui32ui32ISPMergeLowerX;
	IMG_UINT32 ui32ui32ISPMergeLowerY;
	IMG_UINT32 ui32ui32ISPMergeUpperX;
	IMG_UINT32 ui32ui32ISPMergeUpperY;
	IMG_UINT32 ui32ui32ISPMergeScaleX;
	IMG_UINT32 ui32ui32ISPMergeScaleY;
	IMG_UINT16 ui16MaxRTs;
} PVRSRV_BRIDGE_IN_RGXCREATEHWRTDATA;


/* Bridge out structure for RGXCreateHWRTData */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATEHWRTDATA_TAG
{
	IMG_HANDLE hCleanupCookie;
	IMG_HANDLE hRTACtlMemDesc;
	IMG_HANDLE hsHWRTDataMemDesc;
	IMG_UINT32 ui32FWHWRTData;
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXCREATEHWRTDATA;

/*******************************************
			RGXDestroyHWRTData
 *******************************************/

/* Bridge in structure for RGXDestroyHWRTData */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYHWRTDATA_TAG
{
	IMG_HANDLE hCleanupCookie;
} PVRSRV_BRIDGE_IN_RGXDESTROYHWRTDATA;


/* Bridge out structure for RGXDestroyHWRTData */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYHWRTDATA_TAG
{
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXDESTROYHWRTDATA;

/*******************************************
			RGXCreateRenderTarget
 *******************************************/

/* Bridge in structure for RGXCreateRenderTarget */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATERENDERTARGET_TAG
{
	IMG_HANDLE hDevNode;
	IMG_DEV_VIRTADDR spsVHeapTableDevVAddr;
} PVRSRV_BRIDGE_IN_RGXCREATERENDERTARGET;


/* Bridge out structure for RGXCreateRenderTarget */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATERENDERTARGET_TAG
{
	IMG_HANDLE hsRenderTargetMemDesc;
	IMG_UINT32 ui32sRenderTargetFWDevVAddr;
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXCREATERENDERTARGET;

/*******************************************
			RGXDestroyRenderTarget
 *******************************************/

/* Bridge in structure for RGXDestroyRenderTarget */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYRENDERTARGET_TAG
{
	IMG_HANDLE hsRenderTargetMemDesc;
} PVRSRV_BRIDGE_IN_RGXDESTROYRENDERTARGET;


/* Bridge out structure for RGXDestroyRenderTarget */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYRENDERTARGET_TAG
{
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXDESTROYRENDERTARGET;

/*******************************************
			RGXCreateZSBuffer
 *******************************************/

/* Bridge in structure for RGXCreateZSBuffer */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATEZSBUFFER_TAG
{
	IMG_HANDLE hDevNode;
	IMG_HANDLE hReservation;
	IMG_HANDLE hPMR;
	PVRSRV_MEMALLOCFLAGS_T uiMapFlags;
} PVRSRV_BRIDGE_IN_RGXCREATEZSBUFFER;


/* Bridge out structure for RGXCreateZSBuffer */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATEZSBUFFER_TAG
{
	IMG_HANDLE hsZSBufferKM;
	IMG_UINT32 ui32sZSBufferFWDevVAddr;
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXCREATEZSBUFFER;

/*******************************************
			RGXDestroyZSBuffer
 *******************************************/

/* Bridge in structure for RGXDestroyZSBuffer */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYZSBUFFER_TAG
{
	IMG_HANDLE hsZSBufferMemDesc;
} PVRSRV_BRIDGE_IN_RGXDESTROYZSBUFFER;


/* Bridge out structure for RGXDestroyZSBuffer */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYZSBUFFER_TAG
{
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXDESTROYZSBUFFER;

/*******************************************
			RGXPopulateZSBuffer
 *******************************************/

/* Bridge in structure for RGXPopulateZSBuffer */
typedef struct PVRSRV_BRIDGE_IN_RGXPOPULATEZSBUFFER_TAG
{
	IMG_HANDLE hsZSBufferKM;
} PVRSRV_BRIDGE_IN_RGXPOPULATEZSBUFFER;


/* Bridge out structure for RGXPopulateZSBuffer */
typedef struct PVRSRV_BRIDGE_OUT_RGXPOPULATEZSBUFFER_TAG
{
	IMG_HANDLE hsPopulation;
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXPOPULATEZSBUFFER;

/*******************************************
			RGXUnpopulateZSBuffer
 *******************************************/

/* Bridge in structure for RGXUnpopulateZSBuffer */
typedef struct PVRSRV_BRIDGE_IN_RGXUNPOPULATEZSBUFFER_TAG
{
	IMG_HANDLE hsPopulation;
} PVRSRV_BRIDGE_IN_RGXUNPOPULATEZSBUFFER;


/* Bridge out structure for RGXUnpopulateZSBuffer */
typedef struct PVRSRV_BRIDGE_OUT_RGXUNPOPULATEZSBUFFER_TAG
{
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXUNPOPULATEZSBUFFER;

/*******************************************
			RGXCreateFreeList
 *******************************************/

/* Bridge in structure for RGXCreateFreeList */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATEFREELIST_TAG
{
	IMG_HANDLE hDevNode;
	IMG_UINT32 ui32ui32MaxFLPages;
	IMG_UINT32 ui32ui32InitFLPages;
	IMG_UINT32 ui32ui32GrowFLPages;
	IMG_BOOL bbFreeListCheck;
	IMG_DEV_VIRTADDR spsFreeListDevVAddr;
	IMG_HANDLE hsFreeListPMR;
	IMG_DEVMEM_OFFSET_T uiPMROffset;
} PVRSRV_BRIDGE_IN_RGXCREATEFREELIST;


/* Bridge out structure for RGXCreateFreeList */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATEFREELIST_TAG
{
	IMG_HANDLE hCleanupCookie;
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXCREATEFREELIST;

/*******************************************
			RGXDestroyFreeList
 *******************************************/

/* Bridge in structure for RGXDestroyFreeList */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYFREELIST_TAG
{
	IMG_HANDLE hCleanupCookie;
} PVRSRV_BRIDGE_IN_RGXDESTROYFREELIST;


/* Bridge out structure for RGXDestroyFreeList */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYFREELIST_TAG
{
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXDESTROYFREELIST;

/*******************************************
			RGXAddBlockToFreeList
 *******************************************/

/* Bridge in structure for RGXAddBlockToFreeList */
typedef struct PVRSRV_BRIDGE_IN_RGXADDBLOCKTOFREELIST_TAG
{
	IMG_HANDLE hsFreeList;
	IMG_UINT32 ui3232NumPages;
} PVRSRV_BRIDGE_IN_RGXADDBLOCKTOFREELIST;


/* Bridge out structure for RGXAddBlockToFreeList */
typedef struct PVRSRV_BRIDGE_OUT_RGXADDBLOCKTOFREELIST_TAG
{
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXADDBLOCKTOFREELIST;

/*******************************************
			RGXRemoveBlockFromFreeList
 *******************************************/

/* Bridge in structure for RGXRemoveBlockFromFreeList */
typedef struct PVRSRV_BRIDGE_IN_RGXREMOVEBLOCKFROMFREELIST_TAG
{
	IMG_HANDLE hsFreeList;
} PVRSRV_BRIDGE_IN_RGXREMOVEBLOCKFROMFREELIST;


/* Bridge out structure for RGXRemoveBlockFromFreeList */
typedef struct PVRSRV_BRIDGE_OUT_RGXREMOVEBLOCKFROMFREELIST_TAG
{
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXREMOVEBLOCKFROMFREELIST;

/*******************************************
			RGXCreateRenderContext
 *******************************************/

/* Bridge in structure for RGXCreateRenderContext */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATERENDERCONTEXT_TAG
{
	IMG_HANDLE hDevNode;
	IMG_UINT32 ui32Priority;
	IMG_DEV_VIRTADDR sMCUFenceAddr;
	IMG_DEV_VIRTADDR sVDMCallStackAddr;
	IMG_UINT32 ui32FrameworkCmdize;
	IMG_BYTE * psFrameworkCmd;
	IMG_HANDLE hPrivData;
} PVRSRV_BRIDGE_IN_RGXCREATERENDERCONTEXT;


/* Bridge out structure for RGXCreateRenderContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATERENDERCONTEXT_TAG
{
	IMG_HANDLE hRenderContext;
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXCREATERENDERCONTEXT;

/*******************************************
			RGXDestroyRenderContext
 *******************************************/

/* Bridge in structure for RGXDestroyRenderContext */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYRENDERCONTEXT_TAG
{
	IMG_HANDLE hCleanupCookie;
} PVRSRV_BRIDGE_IN_RGXDESTROYRENDERCONTEXT;


/* Bridge out structure for RGXDestroyRenderContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYRENDERCONTEXT_TAG
{
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXDESTROYRENDERCONTEXT;

/*******************************************
			RGXKickTA3D
 *******************************************/

/* Bridge in structure for RGXKickTA3D */
typedef struct PVRSRV_BRIDGE_IN_RGXKICKTA3D_TAG
{
	IMG_HANDLE hRenderContext;
	IMG_UINT32 ui32ClientTAFenceCount;
	PRGXFWIF_UFO_ADDR * psClientTAFenceUFOAddress;
	IMG_UINT32 * pui32ClientTAFenceValue;
	IMG_UINT32 ui32ClientTAUpdateCount;
	PRGXFWIF_UFO_ADDR * psClientTAUpdateUFOAddress;
	IMG_UINT32 * pui32ClientTAUpdateValue;
	IMG_UINT32 ui32ServerTASyncPrims;
	IMG_UINT32 * pui32ServerTASyncFlags;
	IMG_HANDLE * phServerTASyncs;
	IMG_UINT32 ui32Client3DFenceCount;
	PRGXFWIF_UFO_ADDR * psClient3DFenceUFOAddress;
	IMG_UINT32 * pui32Client3DFenceValue;
	IMG_UINT32 ui32Client3DUpdateCount;
	PRGXFWIF_UFO_ADDR * psClient3DUpdateUFOAddress;
	IMG_UINT32 * pui32Client3DUpdateValue;
	IMG_UINT32 ui32Server3DSyncPrims;
	IMG_UINT32 * pui32Server3DSyncFlags;
	IMG_HANDLE * phServer3DSyncs;
	PRGXFWIF_UFO_ADDR sPRFenceUFOAddress;
	IMG_UINT32 ui32FRFenceValue;
	IMG_UINT32 ui32NumFenceFds;
	IMG_INT32 * pi32FenceFds;
	IMG_UINT32 ui32TACmdSize;
	IMG_BYTE * psTACmd;
	IMG_UINT32 ui323DPRCmdSize;
	IMG_BYTE * ps3DPRCmd;
	IMG_UINT32 ui323DCmdSize;
	IMG_BYTE * ps3DCmd;
	IMG_UINT32 ui32TAFrameNum;
	IMG_UINT32 ui32TARTData;
	IMG_BOOL bbLastTAInScene;
	IMG_BOOL bbKickTA;
	IMG_BOOL bbKickPR;
	IMG_BOOL bbKick3D;
	IMG_BOOL bbAbort;
	IMG_BOOL bbPDumpContinuous;
	IMG_HANDLE hRTDataCleanup;
	IMG_HANDLE hZBuffer;
	IMG_HANDLE hSBuffer;
	IMG_BOOL bbCommitRefCountsTA;
	IMG_BOOL bbCommitRefCounts3D;
} PVRSRV_BRIDGE_IN_RGXKICKTA3D;


/* Bridge out structure for RGXKickTA3D */
typedef struct PVRSRV_BRIDGE_OUT_RGXKICKTA3D_TAG
{
	IMG_BOOL bbCommittedRefCountsTA;
	IMG_BOOL bbCommittedRefCounts3D;
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXKICKTA3D;

/*******************************************
			RGXSetRenderContextPriority
 *******************************************/

/* Bridge in structure for RGXSetRenderContextPriority */
typedef struct PVRSRV_BRIDGE_IN_RGXSETRENDERCONTEXTPRIORITY_TAG
{
	IMG_HANDLE hRenderContext;
	IMG_UINT32 ui32Priority;
} PVRSRV_BRIDGE_IN_RGXSETRENDERCONTEXTPRIORITY;


/* Bridge out structure for RGXSetRenderContextPriority */
typedef struct PVRSRV_BRIDGE_OUT_RGXSETRENDERCONTEXTPRIORITY_TAG
{
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXSETRENDERCONTEXTPRIORITY;

/*******************************************
			RGXGetLastRenderContextResetReason
 *******************************************/

/* Bridge in structure for RGXGetLastRenderContextResetReason */
typedef struct PVRSRV_BRIDGE_IN_RGXGETLASTRENDERCONTEXTRESETREASON_TAG
{
	IMG_HANDLE hRenderContext;
} PVRSRV_BRIDGE_IN_RGXGETLASTRENDERCONTEXTRESETREASON;


/* Bridge out structure for RGXGetLastRenderContextResetReason */
typedef struct PVRSRV_BRIDGE_OUT_RGXGETLASTRENDERCONTEXTRESETREASON_TAG
{
	IMG_UINT32 ui32LastResetReason;
	PVRSRV_ERROR eError;
} PVRSRV_BRIDGE_OUT_RGXGETLASTRENDERCONTEXTRESETREASON;

#endif /* COMMON_RGXTA3D_BRIDGE_H */
