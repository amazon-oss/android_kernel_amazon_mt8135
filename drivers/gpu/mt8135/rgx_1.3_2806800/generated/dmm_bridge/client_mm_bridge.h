/*************************************************************************/ /*!
@Title          Direct client bridge header for mm
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

#ifndef CLIENT_MM_BRIDGE_H
#define CLIENT_MM_BRIDGE_H

#include "common_mm_bridge.h"

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRExportPMR(IMG_HANDLE hBridge,
							  IMG_HANDLE hPMR,
							  IMG_HANDLE *phPMRExport,
							  IMG_UINT64 *pui64Size,
							  IMG_UINT32 *pui32Log2Contig,
							  IMG_UINT64 *pui64Password);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRUnexportPMR(IMG_HANDLE hBridge,
								IMG_HANDLE hPMRExport);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRGetUID(IMG_HANDLE hBridge,
							   IMG_HANDLE hPMR,
							   IMG_UINT64 *pui64UID);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRMakeServerExportClientExport(IMG_HANDLE hBridge,
										 DEVMEM_SERVER_EXPORTCOOKIE hPMRServerExport,
										 IMG_HANDLE *phPMRExportOut,
										 IMG_UINT64 *pui64Size,
										 IMG_UINT32 *pui32Log2Contig,
										 IMG_UINT64 *pui64Password);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRUnmakeServerExportClientExport(IMG_HANDLE hBridge,
										   IMG_HANDLE hPMRExport);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRImportPMR(IMG_HANDLE hBridge,
							  IMG_HANDLE hPMRExport,
							  IMG_UINT64 ui64uiPassword,
							  IMG_UINT64 ui64uiSize,
							  IMG_UINT32 ui32uiLog2Contig,
							  IMG_HANDLE *phPMR);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntCtxCreate(IMG_HANDLE hBridge,
								IMG_HANDLE hDeviceNode,
								IMG_HANDLE *phDevMemServerContext,
								IMG_HANDLE *phPrivData);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntCtxDestroy(IMG_HANDLE hBridge,
								 IMG_HANDLE hDevmemServerContext);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntHeapCreate(IMG_HANDLE hBridge,
								 IMG_HANDLE hDevmemCtx,
								 IMG_DEV_VIRTADDR sHeapBaseAddr,
								 IMG_DEVMEM_SIZE_T uiHeapLength,
								 IMG_UINT32 ui32Log2DataPageSize,
								 IMG_HANDLE *phDevmemHeapPtr);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntHeapDestroy(IMG_HANDLE hBridge,
								  IMG_HANDLE hDevmemHeap);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntMapPMR(IMG_HANDLE hBridge,
								 IMG_HANDLE hDevmemServerHeap,
								 IMG_HANDLE hReservation,
								 IMG_HANDLE hPMR,
								 PVRSRV_MEMALLOCFLAGS_T uiMapFlags,
								 IMG_HANDLE *phMapping);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntUnmapPMR(IMG_HANDLE hBridge,
								   IMG_HANDLE hMapping);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntReserveRange(IMG_HANDLE hBridge,
								   IMG_HANDLE hDevmemServerHeap,
								   IMG_DEV_VIRTADDR sAddress,
								   IMG_DEVMEM_SIZE_T uiLength,
								   IMG_HANDLE *phReservation);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntUnreserveRange(IMG_HANDLE hBridge,
									 IMG_HANDLE hReservation);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePhysmemNewRamBackedPMR(IMG_HANDLE hBridge,
									IMG_HANDLE hDeviceNode,
									IMG_DEVMEM_SIZE_T uiSize,
									IMG_DEVMEM_SIZE_T uiChunkSize,
									IMG_UINT32 ui32NumPhysChunks,
									IMG_UINT32 ui32NumVirtChunks,
									IMG_BOOL *pbMappingTable,
									IMG_UINT32 ui32Log2PageSize,
									PVRSRV_MEMALLOCFLAGS_T uiFlags,
									IMG_HANDLE *phPMRPtr);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRLocalImportPMR(IMG_HANDLE hBridge,
								   IMG_HANDLE hExtHandle,
								   IMG_HANDLE *phPMR,
								   IMG_DEVMEM_SIZE_T *puiSize,
								   IMG_DEVMEM_ALIGN_T *psAlign);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRUnrefPMR(IMG_HANDLE hBridge,
							 IMG_HANDLE hPMR);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemSLCFlushInvalRequest(IMG_HANDLE hBridge,
									IMG_HANDLE hDeviceNode,
									IMG_HANDLE hPmr);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeHeapCfgHeapConfigCount(IMG_HANDLE hBridge,
									IMG_HANDLE hDeviceNode,
									IMG_UINT32 *pui32NumHeapConfigs);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeHeapCfgHeapCount(IMG_HANDLE hBridge,
								  IMG_HANDLE hDeviceNode,
								  IMG_UINT32 ui32HeapConfigIndex,
								  IMG_UINT32 *pui32NumHeaps);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeHeapCfgHeapConfigName(IMG_HANDLE hBridge,
								   IMG_HANDLE hDeviceNode,
								   IMG_UINT32 ui32HeapConfigIndex,
								   IMG_UINT32 ui32HeapConfigNameBufSz,
								   IMG_CHAR *puiHeapConfigName);

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeHeapCfgHeapDetails(IMG_HANDLE hBridge,
								IMG_HANDLE hDeviceNode,
								IMG_UINT32 ui32HeapConfigIndex,
								IMG_UINT32 ui32HeapIndex,
								IMG_UINT32 ui32HeapNameBufSz,
								IMG_CHAR *puiHeapNameOut,
								IMG_DEV_VIRTADDR *psDevVAddrBase,
								IMG_DEVMEM_SIZE_T *puiHeapLength,
								IMG_UINT32 *pui32Log2DataPageSizeOut);


#endif /* CLIENT_MM_BRIDGE_H */
