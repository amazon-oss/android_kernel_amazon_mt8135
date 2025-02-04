/*************************************************************************/ /*!
@File
@Title          core services functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Main APIs for core services functions
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

#include "rgxdebug.h"
#include "handle.h"
#include "connection_server.h"
#include "pdump_km.h"
#include "ra.h"
#include "allocmem.h"
#include "pmr.h"
#include "dc_server.h"
#include "pvrsrv.h"
#include "pvrsrv_device.h"
#include "pvr_debug.h"
#include "sync.h"
#include "sync_server.h"
#include "devicemem.h"

#include "pvrversion.h"

#include "lists.h"
#include "dllist.h"
#include "syscommon.h"

#include "physmem_lma.h"
#include "physmem_osmem.h"

#include "tlintern.h"

#if defined (SUPPORT_RGX)
#include "rgxinit.h"
#endif

#include "debug_request_ids.h"
#include "pvrsrv.h"

#include "mtk_pp.h"

#if defined(PVR_RI_DEBUG)
#include "ri_server.h"
#endif

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#include "process_stats.h"
#endif
/*! Wait 100ms before retrying deferred clean-up again */
#define CLEANUP_THREAD_WAIT_RETRY_TIMEOUT 0x00000064

/*! Wait 8hrs when no deferred clean-up required. Allows a poll several times
 * a day to check for any missed clean-up. */
#define CLEANUP_THREAD_WAIT_SLEEP_TIMEOUT 0x01B77400


typedef struct DEBUG_REQUEST_ENTRY_TAG
{
	IMG_UINT32		ui32RequesterID;
	DLLIST_NODE		sListHead;
} DEBUG_REQUEST_ENTRY;

typedef struct DEBUG_REQUEST_TABLE_TAG
{
	IMG_UINT32				ui32RequestCount;
	DEBUG_REQUEST_ENTRY		asEntry[1];
}DEBUG_REQUEST_TABLE;

static PVRSRV_DATA	*gpsPVRSRVData = IMG_NULL;
static IMG_HANDLE   g_hDbgSysNotify;

static PVRSRV_SYSTEM_CONFIG *gpsSysConfig = IMG_NULL;

typedef PVRSRV_ERROR (*PFN_REGISTER_DEVICE)(PVRSRV_DEVICE_NODE *psDeviceNode);
typedef PVRSRV_ERROR (*PFN_UNREGISTER_DEVICE)(PVRSRV_DEVICE_NODE *psDeviceNode);

static PFN_REGISTER_DEVICE sRegisterDevice[PVRSRV_DEVICE_TYPE_LAST + 1];
static PFN_UNREGISTER_DEVICE sUnregisterDevice[PVRSRV_DEVICE_TYPE_LAST + 1];

static PVRSRV_ERROR IMG_CALLCONV PVRSRVRegisterDevice(PVRSRV_DEVICE_CONFIG *psDevConfig);
static PVRSRV_ERROR IMG_CALLCONV PVRSRVUnregisterDevice(PVRSRV_DEVICE_NODE *psDeviceNode);

static PVRSRV_ERROR PVRSRVRegisterDbgTable(IMG_UINT32 *paui32Table, IMG_UINT32 ui32Length, IMG_PVOID *phTable);
static IMG_VOID PVRSRVUnregisterDbgTable(IMG_PVOID hTable);

static IMG_VOID _SysDebugRequestNotify(PVRSRV_DBGREQ_HANDLE hDebugRequestHandle, IMG_UINT32 ui32VerbLevel);

IMG_UINT32	g_ui32InitFlags;

/* mark which parts of Services were initialised */
#define		INIT_DATA_ENABLE_PDUMPINIT	0x1U
#define		INIT_GLOBAL_RESMAN 0x2U

/* Head of the list of callbacks called when Cmd complete happens */
static DLLIST_NODE sCmdCompNotifyHead;
static POSWR_LOCK hNotifyLock = IMG_NULL;

/* Debug request table and lock */
static POSWR_LOCK g_hDbgNotifyLock = IMG_NULL;
static DEBUG_REQUEST_TABLE *g_psDebugTable;

static IMG_PVOID g_hDebugTable = IMG_NULL;

static IMG_UINT32 g_aui32DebugOrderTable[] = {
	DEBUG_REQUEST_SYS,
	DEBUG_REQUEST_RGX,
	DEBUG_REQUEST_DC,
	DEBUG_REQUEST_SERVERSYNC,
	DEBUG_REQUEST_ANDROIDSYNC
};

DUMPDEBUG_PRINTF_FUNC *g_pfnDumpDebugPrintf = IMG_NULL;
/*!
******************************************************************************

 @Function	AllocateDeviceID

 @Description

 allocates a device id from the pool of valid ids

 @input psPVRSRVData :	Services private data

 @input pui32DevID : device id to return

 @Return device id

******************************************************************************/
static PVRSRV_ERROR AllocateDeviceID(PVRSRV_DATA *psPVRSRVData, IMG_UINT32 *pui32DevID)
{
	SYS_DEVICE_ID* psDeviceWalker;
	SYS_DEVICE_ID* psDeviceEnd;

	psDeviceWalker = &psPVRSRVData->sDeviceID[0];
	psDeviceEnd = psDeviceWalker + SYS_DEVICE_COUNT;

	/* find a free ID */
	while (psDeviceWalker < psDeviceEnd)
	{
		if (!psDeviceWalker->bInUse)
		{
			psDeviceWalker->bInUse = IMG_TRUE;
			*pui32DevID = psDeviceWalker->uiID;

			return PVRSRV_OK;
		}
		psDeviceWalker++;
	}

	PVR_DPF((PVR_DBG_ERROR,"AllocateDeviceID: No free and valid device IDs available!"));

	/* Should never get here: sDeviceID[] may have been setup too small */
	PVR_ASSERT(psDeviceWalker < psDeviceEnd);

	return PVRSRV_ERROR_NO_FREE_DEVICEIDS_AVAILABLE;
}


/*!
******************************************************************************

 @Function	FreeDeviceID

 @Description

 frees a device id from the pool of valid ids

 @input psPVRSRVData :	Services private data

 @input ui32DevID : device id to free

 @Return device id

******************************************************************************/
static PVRSRV_ERROR FreeDeviceID(PVRSRV_DATA *psPVRSRVData, IMG_UINT32 ui32DevID)
{
	SYS_DEVICE_ID* psDeviceWalker;
	SYS_DEVICE_ID* psDeviceEnd;

	psDeviceWalker = &psPVRSRVData->sDeviceID[0];
	psDeviceEnd = psDeviceWalker + SYS_DEVICE_COUNT;

	/* find the ID to free */
	while (psDeviceWalker < psDeviceEnd)
	{
		/* if matching id and in use, free */
		if	(
				(psDeviceWalker->uiID == ui32DevID) &&
				(psDeviceWalker->bInUse)
			)
		{
			psDeviceWalker->bInUse = IMG_FALSE;
			return PVRSRV_OK;
		}
		psDeviceWalker++;
	}

	PVR_DPF((PVR_DBG_ERROR,"FreeDeviceID: no matching dev ID that is in use!"));

	/* should never get here */
	PVR_ASSERT(psDeviceWalker < psDeviceEnd);

	return PVRSRV_ERROR_INVALID_DEVICEID;
}


/*!
******************************************************************************
 @Function	PVRSRVEnumerateDevicesKM_ForEachVaCb

 @Description

 Enumerates the device node (if is of the same class as given).

 @Input psDeviceNode	- The device node to be enumerated
 		va				- variable arguments list, with:
							pui32DevCount	- The device count pointer (to be increased)
							ppeDeviceType     - The pointer to the device type pointer (to be updated and increased)
							ppeDeviceClass    - The pointer to the device classes pointer (to be updated and increased)
							ppui32DeviceIndex - The pointer to the device indexes pointer (to be updated and increased)
******************************************************************************/
static IMG_VOID PVRSRVEnumerateDevicesKM_ForEachVaCb(PVRSRV_DEVICE_NODE *psDeviceNode, va_list va)
{
	IMG_UINT *pui32DevCount;
	PVRSRV_DEVICE_TYPE **ppeDeviceType;
	PVRSRV_DEVICE_CLASS **ppeDeviceClass;
	IMG_UINT32 **ppui32DeviceIndex;

	pui32DevCount = va_arg(va, IMG_UINT*);
	ppeDeviceType = va_arg(va, PVRSRV_DEVICE_TYPE**);
	ppeDeviceClass = va_arg(va, PVRSRV_DEVICE_CLASS**);
	ppui32DeviceIndex = va_arg(va, IMG_UINT32**);

	if (psDeviceNode->sDevId.eDeviceType != PVRSRV_DEVICE_TYPE_EXT)
	{
		**ppeDeviceType = psDeviceNode->sDevId.eDeviceType;
		**ppeDeviceClass = psDeviceNode->sDevId.eDeviceClass;
		**ppui32DeviceIndex = psDeviceNode->sDevId.ui32DeviceIndex;

		(*ppeDeviceType)++;
		(*ppeDeviceClass)++;
		(*ppui32DeviceIndex)++;

		(*pui32DevCount)++;
	}
}



/*!
******************************************************************************

 @Function PVRSRVEnumerateDevicesKM

 @Description
 This function will enumerate all the devices supported by the
 PowerVR services within the target system.
 The function returns a list of the device ID structures stored either in
 the services or constructed in the user mode glue component in certain
 environments. The number of devices in the list is also returned.

 In a binary layered component which does not support dynamic runtime selection,
 the glue code should compile to return the supported devices statically,
 e.g. multiple instances of the same device if multiple devices are supported,
 or the target combination of Rogue and display device.

 In the case of an environment (for instance) where one Rogue may connect to two
 display devices this code would enumerate all three devices and even
 non-dynamic Rogue selection code should retain the facility to parse the list
 to find the index of the Rogue device

 @output pui32NumDevices :	On success, contains the number of devices present
 							in the system

 @output peDeviceType	 :	Pointer to called supplied buffer to receive the
 							list of PVRSRV_DEVICE_TYPE

 @output peDeviceClass	 :	Pointer to called supplied buffer to receive the
 							list of PVRSRV_DEVICE_CLASS

 @output pui32DeviceIndex:	Pointer to called supplied buffer to receive the
 							list of device indexes

 @return PVRSRV_ERROR  :	

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVEnumerateDevicesKM(IMG_UINT32 *pui32NumDevices,
                                                   PVRSRV_DEVICE_TYPE *peDeviceType,
                                                   PVRSRV_DEVICE_CLASS *peDeviceClass,
                                                   IMG_UINT32 *pui32DeviceIndex)
{
	PVRSRV_DATA			*psPVRSRVData = PVRSRVGetPVRSRVData();
	IMG_UINT32 			i;

	if (!pui32NumDevices || !peDeviceType || !peDeviceClass || !pui32DeviceIndex)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVEnumerateDevicesKM: Invalid params"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/*
		setup input buffer to be `empty'
	*/
	for (i=0; i<PVRSRV_MAX_DEVICES; i++)
	{
		peDeviceType[i] = PVRSRV_DEVICE_TYPE_UNKNOWN;
	}

	/* and zero device count */
	*pui32NumDevices = 0;

	/*
		Search through the device list for services managed devices
		return id info for each device and the number of devices
		available
	*/
	List_PVRSRV_DEVICE_NODE_ForEach_va(psPVRSRVData->psDeviceNodeList,
									   &PVRSRVEnumerateDevicesKM_ForEachVaCb,
									   pui32NumDevices,
									   &peDeviceType,
									   &peDeviceClass,
									   &pui32DeviceIndex);

	return PVRSRV_OK;
}

// #define CLEANUP_DPFL PVR_DBG_WARNING
#define CLEANUP_DPFL    PVR_DBG_MESSAGE

static IMG_VOID CleanupThread(IMG_PVOID pvData)
{
	PVRSRV_DATA *psPVRSRVData = pvData;
	IMG_BOOL     bRetryCleanup = IMG_FALSE;
	IMG_HANDLE	 hOSEvent;
	PVRSRV_ERROR eRc;
	IMG_UINT64   ui64TimesliceLimit;

	/* Store the process id (pid) of the clean-up thread */
	psPVRSRVData->cleanupThreadPid = OSGetCurrentProcessIDKM();

	PVR_DPF((CLEANUP_DPFL, "CleanupThread: thread starting... "));

	/* Open an event on the clean up event object so we can listen on it,
	 * abort the clean up thread and driver if this fails.
	 */
	eRc = OSEventObjectOpen(psPVRSRVData->hCleanupEventObject, &hOSEvent);
	PVR_ASSERT(eRc == PVRSRV_OK);

	/* While the driver is in a good state and is not being unloaded
	 * try to free any deferred items when RESMAN signals
	 */
	while ((psPVRSRVData->eServicesState == PVRSRV_SERVICES_STATE_OK) && 
			(!psPVRSRVData->bUnload))
	{
		/* Wait until RESMAN signals for deferred clean up OR wait for a
		 * short period if the previous deferred clean up was not able
		 * to release all the resources before trying again.
		 * Bridge lock re-acquired on our behalf before the wait call returns.
		 */
		eRc = OSEventObjectWaitTimeout(hOSEvent, (bRetryCleanup) ?
				CLEANUP_THREAD_WAIT_RETRY_TIMEOUT :
				CLEANUP_THREAD_WAIT_SLEEP_TIMEOUT);
		if (eRc == PVRSRV_ERROR_TIMEOUT)
		{
			PVR_DPF((CLEANUP_DPFL, "CleanupThread: wait timeout"));
		}
		else if (eRc == PVRSRV_OK)
		{
			PVR_DPF((CLEANUP_DPFL, "CleanupThread: wait OK, signal received"));
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "CleanupThread: wait error %d", eRc));
		}

		/* Acquire the bridge lock to protect the flush of the deferred contexts
		 * and to guarantee the consistency of the deferred contexts list.
		 * In order to avoid to block the system during the cleanup the lock is
		 * released periodically every time a specific time expires.
		 */
		OSAcquireBridgeLock();

		/* Estimate the time limit as soon as we acquire the global lock */
		ui64TimesliceLimit = OSClockns64() + RESMAN_DEFERRED_CLEANUP_TIMESLICE_NS;

		/* Attempt to clean up all deferred contexts that may exist. If
		 * resources still need cleanup on exit bRetryCleanup set to true.
		 */
		bRetryCleanup = PVRSRVResManFlushDeferContext(
				psPVRSRVData->hResManDeferContext,
				ui64TimesliceLimit);

		/* Release the bridge lock after the cleanup of the defer context */
		OSReleaseBridgeLock();
	}

	eRc = OSEventObjectClose(hOSEvent);
	PVR_LOG_IF_ERROR(eRc, "OSEventObjectClose");

	PVR_DPF((CLEANUP_DPFL, "CleanupThread: thread ending... "));
}


static IMG_VOID DevicesWatchdogThread(IMG_PVOID pvData)
{
	PVRSRV_DATA *psPVRSRVData = pvData;
	PVRSRV_DEVICE_HEALTH_STATUS ePreviousHealthStatus = PVRSRV_DEVICE_HEALTH_STATUS_OK;
	IMG_HANDLE hOSEvent;
	PVRSRV_ERROR  eError;
	IMG_UINT32 ui32Timeout = DEVICES_WATCHDOG_POWER_ON_SLEEP_TIMEOUT;
	PVRSRV_DEV_POWER_STATE ePowerState;

	PVR_DPF((PVR_DBG_MESSAGE, "DevicesWatchdogThread: Power off sleep time: %d.",
			DEVICES_WATCHDOG_POWER_OFF_SLEEP_TIMEOUT));

	/* Open an event on the devices watchdog event object so we can listen on it
	   and abort the devices watchdog thread. */
	eError = OSEventObjectOpen(psPVRSRVData->hDevicesWatchdogEvObj, &hOSEvent);
	PVR_LOGRN_IF_ERROR(eError, "OSEventObjectOpen");

	/* Loop continuously checking the device status every few seconds. */
	while (!psPVRSRVData->bUnload)
	{
		IMG_UINT32 i;
		IMG_BOOL bPwrIsOn = IMG_FALSE;

		/* Wait time between polls (done at the start of the loop to allow devices
		   to initialise) or for the event signal (shutdown or power on). */
		eError = OSEventObjectWaitTimeout(hOSEvent, ui32Timeout);

#ifdef PVR_TESTING_UTILS
		psPVRSRVData->ui32DevicesWdWakeupCounter++;
#endif
		if (eError == PVRSRV_OK)
		{
			if (psPVRSRVData->bUnload)
			{
				PVR_DPF((PVR_DBG_MESSAGE, "DevicesWatchdogThread: Shutdown event received."));
				break;
			}
			else
			{
				PVR_DPF((PVR_DBG_MESSAGE, "DevicesWatchdogThread: Power state change event received."));
			}
		}
		else if (eError != PVRSRV_ERROR_TIMEOUT)
		{
			/* If timeout do nothing otherwise print warning message. */
			PVR_DPF((PVR_DBG_ERROR, "DevicesWatchdogThread: "
					"Error (%d) when waiting for event!", eError));
		}

		eError = PVRSRVPowerLock();
		if (eError == PVRSRV_ERROR_RETRY)
		{
			/* power lock cannot be acquired at this time (sys power is off) */
			ui32Timeout = psPVRSRVData->ui32DevicesWatchdogTimeout = DEVICES_WATCHDOG_POWER_OFF_SLEEP_TIMEOUT;
		}
		else if (eError != PVRSRV_OK)
		{
			/* any other error is unexpected */
			PVR_DPF((PVR_DBG_ERROR,"DevicesWatchdogThread: Failed to acquire power lock (%s)", PVRSRVGetErrorStringKM(eError)));
		}
		else
		{
			/* Check if at least one of the devices is on. */
			for (i = 0; i < psPVRSRVData->ui32RegisteredDevices && !bPwrIsOn; i++)
			{
				if (PVRSRVGetDevicePowerState(i, &ePowerState) == PVRSRV_OK)
				{
					bPwrIsOn = ePowerState == PVRSRV_DEV_POWER_STATE_ON;
					break;
				}
			}

			if (bPwrIsOn || psPVRSRVData->ui32DevicesWatchdogPwrTrans)
			{
				psPVRSRVData->ui32DevicesWatchdogPwrTrans = 0;
				ui32Timeout = psPVRSRVData->ui32DevicesWatchdogTimeout = DEVICES_WATCHDOG_POWER_ON_SLEEP_TIMEOUT;
			}
			else
			{
				ui32Timeout = psPVRSRVData->ui32DevicesWatchdogTimeout = DEVICES_WATCHDOG_POWER_OFF_SLEEP_TIMEOUT;
			}

			PVRSRVPowerUnlock();
		}

		for (i = 0;  i < psPVRSRVData->ui32RegisteredDevices; i++)
		{
			PVRSRV_DEVICE_NODE* psDeviceNode = psPVRSRVData->apsRegisteredDevNodes[i];
			PVRSRV_RGXDEV_INFO* psDevInfo = (PVRSRV_RGXDEV_INFO*) psDeviceNode->pvDevice;
			
			if (psDeviceNode->pfnUpdateHealthStatus != IMG_NULL)
			{
				eError = psDeviceNode->pfnUpdateHealthStatus(psDeviceNode, IMG_TRUE);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_WARNING, "DevicesWatchdogThread: "
							"Could not check for fatal error (%d)!",
							eError));
				}
			}

			if (psDeviceNode->eHealthStatus != PVRSRV_DEVICE_HEALTH_STATUS_OK)
			{
				if (psDeviceNode->eHealthStatus != ePreviousHealthStatus)
				{
					if (!(psDevInfo->ui32DeviceFlags & RGXKM_DEVICE_STATE_DISABLE_DW_LOGGING_EN))
					{
						PVR_DPF((PVR_DBG_ERROR, "DevicesWatchdogThread: Device not responding!!!"));
						PVRSRVDebugRequest(DEBUG_REQUEST_VERBOSITY_MAX, IMG_NULL);

						/* get the log */
						{
							static int triggered = 0;

							if (triggered == 0)
							{
								MTKPP_TriggerAEE();
							}

							triggered = 1;
						}
					}
				}
			}
			ePreviousHealthStatus = psDeviceNode->eHealthStatus;
			
			/* Attempt to service the HWPerf buffer to regularly transport 
			 * idle / periodic packets to host buffer. */
			if (psDeviceNode->pfnServiceHWPerf != IMG_NULL)
			{
				eError = psDeviceNode->pfnServiceHWPerf(psDeviceNode);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_WARNING, "DevicesWatchdogThread: "
							"Error occurred when servicing HWPerf buffer (%d)",
							eError));
				}
			}
		}
	}

	eError = OSEventObjectClose(hOSEvent);
	PVR_LOG_IF_ERROR(eError, "OSEventObjectClose");
}


PVRSRV_DATA *PVRSRVGetPVRSRVData()
{
	return gpsPVRSRVData;
}


PVRSRV_ERROR IMG_CALLCONV PVRSRVInit(IMG_VOID)
{
	PVRSRV_ERROR	eError;
	PVRSRV_SYSTEM_CONFIG *psSysConfig;
	IMG_UINT32 i;

#if defined (SUPPORT_RGX)
	
	sRegisterDevice[PVRSRV_DEVICE_TYPE_RGX] = RGXRegisterDevice;
#endif

	SET_LOG2_PAGESIZE(OSGetPageShift());

	eError = PhysHeapInit();
	if (eError != PVRSRV_OK)
	{
		goto Error;
	}

	/* Get the system config */
	eError = SysCreateConfigData(&psSysConfig);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	/* Save to global pointer for later */
	gpsSysConfig = psSysConfig;

    /*
     * Allocate the device-independent data
     */
    gpsPVRSRVData = OSAllocMem(sizeof(*gpsPVRSRVData));
    if (gpsPVRSRVData == IMG_NULL)
    {
        return PVRSRV_ERROR_OUT_OF_MEMORY;
    }
    OSMemSet(gpsPVRSRVData, 0, sizeof(*gpsPVRSRVData));
	gpsPVRSRVData->ui32NumDevices = psSysConfig->uiDeviceCount;

	for (i=0;i<SYS_DEVICE_COUNT;i++)
	{
		gpsPVRSRVData->sDeviceID[i].uiID = i;
		gpsPVRSRVData->sDeviceID[i].bInUse = IMG_FALSE;
	}

	/*
	 * Register the physical memory heaps
	 */
	PVR_ASSERT(psSysConfig->ui32PhysHeapCount <= SYS_PHYS_HEAP_COUNT);
	for (i=0;i<psSysConfig->ui32PhysHeapCount;i++)
	{
		eError = PhysHeapRegister(&psSysConfig->pasPhysHeaps[i],
								  &gpsPVRSRVData->apsRegisteredPhysHeaps[i]);
		if (eError != PVRSRV_OK)
		{
			goto Error;
		}
		gpsPVRSRVData->ui32RegisteredPhysHeaps++;
	}

	/* Init any OS specific's */
	eError = OSInitEnvData();
	if (eError != PVRSRV_OK)
	{
		goto Error;
	}

	/* Initialise Resource Manager */
	eError = ResManInit();
	if (eError != PVRSRV_OK)
	{
		goto Error;
	}

	eError = PVRSRVConnectionInit();
	if(eError != PVRSRV_OK)
	{
		goto Error;
	}

#if defined(PVR_RI_DEBUG)
	RIInitKM();
#endif

	eError = PMRInit();
	if (eError != PVRSRV_OK)
	{
		goto Error;
	}

#if !defined(UNDER_WDDM) && defined(SUPPORT_DISPLAY_CLASS)
	eError = DCInit();
	if (eError != PVRSRV_OK)
	{
		goto Error;
	}
#endif

	/* Initialise handles */
	eError = PVRSRVHandleInit();
	if(eError != PVRSRV_OK)
	{
		goto Error;
	}

	/* Initialise Power Manager Lock */
	eError = OSLockCreate(&gpsPVRSRVData->hPowerLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		goto Error;
	}

	/* Initialise system power state */
	gpsPVRSRVData->eCurrentPowerState = PVRSRV_SYS_POWER_STATE_ON;
	gpsPVRSRVData->eFailedPowerState = PVRSRV_SYS_POWER_STATE_Unspecified;

	/* Initialise overall system state */
	gpsPVRSRVData->eServicesState = PVRSRV_SERVICES_STATE_OK;

	/* Create an event object */
	eError = OSEventObjectCreate("PVRSRV_GLOBAL_EVENTOBJECT", &gpsPVRSRVData->hGlobalEventObject);
	if (eError != PVRSRV_OK)
	{
		goto Error;
	}
	gpsPVRSRVData->ui32GEOConsecutiveTimeouts = 0;

	/* initialise list of command complete notifiers */
	dllist_init(&sCmdCompNotifyHead);

	/* Create a lock of the list notifiers */
	eError = OSWRLockCreate(&hNotifyLock);
	if (eError != PVRSRV_OK)
	{
		goto Error;
	}

	/* Create a lock of the debug notifiers */
	eError = OSWRLockCreate(&g_hDbgNotifyLock);
	if (eError != PVRSRV_OK)
	{
		goto Error;
	}

	eError = PVRSRVRegisterDbgTable(g_aui32DebugOrderTable,
									sizeof(g_aui32DebugOrderTable)/sizeof(g_aui32DebugOrderTable[0]),
									&g_hDebugTable);
	if (eError != PVRSRV_OK)
	{
		goto Error;
	}

	PVRSRVRegisterDbgRequestNotify(&g_hDbgSysNotify, &_SysDebugRequestNotify, DEBUG_REQUEST_SYS, gpsPVRSRVData);

	eError = ServerSyncInit();
	if (eError != PVRSRV_OK)
	{
		goto Error;
	}

	/* Initialise pdump */
	eError = PDUMPINIT();
	if(eError != PVRSRV_OK)
	{
		goto Error;
	}

	g_ui32InitFlags |= INIT_DATA_ENABLE_PDUMPINIT;

	/* Register all the system devices */
	for (i=0;i<psSysConfig->uiDeviceCount;i++)
	{
		if (PVRSRVRegisterDevice(&psSysConfig->pasDevices[i]) != PVRSRV_OK)
		{
			
			return eError;
		}

		/* Initialise the Transport Layer.
		 * Need to remember the RGX device node for use in the Transport Layer
		 * when allocating stream buffers that are shared with clients.
		 * Note however when the device is an LMA device our buffers will not
		 * be in host memory but card memory.
		 */
		if (gpsPVRSRVData->apsRegisteredDevNodes[gpsPVRSRVData->ui32RegisteredDevices-1]->psDevConfig->eDeviceType == PVRSRV_DEVICE_TYPE_RGX)
		{
			eError = TLInit(gpsPVRSRVData->apsRegisteredDevNodes[gpsPVRSRVData->ui32RegisteredDevices-1]);
			PVR_LOGG_IF_ERROR(eError, "TLInit", Error);
		}
	}

	/* Create the clean up event object */
	eError = OSEventObjectCreate("PVRSRV_CLEANUP_EVENTOBJECT", &gpsPVRSRVData->hCleanupEventObject);
	PVR_LOGG_IF_ERROR(eError, "OSEventObjectCreate", Error);

	eError = PVRSRVResManCreateDeferContext(gpsPVRSRVData->hCleanupEventObject,
			&gpsPVRSRVData->hResManDeferContext);
	PVR_LOGG_IF_ERROR(eError, "PVRSRVResManCreateDeferContext", Error);

	g_ui32InitFlags |= INIT_GLOBAL_RESMAN;

	/* Create a thread which is used to do the deferred cleanup running with the
	 * lowest priority */
	eError = OSThreadCreatePriority(&gpsPVRSRVData->hCleanupThread,
							"pvr_defer_free",
							CleanupThread,
							gpsPVRSRVData,
							LOWEST_PRIORITY);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVInit: Failed to create deferred cleanup thread"));
		goto Error;
	}

	/* Create the devices watchdog event object */
	eError = OSEventObjectCreate("PVRSRV_DEVICESWATCHDOG_EVENTOBJECT", &gpsPVRSRVData->hDevicesWatchdogEvObj);
	PVR_LOGG_IF_ERROR(eError, "OSEventObjectCreate", Error);

	/* Create a thread which is used to detect fatal errors */
	eError = OSThreadCreate(&gpsPVRSRVData->hDevicesWatchdogThread,
							"pvr_devices_wd_thread",
							DevicesWatchdogThread,
							gpsPVRSRVData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVInit: Failed to create devices watchdog thread"));
		goto Error;
	}

#if defined(PVR_TESTING_UTILS)
	TUtilsInit();
#endif

	return eError;

Error:
	PVRSRVDeInit();
	return eError;
}


IMG_VOID IMG_CALLCONV PVRSRVDeInit(IMG_VOID)
{
	PVRSRV_DATA		*psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_ERROR	eError;
	IMG_UINT32		i;

	if (gpsPVRSRVData == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeInit failed - invalid gpsPVRSRVData"));
		return;
	}

#if defined(PVR_TESTING_UTILS)
	TUtilsDeinit();
#endif

#if defined (SUPPORT_RGX)
	sUnregisterDevice[PVRSRV_DEVICE_TYPE_RGX] = DevDeInitRGX;
#endif

	psPVRSRVData->bUnload = IMG_TRUE;
	if (psPVRSRVData->hGlobalEventObject)
	{
		OSEventObjectSignal(psPVRSRVData->hGlobalEventObject);
	}

	/* Stop and cleanup the devices watchdog thread */
	if (psPVRSRVData->hDevicesWatchdogThread)
	{
		if (psPVRSRVData->hDevicesWatchdogEvObj)
		{
			eError = OSEventObjectSignal(psPVRSRVData->hDevicesWatchdogEvObj);
			PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");
		}
		eError = OSThreadDestroy(gpsPVRSRVData->hDevicesWatchdogThread);
		gpsPVRSRVData->hDevicesWatchdogThread = IMG_NULL;
		PVR_LOG_IF_ERROR(eError, "OSThreadDestroy");
	}

	if (gpsPVRSRVData->hDevicesWatchdogEvObj)
	{
		eError = OSEventObjectDestroy(gpsPVRSRVData->hDevicesWatchdogEvObj);
		gpsPVRSRVData->hDevicesWatchdogEvObj = IMG_NULL;
		PVR_LOG_IF_ERROR(eError, "OSEventObjectDestroy");
	}

	/* Stop and cleanup the deferred clean up thread, event object and
	 * deferred context list.
	 */
	if (psPVRSRVData->hCleanupThread)
	{
		if (psPVRSRVData->hCleanupEventObject)
		{
			eError = OSEventObjectSignal(psPVRSRVData->hCleanupEventObject);
			PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");
		}
		eError = OSThreadDestroy(gpsPVRSRVData->hCleanupThread);
		gpsPVRSRVData->hCleanupThread = IMG_NULL;
		PVR_LOG_IF_ERROR(eError, "OSThreadDestroy");
	}

	if (gpsPVRSRVData->hCleanupEventObject)
	{
		eError = OSEventObjectDestroy(gpsPVRSRVData->hCleanupEventObject);
		gpsPVRSRVData->hCleanupEventObject = IMG_NULL;
		PVR_LOG_IF_ERROR(eError, "OSEventObjectDestroy");
	}

	if (g_ui32InitFlags & INIT_GLOBAL_RESMAN)
	{
		PVRSRVResManDestroyDeferContext(gpsPVRSRVData->hResManDeferContext);
	}

	/* Unregister all the system devices */
	for (i=0;i<psPVRSRVData->ui32RegisteredDevices;i++)
	{
		PVRSRV_DEVICE_NODE *psDeviceNode = psPVRSRVData->apsRegisteredDevNodes[i];

		/* set device state */
		psDeviceNode->eDevState = PVRSRV_DEVICE_STATE_DEINIT;

		/* Counter part to what gets done in PVRSRVFinaliseSystem */
		if (psDeviceNode->hSyncPrimContext != IMG_NULL)
		{
			if (psDeviceNode->psSyncPrim != IMG_NULL)
			{
				/* Free general pupose sync primitive */
				SyncPrimFree(psDeviceNode->psSyncPrim);
				psDeviceNode->psSyncPrim = IMG_NULL;
			}
			if (psDeviceNode->psSyncPrimPreKick != IMG_NULL)
			{
				/* Free PreKick sync primitive */
				SyncPrimFree(psDeviceNode->psSyncPrimPreKick);
				psDeviceNode->psSyncPrimPreKick = IMG_NULL;
			}

			SyncPrimContextDestroy(psDeviceNode->hSyncPrimContext);
			psDeviceNode->hSyncPrimContext = IMG_NULL;
		}

		PVRSRVUnregisterDevice(psDeviceNode);
		psPVRSRVData->apsRegisteredDevNodes[i] = IMG_NULL;
	}
	SysDestroyConfigData(gpsSysConfig);

	/* Clean up Transport Layer resources that remain. 
	 * Done after RGX node clean up as HWPerf stream is destroyed during 
	 * this
	 */
	TLDeInit();

	ServerSyncDeinit();

	if (g_hDbgSysNotify)
	{
		PVRSRVUnregisterDbgRequestNotify(g_hDbgSysNotify);
	}

	if (g_hDebugTable)
	{
		PVRSRVUnregisterDbgTable(g_hDebugTable);
	}

	if (g_hDbgNotifyLock)
	{
		OSWRLockDestroy(g_hDbgNotifyLock);
	}

	if (hNotifyLock)
	{
		OSWRLockDestroy(hNotifyLock);
	}

	/* deinitialise pdump */
	if ((g_ui32InitFlags & INIT_DATA_ENABLE_PDUMPINIT) > 0)
	{
		PDUMPDEINIT();
	}
	
	/* destroy event object */
	if (gpsPVRSRVData->hGlobalEventObject)
	{
		OSEventObjectDestroy(gpsPVRSRVData->hGlobalEventObject);
		gpsPVRSRVData->hGlobalEventObject = IMG_NULL;
	}

	/* Check there is no notify function */
	if (!dllist_is_empty(&sCmdCompNotifyHead))
	{
		PDLLIST_NODE psNode = dllist_get_next_node(&sCmdCompNotifyHead);

		/* some device did not unregistered properly */
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeInit: Notify list for cmd complete is not empty!!"));

		/* clean the nodes anyway */
		while (psNode != IMG_NULL)
		{
			PVRSRV_CMDCOMP_NOTIFY	*psNotify;

			dllist_remove_node(psNode);
			
			psNotify = IMG_CONTAINER_OF(psNode, PVRSRV_CMDCOMP_NOTIFY, sListNode);
			OSFreeMem(psNotify);

			psNode = dllist_get_next_node(&sCmdCompNotifyHead);
		}
	}

	OSLockDestroy(gpsPVRSRVData->hPowerLock);

	eError = PVRSRVHandleDeInit();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeInit: PVRSRVHandleDeInit failed"));
	}

#if !defined(UNDER_WDDM) && defined(SUPPORT_DISPLAY_CLASS)
	eError = DCDeInit();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeInit: DCInit() failed"));
	}
#endif


    eError = PMRDeInit();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeInit: PMRDeInit() failed"));
	}

#if defined(PVR_RI_DEBUG)
	RIDeInitKM();
#endif

	eError = PVRSRVConnectionDeInit();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeInit: PVRSRVConnectionDataDeInit failed"));
	}

	ResManDeInit();
	
	OSDeInitEnvData();

	for (i=0;i<gpsPVRSRVData->ui32RegisteredPhysHeaps;i++)
	{
		PhysHeapUnregister(gpsPVRSRVData->apsRegisteredPhysHeaps[i]);
	}
	eError = PhysHeapDeinit();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeInit: PhysHeapDeinit() failed"));
	}

	OSFreeMem(gpsPVRSRVData);
	gpsPVRSRVData = IMG_NULL;
}

PVRSRV_ERROR LMA_MMUPxAlloc(PVRSRV_DEVICE_NODE *psDevNode, IMG_SIZE_T uiSize,
							Px_HANDLE *psMemHandle, IMG_DEV_PHYADDR *psDevPAddr)
{
	IMG_BOOL bSuccess;
	RA_BASE_T uiCardAddr;
	RA_LENGTH_T uiActualSize;

	PVR_ASSERT((uiSize & OSGetPageMask()) == 0);

	bSuccess = RA_Alloc(psDevNode->psLocalDevMemArena, 
						uiSize,
						0,					/* No flags */
						OSGetPageSize(),
						&uiCardAddr,
						&uiActualSize,
						IMG_NULL);			/* No private handle */

	PVR_ASSERT(uiSize == uiActualSize);

	psMemHandle->u.ui64Handle = uiCardAddr;
	psDevPAddr->uiAddr = (IMG_UINT64) uiCardAddr;

	if (bSuccess)
	{
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
	/* Allocation is done a page at a time */
	PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA, OSGetPageSize());
#else
		IMG_CPU_PHYADDR	sCpuPAddr;
		sCpuPAddr.uiAddr = psDevPAddr->uiAddr;
		PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA,
															 IMG_NULL,
															 sCpuPAddr,
															 OSGetPageSize(),
															 IMG_NULL);
#endif
#endif
		return PVRSRV_OK;
	}
	else
		return PVRSRV_ERROR_OUT_OF_MEMORY;
}

IMG_VOID LMA_MMUPxFree(PVRSRV_DEVICE_NODE *psDevNode, Px_HANDLE *psMemHandle)
{
	RA_BASE_T uiCardAddr = (RA_BASE_T) psMemHandle->u.ui64Handle;

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
	/* Allocation is done a page at a time */
	PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA, OSGetPageSize());
#else
		PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA, (IMG_UINT64)uiCardAddr);
#endif
#endif

	RA_Free(psDevNode->psLocalDevMemArena, uiCardAddr);
}

PVRSRV_ERROR LMA_MMUPxMap(PVRSRV_DEVICE_NODE *psDevNode, Px_HANDLE *psMemHandle,
							IMG_SIZE_T uiSize, IMG_DEV_PHYADDR *psDevPAddr,
							IMG_VOID **pvPtr)
{
	IMG_CPU_PHYADDR sCpuPAddr;
	PVR_UNREFERENCED_PARAMETER(psMemHandle);
	PVR_UNREFERENCED_PARAMETER(uiSize);

	PhysHeapDevPAddrToCpuPAddr(psDevNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL], &sCpuPAddr, psDevPAddr);
	*pvPtr = OSMapPhysToLin(sCpuPAddr,
							OSGetPageSize(),
							0);
	if (*pvPtr == IMG_NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	else
	{
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
		/* Mapping is done a page at a time */
		PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA, OSGetPageSize());
#else
		{
			PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA,
										 *pvPtr,
										 sCpuPAddr,
										 uiSize,
										 IMG_NULL);
		}
#endif
#endif
		return PVRSRV_OK;
	}
}

IMG_VOID LMA_MMUPxUnmap(PVRSRV_DEVICE_NODE *psDevNode, Px_HANDLE *psMemHandle,
						IMG_VOID *pvPtr)
{
	PVR_UNREFERENCED_PARAMETER(psMemHandle);
	PVR_UNREFERENCED_PARAMETER(psDevNode);

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
		/* Mapping is done a page at a time */
		PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA, OSGetPageSize());
#else
	PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA, (IMG_UINT64)(IMG_UINTPTR_T)pvPtr);
#endif
#endif

	OSUnMapPhysToLin(pvPtr, OSGetPageSize(), 0);
}

/*!
******************************************************************************

 @Function	PVRSRVRegisterDevice

 @Description

 registers a device with the system

 @Input	   psDevConfig			: Device configuration structure

 @Return   PVRSRV_ERROR  :

******************************************************************************/
static PVRSRV_ERROR IMG_CALLCONV PVRSRVRegisterDevice(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	PVRSRV_DATA				*psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_ERROR			eError;
	PVRSRV_DEVICE_NODE		*psDeviceNode;
	PVRSRV_DEVICE_PHYS_HEAP	physHeapIndex;

	/* Allocate device node */
	psDeviceNode = OSAllocMem(sizeof(PVRSRV_DEVICE_NODE));
	if (psDeviceNode == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterDevice : Failed to alloc memory for psDeviceNode"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}
	OSMemSet(psDeviceNode, 0, sizeof(PVRSRV_DEVICE_NODE));

	/* set device state */
	psDeviceNode->eDevState = PVRSRV_DEVICE_STATE_INIT;

	eError = PhysHeapAcquire(psDevConfig->aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL], &psDeviceNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL]);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterDevice : Failed to acquire PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL physical memory heap"));
		goto e1;
	}
	eError = PhysHeapAcquire(psDevConfig->aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL], &psDeviceNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL]);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterDevice : Failed to acquire PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL physical memory heap"));
		goto e1;
	}

	/* Do we have card memory? If so create an RA to manage it */
	if (PhysHeapGetType(psDeviceNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL]) == PHYS_HEAP_TYPE_LMA)
	{
		RA_BASE_T uBase;
		RA_LENGTH_T uSize;
		IMG_CPU_PHYADDR sCpuPAddr;
		IMG_UINT64 ui64Size;

		eError = PhysHeapGetAddress(psDeviceNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL], &sCpuPAddr);
		if (eError != PVRSRV_OK)
		{
			/* We can only get here if there is a bug in this module */
			PVR_ASSERT(IMG_FALSE);
			return eError;
		}

		eError = PhysHeapGetSize(psDeviceNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL], &ui64Size);
		if (eError != PVRSRV_OK)
		{
			/* We can only get here if there is a bug in this module */
			PVR_ASSERT(IMG_FALSE);
			return eError;
		}


		PVR_DPF((PVR_DBG_MESSAGE, "Creating RA for card memory 0x%016llx-0x%016llx",
				 (IMG_UINT64) sCpuPAddr.uiAddr, sCpuPAddr.uiAddr + ui64Size));

		OSSNPrintf(psDeviceNode->szRAName, sizeof(psDeviceNode->szRAName),
											"%s card mem",
											psDevConfig->pszName);

		uBase = 0;
		if (psDevConfig->uiFlags & PVRSRV_DEVICE_CONFIG_LMA_USE_CPU_ADDR)
		{
			uBase = sCpuPAddr.uiAddr;
		}

		uSize = (RA_LENGTH_T) ui64Size;
		PVR_ASSERT(uSize == ui64Size);

		psDeviceNode->psLocalDevMemArena =
			RA_Create(psDeviceNode->szRAName,
						OSGetPageShift(),	/* Use host page size, keeps things simple */
						RA_LOCKCLASS_0,     /* This arena doesn't use any other arenas. */
						IMG_NULL,			/* No Import */
						IMG_NULL,			/* No free import */
						IMG_NULL);			/* No import handle */

		if (psDeviceNode->psLocalDevMemArena == IMG_NULL)
		{
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto e2;
		}

		if (!RA_Add(psDeviceNode->psLocalDevMemArena, uBase, uSize, 0 /* No flags */, IMG_NULL /* No private data */))
		{
			RA_Delete(psDeviceNode->psLocalDevMemArena);
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto e2;		
		}


		psDeviceNode->pfnMMUPxAlloc = LMA_MMUPxAlloc;
		psDeviceNode->pfnMMUPxFree = LMA_MMUPxFree;
		psDeviceNode->pfnMMUPxMap = LMA_MMUPxMap;
		psDeviceNode->pfnMMUPxUnmap = LMA_MMUPxUnmap;
		psDeviceNode->uiMMUPxLog2AllocGran = OSGetPageShift();
		psDeviceNode->ui32Flags = PRVSRV_DEVICE_FLAGS_LMA;
		psDeviceNode->pfnCreateRamBackedPMR[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL] = PhysmemNewLocalRamBackedPMR;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "===== OS System memory only, no local card memory"));

		/* else we only have OS system memory */
		psDeviceNode->pfnMMUPxAlloc = OSMMUPxAlloc;
		psDeviceNode->pfnMMUPxFree = OSMMUPxFree;
		psDeviceNode->pfnMMUPxMap = OSMMUPxMap;
		psDeviceNode->pfnMMUPxUnmap = OSMMUPxUnmap;
		psDeviceNode->pfnCreateRamBackedPMR[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL] = PhysmemNewOSRamBackedPMR;
	}

	if (PhysHeapGetType(psDeviceNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL]) == PHYS_HEAP_TYPE_LMA)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "===== Local card memory only, no OS system memory"));
		psDeviceNode->pfnCreateRamBackedPMR[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL] = PhysmemNewLocalRamBackedPMR;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "===== OS System memory, 2nd phys heap"));
		psDeviceNode->pfnCreateRamBackedPMR[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL] = PhysmemNewOSRamBackedPMR;
	}

	
	psDeviceNode->pszMMUPxPDumpMemSpaceName = PhysHeapPDumpMemspaceName(psDeviceNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL]);
	psDeviceNode->uiMMUPxLog2AllocGran = OSGetPageShift();

	/* Add the devnode to our list so we can unregister it later */
	psPVRSRVData->apsRegisteredDevNodes[psPVRSRVData->ui32RegisteredDevices++] = psDeviceNode;

	psDeviceNode->ui32RefCount = 1;
	psDeviceNode->psDevConfig = psDevConfig;

	/* all devices need a unique identifier */
	AllocateDeviceID(psPVRSRVData, &psDeviceNode->sDevId.ui32DeviceIndex);

	/* Device type and class will be setup during this callback */
	eError = sRegisterDevice[psDevConfig->eDeviceType](psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		/*not nulling pointer, out of scope*/
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterDevice : Failed to register device"));
		eError = PVRSRV_ERROR_DEVICE_REGISTER_FAILED;
		goto e3;
	}


	PVR_DPF((PVR_DBG_MESSAGE, "Registered device %d of type %d", psDeviceNode->sDevId.ui32DeviceIndex, psDeviceNode->sDevId.eDeviceType));
	PVR_DPF((PVR_DBG_MESSAGE, "Register bank address = 0x%08lx", (unsigned long)psDevConfig->sRegsCpuPBase.uiAddr));
	PVR_DPF((PVR_DBG_MESSAGE, "IRQ = %d", psDevConfig->ui32IRQ));
	
	/* and finally insert the device into the dev-list */
	List_PVRSRV_DEVICE_NODE_Insert(&psPVRSRVData->psDeviceNodeList, psDeviceNode);

	/* set device state */
	psDeviceNode->eDevState = PVRSRV_DEVICE_STATE_ACTIVE;

	return PVRSRV_OK;
e3:
	if (psDeviceNode->psLocalDevMemArena)
	{
		RA_Delete(psDeviceNode->psLocalDevMemArena);
	}
e2:
e1:
	for(physHeapIndex=0; physHeapIndex < PVRSRV_DEVICE_PHYS_HEAP_LAST; physHeapIndex++)
	{
		if (psDeviceNode->apsPhysHeap[physHeapIndex])
		{
			PhysHeapRelease(psDeviceNode->apsPhysHeap[physHeapIndex]);
		}
	}
	OSFreeMem(psDeviceNode);
e0:
	return eError;
}

PVRSRV_ERROR IMG_CALLCONV PVRSRVSysPrePowerState(PVRSRV_SYS_POWER_STATE eNewPowerState, IMG_BOOL bForced)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER(bForced);

	if (gpsSysConfig->pfnSysPrePowerState)
	{
		eError = gpsSysConfig->pfnSysPrePowerState(eNewPowerState);
	}
	return eError;
}

PVRSRV_ERROR IMG_CALLCONV PVRSRVSysPostPowerState(PVRSRV_SYS_POWER_STATE eNewPowerState, IMG_BOOL bForced)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER(bForced);

	if (gpsSysConfig->pfnSysPostPowerState)
	{
		eError = gpsSysConfig->pfnSysPostPowerState(eNewPowerState);
	}
	return eError;
}

PVRSRV_ERROR IMG_CALLCONV PVRSRVRegisterExtDevice(PVRSRV_DEVICE_NODE *psDeviceNode,
													IMG_UINT32 *pui32DeviceIndex,
													IMG_UINT32 ui32PhysHeapID)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	PHYS_HEAP   *psPhysHeapTmp;
	PVRSRV_DEVICE_PHYS_HEAP eDevPhysHeap;
	PVRSRV_ERROR eError;

	psDeviceNode->ui32RefCount = 1;

	eError = PhysHeapAcquire(ui32PhysHeapID, &psPhysHeapTmp);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterExtDevice: Failed to acquire physical memory heap"));
		goto e0;
	}
	if (PhysHeapGetType(psPhysHeapTmp) == PHYS_HEAP_TYPE_LMA)
	{
		eDevPhysHeap = PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL;
	}
	else
	{
		eDevPhysHeap = PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL;
	}
	psDeviceNode->apsPhysHeap[eDevPhysHeap] = psPhysHeapTmp;

	/* allocate a unique device id */
	eError = AllocateDeviceID(psPVRSRVData, &psDeviceNode->sDevId.ui32DeviceIndex);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterExtDevice: Failed to allocate Device ID"));
		goto e1;
	}

	if (pui32DeviceIndex)
	{
		*pui32DeviceIndex = psDeviceNode->sDevId.ui32DeviceIndex;
	}

	List_PVRSRV_DEVICE_NODE_Insert(&psPVRSRVData->psDeviceNodeList, psDeviceNode);

	return PVRSRV_OK;
e1:
	PhysHeapRelease(psDeviceNode->apsPhysHeap[eDevPhysHeap]);
e0:
	return eError;
}

IMG_VOID IMG_CALLCONV PVRSRVUnregisterExtDevice(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_DEVICE_PHYS_HEAP eDevPhysHeap;

	List_PVRSRV_DEVICE_NODE_Remove(psDeviceNode);
	(IMG_VOID)FreeDeviceID(psPVRSRVData, psDeviceNode->sDevId.ui32DeviceIndex);
	for (eDevPhysHeap = 0; eDevPhysHeap < PVRSRV_DEVICE_PHYS_HEAP_LAST; eDevPhysHeap++)
	{
		if (psDeviceNode->apsPhysHeap[eDevPhysHeap])
		{
			PhysHeapRelease(psDeviceNode->apsPhysHeap[eDevPhysHeap]);
		}
	}
}

static PVRSRV_ERROR PVRSRVFinaliseSystem_SetPowerState_AnyCb(PVRSRV_DEVICE_NODE *psDeviceNode, va_list va)
{
	PVRSRV_ERROR eError;
	PVRSRV_DEV_POWER_STATE ePowState;
    
#if defined(MTK_USE_HW_APM) && defined(CONFIG_ARCH_MT8135)
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_TRACEBUF *psFWTraceBuf = psDevInfo->psRGXFWIfTraceBuf;

	ePowState = va_arg(va, PVRSRV_DEV_POWER_STATE);
    
#ifdef MTK_DEBUG
	PVR_DPF((PVR_DBG_ERROR,"PVRSRVFinaliseSystem: ePowtate: %d, psFWTraceBuf->ePowState = %d", ePowState, psFWTraceBuf->ePowState));
#endif

	/*Check the if IDLE*/
	if ((ePowState == PVRSRV_DEV_POWER_STATE_ON) || (psFWTraceBuf->ePowState == RGXFWIF_POW_IDLE))
	{
	    eError = PVRSRVSetDevicePowerStateKM(psDeviceNode->sDevId.ui32DeviceIndex, ePowState, IMG_TRUE);
	}
	else
	{
	    eError = PVRSRV_OK;
#ifdef MTK_DEBUG
	    PVR_DPF((PVR_DBG_ERROR, "PVRSRVFinaliseSystem: GPU is still running"));
#endif
	}
#else

	ePowState = va_arg(va, PVRSRV_DEV_POWER_STATE);

	eError = PVRSRVSetDevicePowerStateKM(psDeviceNode->sDevId.ui32DeviceIndex,
										 ePowState,
										 IMG_TRUE);
#endif

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVFinaliseSystem: Failed PVRSRVSetDevicePowerStateKM call (%s, device index: %d)", 
						PVRSRVGetErrorStringKM(eError),
						psDeviceNode->sDevId.ui32DeviceIndex));
	}

	return eError;
}

/*wraps the PVRSRVDevInitCompatCheck call and prints a debugging message if failed*/
static PVRSRV_ERROR PVRSRVFinaliseSystem_CompatCheck_Any_va(PVRSRV_DEVICE_NODE *psDeviceNode, va_list va)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 *pui32ClientBuildOptions;

	pui32ClientBuildOptions = va_arg(va, IMG_UINT32*);

	eError = PVRSRVDevInitCompatCheck(psDeviceNode, *pui32ClientBuildOptions);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVFinaliseSystem: Failed PVRSRVDevInitCompatCheck call (device index: %d)", psDeviceNode->sDevId.ui32DeviceIndex));
	}
	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVFinaliseSystem

 @Description

 Final part of system initialisation.

 @Input	   ui32DevIndex : Index to the required device

 @Return   PVRSRV_ERROR  :

******************************************************************************/
PVRSRV_ERROR IMG_CALLCONV PVRSRVFinaliseSystem(IMG_BOOL bInitSuccessful, IMG_UINT32 ui32ClientBuildOptions)
{
	PVRSRV_DATA			*psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_ERROR		eError;
	IMG_UINT32			i;

	PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVFinaliseSystem"));

	if (bInitSuccessful)
	{
		for (i=0;i<psPVRSRVData->ui32RegisteredDevices;i++)
		{
			PVRSRV_DEVICE_NODE *psDeviceNode = psPVRSRVData->apsRegisteredDevNodes[i];

			SyncPrimContextCreate(IMG_NULL,
								  psDeviceNode,
								  &psDeviceNode->hSyncPrimContext);

			/* Allocate general purpose sync primitive */
			eError = SyncPrimAlloc(psDeviceNode->hSyncPrimContext, &psDeviceNode->psSyncPrim, "pvrsrv dev general");
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"PVRSRVFinaliseSystem: Failed to allocate sync primitive with error (%u)", eError));
				return eError;
			}
			/* Allocate PreKick sync primitive */
			eError = SyncPrimAlloc(psDeviceNode->hSyncPrimContext, &psDeviceNode->psSyncPrimPreKick, "pvrsrv pre kick");
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"PVRSRVFinaliseSystem: Failed to allocate PreKick sync primitive with error (%u)", eError));
				return eError;
			}

		}

		eError = PVRSRVPowerLock();
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVFinaliseSystem_SetPowerState_AnyCb: Failed to acquire power lock"));
			return eError;
		}

		/* Always ensure a single power on command appears in the pdump.
		 * This should be the only power related call outside of PDUMPPOWCMDSTART/END.
		 * Place all devices into ON power state. */
		eError = List_PVRSRV_DEVICE_NODE_PVRSRV_ERROR_Any_va(psPVRSRVData->psDeviceNodeList,
														&PVRSRVFinaliseSystem_SetPowerState_AnyCb,
														PVRSRV_DEV_POWER_STATE_ON);
		if (eError != PVRSRV_OK)
		{
			PVRSRVPowerUnlock();
			return eError;
		}

		/* Verify firmware compatibility for devices */
		eError = List_PVRSRV_DEVICE_NODE_PVRSRV_ERROR_Any_va(psPVRSRVData->psDeviceNodeList,
													&PVRSRVFinaliseSystem_CompatCheck_Any_va,
													&ui32ClientBuildOptions);
		if (eError != PVRSRV_OK)
		{
			PVRSRVPowerUnlock();
			PVRSRVDebugRequest(DEBUG_REQUEST_VERBOSITY_MAX, IMG_NULL);
			return eError;
		}

		PDUMPPOWCMDSTART();
		/* Place all devices into their default power state. */
		eError = List_PVRSRV_DEVICE_NODE_PVRSRV_ERROR_Any_va(psPVRSRVData->psDeviceNodeList,
														&PVRSRVFinaliseSystem_SetPowerState_AnyCb,
														PVRSRV_DEV_POWER_STATE_DEFAULT);
		PDUMPPOWCMDEND();

		if (eError != PVRSRV_OK)
		{
			PVRSRVPowerUnlock();
			return eError;
		}

		PVRSRVPowerUnlock();

	}

	eError = PDumpStopInitPhaseKM(IMG_SRV_INIT);

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to stop PDump init phase"));
		return eError;
	}

	return PVRSRV_OK;
}


PVRSRV_ERROR IMG_CALLCONV PVRSRVDevInitCompatCheck(PVRSRV_DEVICE_NODE *psDeviceNode,IMG_UINT32 ui32ClientBuildOptions)
{
	/* Only check devices which specify a compatibility check callback */
	if (psDeviceNode->pfnInitDeviceCompatCheck)
		return psDeviceNode->pfnInitDeviceCompatCheck(psDeviceNode, ui32ClientBuildOptions);
	else
		return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVAcquireDeviceDataKM

 @Description

 Matchs a device given a device type and a device index.

 @input	psDeviceNode :The device node to be matched.

 @Input	   va : Variable argument list with:
			eDeviceType : Required device type. If type is unknown use ui32DevIndex
						 to locate device data

 			ui32DevIndex : Index to the required device obtained from the
						PVRSRVEnumerateDevice function

 @Return   PVRSRV_ERROR  :

******************************************************************************/
static IMG_VOID * PVRSRVAcquireDeviceDataKM_Match_AnyVaCb(PVRSRV_DEVICE_NODE *psDeviceNode, va_list va)
{
	PVRSRV_DEVICE_TYPE eDeviceType;
	IMG_UINT32 ui32DevIndex;

	eDeviceType = va_arg(va, PVRSRV_DEVICE_TYPE);
	ui32DevIndex = va_arg(va, IMG_UINT32);

	if ((eDeviceType != PVRSRV_DEVICE_TYPE_UNKNOWN &&
		psDeviceNode->sDevId.eDeviceType == eDeviceType) ||
		(eDeviceType == PVRSRV_DEVICE_TYPE_UNKNOWN &&
		 psDeviceNode->sDevId.ui32DeviceIndex == ui32DevIndex))
	{
		return psDeviceNode;
	}
	else
	{
		return IMG_NULL;
	}
}

/*!
******************************************************************************

 @Function	PVRSRVAcquireDeviceDataKM

 @Description

 Returns device information

 @Input	   ui32DevIndex : Index to the required device obtained from the
						PVRSRVEnumerateDevice function

 @Input	   eDeviceType : Required device type. If type is unknown use ui32DevIndex
						 to locate device data

 @Output  *phDevCookie : Dev Cookie


 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAcquireDeviceDataKM (IMG_UINT32			ui32DevIndex,
													 PVRSRV_DEVICE_TYPE	eDeviceType,
													 IMG_HANDLE			*phDevCookie)
{
	PVRSRV_DATA			*psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_DEVICE_NODE	*psDeviceNode;

	PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVAcquireDeviceDataKM"));

	/* Find device in the list */
	psDeviceNode = List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
												&PVRSRVAcquireDeviceDataKM_Match_AnyVaCb,
												eDeviceType,
												ui32DevIndex);


	if (!psDeviceNode)
	{
		/* device can't be found in the list so it isn't in the system */
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVAcquireDeviceDataKM: requested device is not present"));
		return PVRSRV_ERROR_INIT_FAILURE;
	}

/*FoundDevice:*/

	PVR_ASSERT (psDeviceNode->ui32RefCount > 0);

	/* return the dev cookie? */
	if (phDevCookie)
	{
		*phDevCookie = (IMG_HANDLE)psDeviceNode;
	}

	return PVRSRV_OK;
}

IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVReleaseDeviceDataKM (IMG_HANDLE hDevCookie)
{
	PVR_UNREFERENCED_PARAMETER(hDevCookie);

	/* 
	  Empty release body as the lifetime of this resource accessed by 
	  PVRSRVAcquireDeviceDataKM is linked to driver lifetime, not API allocation.
	  This is one reason why this type crosses the bridge with a shared handle.
	  Thus no server release action is required, just bridge action to ensure
	  associated handle is freed.
    */ 
	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVUnregisterDevice

 @Description

 This De-inits device

 @Input	   ui32DevIndex : Index to the required device

 @Return   PVRSRV_ERROR  :

******************************************************************************/
static PVRSRV_ERROR IMG_CALLCONV PVRSRVUnregisterDevice(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_DATA				*psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_DEVICE_PHYS_HEAP ePhysHeapIdx;
	PVRSRV_ERROR			eError;

	eError = PVRSRVPowerLock();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVUnregisterDevice: Failed to acquire power lock"));
		return eError;
	}

	/*
		Power down the device if necessary.
	 */
	eError = PVRSRVSetDevicePowerStateKM(psDeviceNode->sDevId.ui32DeviceIndex,
										 PVRSRV_DEV_POWER_STATE_OFF,
										 IMG_TRUE);

	PVRSRVPowerUnlock();

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVUnregisterDevice: Failed PVRSRVSetDevicePowerStateKM call (%s). Dump debug.", PVRSRVGetErrorStringKM(eError)));

		PVRSRVDebugRequest(DEBUG_REQUEST_VERBOSITY_MAX, IMG_NULL);

		/* If the driver is okay then return the error, otherwise we can ignore this error. */
		if (PVRSRVGetPVRSRVData()->eServicesState == PVRSRV_SERVICES_STATE_OK)
		{
			return eError;
		}
		else
		{
			PVR_DPF((PVR_DBG_MESSAGE,"PVRSRVUnregisterDevice: Will continue to unregister as driver status is not OK"));
		}
	}

	/*
		De-init the device.
	*/
	sUnregisterDevice[psDeviceNode->sDevId.eDeviceType](psDeviceNode);

	/* Remove RA for local card memory */
	if (psDeviceNode->psLocalDevMemArena)
	{
		RA_Delete(psDeviceNode->psLocalDevMemArena);
	}

	/* remove node from list */
	List_PVRSRV_DEVICE_NODE_Remove(psDeviceNode);

	/* deallocate id and memory */
	(IMG_VOID)FreeDeviceID(psPVRSRVData, psDeviceNode->sDevId.ui32DeviceIndex);

	for (ePhysHeapIdx = 0; ePhysHeapIdx < PVRSRV_DEVICE_PHYS_HEAP_LAST; ePhysHeapIdx++)
	{
		if (psDeviceNode->apsPhysHeap[ePhysHeapIdx])
		{
			PhysHeapRelease(psDeviceNode->apsPhysHeap[ePhysHeapIdx]);
		}
	}

	OSFreeMem(psDeviceNode);
	/*not nulling pointer, out of scope*/

	return (PVRSRV_OK);
}


/*
	PollForValueKM
*/
static
PVRSRV_ERROR IMG_CALLCONV PollForValueKM (volatile IMG_UINT32*	pui32LinMemAddr,
										  IMG_UINT32			ui32Value,
										  IMG_UINT32			ui32Mask,
										  IMG_UINT32			ui32Timeoutus,
										  IMG_UINT32			ui32PollPeriodus,
										  IMG_BOOL				bAllowPreemption)
{
#if defined(NO_HARDWARE)
	PVR_UNREFERENCED_PARAMETER(pui32LinMemAddr);
	PVR_UNREFERENCED_PARAMETER(ui32Value);
	PVR_UNREFERENCED_PARAMETER(ui32Mask);
	PVR_UNREFERENCED_PARAMETER(ui32Timeoutus);
	PVR_UNREFERENCED_PARAMETER(ui32PollPeriodus);
	PVR_UNREFERENCED_PARAMETER(bAllowPreemption);
	return PVRSRV_OK;
#else
	IMG_UINT32	ui32ActualValue = 0xFFFFFFFFU; /* Initialiser only required to prevent incorrect warning */

	if (bAllowPreemption)
	{
		PVR_ASSERT(ui32PollPeriodus >= 1000);
	}

	LOOP_UNTIL_TIMEOUT(ui32Timeoutus)
	{
		ui32ActualValue = (*pui32LinMemAddr & ui32Mask);
		if(ui32ActualValue == ui32Value)
		{
			return PVRSRV_OK;
		}

		if (gpsPVRSRVData->eServicesState != PVRSRV_SERVICES_STATE_OK)
		{
			return PVRSRV_ERROR_TIMEOUT;
		}

		if (bAllowPreemption)
		{
			OSSleepms(ui32PollPeriodus / 1000);
		}
		else
		{
			OSWaitus(ui32PollPeriodus);
		}
	} END_LOOP_UNTIL_TIMEOUT();

	PVR_DPF((PVR_DBG_ERROR,"PollForValueKM: Timeout. Expected 0x%x but found 0x%x (mask 0x%x).",
			ui32Value, ui32ActualValue, ui32Mask));
	
	return PVRSRV_ERROR_TIMEOUT;
#endif /* NO_HARDWARE */
}


/*
	PVRSRVPollForValueKM
*/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVPollForValueKM (volatile IMG_UINT32	*pui32LinMemAddr,
												IMG_UINT32			ui32Value,
												IMG_UINT32			ui32Mask)
{
	return PollForValueKM(pui32LinMemAddr, ui32Value, ui32Mask,
						  MAX_HW_TIME_US,
						  MAX_HW_TIME_US/WAIT_TRY_COUNT,
						  IMG_FALSE);
}

/*
	PVRSRVWaitForValueKM
*/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVWaitForValueKM (volatile IMG_UINT32	*pui32LinMemAddr,
												IMG_UINT32			ui32Value,
												IMG_UINT32			ui32Mask)
{
#if defined(NO_HARDWARE)
	PVR_UNREFERENCED_PARAMETER(pui32LinMemAddr);
	PVR_UNREFERENCED_PARAMETER(ui32Value);
	PVR_UNREFERENCED_PARAMETER(ui32Mask);
	return PVRSRV_OK;
#else

	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	IMG_HANDLE hOSEvent;
	PVRSRV_ERROR eError;
	PVRSRV_ERROR eErrorWait;
	IMG_UINT32 ui32ActualValue;

	eError = OSEventObjectOpen(psPVRSRVData->hGlobalEventObject, &hOSEvent);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVWaitForValueKM: Failed to setup EventObject with error (%d)", eError));
		goto EventObjectOpenError;
	}

	eError = PVRSRV_ERROR_TIMEOUT;
	
	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		ui32ActualValue = (*pui32LinMemAddr & ui32Mask);

		if (ui32ActualValue == ui32Value)
		{
			/* Expected value has been found */
			eError = PVRSRV_OK;
			break;
		}
		else if (psPVRSRVData->eServicesState != PVRSRV_SERVICES_STATE_OK)
		{
			/* Services in bad state, don't wait any more */
			eError = PVRSRV_ERROR_NOT_READY;
			break;
		}
		else
		{
			/* wait for event and retry */
			eErrorWait = OSEventObjectWait(hOSEvent);
			if (eErrorWait != PVRSRV_OK  &&  eErrorWait != PVRSRV_ERROR_TIMEOUT)
			{
				PVR_DPF((PVR_DBG_WARNING,"PVRSRVWaitForValueKM: Waiting for value failed with error %d. Expected 0x%x but found 0x%x (Mask 0x%08x). Retrying",
							eErrorWait,
							ui32Value,
							ui32ActualValue,
							ui32Mask));
			}
		}
	} END_LOOP_UNTIL_TIMEOUT();

	OSEventObjectClose(hOSEvent);

	/* One last check incase the object wait ended after the loop timeout... */
	if (eError != PVRSRV_OK  &&  (*pui32LinMemAddr & ui32Mask) == ui32Value)
	{
		eError = PVRSRV_OK;
	}

	/* Provide event timeout information to aid the Device Watchdog Thread... */
	if (eError == PVRSRV_OK)
	{
		psPVRSRVData->ui32GEOConsecutiveTimeouts = 0;
	}
	else if (eError == PVRSRV_ERROR_TIMEOUT)
	{
		psPVRSRVData->ui32GEOConsecutiveTimeouts++;
	}

EventObjectOpenError:

	return eError;

#endif /* NO_HARDWARE */
}

#if !defined(NO_HARDWARE)
static IMG_BOOL _CheckStatus(PDLLIST_NODE psNode, IMG_PVOID pvCallbackData)
{
	PVRSRV_CMDCOMP_HANDLE	hCmdCompCallerHandle = (PVRSRV_CMDCOMP_HANDLE) pvCallbackData;
	PVRSRV_CMDCOMP_NOTIFY	*psNotify;

	psNotify = IMG_CONTAINER_OF(psNode, PVRSRV_CMDCOMP_NOTIFY, sListNode);

	/* A device has finished some processing, check if that unblocks other devices */
	if (hCmdCompCallerHandle != psNotify->hCmdCompHandle)
	{
		psNotify->pfnCmdCompleteNotify(psNotify->hCmdCompHandle);
	}

	/* keep processing until the end of the list */
	return IMG_TRUE;
}
#endif

IMG_VOID IMG_CALLCONV PVRSRVCheckStatus(PVRSRV_CMDCOMP_HANDLE hCmdCompCallerHandle)
{
	PVRSRV_DATA			*psPVRSRVData = PVRSRVGetPVRSRVData();

	/* notify any registered device to check if block work items can now proceed */
#if !defined(NO_HARDWARE)
	OSWRLockAcquireRead(hNotifyLock, GLOBAL_NOTIFY);
	dllist_foreach_node(&sCmdCompNotifyHead, _CheckStatus, hCmdCompCallerHandle);
	OSWRLockReleaseRead(hNotifyLock);
#endif

	/* signal global event object */
	if (psPVRSRVData->hGlobalEventObject)
	{
		IMG_HANDLE hOSEventKM = psPVRSRVData->hGlobalEventObject;
		if(hOSEventKM)
		{
			OSEventObjectSignal(hOSEventKM);
		}
	}
}

PVRSRV_ERROR IMG_CALLCONV PVRSRVKickDevicesKM(IMG_VOID)
{
	PVR_DPF((PVR_DBG_ERROR, "PVRSRVKickDevicesKM"));
	PVRSRVCheckStatus(IMG_NULL);
	return PVRSRV_OK;
}

/*!
 ******************************************************************************

 @Function		PVRSRVGetErrorStringKM

 @Description	Returns a text string relating to the PVRSRV_ERROR enum.

 @Note		case statement used rather than an indexed arrary to ensure text is
 			synchronised with the correct enum

 @Input		eError : PVRSRV_ERROR enum

 @Return	const IMG_CHAR * : Text string

 @Note		Must be kept in sync with servicesext.h

******************************************************************************/

IMG_EXPORT
const IMG_CHAR *PVRSRVGetErrorStringKM(PVRSRV_ERROR eError)
{
	switch(eError)
	{
		case PVRSRV_OK:
			return "PVRSRV_OK";
#define PVRE(x) \
		case x: \
			return #x;
#include "pvrsrv_errors.h"
#undef PVRE
		default:
			return "Unknown PVRSRV error number";
	}
}

/*
	PVRSRVSystemDebugInfo
 */
PVRSRV_ERROR PVRSRVSystemDebugInfo( DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf)
{
	return SysDebugInfo(gpsSysConfig, pfnDumpDebugPrintf);
}

/*
	PVRSRVGetSystemName
*/
const IMG_CHAR *PVRSRVGetSystemName(IMG_VOID)
{
	return gpsSysConfig->pszSystemName;
}

/*
	PVRSRVSystemHasCacheSnooping
*/
IMG_BOOL PVRSRVSystemHasCacheSnooping(IMG_VOID)
{
	if (gpsSysConfig->eCacheSnoopingMode != PVRSRV_SYSTEM_SNOOP_NONE)
	{
		return IMG_TRUE;
	}
	return IMG_FALSE;
}

IMG_BOOL PVRSRVSystemSnoopingOfCPUCache(IMG_VOID)
{
	if ((gpsSysConfig->eCacheSnoopingMode == PVRSRV_SYSTEM_SNOOP_CPU_ONLY) ||
		(gpsSysConfig->eCacheSnoopingMode == PVRSRV_SYSTEM_SNOOP_CROSS))
	{
		return IMG_TRUE;
	}
	return IMG_FALSE;	
}

IMG_BOOL PVRSRVSystemSnoopingOfDeviceCache(IMG_VOID)
{
	if ((gpsSysConfig->eCacheSnoopingMode == PVRSRV_SYSTEM_SNOOP_DEVICE_ONLY) ||
		(gpsSysConfig->eCacheSnoopingMode == PVRSRV_SYSTEM_SNOOP_CROSS))
	{
		return IMG_TRUE;
	}
	return IMG_FALSE;
}

/*
	PVRSRVSystemWaitCycles
*/
IMG_VOID PVRSRVSystemWaitCycles(PVRSRV_DEVICE_CONFIG *psDevConfig, IMG_UINT32 ui32Cycles)
{
	/* Delay in us */
	IMG_UINT32 ui32Delayus = 1;

	/* obtain the device freq */
	if (psDevConfig->pfnClockFreqGet != IMG_NULL)
	{
		IMG_UINT32 ui32DeviceFreq;

		ui32DeviceFreq = psDevConfig->pfnClockFreqGet(psDevConfig->hSysData);

		ui32Delayus = (ui32Cycles*1000000)/ui32DeviceFreq;

		if (ui32Delayus == 0)
		{
			ui32Delayus = 1;
		}
	}

	OSWaitus(ui32Delayus);
}

/*
	PVRSRVRegisterCmdCompleteNotify
*/
PVRSRV_ERROR PVRSRVRegisterCmdCompleteNotify(IMG_HANDLE *phNotify, PFN_CMDCOMP_NOTIFY pfnCmdCompleteNotify, PVRSRV_CMDCOMP_HANDLE hCmdCompHandle)
{
	PVRSRV_CMDCOMP_NOTIFY *psNotify;

	if ((phNotify == IMG_NULL) || (pfnCmdCompleteNotify == IMG_NULL) || (hCmdCompHandle == IMG_NULL))
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: Bad arguments (%p, %p, %p)", __FUNCTION__, phNotify, pfnCmdCompleteNotify, hCmdCompHandle));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psNotify = OSAllocMem(sizeof(*psNotify));
	if (psNotify == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: Not enough memory to allocate CmdCompleteNotify function", __FUNCTION__));
		return PVRSRV_ERROR_OUT_OF_MEMORY;		
	}

	/* Set-up the notify data */
	psNotify->hCmdCompHandle = hCmdCompHandle;
	psNotify->pfnCmdCompleteNotify = pfnCmdCompleteNotify;

	/* Add it to the list of Notify functions */
	OSWRLockAcquireWrite(hNotifyLock, GLOBAL_NOTIFY);
	dllist_add_to_tail(&sCmdCompNotifyHead, &psNotify->sListNode);
	OSWRLockReleaseWrite(hNotifyLock);

	*phNotify = psNotify;

	return PVRSRV_OK;
}

/*
	PVRSRVUnregisterCmdCompleteNotify
*/
PVRSRV_ERROR PVRSRVUnregisterCmdCompleteNotify(IMG_HANDLE hNotify)
{
	PVRSRV_CMDCOMP_NOTIFY *psNotify = (PVRSRV_CMDCOMP_NOTIFY*) hNotify;

	if (psNotify == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: Bad arguments (%p)", __FUNCTION__, hNotify));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* remove the node from the list */
	OSWRLockAcquireWrite(hNotifyLock, GLOBAL_NOTIFY);
	dllist_remove_node(&psNotify->sListNode);
	OSWRLockReleaseWrite(hNotifyLock);

	/* free the notify structure that holds the node */
	OSFreeMem(psNotify);

	return PVRSRV_OK;

}

static IMG_VOID _SysDebugRequestNotify(PVRSRV_DBGREQ_HANDLE hDebugRequestHandle, IMG_UINT32 ui32VerbLevel)
{
	PVRSRV_DATA *psPVRSRVData = (PVRSRV_DATA*) hDebugRequestHandle;
	DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf = IMG_NULL;

	pfnDumpDebugPrintf = g_pfnDumpDebugPrintf;	
	/* only dump info on the lowest verbosity level */
	if (ui32VerbLevel != DEBUG_REQUEST_VERBOSITY_LOW)
	{
		return;
	}

	PVR_DUMPDEBUG_LOG(("DDK info: %s (%s) %s", PVRVERSION_STRING, PVR_BUILD_TYPE, PVR_BUILD_DIR));

	/* Services state */
	switch (psPVRSRVData->eServicesState)
	{
		case PVRSRV_SERVICES_STATE_OK:
		{
			PVR_DUMPDEBUG_LOG(("Services State: OK"));
			break;
		}
		
		case PVRSRV_SERVICES_STATE_BAD:
		{
			PVR_DUMPDEBUG_LOG(("Services State: BAD"));
			break;
		}
		
		default:
		{
			PVR_DUMPDEBUG_LOG(("Services State: UNKNOWN (%d)", psPVRSRVData->eServicesState));
			break;
		}
	}

	/* Power state */
	switch (psPVRSRVData->eCurrentPowerState)
	{
		case PVRSRV_SYS_POWER_STATE_OFF:
		{
			PVR_DUMPDEBUG_LOG(("System Power State: OFF"));
			break;
		}
		case PVRSRV_SYS_POWER_STATE_ON:
		{
			PVR_DUMPDEBUG_LOG(("System Power State: ON"));
			break;
		}
		default:
		{
			PVR_DUMPDEBUG_LOG(("System Power State: UNKNOWN (%d)", psPVRSRVData->eCurrentPowerState));
			break;
		}
	}

	/* Dump system specific debug info */
	PVRSRVSystemDebugInfo(pfnDumpDebugPrintf);

}

static IMG_BOOL _DebugRequest(PDLLIST_NODE psNode, IMG_PVOID hVerbLevel)
{
	IMG_UINT32 *pui32VerbLevel = (IMG_UINT32 *) hVerbLevel;
	PVRSRV_DBGREQ_NOTIFY *psNotify;

	psNotify = IMG_CONTAINER_OF(psNode, PVRSRV_DBGREQ_NOTIFY, sListNode);

	psNotify->pfnDbgRequestNotify(psNotify->hDbgRequestHandle, *pui32VerbLevel);

	/* keep processing until the end of the list */
	return IMG_TRUE;
}

/*
	PVRSRVDebugRequest
*/
IMG_VOID IMG_CALLCONV PVRSRVDebugRequest(IMG_UINT32 ui32VerbLevel, DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf)
{
	IMG_UINT32 i,j;

	g_pfnDumpDebugPrintf = pfnDumpDebugPrintf;
	OSDumpStack();

	/* notify any registered device to check if block work items can now proceed */
	/* Lock the lists */
	OSWRLockAcquireRead(g_hDbgNotifyLock, GLOBAL_DBGNOTIFY);

	PVR_DUMPDEBUG_LOG(("------------[ PVR DBG: START ]------------"));

	if (!pfnDumpDebugPrintf)
	{
		MTKPP_LOGTIME(MTKPP_ID_FW, "Dump Debug Data");
	}

	/* For each verbosity level */
	for (j=0;j<(ui32VerbLevel+1);j++)
	{
		/* For each requester */
		for (i=0;i<g_psDebugTable->ui32RequestCount;i++)
		{
			dllist_foreach_node(&g_psDebugTable->asEntry[i].sListHead, _DebugRequest, &j);
		}
	}
	PVR_DUMPDEBUG_LOG(("------------[ PVR DBG: END ]------------"));

	/* Unlock the lists */
	OSWRLockReleaseRead(g_hDbgNotifyLock);
}

/*
	PVRSRVRegisterDebugRequestNotify
*/
PVRSRV_ERROR PVRSRVRegisterDbgRequestNotify(IMG_HANDLE *phNotify, PFN_DBGREQ_NOTIFY pfnDbgRequestNotify, IMG_UINT32 ui32RequesterID, PVRSRV_DBGREQ_HANDLE hDbgRequestHandle)
{
	PVRSRV_DBGREQ_NOTIFY *psNotify;
	PDLLIST_NODE psHead = IMG_NULL;
	IMG_UINT32 i;
	PVRSRV_ERROR eError;

	if ((phNotify == IMG_NULL) || (pfnDbgRequestNotify == IMG_NULL))
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: Bad arguments (%p, %p,)", __FUNCTION__, phNotify, pfnDbgRequestNotify));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto fail_params;
	}

	psNotify = OSAllocMem(sizeof(*psNotify));
	if (psNotify == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: Not enough memory to allocate DbgRequestNotify structure", __FUNCTION__));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}

	/* Set-up the notify data */
	psNotify->hDbgRequestHandle = hDbgRequestHandle;
	psNotify->pfnDbgRequestNotify = pfnDbgRequestNotify;
	psNotify->ui32RequesterID = ui32RequesterID;

	/* Lock down all the lists */
	OSWRLockAcquireWrite(g_hDbgNotifyLock, GLOBAL_DBGNOTIFY);

	/* Find which list to add it to */
	for (i=0;i<g_psDebugTable->ui32RequestCount;i++)
	{
		if (g_psDebugTable->asEntry[i].ui32RequesterID == ui32RequesterID)
		{
			psHead = &g_psDebugTable->asEntry[i].sListHead;
		}
	}

	if (psHead == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: Failed to find debug requester", __FUNCTION__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto fail_add;
	}

	/* Add it to the list of Notify functions */
	dllist_add_to_tail(psHead, &psNotify->sListNode);

	/* Unlock the lists */
	OSWRLockReleaseWrite(g_hDbgNotifyLock);

	*phNotify = psNotify;

	return PVRSRV_OK;

fail_add:
	OSWRLockReleaseWrite(g_hDbgNotifyLock);
	OSFreeMem(psNotify);
fail_alloc:
fail_params:
	return eError;
}

/*
	PVRSRVUnregisterCmdCompleteNotify
*/
PVRSRV_ERROR PVRSRVUnregisterDbgRequestNotify(IMG_HANDLE hNotify)
{
	PVRSRV_DBGREQ_NOTIFY *psNotify = (PVRSRV_DBGREQ_NOTIFY*) hNotify;

	if (psNotify == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: Bad arguments (%p)", __FUNCTION__, hNotify));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* remove the node from the list */
	OSWRLockAcquireWrite(g_hDbgNotifyLock, GLOBAL_DBGNOTIFY);
	dllist_remove_node(&psNotify->sListNode);
	OSWRLockReleaseWrite(g_hDbgNotifyLock);

	/* free the notify structure that holds the node */
	OSFreeMem(psNotify);

	return PVRSRV_OK;
}


static PVRSRV_ERROR PVRSRVRegisterDbgTable(IMG_UINT32 *paui32Table, IMG_UINT32 ui32Length, IMG_PVOID *phTable)
{
	IMG_UINT32 i;
	if (g_psDebugTable != IMG_NULL)
	{
		return PVRSRV_ERROR_DBGTABLE_ALREADY_REGISTERED;
	}

	g_psDebugTable = OSAllocMem(sizeof(DEBUG_REQUEST_TABLE) + (sizeof(DEBUG_REQUEST_ENTRY) * (ui32Length-1)));
	if (!g_psDebugTable)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	g_psDebugTable->ui32RequestCount = ui32Length;

	/* Init the list heads */
	for (i=0;i<ui32Length;i++)
	{
		g_psDebugTable->asEntry[i].ui32RequesterID = paui32Table[i];
		dllist_init(&g_psDebugTable->asEntry[i].sListHead);
	}

	*phTable = g_psDebugTable;
	return PVRSRV_OK;
}

static IMG_VOID PVRSRVUnregisterDbgTable(IMG_PVOID hTable)
{
	IMG_UINT32 i;

	PVR_ASSERT(hTable == g_psDebugTable);

	for (i=0;i<g_psDebugTable->ui32RequestCount;i++)
	{
		if (!dllist_is_empty(&g_psDebugTable->asEntry[i].sListHead))
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVUnregisterDbgTable: Found registered callback(s) on %d", i));
		}
	}
	OSFREEMEM(g_psDebugTable);
	g_psDebugTable = IMG_NULL;
}

PVRSRV_ERROR AcquireGlobalEventObjectServer(IMG_HANDLE *phGlobalEventObject)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	*phGlobalEventObject = psPVRSRVData->hGlobalEventObject;

	return PVRSRV_OK;
}

PVRSRV_ERROR ReleaseGlobalEventObjectServer(IMG_HANDLE hGlobalEventObject)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	PVR_ASSERT(psPVRSRVData->hGlobalEventObject == hGlobalEventObject);

	return PVRSRV_OK;
}

PVRSRV_ERROR GetBIFTilingHeapXStride(IMG_UINT32 uiHeapNum, IMG_UINT32 *puiXStride)
{
	IMG_UINT32 uiMaxHeaps;

	PVR_ASSERT(puiXStride != IMG_NULL);

	GetNumBifTilingHeapConfigs(&uiMaxHeaps);

	if(uiHeapNum < 1 || uiHeapNum > uiMaxHeaps) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*puiXStride = gpsSysConfig->pui32BIFTilingHeapConfigs[uiHeapNum - 1];

	return PVRSRV_OK;
}

PVRSRV_ERROR GetNumBifTilingHeapConfigs(IMG_UINT32 *puiNumHeaps)
{
	*puiNumHeaps = gpsSysConfig->ui32BIFTilingHeapCount;
	return PVRSRV_OK;
}

/*
	PVRSRVResetHWRLogsKM
*/
PVRSRV_ERROR PVRSRVResetHWRLogsKM(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_LOG(("User requested HWR logs reset"));

	if(psDeviceNode && psDeviceNode->pfnResetHWRLogs)
	{
		return psDeviceNode->pfnResetHWRLogs(psDeviceNode);
	}

	return PVRSRV_ERROR_NO_DEVICENODE_FOUND;
}

/*****************************************************************************
 End of file (pvrsrv.c)
*****************************************************************************/
