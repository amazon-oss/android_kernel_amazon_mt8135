/*************************************************************************/ /*!
@File
@Title          Services definitions required by external drivers
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Provides services data structures, defines and prototypes
				required by external drivers
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

#if !defined (__SERVICESEXT_H__)
#define __SERVICESEXT_H__

/* include/ */
#include "pvrsrv_error.h"
#include "img_types.h"
#include "pvrsrv_device_types.h"


/*
 * Lock buffer read/write flags
 */
#define PVRSRV_LOCKFLG_READONLY     	(1)		/*!< The locking process will only read the locked surface */

/*!
 *****************************************************************************
 *	Services State
 *****************************************************************************/
typedef enum _PVRSRV_SERVICES_STATE_
{
	PVRSRV_SERVICES_STATE_OK = 0,
	PVRSRV_SERVICES_STATE_BAD,
} PVRSRV_SERVICES_STATE;


/*!
 *****************************************************************************
 *	States for power management
 *****************************************************************************/
/*!
  System Power State Enum
 */
typedef enum _PVRSRV_SYS_POWER_STATE_
{
	PVRSRV_SYS_POWER_STATE_Unspecified		= -1,	/*!< Unspecified : Uninitialised */
	PVRSRV_SYS_POWER_STATE_OFF				= 0,	/*!< Off */
	PVRSRV_SYS_POWER_STATE_ON				= 1,	/*!< On */

	PVRSRV_SYS_POWER_STATE_FORCE_I32 = 0x7fffffff   /*!< Force enum to be at least 32-bits wide */

} PVRSRV_SYS_POWER_STATE, *PPVRSRV_SYS_POWER_STATE; /*!< Typedef for ptr to PVRSRV_SYS_POWER_STATE */

/*!
  Device Power State Enum
 */
typedef enum _PVRSRV_DEV_POWER_STATE_
{
	PVRSRV_DEV_POWER_STATE_DEFAULT	= -1,	/*!< Default state for the device */
	PVRSRV_DEV_POWER_STATE_OFF		= 0,	/*!< Unpowered */
	PVRSRV_DEV_POWER_STATE_ON		= 1,	/*!< Running */

	PVRSRV_DEV_POWER_STATE_FORCE_I32 = 0x7fffffff   /*!< Force enum to be at least 32-bits wide */

} PVRSRV_DEV_POWER_STATE, *PPVRSRV_DEV_POWER_STATE;	/*!< Typedef for ptr to PVRSRV_DEV_POWER_STATE */ /* PRQA S 3205 */


/* Power transition handler prototypes */

/*!
  Typedef for a pointer to a Function that will be called before a transition
  from one power state to another. See also PFN_POST_POWER.
 */
typedef PVRSRV_ERROR (*PFN_PRE_POWER) (IMG_HANDLE				hDevHandle,
									   PVRSRV_DEV_POWER_STATE	eNewPowerState,
									   PVRSRV_DEV_POWER_STATE	eCurrentPowerState,
									   IMG_BOOL					bForced);
/*!
  Typedef for a pointer to a Function that will be called after a transition
  from one power state to another. See also PFN_PRE_POWER.
 */
typedef PVRSRV_ERROR (*PFN_POST_POWER) (IMG_HANDLE				hDevHandle,
										PVRSRV_DEV_POWER_STATE	eNewPowerState,
										PVRSRV_DEV_POWER_STATE	eCurrentPowerState,
										IMG_BOOL				bForced);

/* Clock speed handler prototypes */

/*!
  Typedef for a pointer to a Function that will be caled before a transition
  from one clockspeed to another. See also PFN_POST_CLOCKSPEED_CHANGE.
 */
typedef PVRSRV_ERROR (*PFN_PRE_CLOCKSPEED_CHANGE) (IMG_HANDLE				hDevHandle,
												   IMG_BOOL					bIdleDevice,
												   PVRSRV_DEV_POWER_STATE	eCurrentPowerState);

/*!
  Typedef for a pointer to a Function that will be caled after a transition
  from one clockspeed to another. See also PFN_PRE_CLOCKSPEED_CHANGE.
 */
typedef PVRSRV_ERROR (*PFN_POST_CLOCKSPEED_CHANGE) (IMG_HANDLE				hDevHandle,
													IMG_BOOL				bIdleDevice,
													PVRSRV_DEV_POWER_STATE	eCurrentPowerState);

/*!
 *****************************************************************************
 * Enumeration of possible alpha types.
 *****************************************************************************/
typedef enum _PVRSRV_ALPHA_FORMAT_ {
	PVRSRV_ALPHA_FORMAT_UNKNOWN		=  0x00000000,  /*!< Alpha Format: Unknown */
	PVRSRV_ALPHA_FORMAT_PRE			=  0x00000001,  /*!< Alpha Format: Pre-Alpha */
	PVRSRV_ALPHA_FORMAT_NONPRE		=  0x00000002,  /*!< Alpha Format: Non-Pre-Alpha */
	PVRSRV_ALPHA_FORMAT_MASK		=  0x0000000F,  /*!< Alpha Format Mask */
} PVRSRV_ALPHA_FORMAT;

/*!
 *****************************************************************************
 * Enumeration of possible alpha types.
 *****************************************************************************/
typedef enum _PVRSRV_COLOURSPACE_FORMAT_ {
	PVRSRV_COLOURSPACE_FORMAT_UNKNOWN		=  0x00000000,  /*!< Colourspace Format: Unknown */
	PVRSRV_COLOURSPACE_FORMAT_LINEAR		=  0x00010000,  /*!< Colourspace Format: Linear */
	PVRSRV_COLOURSPACE_FORMAT_NONLINEAR		=  0x00020000,  /*!< Colourspace Format: Non-Linear */
	PVRSRV_COLOURSPACE_FORMAT_MASK			=  0x000F0000,  /*!< Colourspace Format Mask */
} PVRSRV_COLOURSPACE_FORMAT;


/*!
 * Drawable orientation (in degrees clockwise).
 */
typedef enum _PVRSRV_ROTATION_ {
	PVRSRV_ROTATE_0		=	0,  /*!< Rotate by 0 degres */
	PVRSRV_ROTATE_90	=	1,  /*!< Rotate by 90 degrees */
	PVRSRV_ROTATE_180	=	2,  /*!< Rotate by 180 degrees */
	PVRSRV_ROTATE_270	=	3,  /*!< Rotate by 270 degrees */
	PVRSRV_FLIP_Y

} PVRSRV_ROTATION;

/*!
 *****************************************************************************
 * This structure is used for OS independent registry (profile) access
 *****************************************************************************/

typedef struct _PVRSRV_REGISTRY_INFO
{
	IMG_UINT32			ui32DevCookie;
	IMG_PCHAR			pszKey;
	IMG_PCHAR			pszValue;
	IMG_PCHAR			pszBuf;
	IMG_UINT32			ui32BufSize;
} PVRSRV_REGISTRY_INFO, *PPVRSRV_REGISTRY_INFO;

#endif /* __SERVICESEXT_H__ */
/*****************************************************************************
 End of file (servicesext.h)
*****************************************************************************/

