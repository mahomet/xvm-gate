/*
 * Copyright 2008 Sun Microsystems, Inc.  All Rights Reserved.
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation. This program is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License version 2 for more details (a copy is
 * included in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version 2
 * along with this program; If not, see
 *    http://doc.java.sun.com/DocWeb/license.html.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 */

#ifndef _VDISK_LOG_H
#define	_VDISK_LOG_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <assert.h>
#include <time.h>
#include <strings.h>
#include <sys/types.h>


typedef enum {
	VDISK_LOPT_FILE			= 1 << 0,
	VDISK_LOPT_BUF			= 1 << 1,
	VDISK_LOPT_DONT_WRAP_BUF	= 1 << 2,
	VDISK_LOPT_TIMESTAMP		= 1 << 3,
	VDISK_LOPT_CLEAR_BUF		= 1 << 31
} vdisk_log_options_t;

typedef enum {
	VDISK_LFLG_ERR		= 1 << 0, /* errors always get logged to file */
	VDISK_LFLG_INFO		= 1 << 4,
	VDISK_LFLG_HDRS		= 1 << 5,
	VDISK_LFLG_SEGS		= 1 << 6,
	VDISK_LFLG_DATA		= 1 << 7,
} vdisk_log_flags_t;

typedef struct vdisk_log_s *vdisk_log_t;

#ifdef DEBUG
#define	VDISK_DLOG(log, flags, fmt, args...) \
	vdisk_log(log, flags, "%s[%d]: " fmt, __FILE__, __LINE__, ##args)
#else
#define	VDISK_DLOG(args...)
#endif
#define	VDISK_LOG(log, flags, fmt, args...) \
	vdisk_log(log, flags, "%s[%d]: " fmt, __FILE__, __LINE__, ##args)

int vdisk_log_init(vdisk_log_t *log);
void vdisk_log_fini(vdisk_log_t *log);
void vdisk_log(vdisk_log_t log, unsigned int flags, char *fmt, ...);


#ifdef	__cplusplus
}
#endif
#endif /* _VDISK_LOG_H */
