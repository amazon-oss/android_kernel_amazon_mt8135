/*************************************************************************/ /*!
@File
@Title          PVR Common Bridge Module (kernel side)
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements core PVRSRV API, server side
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

#include "img_defs.h"
#include "pvr_debug.h"
#include "ra.h"
#include "pvr_bridge.h"
#include "connection_server.h"
#include "device.h"

#include "pdump_km.h"

#if defined LINUX
#include "env_data.h"  
#endif

#include "srvkm.h"
#include "allocmem.h"
#include "devicemem.h"

#include "srvcore.h"
#include "pvrsrv.h"
#include "power.h"
#include "lists.h"

#include "rgx_options_km.h"
#include "pvrversion.h"

/* For the purpose of maintainability, it is intended that this file should not
 * contain any OS specific #ifdefs. Please find a way to add e.g.
 * an osfunc.c abstraction or override the entire function in question within
 * env,*,pvr_bridge_k.c
 */

PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY g_BridgeDispatchTable[BRIDGE_DISPATCH_TABLE_ENTRY_COUNT];

#if defined(DEBUG_BRIDGE_KM)
PVRSRV_BRIDGE_GLOBAL_STATS g_BridgeGlobalStats;
#endif



#if defined(DEBUG_BRIDGE_KM)
PVRSRV_ERROR
CopyFromUserWrapper(CONNECTION_DATA *psConnection,
					IMG_UINT32 ui32BridgeID,
					IMG_VOID *pvDest,
					IMG_VOID *pvSrc,
					IMG_UINT32 ui32Size)
{
	g_BridgeDispatchTable[ui32BridgeID].ui32CopyFromUserTotalBytes+=ui32Size;
	g_BridgeGlobalStats.ui32TotalCopyFromUserBytes+=ui32Size;
	return OSCopyFromUser(psConnection, pvDest, pvSrc, ui32Size);
}
PVRSRV_ERROR
CopyToUserWrapper(CONNECTION_DATA *psConnection,
				  IMG_UINT32 ui32BridgeID,
				  IMG_VOID *pvDest,
				  IMG_VOID *pvSrc,
				  IMG_UINT32 ui32Size)
{
	g_BridgeDispatchTable[ui32BridgeID].ui32CopyToUserTotalBytes+=ui32Size;
	g_BridgeGlobalStats.ui32TotalCopyToUserBytes+=ui32Size;
	return OSCopyToUser(psConnection, pvDest, pvSrc, ui32Size);
}
#endif

PVRSRV_ERROR
PVRSRVConnectKM(CONNECTION_DATA *psConnection,
				IMG_UINT32 ui32Flags,
				IMG_UINT32 ui32ClientBuildOptions,
				IMG_UINT32 ui32ClientDDKVersion,
				IMG_UINT32 ui32ClientDDKBuild,
				IMG_UINT8  *pui8KernelArch,
				IMG_UINT32 *ui32Log2PageSize)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	IMG_UINT32			ui32BuildOptions, ui32BuildOptionsMismatch;
	IMG_UINT32			ui32DDKVersion, ui32DDKBuild;
	
	*ui32Log2PageSize = GET_LOG2_PAGESIZE();

	PVR_UNREFERENCED_PARAMETER(ui32Flags);
	if(ui32Flags & SRV_FLAGS_INIT_PROCESS)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: Connecting as init process", __func__));
		if ((OSProcHasPrivSrvInit() == IMG_FALSE) || PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RUNNING) || PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RAN))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Rejecting init process", __func__));
			eError = PVRSRV_ERROR_SRV_CONNECT_FAILED;
			goto chk_exit;
		}
#if defined (__linux__)
		PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_RUNNING, IMG_TRUE);
#endif
	}
	else
	{
		if(PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RAN))
		{
			if (!PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_SUCCESSFUL))
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Initialisation failed.  Driver unusable.",
					__FUNCTION__));
				eError = PVRSRV_ERROR_INIT_FAILURE;
				goto chk_exit;
			}
		}
		else
		{
			if(PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RUNNING))
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Initialisation is in progress",
						 __FUNCTION__));
				eError = PVRSRV_ERROR_RETRY;
				goto chk_exit;
			}
			else
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Driver initialisation not completed yet.",
						 __FUNCTION__));
				eError = PVRSRV_ERROR_RETRY;
				goto chk_exit;
			}
		}
	}
	ui32ClientBuildOptions &= RGX_BUILD_OPTIONS_MASK_KM;
	/*
	 * Validate the build options
	 */
	ui32BuildOptions = (RGX_BUILD_OPTIONS_KM);
	if (ui32BuildOptions != ui32ClientBuildOptions)
	{
		ui32BuildOptionsMismatch = ui32BuildOptions ^ ui32ClientBuildOptions;
		if ( (ui32ClientBuildOptions & ui32BuildOptionsMismatch) != 0)
		{
			PVR_LOG(("(FAIL) %s: Mismatch in client-side and KM driver build options; "
				"extra options present in client-side driver: (0x%x). Please check rgx_options.h",
				__FUNCTION__,
				ui32ClientBuildOptions & ui32BuildOptionsMismatch ));
		}

		if ( (ui32BuildOptions & ui32BuildOptionsMismatch) != 0)
		{
			PVR_LOG(("(FAIL) %s: Mismatch in client-side and KM driver build options; "
				"extra options present in KM driver: (0x%x). Please check rgx_options.h",
				__FUNCTION__,
				ui32BuildOptions & ui32BuildOptionsMismatch ));
		}
		eError = PVRSRV_ERROR_BUILD_OPTIONS_MISMATCH;
		goto chk_exit;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: COMPAT_TEST: Client-side and KM driver build options match. [ OK ]", __FUNCTION__));
	}

	/*
	 * Validate DDK version
	 */
	ui32DDKVersion = PVRVERSION_PACK(PVRVERSION_MAJ, PVRVERSION_MIN);
	if (ui32ClientDDKVersion != ui32DDKVersion)
	{
		PVR_LOG(("(FAIL) %s: Incompatible driver DDK revision (%u.%u) / client DDK revision (%u.%u).",
				__FUNCTION__,
				PVRVERSION_MAJ, PVRVERSION_MIN,
				PVRVERSION_UNPACK_MAJ(ui32ClientDDKVersion),
				PVRVERSION_UNPACK_MIN(ui32ClientDDKVersion)));
		eError = PVRSRV_ERROR_DDK_VERSION_MISMATCH;
		PVR_DBG_BREAK;
		goto chk_exit;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: COMPAT_TEST: driver DDK revision (%u.%u) and client DDK revision (%u.%u) match. [ OK ]",
				__FUNCTION__,
				PVRVERSION_MAJ, PVRVERSION_MIN, PVRVERSION_MAJ, PVRVERSION_MIN));
	}
	
	/*
	 * Validate DDK build
	 */
	ui32DDKBuild = PVRVERSION_BUILD;
	if (ui32ClientDDKBuild != ui32DDKBuild)
	{
		PVR_LOG(("(FAIL) %s: Incompatible driver DDK build (%d) / client DDK build (%d).",
				__FUNCTION__, ui32DDKBuild, ui32ClientDDKBuild));
		eError = PVRSRV_ERROR_DDK_BUILD_MISMATCH;
		PVR_DBG_BREAK;
		goto chk_exit;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: COMPAT_TEST: driver DDK build (%d) and client DDK build (%d) match. [ OK ]",
				__FUNCTION__, ui32DDKBuild, ui32ClientDDKBuild));
	}

	/* Success so far so is it the PDump client that is connecting? */
	if (ui32Flags & SRV_FLAGS_PDUMPCTRL)
	{
		PDumpConnectionNotify();
	}

	PVR_ASSERT(pui8KernelArch != NULL);
	/* Can't use __SIZEOF_POINTER__ here as it is not defined on WDDM */
	if (sizeof(IMG_PVOID) == 8)
	{
		*pui8KernelArch = 64;
	}
	else
	{
		*pui8KernelArch = 32;
	}

	if (ui32Flags & SRV_FLAGS_INIT_PROCESS)
	{
		psConnection->bInitProcess = IMG_TRUE;
	}

chk_exit:
	return eError;
}

PVRSRV_ERROR
PVRSRVDisconnectKM(IMG_VOID)
{
	/* just return OK, per-process data is cleaned up by resmgr */

	return PVRSRV_OK;
}

/*
	PVRSRVDumpDebugInfoKM
*/
PVRSRV_ERROR
PVRSRVDumpDebugInfoKM(IMG_UINT32 ui32VerbLevel)
{
	if (ui32VerbLevel > DEBUG_REQUEST_VERBOSITY_MAX)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	PVR_LOG(("User requested PVR debug info"));

	PVRSRVDebugRequest(ui32VerbLevel, IMG_NULL);
									   
	return PVRSRV_OK;
}

/*
	PVRSRVGetDevClockSpeedKM
*/
PVRSRV_ERROR
PVRSRVGetDevClockSpeedKM(PVRSRV_DEVICE_NODE *psDeviceNode,
						 IMG_PUINT32  pui32RGXClockSpeed)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVR_ASSERT(psDeviceNode->pfnDeviceClockSpeed != IMG_NULL);

	eError = psDeviceNode->pfnDeviceClockSpeed(psDeviceNode, pui32RGXClockSpeed);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "PVRSRVGetDevClockSpeedKM: "
				"Could not get device clock speed (%d)!",
				eError));
	}

	return eError;
}


/*
	PVRSRVHWOpTimeoutKM
*/
PVRSRV_ERROR
PVRSRVHWOpTimeoutKM(IMG_VOID)
{
#if defined(PVRSRV_RESET_ON_HWTIMEOUT)
	PVR_LOG(("User requested OS reset"));
	OSPanic();
#endif
	PVR_LOG(("HW operation timeout, dump server info"));
	PVRSRVDebugRequest(DEBUG_REQUEST_VERBOSITY_MEDIUM, IMG_NULL);
	return PVRSRV_OK;
}

IMG_INT
DummyBW(IMG_UINT32 ui32BridgeID,
		IMG_VOID *psBridgeIn,
		IMG_VOID *psBridgeOut,
		CONNECTION_DATA *psConnection)
{
#if !defined(DEBUG)
	PVR_UNREFERENCED_PARAMETER(ui32BridgeID);
#endif
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);
	PVR_UNREFERENCED_PARAMETER(psBridgeOut);
	PVR_UNREFERENCED_PARAMETER(psConnection);

#if defined(DEBUG_BRIDGE_KM)
	PVR_DPF((PVR_DBG_ERROR, "%s: BRIDGE ERROR: BridgeID %u (%s) mapped to "
			 "Dummy Wrapper (probably not what you want!)",
			 __FUNCTION__, ui32BridgeID, g_BridgeDispatchTable[ui32BridgeID].pszIOCName));
#else
	PVR_DPF((PVR_DBG_ERROR, "%s: BRIDGE ERROR: BridgeID %u mapped to "
			 "Dummy Wrapper (probably not what you want!)",
			 __FUNCTION__, ui32BridgeID));
#endif
	return -ENOTTY;
}


/*
	PVRSRVSoftResetKM
*/
PVRSRV_ERROR
PVRSRVSoftResetKM(PVRSRV_DEVICE_NODE *psDeviceNode, IMG_UINT64 ui64ResetValue)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if ((psDeviceNode == IMG_NULL) || (psDeviceNode->pfnSoftReset == IMG_NULL))
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = psDeviceNode->pfnSoftReset(psDeviceNode, ui64ResetValue);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "PVRSRVSoftResetKM: "
				"Failed to soft reset (error %d)",
				eError));
	}

	return eError;
}


/*!
 * *****************************************************************************
 * @brief A wrapper for filling in the g_BridgeDispatchTable array that does
 * 		  error checking.
 *
 * @param ui32Index
 * @param pszIOCName
 * @param pfFunction
 * @param pszFunctionName
 *
 * @return
 ********************************************************************************/
IMG_VOID
_SetDispatchTableEntry(IMG_UINT32 ui32Index,
					   const IMG_CHAR *pszIOCName,
					   BridgeWrapperFunction pfFunction,
					   const IMG_CHAR *pszFunctionName)
{
	static IMG_UINT32 ui32PrevIndex = IMG_UINT32_MAX;		/* -1 */
#if !defined(DEBUG)
	PVR_UNREFERENCED_PARAMETER(pszIOCName);
#endif
#if !defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE) && !defined(DEBUG_BRIDGE_KM)
	PVR_UNREFERENCED_PARAMETER(pszFunctionName);
#endif

#if defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE)
	/* Enable this to dump out the dispatch table entries */
	PVR_DPF((PVR_DBG_WARNING, "%s: %d %s %s", __FUNCTION__, ui32Index, pszIOCName, pszFunctionName));
#endif

	/* Any gaps are sub-optimal in-terms of memory usage, but we are mainly
	 * interested in spotting any large gap of wasted memory that could be
	 * accidentally introduced.
	 *
	 * This will currently flag up any gaps > 5 entries.
	 *
	 * NOTE: This shouldn't be debug only since switching from debug->release
	 * etc is likely to modify the available ioctls and thus be a point where
	 * mistakes are exposed. This isn't run at at a performance critical time.
	 */
	if((ui32PrevIndex != IMG_UINT32_MAX) &&
	   ((ui32Index >= ui32PrevIndex + DISPATCH_TABLE_GAP_THRESHOLD) ||
		(ui32Index <= ui32PrevIndex)))
	{
#if defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE)
		PVR_DPF((PVR_DBG_WARNING,
				 "%s: There is a gap in the dispatch table between indices %u (%s) and %u (%s)",
				 __FUNCTION__, ui32PrevIndex, g_BridgeDispatchTable[ui32PrevIndex].pszIOCName,
				 ui32Index, pszIOCName));
#else
		PVR_DPF((PVR_DBG_MESSAGE,
				 "%s: There is a gap in the dispatch table between indices %u and %u (%s)",
				 __FUNCTION__, (IMG_UINT)ui32PrevIndex, (IMG_UINT)ui32Index, pszIOCName));
#endif
#if !defined(PVRSRV_ALLOW_BRIDGE_DISPATCH_TABLE_GAPS)
		/* In WDDM we do not register all the bridge modules and therefore
		 * it is natural to have gaps, hence do not panic.
		 */
		PVR_DPF((PVR_DBG_ERROR, "NOTE: Enabling DEBUG_BRIDGE_KM_DISPATCH_TABLE may help debug this issue."));
		OSPanic();
#endif
	}

	if (ui32Index >= BRIDGE_DISPATCH_TABLE_ENTRY_COUNT)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Index %u (%s) out of range",
				 __FUNCTION__, (IMG_UINT)ui32Index, pszIOCName));
		OSPanic();
	}

	/* Panic if the previous entry has been overwritten as this is not allowed!
	 * NOTE: This shouldn't be debug only since switching from debug->release
	 * etc is likely to modify the available ioctls and thus be a point where
	 * mistakes are exposed. This isn't run at at a performance critical time.
	 */
	if(g_BridgeDispatchTable[ui32Index].pfFunction)
	{
#if defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE)
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: BUG!: Adding dispatch table entry for %s clobbers an existing entry for %s",
				 __FUNCTION__, pszIOCName, g_BridgeDispatchTable[ui32Index].pszIOCName));
#else
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: BUG!: Adding dispatch table entry for %s clobbers an existing entry (index=%u)",
				 __FUNCTION__, pszIOCName, ui32Index));
#endif
		PVR_DPF((PVR_DBG_ERROR, "NOTE: Enabling DEBUG_BRIDGE_KM_DISPATCH_TABLE may help debug this issue."));
		OSPanic();
	}

	g_BridgeDispatchTable[ui32Index].pfFunction = pfFunction;
#if defined(DEBUG_BRIDGE_KM)
	g_BridgeDispatchTable[ui32Index].pszIOCName = pszIOCName;
	g_BridgeDispatchTable[ui32Index].pszFunctionName = pszFunctionName;
	g_BridgeDispatchTable[ui32Index].ui32CallCount = 0;
	g_BridgeDispatchTable[ui32Index].ui32CopyFromUserTotalBytes = 0;
#endif

	ui32PrevIndex = ui32Index;
}

PVRSRV_ERROR
PVRSRVInitSrvDisconnectKM(CONNECTION_DATA *psConnection,
							IMG_BOOL bInitSuccesful,
							IMG_UINT32 ui32ClientBuildOptions)
{
	PVRSRV_ERROR eError;

	if(!psConnection->bInitProcess)
	{
		return PVRSRV_ERROR_SRV_DISCONNECT_FAILED;
	}

	psConnection->bInitProcess = IMG_FALSE;

	PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_RUNNING, IMG_FALSE);
	PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_RAN, IMG_TRUE);

	eError = PVRSRVFinaliseSystem(bInitSuccesful, ui32ClientBuildOptions);

	PVRSRVSetInitServerState( PVRSRV_INIT_SERVER_SUCCESSFUL ,
				(eError == PVRSRV_OK) && bInitSuccesful);

	return eError;
}

IMG_INT BridgedDispatchKM(CONNECTION_DATA * psConnection,
					  PVRSRV_BRIDGE_PACKAGE   * psBridgePackageKM)
{

	IMG_VOID   * psBridgeIn;
	IMG_VOID   * psBridgeOut;
	BridgeWrapperFunction pfBridgeHandler;
	IMG_UINT32   ui32BridgeID = psBridgePackageKM->ui32BridgeID;
	IMG_INT      err          = -EFAULT;

#if defined(DEBUG_BRIDGE_KM_STOP_AT_DISPATCH)
	PVR_DBG_BREAK;
#endif
	
#if defined(DEBUG_BRIDGE_KM)
	PVR_DPF((PVR_DBG_MESSAGE, "%s: %s",
			 __FUNCTION__,
			 g_BridgeDispatchTable[ui32BridgeID].pszIOCName));
	g_BridgeDispatchTable[ui32BridgeID].ui32CallCount++;
	g_BridgeGlobalStats.ui32IOCTLCount++;
#endif

#if defined(__linux__)
	{
		
		ENV_DATA *psEnvData = OSGetEnvData();

		/* We have already set up some static buffers to store our ioctl data... */
		psBridgeIn = psEnvData->pvBridgeData;
		psBridgeOut = (IMG_PVOID)((IMG_PBYTE)psBridgeIn + PVRSRV_MAX_BRIDGE_IN_SIZE);

		if (psBridgePackageKM->ui32InBufferSize > PVRSRV_MAX_BRIDGE_IN_SIZE)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Bridge input buffer too small "
			        "(data size %u, buffer size %u)!", __FUNCTION__,
			        psBridgePackageKM->ui32InBufferSize, PVRSRV_MAX_BRIDGE_IN_SIZE));
			err = PVRSRV_ERROR_BUFFER_TOO_SMALL;
			goto return_fault;
		}

		if (psBridgePackageKM->ui32OutBufferSize > PVRSRV_MAX_BRIDGE_OUT_SIZE)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Bridge output buffer too small "
			        "(data size %u, buffer size %u)!", __FUNCTION__,
			        psBridgePackageKM->ui32InBufferSize, PVRSRV_MAX_BRIDGE_IN_SIZE));
			err = PVRSRV_ERROR_BUFFER_TOO_SMALL;
			goto return_fault;
		}

		if(psBridgePackageKM->ui32InBufferSize > 0)
		{
			if(!OSAccessOK(PVR_VERIFY_READ,
							psBridgePackageKM->pvParamIn,
							psBridgePackageKM->ui32InBufferSize))
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Invalid pvParamIn pointer", __FUNCTION__));
			}

			if(CopyFromUserWrapper(psConnection,
					               ui32BridgeID,
								   psBridgeIn,
								   psBridgePackageKM->pvParamIn,
								   psBridgePackageKM->ui32InBufferSize)
			  != PVRSRV_OK)
			{
				goto return_fault;
			}
		}
	}
#else
	psBridgeIn  = psBridgePackageKM->pvParamIn;
	psBridgeOut = psBridgePackageKM->pvParamOut;
#endif

	if(ui32BridgeID >= (BRIDGE_DISPATCH_TABLE_ENTRY_COUNT))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: ui32BridgeID = %d is out if range!",
				 __FUNCTION__, ui32BridgeID));
		goto return_fault;
	}
	pfBridgeHandler =
		(BridgeWrapperFunction)g_BridgeDispatchTable[ui32BridgeID].pfFunction;
	
	if (pfBridgeHandler == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: ui32BridgeID = %d is not a registered function!",
				 __FUNCTION__, ui32BridgeID));
		goto return_fault;
	}
	
	err = pfBridgeHandler(ui32BridgeID,
						  psBridgeIn,
						  psBridgeOut,
						  psConnection);
	if(err < 0)
	{
		goto return_fault;
	}


#if defined(__linux__)
	/* 
	   This should always be true as a.t.m. all bridge calls have to
	   return an error message, but this could change so we do this
	   check to be safe.
	*/
	if(psBridgePackageKM->ui32OutBufferSize > 0)
	{
		
		if(CopyToUserWrapper(psConnection,
							 ui32BridgeID,
							 psBridgePackageKM->pvParamOut,
							 psBridgeOut,
							 psBridgePackageKM->ui32OutBufferSize)
		   != PVRSRV_OK)
		{
			goto return_fault;
		}
	}
#endif

	err = 0;

return_fault:
	return err;
}
