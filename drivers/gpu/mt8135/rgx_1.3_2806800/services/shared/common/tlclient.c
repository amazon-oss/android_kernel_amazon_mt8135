/*************************************************************************/ /*!
@File			tlclient.c
@Title          Services Transport Layer shared API
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Transport layer common API used in both clients and server
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

/* DESIGN NOTE
 * This transport layer consumer-role API was created as a shared API when a
 * client wanted to read the data of a TL stream from within the KM server
 * driver. This was in addition to the existing clients supported externally
 * by the UM client library component via PVR API layer.
 * This shared API is thus used by the PVR TL API in the client library and
 * by clients internal to the server driver module. It depends on
 * client entry points of the TL and DEVMEM bridge modules. These entry points
 * encapsulate from the TL shared API whether a direct bridge or an indirect
 * (ioctl) bridge is used.
 * One reason for needing this layer centres around the fact that some of the
 * API functions make multiple bridge calls and the logic that glues these
 * together is common regardless of client location. Further this layer has
 * allowed the defensive coding that checks parameters to move into the PVR
 * API layer where untrusted clients enter giving a more efficient KM code path.
 */

#include "img_defs.h"
#include "pvrsrv_error.h"
#include "pvr_debug.h"
#include "osfunc.h"

#include "allocmem.h"
#include "devicemem.h"

#include "tlclient.h"
#include "client_pvrtl_bridge.h"

/* Defines/Constants
 */

#define PVR_CONNECT_NO_FLAGS 	0x00U
#define NO_ACQUIRE 				0xffffffffU
#define DIRECT_BRIDGE_HANDLE	((IMG_HANDLE)0xDEADBEEFU)

/* User-side stream descriptor structure.
 */
typedef struct _TL_STREAM_DESC_
{
	/* Handle on kernel-side stream descriptor*/
	IMG_HANDLE		hServerSD;

	/* Stream data buffer variables */
	DEVMEM_EXPORTCOOKIE		sExportCookie;
	DEVMEM_MEMDESC*			psUMmemDesc;
	IMG_PBYTE				pBaseAddr;

	/* Offset in bytes into the circular buffer and valid only after
	 * an Acquire call and undefined after a release. */
	IMG_UINT32 	uiReadOffset;

	/* Always a positive integer when the Acquire call returns and a release
	 * is outstanding. Undefined at all other times. */
	IMG_UINT32	uiReadLen;

} TL_STREAM_DESC, *PTL_STREAM_DESC;


/* Used in direct connections only */
IMG_INTERNAL
PVRSRV_ERROR TLClientConnect(IMG_HANDLE* phSrvHandle)
{
	/* Check the caller provided valid pointer*/
	if(!phSrvHandle)
	{
		PVR_DPF((PVR_DBG_ERROR, "TLClientConnect: Null connection handle"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*phSrvHandle = DIRECT_BRIDGE_HANDLE;

	return PVRSRV_OK;


}


/* Used in direct connections only */
IMG_INTERNAL
PVRSRV_ERROR IMG_CALLCONV TLClientDisconnect(IMG_HANDLE hSrvHandle)
{
	if (hSrvHandle != (IMG_HANDLE)0xDEADBEEF)
	{
		PVR_DPF((PVR_DBG_ERROR, "TLClientDisconnect: Invalid connection handle"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	return PVRSRV_OK;
}


IMG_INTERNAL
PVRSRV_ERROR TLClientOpenStream(IMG_HANDLE hSrvHandle,
		IMG_PCHAR    pszName,
		IMG_UINT32   ui32Mode,
		IMG_HANDLE*  phSD)
{
	PVRSRV_ERROR 				eError = PVRSRV_OK;
	TL_STREAM_DESC* 			psSD = 0;
	DEVMEM_SERVER_EXPORTCOOKIE 	hServerExportCookie;

	PVR_ASSERT(hSrvHandle);
	PVR_ASSERT(pszName);
	PVR_ASSERT(phSD);
	*phSD = NULL;

	/* Allocate memory for the stream descriptor object, initialise with
	 * "no data read" yet. */
	psSD = OSAllocZMem(sizeof(TL_STREAM_DESC));
	if (psSD == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		PVR_DPF((PVR_DBG_ERROR, "BridgeTLOpenStream: KM returned %d", eError));
		goto e0;
	}
	psSD->uiReadLen = psSD->uiReadOffset = NO_ACQUIRE;

	/* Send open stream request to kernel server to get stream handle and
	 * buffer cookie so we can get access to the buffer in this process. */
	eError = BridgeTLOpenStream(hSrvHandle, pszName, ui32Mode,
										&psSD->hServerSD, &hServerExportCookie);
	if (eError != PVRSRV_OK)
	{
		if ((ui32Mode & PVRSRV_STREAM_FLAG_OPEN_WAIT) &&
			(eError == PVRSRV_ERROR_TIMEOUT))
		{
			goto e1;
		}
		PVR_LOGG_IF_ERROR(eError, "BridgeTLOpenStream", e1);
	}

	/* Convert server export cookie into a cookie for use by this client */
	eError = DevmemMakeServerExportClientExport(hSrvHandle,
									hServerExportCookie, &psSD->sExportCookie);
	PVR_LOGG_IF_ERROR(eError, "DevmemMakeServerExportClientExport", e2);

	/* Now convert client cookie into a client handle on the buffer's
	 * physical memory region */
	eError = DevmemImport(hSrvHandle, &psSD->sExportCookie,
						PVRSRV_MEMALLOCFLAG_CPU_READABLE, &psSD->psUMmemDesc);
	PVR_LOGG_IF_ERROR(eError, "DevmemImport", e3);

	/* Now map the memory into the virtual address space of this process. */
	eError = DevmemAcquireCpuVirtAddr(psSD->psUMmemDesc, (IMG_PVOID *)
															&psSD->pBaseAddr);
	PVR_LOGG_IF_ERROR(eError, "DevmemAcquireCpuVirtAddr", e4);

	/* Return client descriptor handle to caller */
	*phSD = psSD;
	return PVRSRV_OK;

/* Clean up post buffer setup */
e4:
	DevmemFree(psSD->psUMmemDesc);
e3:
	(void) DevmemUnmakeServerExportClientExport(hSrvHandle,
				&psSD->sExportCookie);
/* Clean up post stream open */
e2:
	BridgeTLCloseStream(hSrvHandle, psSD->hServerSD);

/* Cleanup post allocation of the descriptor object */
e1:
	OSFreeMem(psSD);

e0:
	return eError;
}


IMG_INTERNAL
PVRSRV_ERROR TLClientCloseStream(IMG_HANDLE hSrvHandle,
		IMG_HANDLE hSD)
{
	PVRSRV_ERROR          eError = PVRSRV_OK;
	TL_STREAM_DESC* psSD = (TL_STREAM_DESC*) hSD;

	PVR_ASSERT(hSrvHandle);
	PVR_ASSERT(hSD);

	/* Check the caller provided connection is valid */
	if(!psSD->hServerSD)
	{
		PVR_DPF((PVR_DBG_ERROR, "TLClientCloseStream: descriptor already closed/not open"));
		return PVRSRV_ERROR_HANDLE_NOT_FOUND;
	}

	/* Check if acquire is outstanding, perform release if it is, ignore result
	 * as there is not much we can do if it is an error other than close */
	if (psSD->uiReadLen != NO_ACQUIRE)
	{
		(void) BridgeTLReleaseData(hSrvHandle, psSD->hServerSD,
									psSD->uiReadOffset, psSD->uiReadLen);
		psSD->uiReadLen = psSD->uiReadOffset = NO_ACQUIRE;
	}

	/* Clean up DevMem resources used for this stream in this client */
	DevmemReleaseCpuVirtAddr(psSD->psUMmemDesc);

	DevmemFree(psSD->psUMmemDesc);

	/* Ignore error, not much that can be done */
	(void) DevmemUnmakeServerExportClientExport(hSrvHandle,
			&psSD->sExportCookie);


	/* Send close to server to clean up kernel mode resources for this
	 * handle and release the memory. */
	eError = BridgeTLCloseStream(hSrvHandle, psSD->hServerSD);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "BridgeTLCloseStream: KM returned %d", eError));
		/*/ Not much we can do with error, fall through to clean up
		 * return eError; */
	}

	OSMemSet(psSD, 0x00, sizeof(TL_STREAM_DESC));
	OSFreeMem (psSD);

	return eError;
}


IMG_INTERNAL
PVRSRV_ERROR TLClientAcquireData(IMG_HANDLE hSrvHandle,
		IMG_HANDLE  hSD,
		IMG_PBYTE*  ppPacketBuf,
		IMG_UINT32* pui32BufLen)
{
	PVRSRV_ERROR 		  eError = PVRSRV_OK;
	TL_STREAM_DESC* psSD = (TL_STREAM_DESC*) hSD;

	PVR_ASSERT(hSrvHandle);
	PVR_ASSERT(hSD);
	PVR_ASSERT(ppPacketBuf);
	PVR_ASSERT(pui32BufLen);

	/* Check Acquire has not been called twice in a row without a release */
	if (psSD->uiReadOffset != NO_ACQUIRE)
	{
		PVR_DPF((PVR_DBG_ERROR, "TLClientAcquireData: acquire already outstanding"));
		return PVRSRV_ERROR_RETRY;
	}

	*pui32BufLen = 0;
	/* Ask the kernel server for the next chunk of data to read */
	eError = BridgeTLAcquireData(hSrvHandle, psSD->hServerSD,
									&psSD->uiReadOffset, &psSD->uiReadLen);
	if (eError != PVRSRV_OK)
	{
		if ((eError != PVRSRV_ERROR_RESOURCE_UNAVAILABLE) &&
			(eError != PVRSRV_ERROR_TIMEOUT))
		{
			PVR_DPF((PVR_DBG_ERROR, "BridgeTLAcquireData: KM returned %d", eError));
		}
		psSD->uiReadOffset = psSD->uiReadLen = NO_ACQUIRE;
		return eError;
	}

	/* Return the data offset and length to the caller if bytes are available
	 * to be read. Could be zero for non-blocking mode. */
	if (psSD->uiReadLen)
	{
		*ppPacketBuf = psSD->pBaseAddr + psSD->uiReadOffset;
		*pui32BufLen = psSD->uiReadLen;
	}
	else
	{
		/* On non blocking, zero length data could be returned from server
		 * Which is basically a no-acquire operation */
		*ppPacketBuf = 0;
		*pui32BufLen = 0;
	}

	return eError;
}


IMG_INTERNAL
PVRSRV_ERROR TLClientReleaseData(IMG_HANDLE hSrvHandle,
		IMG_HANDLE hSD)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	TL_STREAM_DESC* psSD = (TL_STREAM_DESC*) hSD;

	PVR_ASSERT(hSrvHandle);
	PVR_ASSERT(hSD);

	/* the previous acquire did not return any data, this is a no-operation */
	if (psSD->uiReadLen == 0)
	{
		return PVRSRV_OK;
	}

	/* Check release has not been called twice in a row without an acquire */
	if (psSD->uiReadOffset == NO_ACQUIRE)
	{
		PVR_DPF((PVR_DBG_ERROR, "TLClientReleaseData_: no acquire to release"));
		return PVRSRV_ERROR_RETRY;
	}

	/* Inform the kernel to release the data from the buffer */
	eError = BridgeTLReleaseData(hSrvHandle, psSD->hServerSD,
										psSD->uiReadOffset, psSD->uiReadLen);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "BridgeTLReleaseData: KM returned %d", eError));
		/* Need to continue to keep client data consistent, fall through
		 * return eError */
	}

	/* Reset state to indicate no outstanding acquire */
	psSD->uiReadLen = psSD->uiReadOffset = NO_ACQUIRE;

	return eError;
}


/******************************************************************************
 End of file (tlclient.c)
******************************************************************************/

