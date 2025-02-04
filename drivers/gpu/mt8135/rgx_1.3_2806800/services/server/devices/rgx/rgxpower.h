/*************************************************************************/ /*!
@File
@Title          RGX power header file
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the RGX power
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

#if !defined(__RGXPOWER_H__)
#define __RGXPOWER_H__

#include "pvrsrv_error.h"
#include "img_types.h"
#include "servicesext.h"

#if defined (__cplusplus)
extern "C" {
#endif


/*!
******************************************************************************

 @Function	RGXPrePowerState

 @Description

 does necessary preparation before power state transition

 @Input	   hDevHandle : RGX Device Node
 @Input	   eNewPowerState : New power state
 @Input	   eCurrentPowerState : Current power state

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXPrePowerState(IMG_HANDLE				hDevHandle,
							  PVRSRV_DEV_POWER_STATE	eNewPowerState,
							  PVRSRV_DEV_POWER_STATE	eCurrentPowerState,
							  IMG_BOOL					bForced);

/*!
******************************************************************************

 @Function	RGXPostPowerState

 @Description

 does necessary preparation after power state transition

 @Input	   hDevHandle : RGX Device Node
 @Input	   eNewPowerState : New power state
 @Input	   eCurrentPowerState : Current power state

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXPostPowerState(IMG_HANDLE				hDevHandle,
							   PVRSRV_DEV_POWER_STATE	eNewPowerState,
							   PVRSRV_DEV_POWER_STATE	eCurrentPowerState,
							  IMG_BOOL					bForced);


/*!
******************************************************************************

 @Function	RGXPreClockSpeedChange

 @Description

	Does processing required before an RGX clock speed change.

 @Input	   hDevHandle : RGX Device Node
 @Input	   bIdleDevice : Whether the firmware needs to be idled
 @Input	   eCurrentPowerState : Power state of the device

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXPreClockSpeedChange(IMG_HANDLE				hDevHandle,
									IMG_BOOL				bIdleDevice,
									PVRSRV_DEV_POWER_STATE	eCurrentPowerState);

/*!
******************************************************************************

 @Function	RGXPostClockSpeedChange

 @Description

	Does processing required after an RGX clock speed change.

 @Input	   hDevHandle : RGX Device Node
 @Input	   bIdleDevice : Whether the firmware had been idled previously
 @Input	   eCurrentPowerState : Power state of the device

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXPostClockSpeedChange(IMG_HANDLE				hDevHandle,
									 IMG_BOOL				bIdleDevice,
									 PVRSRV_DEV_POWER_STATE	eCurrentPowerState);


/*!
******************************************************************************

 @Function	RGXDustCountChange

 @Description Change of number of DUSTs

 @Input	   hDevHandle : RGX Device Node
 @Input	   ui32NumberOfDusts : Number of DUSTs to make transition to

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXDustCountChange(IMG_HANDLE				hDevHandle,
								IMG_UINT32				ui32NumberOfDusts);

/*!
******************************************************************************

 @Function	RGXActivePowerRequest

 @Description Initiate a handshake with the FW to power off the GPU

 @Input	   hDevHandle : RGX Device Node

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXActivePowerRequest(IMG_HANDLE hDevHandle);

#endif /* __RGXPOWER_H__ */
