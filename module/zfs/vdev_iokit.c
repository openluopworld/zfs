/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Portions Copyright 2007 Apple Inc. All rights reserved.
 * Use is subject to license terms.
 * Copyright (C) 2008-2010 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Rewritten for Linux by Brian Behlendorf <behlendorf1@llnl.gov>.
 * LLNL-CODE-403049.
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_iokit.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#ifdef __APPLE__
#include <sys/mount.h>
#else
#include <sys/sunldi.h>
#endif /*__APPLE__*/


unsigned int zfs_iokit_vdev_ashift = 0;

extern void vdev_iokit_log(const char *);
extern void vdev_iokit_log_str(const char *, const char *);
extern void vdev_iokit_log_ptr(const char *, const void *);
extern void vdev_iokit_log_num(const char *, const uint64_t);

/*
 * Virtual device vector for disks via Mac OS X IOKit.
 */

int vdev_iokit_alloc(vdev_iokit_t **dvd)
{
//vdev_iokit_log_ptr( "vdev_iokit_alloc: dvd", dvd );
//vdev_iokit_log_ptr( "vdev_iokit_alloc: *dvd", *dvd );
    if (!dvd) {
        return EINVAL;
    }
    
    // KM_SLEEP for vdev context
    *dvd =    (vdev_iokit_t *)kmem_alloc(sizeof(vdev_iokit_t), KM_NOSLEEP);
    
    if ( !dvd || !(*dvd) )
        return ENOMEM;
    
    (*dvd)->vd_iokit_hl =      0;
    (*dvd)->vd_offline =       0;
    (*dvd)->in_command_pool =  0;
    (*dvd)->out_command_pool = 0;
    (*dvd)->command_set =      0;
    
    (*dvd)->vd_zfs_hl =        vdev_iokit_get_service();
    
    return 0;
//vdev_iokit_log_ptr( "vdev_iokit_alloc: vd->vdev_tsd", vd->vdev_tsd );
}

void vdev_iokit_free(vdev_iokit_t **dvd)
{
//vdev_iokit_log_ptr( "vdev_iokit_free: vd", vd );
    if (!dvd)
        return;
    
    (*dvd)->vd_iokit_hl = 0;
    (*dvd)->vd_zfs_hl = 0;
    (*dvd)->vd_offline =   0;
    (*dvd)->in_command_pool = 0;
    (*dvd)->out_command_pool = 0;
    (*dvd)->command_set =  0;
    
    kmem_free(*dvd, sizeof (vdev_iokit_t));
    *dvd = 0;
}

extern void
vdev_iokit_state_change(vdev_t * vd, int faulted, int degraded)
{
vdev_iokit_log_ptr( "vdev_iokit_state_change: vd", vd );
    
}

extern void
vdev_iokit_hold(vdev_t * vd)
{
    vdev_iokit_t * dvd = 0;
vdev_iokit_log_ptr( "vdev_iokit_hold: vd", vd );
    
    if (!vd)
        return;
    
vdev_iokit_log_num( "vdev_iokit_hold: spa mode:",   spa_mode(vd->vdev_spa) );
vdev_iokit_log_num( "vdev_iokit_hold: vd state:",   vd->vdev_state );
vdev_iokit_log_num( "vdev_iokit_hold: prevstate:",  vd->vdev_prevstate );
    
	ASSERT(spa_config_held(vd->vdev_spa, SCL_STATE, RW_WRITER));
    
	/*
	 * We must have a pathname, and it must be absolute.
	 */
	if (vd->vdev_path == NULL || vd->vdev_path[0] != '/')
		return;
    
	/*
	 * Only prefetch path and devid info if the device has
	 * never been opened.
	 */
	if (vd->vdev_tsd != NULL)
		return;
    
    dvd = (vdev_iokit_t *)(vd->vdev_tsd);
    
vdev_iokit_log_ptr( "vdev_iokit_hold: dvd:",        dvd);
    
    if (!dvd)
        return;

vdev_iokit_log_ptr( "vdev_iokit_hold: iokit_hl:",   dvd->vd_iokit_hl);
vdev_iokit_log_ptr( "vdev_iokit_hold: zfs_hl:",     dvd->vd_zfs_hl);
    
	if (vd->vdev_wholedisk == -1ULL) {
		size_t len = strlen(vd->vdev_path) + 3;
		char *buf = kmem_alloc(len, KM_NOSLEEP);
        
		(void) snprintf(buf, len, "%ss0", vd->vdev_path);
        
		(void) vdev_iokit_open_by_path(dvd, buf);
		kmem_free(buf, len);
	}
    
	if (vd->vdev_name_vp == NULL)
		(void) vdev_iokit_open_by_path(dvd, vd->vdev_path);
    
    /* XXX - TO DO
     *  Populate and use devids if possible
     */
    /*
     if (vd->vdev_devid != NULL &&
     ddi_devid_str_decode(vd->vdev_devid, &devid, &minor) == 0) {
     (void) ldi_vp_from_devid(devid, minor, &vd->vdev_devid_vp);
     ddi_devid_str_free(minor);
     ddi_devid_free(devid);
     }
     */
}

extern void
vdev_iokit_rele(vdev_t * vd)
{
    vdev_iokit_t * dvd = 0;
vdev_iokit_log_ptr( "vdev_iokit_rele: vd", vd );
    if (!vd)
        return;

    dvd = (vdev_iokit_t *)(vd->vdev_tsd);
    
vdev_iokit_log_num( "vdev_iokit_rele: spa mode:",   spa_mode(vd->vdev_spa) );
vdev_iokit_log_num( "vdev_iokit_rele: vd state:",   vd->vdev_state );
vdev_iokit_log_num( "vdev_iokit_rele: prevstate:",  vd->vdev_prevstate );

vdev_iokit_log_ptr( "vdev_iokit_rele: dvd:",        dvd);
    
    if (!dvd)
        return;
    
vdev_iokit_log_ptr( "vdev_iokit_rele: iokit_hl:",   dvd->vd_iokit_hl);
vdev_iokit_log_ptr( "vdev_iokit_rele: zfs_hl:",     dvd->vd_zfs_hl);
    
	ASSERT(spa_config_held(vd->vdev_spa, SCL_STATE, RW_WRITER));
    
	if (dvd->vd_iokit_hl) {
        
		//vdev_iokit_release(vd);
        
        //  async( vd, dsl_pool_vnrele_taskq(vd->vdev_spa->spa_dsl_pool));
        
		dvd->vd_iokit_hl =  NULL;
	}
}

/* IOKit doesn't involve the VFS layer to close disks, however we might
 * still need to do this asynchronously to avoid deadlocks
 */
/*
 * Like vn_rele() except if we are going to call VOP_INACTIVE() then do it
 * asynchronously using a taskq. This can avoid deadlocks caused by re-entering
 * the file system as a result of releasing the vnode. Note, file systems
 * already have to handle the race where the vnode is incremented before the
 * inactive routine is called and does its locking.
 *
 * Warning: Excessive use of this routine can lead to performance problems.
 * This is because taskqs throttle back allocation if too many are created.
 */
#if 0           /* NOT CURRENTLY USED */
void
vdev_iokit_hl_rele_async(vdev_t *vd, taskq_t *taskq)
{
	mutex_enter(&vd->v_lock);
	if (vd->v_count == 1) {
		mutex_exit(&vp->v_lock);
		VERIFY(taskq_dispatch(taskq, (task_func_t *)vdev_iokit_release,
                              vd, TQ_SLEEP) != NULL);
		return;
	}
	vp->v_count--;
	mutex_exit(&vp->v_lock);
}
#endif

extern int
vdev_iokit_open(vdev_t *vd, uint64_t *size, uint64_t *max_size, uint64_t *ashift)
{
//	uint64_t blkcnt;
//	uint32_t blksize;
//	int fmode = 0;
    vdev_iokit_t *dvd = 0;
	int error = 0;
    
    if (!vd)
        return EINVAL;
    
//vdev_iokit_log_ptr( "vdev_iokit_open: vd:",         vd );
//vdev_iokit_log_ptr( "vdev_iokit_open: vd->vdev_tsd:", vd->vdev_tsd );
//vdev_iokit_log_num( "vdev_iokit_open: reopening:", vd->vdev_reopening );
//vdev_iokit_log_num( "vdev_iokit_open: spa mode:",   spa_mode(vd->vdev_spa) );
//vdev_iokit_log_num( "vdev_iokit_open: vd state:",   vd->vdev_state );
//vdev_iokit_log_num( "vdev_iokit_open: prevstate:",  vd->vdev_prevstate );
    
    /*
	 * We must have a pathname, and it must be absolute.
	 */
	if (vd->vdev_path == NULL || vd->vdev_path[0] != '/') {
        vdev_iokit_log_str( "vdev_iokit_open: invalid path:", vd->vdev_path );
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (SET_ERROR(EINVAL));
	}
	
    if (vd->vdev_tsd) {
//        if (vd->vdev_reopening) {
//            vdev_iokit_log( "vdev_iokit_open: reopening (unhandled)" );
//            goto skip_open;
//        } else {
//        }
        vdev_iokit_log( "vdev_iokit_open: busy" );
        error = EBUSY;
        goto out;
    }
    
    error =     vdev_iokit_alloc( (vdev_iokit_t **) &(vd->vdev_tsd) );
    dvd = (vdev_iokit_t*)(vd->vdev_tsd);
    
    if(error != 0 || !dvd) {
        vdev_iokit_log_ptr( "vdev_iokit_open: error allocating dvd", dvd );
        return (error != 0 ? error : ENOMEM);
    }

    /*
	 * When opening a disk device, we want to preserve the user's original
	 * intent.  We always want to open the device by the path the user gave
	 * us, even if it is one of multiple paths to the same device.  But we
	 * also want to be able to survive disks being removed/recabled.
	 * Therefore the sequence of opening devices is:
	 *
	 * 1. Try opening the device by path.  For legacy pools without the
	 *    'whole_disk' property, attempt to fix the path by appending 's0'.
	 *
	 * 2. If the devid of the device matches the stored value, return
	 *    success.
	 *
	 * 3. Otherwise, the device may have moved.  Try opening the device
	 *    by the devid instead.
	 */
    
    error = EINVAL;		/* presume failure */
    
    if (vd->vdev_path != NULL) {
        
		if (vd->vdev_wholedisk == -1ULL) {
			size_t len = strlen(vd->vdev_path) + 3;
			char *buf = kmem_alloc(len, KM_NOSLEEP);
            
			(void) snprintf(buf, len, "%ss0", vd->vdev_path);

//            vdev_iokit_log_str( "vdev_iokit_open: path+s0 1", buf );
			error = vdev_iokit_open_by_path(dvd, buf);
            
			if (error == 0) {
				spa_strfree(vd->vdev_path);
				vd->vdev_path = buf;
				vd->vdev_wholedisk = 1ULL;
			} else {
				kmem_free(buf, len);
			}
		}
        
		/*
		 * If we have not yet opened the device, try to open it by the
		 * specified path.
		 */
		if (error != 0) {
//            vdev_iokit_log_str( "vdev_iokit_open: path 1", vd->vdev_path );
			error = vdev_iokit_open_by_path(dvd, vd->vdev_path);
		}
        
        /*
		 * If we succeeded in opening the device, but 'vdev_wholedisk'
		 * is not yet set, then this must be a slice.
		 */
		if (error == 0 && vd->vdev_wholedisk == -1ULL)
			vd->vdev_wholedisk = 0;
    }
    
    /*
	 * If all else fails, then try opening by physical path (if available)
	 * or the logical path (if we failed due to the devid check).  While not
	 * as reliable as the devid, this will give us something, and the higher
	 * level vdev validation will prevent us from opening the wrong device.
	 */
	if (error) {
        
		if (vd->vdev_physpath != NULL) {
//            vdev_iokit_log_str( "vdev_iokit_open: physpath 2", vd->vdev_physpath );
			error = vdev_iokit_open_by_path(dvd, vd->vdev_physpath);
        }
        
		/*
		 * Note that we don't support the legacy auto-wholedisk support
		 * as above.  This hasn't been used in a very long time and we
		 * don't need to propagate its oddities to this edge condition.
		 */
        /*  This is redundant, but will attempt the open again after
         *   the previous attempts by path and physpath
         */
		if (error && vd->vdev_path != NULL) {
//            vdev_iokit_log_str( "vdev_iokit_open: path 2", vd->vdev_path );
			error = vdev_iokit_open_by_path(dvd, vd->vdev_path);
        }
        
        if (error && vd->vdev_guid != 0) {
//            vdev_iokit_log_num( "vdev_iokit_open: guid 2", vd->vdev_guid );
			error = vdev_iokit_open_by_guid(dvd, vd->vdev_guid);
        }
	}
    
    /* If it couldn't be opened, back out now */
    if (error != 0) {
//        vdev_iokit_log( "vdev_iokit_open: couldn't find/open vd" );
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (error);
	}
    
    /* Sync the disk if needed */
    vdev_iokit_sync(dvd,0);
    
    /*
	 * Once a device is opened, verify that the physical device path (if
	 * available) is up to date.
	 */
    char *physpath = 0;
    
    physpath = kmem_alloc(MAXPATHLEN, KM_NOSLEEP);

    if (vdev_iokit_physpath(vd, physpath) == 0 &&
        (vd->vdev_physpath == NULL ||
         strcmp(vd->vdev_physpath, physpath) != 0)) {
            
            if (vd->vdev_physpath) {
                spa_strfree(vd->vdev_physpath);
            }
        vd->vdev_physpath = spa_strdup(physpath);
    }
    kmem_free(physpath, MAXPATHLEN);
    
    /*
     *  XXX - Replace with IOKit lookup -
     *       currently in vdev_iokit_util.cpp
     *
     *   1 physpath - IOService path, or file path
     *   2 path - file path
     *   3 guid - vdev guid
     */
	/* ### APPLE TODO ### */
	/* ddi_devid_str_decode */
    

    /*
     *  XXX - Obtain an opened/referenced IOKit handle for the device
     */
	/* Obtain an opened/referenced vnode for the device. */
    /*
	error = vnode_open(vd->vdev_path, spa_mode(vd->vdev_spa), 0, 0, &devvp, context);
	if (error) {
		goto out;
	}
     */
    
//    if (!dvd->vd_iokit_hl) {
//        error = EINVAL;		/* presume failure */
//
//        error = vdev_iokit_handle_open(dvd, spa_mode(vd->vdev_spa));
//        
//        if (error != 0) {
//            goto out;
//        }
//    }
    
    /*
     if (!vnode_isblk(devvp)) {
     error = ENOTBLK;
     goto out;
     }
     */

skip_open:
    
	/*
	 * Determine the actual size of the device.
	 */
    if( vdev_iokit_get_size(dvd, size, max_size, ashift) != 0 ) {
//        vdev_iokit_log( "vdev_iokit_open: couldn't get size" );
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		error = ENXIO;
        goto out;
    }
    
    /*
     *  XXX - Not necessary here - already done
     *   by IOKit when opening the device handle
     */
	/*
	 *  Disallow opening of a device that is currently in use.
	 *  Flush out any old buffers remaining from a previous use.
	 */
    /*
	if ((error = vfs_mountedon(devvp))) {
		goto out;
	}
	if (VNOP_FSYNC(devvp, MNT_WAIT, context) != 0) {
		error = ENOTBLK;
		goto out;
	}
	if ((error = buf_invalidateblks(devvp, BUF_WRITE_DATA, 0, 0))) {
		goto out;
	}
     */
    
    
    /*
     * Done above in vdev_iokit_get_size
     */
    /*
	if (VNOP_IOCTL(devvp, DKIOCGETBLOCKSIZE, (caddr_t)&blksize, 0, context)
	       	!= 0 || VNOP_IOCTL(devvp, DKIOCGETBLOCKCOUNT, (caddr_t)&blkcnt,
		0, context) != 0) {

		error = EINVAL;
		goto out;
	}
	*size = blkcnt * (uint64_t)blksize;
     */

    
    /* Allocate command pools for async IO */
    if (!dvd->in_command_pool || (spa_mode(vd->vdev_spa) > FREAD && !dvd->out_command_pool)) {
        
        /* Allocate several io_context objects */
        if( vdev_iokit_context_pool_alloc(dvd) != 0 ) {
            vdev_iokit_log_ptr("ZFS: vdev_iokit_handle_open: couldn't allocate context pools:", dvd);
            error =     ENOMEM;
            goto out;
        }
    }
    
    
	/*
	 *  ### APPLE TODO ###
	 * If we own the whole disk, try to enable disk write caching.
	 */

    
    
    /*
     * Done above in vdev_iokit_get_size
     */
	/*
	 * Take the device's minimum transfer size into account.
	 */
	//*ashift = highbit(MAX(blksize, SPA_MINBLOCKSIZE)) - 1;

    /*
     *  XXX - not a problem with this IOKit interface...
     */
    /*
     * Setting the vdev_ashift did in fact break the pool for import
     * on ZEVO. This puts the logic into question. It appears that vdev_top
     * will also then change. It then panics in space_map from metaslab_alloc
     */
//    if (*ashift > 0)
//        vd->vdev_ashift = *ashift;

	/*
	 * Clear the nowritecache bit, so that on a vdev_reopen() we will
	 * try again.
	 */
	vd->vdev_nowritecache = B_FALSE;
 
out:
	if (error != 0) {
        if (dvd->vd_iokit_hl) {
//            vdev_iokit_log_ptr( "vdev_iokit_open: bailing on handle open, trying to close handle [%p]", dvd->vd_iokit_hl );
            vdev_iokit_handle_close(dvd, spa_mode(vd->vdev_spa));
        }
        
//        vdev_iokit_log_ptr( "vdev_iokit_open: couldn't open", dvd );
        
        /* Clear vdev_tsd, see below */
        vdev_iokit_free(&dvd);

		/*
		 * Since the open has failed, vd->vdev_tsd should
		 * be NULL when we get here, signaling to the
		 * rest of the spa not to try and reopen or close this device
		 */
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
	}
    
//    vdev_iokit_log_num( "vdev_iokit_open: error value:", error );
	return (0);
}

extern void
vdev_iokit_close(vdev_t *vd)
{
vdev_iokit_log_ptr( "vdev_iokit_close: vd:",            vd );
    
	vdev_iokit_t *dvd =     0;
    
//vdev_iokit_log_ptr( "vdev_iokit_close: vd->vdev_tsd:",  vd->vdev_tsd );
//vdev_iokit_log_num( "vdev_iokit_close: reopening:",     vd->vdev_reopening );
//vdev_iokit_log_num( "vdev_iokit_close: spa mode:",      spa_mode(vd->vdev_spa) );
//vdev_iokit_log_num( "vdev_iokit_close: vd state:",      vd->vdev_state );
//vdev_iokit_log_num( "vdev_iokit_close: prevstate:",     vd->vdev_prevstate );
    
//    if (!vd || !vd->vdev_tsd || vd->vdev_reopening)
//		return;

    if (!vd || !vd->vdev_tsd)
		return;
    
//    if (vd->vdev_reopening)
//        vdev_iokit_log( "vdev_iokit_close: reopening (unhandled)" );
    
    dvd =       (vdev_iokit_t *)vd->vdev_tsd;
    
    if (dvd->vd_iokit_hl != NULL) {
        /* Sync the disk if needed */
        vdev_iokit_sync(dvd, 0);
        
        /* Close the iokit handle */
        vdev_iokit_handle_close(dvd, spa_mode(vd->vdev_spa));
        
		dvd->vd_iokit_hl =  0;
	}
    
    /* Teardown context pool */
    vdev_iokit_context_pool_free(dvd);
    
	vd->vdev_delayed_close = B_FALSE;
    
	vdev_iokit_free((vdev_iokit_t**)&(vd->vdev_tsd));
    vd->vdev_tsd =  0;
    dvd =           0;
    
    return;
}

extern void
vdev_iokit_ioctl_done(void *zio_arg, const int error)
{
//vdev_iokit_log_ptr( "vdev_iokit_ioctl_done: zio_arg:",  zio_arg );
//vdev_iokit_log_num( "vdev_iokit_ioctl_done: error:",    error );
	zio_t *zio = zio_arg;

	zio->io_error = error;

	//zio_next_stage_async(zio);
    zio_interrupt(zio);
}

extern int
vdev_iokit_io_start(zio_t *zio)
{
	vdev_t *vd = 0;
	vdev_iokit_t *dvd = 0;
	int error = 0;
    
    //vdev_iokit_log_ptr("ZFS: vdev_iokit_io_start: zio:", zio);
    
    if (!zio || !zio->io_vd || !zio->io_vd->vdev_tsd ||
        !(zio->io_data) || zio->io_size == 0)
        return EINVAL;
    
	vd = zio->io_vd;
	dvd = vd->vdev_tsd;
    
	if (zio->io_type == ZIO_TYPE_IOCTL) {
		zio_vdev_io_bypass(zio);

		/* XXPOLICY */
		if (vdev_is_dead(vd)) {
			zio->io_error = ENXIO;
			//zio_next_stage_async(zio);
			return (ZIO_PIPELINE_CONTINUE);
            //return;
		}

		switch (zio->io_cmd) {

		case DKIOCFLUSHWRITECACHE:

			if (zfs_nocacheflush)
				break;

			if (vd->vdev_nowritecache) {
				zio->io_error = SET_ERROR(ENOTSUP);
				break;
			}

            /*
             *  XXX - No context needed
             */
            /*
			context = vfs_context_create((vfs_context_t)0);
             */
                
            /*
             *  XXX - Replace with IOKit ioctl passthrough
             */
            /*
			error = VNOP_IOCTL(dvd->vd_devvp, DKIOCSYNCHRONIZECACHE, NULL, FWRITE, context);
             */
            
//vdev_iokit_log("ZFS: vdev_iokit_io_start: sync");
                
            /* Use IOKit to sync the disk */
            vdev_iokit_sync(dvd, zio);
                
//
//
//            vdev_iokit_ioctl( vd, zio );
//
//
            /*
             *  XXX - No context needed
             */
            /*
			(void) vfs_context_rele(context);
             */
                
			if (error == 0)
				vdev_iokit_ioctl_done(zio, error);
			else
				error = ENOTSUP;

			if (error == 0) {
				/*
				 * The ioctl will be done asychronously,
				 * and will call vdev_iokit_ioctl_done()
				 * upon completion.
				 */
				return ZIO_PIPELINE_STOP;
			} else if (error == ENOTSUP || error == ENOTTY) {
				/*
				 * If we get ENOTSUP or ENOTTY, we know that
				 * no future attempts will ever succeed.
				 * In this case we set a persistent bit so
				 * that we don't bother with the ioctl in the
				 * future.
				 */
				vd->vdev_nowritecache = B_TRUE;
			}
			zio->io_error = error;

			break;

		default:
			zio->io_error = SET_ERROR(ENOTSUP);
		}

		//zio_next_stage_async(zio);
        return (ZIO_PIPELINE_CONTINUE);
	}

	if (zio->io_type == ZIO_TYPE_READ && vdev_cache_read(zio) == 0)
        return (ZIO_PIPELINE_STOP);
    //		return;

	if ((zio = vdev_queue_io(zio)) == NULL)
        return (ZIO_PIPELINE_CONTINUE);
    //		return;

    /*
     *  XXX
     */
//	flags = (zio->io_type == ZIO_TYPE_READ ? B_READ : B_WRITE);
	//flags |= B_NOCACHE;

//	if (zio->io_flags & ZIO_FLAG_FAILFAST)
//		flags |= B_FAILFAST;

	/*
	 * Check the state of this device to see if it has been offlined or
	 * is in an error state.  If the device was offlined or closed,
	 * dvd will be NULL and buf_alloc below will fail
	 */
	//error = vdev_is_dead(vd) ? ENXIO : vdev_error_inject(vd, zio);
	if (vdev_is_dead(vd)) {
        error = ENXIO;
    }

	if (error) {
		zio->io_error = error;
		//zio_next_stage_async(zio);
		return (ZIO_PIPELINE_CONTINUE);
	}

    /*
     *  XXX - Instead pass the zio/flags to IOKit
     */
    /*
	bp = buf_alloc(dvd->vd_devvp);

	ASSERT(bp != NULL);
     */

    /*
     *  XXX - Instead pass the zio/flags to IOKit
     */
    /*
	buf_setflags(bp, flags);
	buf_setcount(bp, zio->io_size);
	buf_setdataptr(bp, (uintptr_t)zio->io_data);
     */

    /*
     *  XXX - Instead calculate this in IOKit if needed
     */
    /*
    if (zfs_iokit_vdev_ashift && vd->vdev_ashift) {
        buf_setlblkno(bp, zio->io_offset>>vd->vdev_ashift);
        buf_setblkno(bp,  zio->io_offset>>vd->vdev_ashift);
    } else {
        buf_setlblkno(bp, lbtodb(zio->io_offset));
        buf_setblkno(bp, lbtodb(zio->io_offset));
    }
     */
    
    /*
     *  XXX - Instead pass the zio/flags to IOKit
     */
    /*
	buf_setsize(bp, zio->io_size);
     */
    
    /*
     *  XXX - Instead pass the callback to IOKit
     */
    /*
	if (buf_setcallback(bp, vdev_iokit_io_intr, zio) != 0)
		panic("vdev_iokit_io_start: buf_setcallback failed\n");
     */
    
    /*
     *  XXX - Instead do the read/write strategy in IOKit
     */
    /*
	if (zio->io_type == ZIO_TYPE_WRITE) {
		vnode_startwrite(dvd->vd_devvp);
	}
	error = VNOP_STRATEGY(bp);
     */
    
    error =     vdev_iokit_strategy( dvd, zio );
    
//	if (error != 0) {
//        vdev_iokit_log_num( "vdev_iokit_io_start: error returned by vdev_iokit_strategy (%d)", error );
//    }

    return (ZIO_PIPELINE_STOP);
}

extern void
vdev_iokit_io_done(zio_t *zio)
{
    /*
     *  XXX - TO DO
     *
     *  By attaching to the IOMedia device, we can both check
     *   the status via IOKit functions, and be informed of
     *   device changes.
     *
     *  Call an IOKit helper function to check the IOMedia
     *   device - status, properties, and/or ioctl.
     */
    vdev_t * vd = 0;
 
//vdev_iokit_log_ptr( "ZFS: vdev_iokit_io_done: zio:", zio );
    
    if (!zio)
        return;

	vd = zio->io_vd;
    
    if (!vd)
        return;
    
    /*     Not needed, currently
     *
//    vdev_iokit_t * dvd = 0;
//
//    dvd = vd->vdev_tsd;
//    
//    if (!dvd)
//        return;
     *
     */
    
	if (zio->io_error == EIO) {
//vdev_iokit_log_num( "ZFS: vdev_iokit_io_done: zio->io_error:", (uint64_t)zio->io_error );
        if ( !vdev_iokit_status(vd->vdev_tsd) ) {
			vd->vdev_remove_wanted = B_TRUE;
			spa_async_request(zio->io_spa, SPA_ASYNC_REMOVE);
        }
    }
}

/* Read configuration from disk */
int
vdev_iokit_read_label(vdev_iokit_t * dvd, nvlist_t **config)
{
	vdev_label_t *label =   0;
    char *pool_name =       0;
    nvlist_t *vdev_tree =   0;
    
    size_t labelsize =      VDEV_SKIP_SIZE + VDEV_PHYS_SIZE;
    uint64_t guid =         0;
    uint64_t id =           0;
	uint64_t s = 0, size =  0;
    uint64_t offset, state, txg =   0;
	int l;
	int error =             EINVAL;
    
//    vdev_iokit_log_ptr("ZFS: vdev_iokit_read_label: dvd->vd_iokit_hl:", dvd->vd_iokit_hl);
    
	/*
	 * Read the device label and build the nvlist.
	 */
    
    if (!dvd || !dvd->vd_iokit_hl || !dvd->vd_zfs_hl)
        return EINVAL;
    
    /* Open the IOKit handle */
    error =     vdev_iokit_handle_open(dvd, FREAD);
    
    if (error != 0) {
//        vdev_iokit_log_num("ZFS: vdev_iokit_read_label: Couldn't open handle. error:", error);
		return (SET_ERROR(EIO));
    }
    
	if (vdev_iokit_get_size(dvd, &s, 0, 0) != 0) {
//        vdev_iokit_log("ZFS: vdev_iokit_read_label: Couldn't get disk size");
        /* Close the disk */
		(void) vdev_iokit_handle_close(dvd, FREAD);
		return (SET_ERROR(EIO));
	}
    
	size =      P2ALIGN_TYPED(s, sizeof (vdev_label_t), uint64_t);
	label =     kmem_alloc(sizeof (vdev_label_t), KM_NOSLEEP);
    
    if (!label) {
    	(void) vdev_iokit_handle_close(dvd, FREAD);
        return ENOMEM;
    }
    
	*config = 0;
	for (l = 0; l < VDEV_LABELS; l++) {
        
//        vdev_iokit_log_num("ZFS: vdev_iokit_read_label: reading label", l);
        
		/* read vdev label */
		offset = vdev_label_offset(size, l, 0);
        
        /* If label is outside disk boundaries, we're done */
        if (offset > s || offset+labelsize > s) {
//            vdev_iokit_log("ZFS: vdev_iokit_read_label: Outside disk boundaries, next...");
            break;
        }
        
		if (vdev_iokit_physio(dvd, (void*)label, labelsize, offset, FREAD) != 0) {
//            vdev_iokit_log("ZFS: vdev_iokit_read_label: Couldn't do physio");
			continue;
        }
        
        error = nvlist_unpack(label->vl_vdev_phys.vp_nvlist,
                              sizeof (label->vl_vdev_phys.vp_nvlist), config, 0);
        
//        vdev_iokit_log_num("ZFS: vdev_iokit_read_label: unpack error:", error);
        
		if ( error != 0) {
            
            //vdev_iokit_log_ptr("ZFS: vdev_iokit_read_label: Couldn't unpack nvlist:", label);
            //vdev_iokit_log_ptr("ZFS: vdev_iokit_read_label: label contents:", &(label->vl_vdev_phys));
            //vdev_iokit_log_ptr("ZFS: vdev_iokit_read_label: vl_vdev_phys contents:", label->vl_vdev_phys.vp_nvlist);
            
			*config = NULL;
			continue;
		}
/*
		if (nvlist_lookup_string(*config, ZPOOL_CONFIG_POOL_NAME, &pool_name) != 0 ||
            nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_GUID, &guid) != 0 ||
            nvlist_lookup_uint64(*config, ZPOOL_CONFIG_TOP_GUID, &guid) != 0 ||
            nvlist_lookup_nvlist(*config, ZPOOL_CONFIG_VDEV_TREE, &vdev_tree) != 0 ||
            nvlist_lookup_uint64(vdev_tree, ZPOOL_CONFIG_ID, &id) != 0 ||
            nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_TXG, &txg) != 0 ||
            nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_STATE, &state) != 0 ||
            nvlist_lookup_uint64(*config, ZPOOL_CONFIG_GUID, &guid) != 0 ||
            state >= POOL_STATE_DESTROYED || txg == 0) {
  */
//            vdev_iokit_log_ptr("ZFS: vdev_iokit_read_label: Invalid config\n", *config);

        if (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_TXG, &txg) != 0 ||
            nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_STATE, &state) != 0 ||
            state >= POOL_STATE_DESTROYED || txg == 0) {
        
			nvlist_free(*config);
			*config = NULL;
			continue;
		}
        
//        vdev_iokit_log_ptr("ZFS: vdev_iokit_read_label: valid label found", label);
//        vdev_iokit_log_ptr("ZFS: vdev_iokit_read_label: valid config found", config);
        break;
	}
    
    if (label) {
        kmem_free(label, sizeof (vdev_label_t));
    }
    
	(void) vdev_iokit_handle_close(dvd, FREAD);
    
	if (*config == NULL) {
//        vdev_iokit_log_ptr("ZFS: vdev_iokit_read_label: No config found\n", *config);
		error = SET_ERROR(EIDRM);
    }
    
//    vdev_iokit_log_num("ZFS: vdev_iokit_read_label: error value\n", error);
	return (error);
}

/*
 * Given the root disk device handle, read the label from
 * the device, and construct a configuration nvlist.
 */
int
vdev_iokit_read_rootlabel(char *devpath, char *devid, nvlist_t **config)
{
    vdev_iokit_t * dvd =    0;
    int error =             EINVAL;
    
    error =     vdev_iokit_alloc(&dvd);
    
    if (error)
        return error;
    
	/* Locate the vdev by pathname */
    error =     vdev_iokit_find_by_path(dvd, devpath);
    
    if (error) {
        goto error;
    }
    
    error =     vdev_iokit_read_label(dvd, config);
    
error:
    vdev_iokit_free(&dvd);
    
    return error;
}

vdev_ops_t vdev_iokit_ops = {
	vdev_iokit_open,
	vdev_iokit_close,
	vdev_default_asize,
	vdev_iokit_io_start,
	vdev_iokit_io_done,
	vdev_iokit_state_change,  /* vdev_op_state_change */
	vdev_iokit_hold,
	vdev_iokit_rele,
	VDEV_TYPE_DISK,         /* name of this vdev type */
	B_TRUE                  /* leaf vdev */
};
