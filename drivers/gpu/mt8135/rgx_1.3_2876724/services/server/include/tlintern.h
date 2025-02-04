/*************************************************************************/ /*!
@File
@Title          Transport Layer internals
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Transport Layer header used by TL internally
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
#ifndef __TLINTERN_H__
#define __TLINTERN_H__

#if defined (__cplusplus)
extern "C" {
#endif

#include "devicemem_typedefs.h"
#include "pvr_tlcommon.h"
#include "device.h"
//Thread Safety: Not yet implemented	#include "lock.h"

/* Forward declarations */
typedef struct _TL_SNODE_* PTL_SNODE;

/*! TL stream structure container.
 *    pbyBuffer   holds the circular buffer.
 *    ui32Read    points to the beginning of the buffer, ie to where data to
 *                  Read begin.
 *    ui32Write   points to the end of data that have been committed, ie this is
 *                  where new data will be written.
 *    ui32Pending number of bytes reserved in last reserve call which have not
 *                  yet been submitted. Therefore these data are not ready to
 *                  be transported.
 *
 *      ui32Read < ui32Write <= ui32Pending
 *        where < and <= operators are overloaded to make sense in a circular way.
 */
typedef struct _TL_STREAM_
{
	IMG_CHAR 			szName[PRVSRVTL_MAX_STREAM_NAME_SIZE];	/*!< String name identifier */
	IMG_BOOL 			bDrop; 					/*!< Flag: When buffer is full drop new data instead of
														   overwriting older data */
	IMG_BOOL 			bBlock;					/*!< Flag: When buffer is full reserve will block until there is
														   enough free space in the buffer to fullfil the request. */
	IMG_BOOL 			bWaitForEmptyOnDestroy; /*!< Flag: On destroying a non empty stream block until
														   stream is drained. */
	IMG_BOOL            bNoSignalOnCommit;      /*!< Flag: Used to avoid the TL signalling waiting consumers
														   that new data is available on every commit. Producers
														   using this flag will need to manually signal when
														   appropriate using the TLStreamSync() API */

	IMG_VOID			(*pfProducerCallback)(IMG_VOID); /*!< Optional producer callback of type TL_STREAM_SOURCECB */
	IMG_PVOID			pvProducerUserData;	             /*!< Producer callback user data */

	volatile IMG_UINT32 ui32Read; 				/*!< Pointer to the beginning of available data */
	volatile IMG_UINT32 ui32Write;				/*!< Pointer to already committed data which are ready to be
													 copied to user space*/
	IMG_UINT32          ui32BufferUt;           /*!< Buffer utilisation high watermark, see
												 * TL_BUFFER_UTILIZATION in tlstream.c */
	IMG_UINT32 			ui32Pending;			/*!< Count pending bytes reserved in buffer */
	IMG_UINT32 			ui32Size; 				/*!< Buffer size */
	IMG_BYTE 			*pbyBuffer;				/*!< Actual data buffer */

	PTL_SNODE 			psNode;                 /*!< Ptr to parent stream node */
	DEVMEM_MEMDESC 		*psStreamMemDesc;		/*!< MemDescriptor used to allocate buffer space through PMR */
	DEVMEM_EXPORTCOOKIE sExportCookie; 			/*!< Export cookie for stream DEVMEM */

	IMG_HANDLE			hProducerEvent;			/*!< Handle to wait on if there is not enough space */
	IMG_HANDLE			hProducerEventObj;		/*!< Handle to signal blocked reserve calls */

//Thread Safety: Not yet implemented	POS_LOCK 			hLock;					/*!< lock this structure */
	IMG_INT				uiRefCount;				/*!< Stream reference count */
} TL_STREAM, *PTL_STREAM;

/* there need to be enough space reserved in the buffer for 2 minimal packets
 * and it needs to be aligned the same way the buffer is or there will be a
 * compile error.*/
#define BUFFER_RESERVED_SPACE 2*PVRSRVTL_PACKET_ALIGNMENT

/* ensure the space reserved follows the buffer's alignment */
BLD_ASSERT(!(BUFFER_RESERVED_SPACE&(PVRSRVTL_PACKET_ALIGNMENT-1)), tlintern_h);

/* Define the largest value that a uint that matches the
 * PVRSRVTL_PACKET_ALIGNMENT size can hold */
#define MAX_UINT 0xffffFFFF

/*! Defines the value used for TL_STREAM.ui32Pending when no reserve is
 * outstanding on the stream. */
#define NOTHING_PENDING IMG_UINT32_MAX


/*
 * Transport Layer Stream Descriptor types/defs
 */
typedef struct _TL_STREAM_DESC_
{
	PTL_SNODE	psNode;			/*!< Ptr to parent stream node */
	IMG_UINT32	ui32Flags;
	IMG_HANDLE	hDataEvent; 	/*!< For wait call */
} TL_STREAM_DESC, *PTL_STREAM_DESC;

PTL_STREAM_DESC TLMakeStreamDesc(PTL_SNODE f1, IMG_UINT32 f2, IMG_HANDLE f3);

#define TL_STREAM_KM_FLAG_MASK	0xFFFF0000
#define TL_STREAM_FLAG_TEST		0x10000000
#define TL_STREAM_FLAG_WRAPREAD	0x00010000

#define TL_STREAM_UM_FLAG_MASK	0x0000FFFF

/*
 * Transport Layer stream list node
 */
typedef struct _TL_SNODE_
{
	struct _TL_SNODE_*  psNext;
	IMG_HANDLE			hDataEventObj;
	PTL_STREAM 			psStream;
	PTL_STREAM_DESC 	psDesc;
} TL_SNODE;

PTL_SNODE TLMakeSNode(IMG_HANDLE f2, TL_STREAM *f3, TL_STREAM_DESC *f4);

/*
 * Transport Layer global top types and variables
 * Use access function to obtain pointer.
 */
typedef struct _TL_GDATA_
{
	IMG_PVOID  psRgxDevNode;        /* Device node to use for buffer allocations */
	IMG_HANDLE hTLEventObj;         /* Global TL signal object, new streams, etc */

	IMG_UINT   uiClientCnt;         /* Counter to track the number of client stream connections. */
	PTL_SNODE  psHead;              /* List of Streams, only 1 node supported at present */

} TL_GLOBAL_DATA, *PTL_GLOBAL_DATA;

/*
 * Transport Layer Internal Kernel-Mode Server API
 */
TL_GLOBAL_DATA* TLGGD(IMG_VOID);		/* TLGetGlobalData() */

PVRSRV_ERROR TLInit(PVRSRV_DEVICE_NODE *psDevNode);
IMG_VOID TLDeInit(IMG_VOID);

PVRSRV_DEVICE_NODE* TLGetGlobalRgxDevice(IMG_VOID);

IMG_VOID  TLAddStreamNode(PTL_SNODE psAdd);
PTL_SNODE TLFindStreamNodeByName(IMG_PCHAR pszName);
PTL_SNODE TLFindStreamNodeByDesc(PTL_STREAM_DESC psDesc);
IMG_VOID  TLRemoveStreamAndTryFreeStreamNode(PTL_SNODE psRemove);
IMG_VOID  TLRemoveDescAndTryFreeStreamNode(PTL_SNODE psRemove);

/*
 * Transport Layer stream interface to server part declared here to avoid
 * circular dependency.
 */
IMG_UINT32 TLStreamAcquireReadPos(PTL_STREAM psStream, IMG_UINT32* puiReadOffset);
IMG_VOID TLStreamAdvanceReadPos(PTL_STREAM psStream, IMG_UINT32 uiReadLen);

DEVMEM_EXPORTCOOKIE* TLStreamGetBufferCookie(PTL_STREAM psStream);
IMG_BOOL TLStreamEOS(PTL_STREAM psStream);

/*
 * Test related functions
 */
PVRSRV_ERROR TLDeInitialiseCleanupTestThread (IMG_VOID);

#if defined (__cplusplus)
}
#endif

#endif /* __TLINTERN_H__ */
/******************************************************************************
 End of file (tlintern.h)
******************************************************************************/

