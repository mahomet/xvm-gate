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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <assert.h>
#include <time.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>

#include "vdisk_log.h"


char *vdisk_log_path = "/var/log/xen";
size_t vdisk_buf_len = 0x010000;


struct vdisk_log_s {
	pthread_mutex_t	vl_mutex;

	char		*vl_filename;
	FILE		*vl_fd;

	int		vl_full;
	char		*vl_cptr;
	char		*vl_eptr;
	char		*vl_buf;
};

#ifdef DEBUG
vdisk_log_options_t vdisk_log_opts =
    VDISK_LOPT_TIMESTAMP | VDISK_LOPT_BUF | VDISK_LOPT_DONT_WRAP_BUF;
vdisk_log_flags_t vdisk_log_enable = VDISK_LFLG_ERR | VDISK_LFLG_INFO;
#else
vdisk_log_options_t vdisk_log_opts = VDISK_LOPT_TIMESTAMP | VDISK_LOPT_FILE;
vdisk_log_flags_t vdisk_log_enable = VDISK_LFLG_ERR;
#endif

static void vdisk_log_fmt(vdisk_log_t state, int flags, char *fmt, ...);
static void vdisk_log_va(vdisk_log_t state, int flags, char *fmt, va_list ap);
static void vdisk_log_file(vdisk_log_t log, char *fmt, va_list ap);
static void vdisk_log_buf(vdisk_log_t log, char *fmt, va_list ap);


int
vdisk_log_init(vdisk_log_t *log)
{
	vdisk_log_t state;
	int len;
	int rc;

	state = malloc(sizeof (struct vdisk_log_s));
	if (state == NULL) {
		return (-1);
	}
	state->vl_fd = NULL;
	rc = pthread_mutex_init(&state->vl_mutex, NULL);
	if (rc != 0) {
		goto loginitfail_mutex;
	}

	len = snprintf(NULL, 0, "%s/vdisk.%d.log", vdisk_log_path,
	    (int)getpid());
	state->vl_filename = malloc(len + 1);
	if (state->vl_filename == NULL) {
		goto loginitfail_name_alloc;
	}
	(void) snprintf(state->vl_filename, len + 1, "%s/vdisk.%d.log",
	    vdisk_log_path, (int)getpid());

	state->vl_full = 0;
	state->vl_buf = malloc(vdisk_buf_len + 1);
	if (state->vl_buf == NULL) {
		goto loginitfail_buf_alloc;
	}
	bzero(state->vl_buf, vdisk_buf_len + 1);
	state->vl_cptr = state->vl_buf;
	state->vl_eptr = state->vl_buf + vdisk_buf_len;

	*log = state;
	return (0);

loginitfail_buf_alloc:
	free(state->vl_filename);
loginitfail_name_alloc:
	(void) pthread_mutex_destroy(&state->vl_mutex);
loginitfail_mutex:
	free(state);
	return (-1);
}


void
vdisk_log_fini(vdisk_log_t *log)
{
	vdisk_log_t state;

	state = *log;
	if (state->vl_fd != NULL) {
		fclose(state->vl_fd);
	}
	free(state->vl_buf);
	free(state->vl_filename);
	(void) pthread_mutex_destroy(&state->vl_mutex);
	free(state);
	*log = NULL;
}


void
vdisk_log(vdisk_log_t state, unsigned int flags, char *fmt, ...)
{
	struct tm lt;
	time_t clock;
	va_list ap;


	if ((vdisk_log_enable & flags) == 0) {
		return;
	}

	pthread_mutex_lock(&state->vl_mutex);

	if (vdisk_log_opts & VDISK_LOPT_TIMESTAMP) {
		clock = time(NULL);
		(void) localtime_r(&clock, &lt);
		vdisk_log_fmt(state, flags, "%04d-%02d-%02d %02d:%02d:%02d ",
		    lt.tm_year + 1900, lt.tm_mon, lt.tm_mday,
		    lt.tm_hour, lt.tm_min, lt.tm_sec);
	}

	va_start(ap, fmt);
	vdisk_log_va(state, flags, fmt, ap);
	va_end(ap);

	pthread_mutex_unlock(&state->vl_mutex);
}


static void
vdisk_log_fmt(vdisk_log_t state, int flags, char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vdisk_log_va(state, flags, fmt, ap);
	va_end(ap);
}


static void
vdisk_log_va(vdisk_log_t state, int flags, char *fmt, va_list ap)
{
	if ((flags & VDISK_LFLG_ERR) || (vdisk_log_opts & VDISK_LOPT_FILE)) {
		vdisk_log_file(state, fmt, ap);
	}
	if (vdisk_log_opts & VDISK_LOPT_BUF) {
		vdisk_log_buf(state, fmt, ap);
	}
}


static void
vdisk_log_file(vdisk_log_t state, char *fmt, va_list ap)
{
	if (state->vl_fd == NULL) {
		state->vl_fd = fopen(state->vl_filename, "w");
		if (state->vl_fd == NULL) {
			return;
		}
	}

	(void) vfprintf(state->vl_fd, fmt, ap);
}


static void
vdisk_log_buf(vdisk_log_t state, char *fmt, va_list ap)
{
	size_t second;
	size_t first;
	size_t len;
	char *buf;


	if (vdisk_log_opts & VDISK_LOPT_CLEAR_BUF) {
		vdisk_log_opts &= ~VDISK_LOPT_CLEAR_BUF;
		bzero(state->vl_buf, vdisk_buf_len);
		state->vl_cptr = state->vl_buf;
		state->vl_full = 0;
	}

	if (state->vl_full) {
		return;
	}

	len = vsnprintf(NULL, 0, fmt, ap);
	if ((state->vl_cptr + len) < state->vl_eptr) {
		(void) vsnprintf(state->vl_cptr, len + 1, fmt, ap);
		state->vl_cptr += len;
		/* mark the end of the buffer */
		state->vl_cptr[0] = '@';

	} else {
		buf = malloc(len + 1);
		if (buf == NULL) {
			return;
		}
		(void) vsnprintf(buf, len + 1, fmt, ap);
		first = state->vl_eptr - state->vl_cptr;
		bcopy(buf, state->vl_cptr, first);
		second = len - first;
	
		if (vdisk_log_opts & VDISK_LOPT_DONT_WRAP_BUF) {
			free(buf);
			state->vl_cptr[0] = '\0';
			state->vl_full = 1;
			return;
		}

		bcopy(buf + first, state->vl_buf, second);
		state->vl_cptr = state->vl_buf + second;
		free(buf);
	}
}
