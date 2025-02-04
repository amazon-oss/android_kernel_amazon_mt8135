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
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/dcache.h>
#include <linux/mount.h>
#include <linux/sched.h>
#include <linux/cred.h>

#include "img_types.h"
#include "ossecure_export.h"
#include "env_connection.h"
#include "private_data.h"
#include "pvr_debug.h"
#include "driverlock.h"

#if defined(SUPPORT_DRM)
#include "pvr_drm.h"
#endif

PVRSRV_ERROR OSSecureExport(CONNECTION_DATA *psConnection,
							IMG_PVOID pvData,
							IMG_SECURE_TYPE *phSecure,
							CONNECTION_DATA **ppsSecureConnection)
{
	ENV_CONNECTION_DATA *psEnvConnection;
	CONNECTION_DATA *psSecureConnection;
	struct file *connection_file;
	struct file *secure_file;
	struct dentry *secure_dentry;
	struct vfsmount *secure_mnt;
	int secure_fd;
	PVRSRV_ERROR eError;

	/* Obtain the current connections struct file */
	psEnvConnection = PVRSRVConnectionPrivateData(psConnection);
	connection_file = LinuxFileFromEnvConnection(psEnvConnection);

	/* Allocate a fd number */
	secure_fd = get_unused_fd();
	if (secure_fd < 0)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	/*
		Get a reference to the dentry so when close is called we don't
		drop the last reference too early and delete the file
	*/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0))
	secure_dentry = dget(connection_file->f_path.dentry);
	secure_mnt = mntget(connection_file->f_path.mnt);
#else
	secure_dentry = dget(connection_file->f_dentry);
	secure_mnt = mntget(connection_file->f_vfsmnt);
#endif

	
	mutex_unlock(&gPVRSRVLock);

	/* Open our device (using the file information from our current connection) */
	secure_file = dentry_open(
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
					  &connection_file->f_path,
#else
					  connection_file->f_dentry,
					  connection_file->f_vfsmnt,
#endif
					  connection_file->f_flags,
					  current_cred());

	mutex_lock(&gPVRSRVLock);

	/* Bail if the open failed */
	if (IS_ERR(secure_file))
	{
		put_unused_fd(secure_fd);
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	/* Bind our struct file with it's fd number */
	fd_install(secure_fd, secure_file);

	/* Return the new services connection our secure data created */
#if defined(SUPPORT_DRM)
	psSecureConnection = LinuxConnectionFromFile(PVR_DRM_FILE_FROM_FILE(secure_file));
#else
	psSecureConnection = LinuxConnectionFromFile(secure_file);
#endif

	/* Save the private data */
	PVR_ASSERT(psSecureConnection->hSecureData == IMG_NULL);
	psSecureConnection->hSecureData = pvData;

	*phSecure = secure_fd;
	*ppsSecureConnection = psSecureConnection;
	return PVRSRV_OK;

e0:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

PVRSRV_ERROR OSSecureImport(IMG_SECURE_TYPE hSecure, IMG_PVOID *ppvData)
{
	struct file *secure_file;
	CONNECTION_DATA *psSecureConnection;
	PVRSRV_ERROR eError;

	secure_file = fget(hSecure);

	if (!secure_file)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err_out;
	}

#ifdef MTK_debug_FILE_PRIV
	if (secure_file->private_data == NULL ||
		((PVRSRV_FILE_PRIVATE_DATA *)secure_file->private_data)->magic != (uintptr_t)secure_file->private_data)
	{
		PVR_DPF((PVR_DBG_ERROR, "fd: %d, secure_file %p invalid", hSecure, secure_file));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err_fput;
	}
#endif

#if defined(SUPPORT_DRM)
	psSecureConnection = LinuxConnectionFromFile(PVR_DRM_FILE_FROM_FILE(secure_file));
#else
	psSecureConnection = LinuxConnectionFromFile(secure_file);
#endif
	if (psSecureConnection->hSecureData == IMG_NULL)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err_fput;
	}

	*ppvData = psSecureConnection->hSecureData;
	fput(secure_file);
	return PVRSRV_OK;

err_fput:
	fput(secure_file);
err_out:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}
