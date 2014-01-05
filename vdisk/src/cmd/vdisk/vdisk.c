/*
 * Copyright 2009 Sun Microsystems, Inc.  All Rights Reserved.
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
#include <strings.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stropts.h>
#include <locale.h>
#include <libintl.h>
#include <signal.h>
#include <bsm/libbsm.h>
#include <priv.h>
#include <libxml/tree.h>

#include <xen/xen.h>
#include <xen/io/blkif.h>

#include "VBox/VBoxHDD.h"
#include "vdisk.h"
#include "vdisk_log.h"

/* wmb needed to use the RING macros pulled in by xen/io/blkif.h */
#define	wmb membar_producer

#ifdef DEBUG
extern vdisk_log_flags_t vdisk_log_enable;
#endif

/*
 * It would be better to include xpvtap.h, but until it is prevalent on build
 * machines, better to just copy and paste the bits here.
 */
/* Notification from user app that it has pushed responses */
#define	XPVTAP_IOCTL_RESP_PUSH		1

#define	VD_RING_SIZE	__RING_SIZE((blkif_sring_t *)0, PAGESIZE)
#define	VD_GREF_BUFSIZE	\
	(VD_RING_SIZE * BLKIF_MAX_SEGMENTS_PER_REQUEST * PAGESIZE)

#define	VD_REQ_OFFSET(req)	((uint64_t)req->sector_number * 512)
#define	VD_REQ_ADDR(st, req, segid) ((void *)(uintptr_t)\
	((uintptr_t)st->grefbuf + \
	(((req->id * BLKIF_MAX_SEGMENTS_PER_REQUEST) + segid) * PAGESIZE) + \
	(req->seg[segid].first_sect * 512)))
#define	VD_SEG_SIZE(req, segid) ((uint64_t)((req->seg[segid].last_sect - \
	req->seg[segid].first_sect + 1) * 512));


/* leastpriv info */
#define	PU_RESETGROUPS		0x0001	/* Remove supplemental groups */
#define	PU_CLEARLIMITSET	0x0008	/* L=0 */
#ifndef	PRIV_XVM_CONTROL
#define	PRIV_XVM_CONTROL	((const char *)"xvm_control")
#endif
extern int __init_daemon_priv(int, uid_t, gid_t, ...);

typedef int (*vd_func_t)(PVBOXHDD, uint64_t, void *, size_t);
typedef struct vd_rw_s {
	const char	*rw_str;
	vd_func_t	rw_func;
} vd_rw_t;

/* keep the state global so we can debug easier */
typedef struct vd_state_s {
	/* ring state for requests/responses */
	blkif_back_ring_t	ring;

	/* shared ring page between driver and user daemon */
	blkif_sring_t		*sringp;

	/* gref pages */
	void			*grefbuf;

	/* vdisk handle */
	void			*vdh;

	/* vbox handle */
	PVBOXHDD		vboxh;

	/* xpvtap minor node file descriptor */
	int			xfd;

	/* set to 0 to stop running */
	int			running;

	/* poll fds */
	fd_set			fds;

	char			*vdiskpath;
	char			*xpvpath;
	int			flush_flag;
} vd_state_t;
vd_state_t *vd_statep;

typedef struct vd_option_s {
	const char	*v_name;
	int		(*v_cmd)(char *vdiskpath);
} vd_option_t;
static int vd_query_sectors(char *vdiskpath);
static vd_option_t vd_options[] = {
	{"sectors", vd_query_sectors},
};
#define	VD_OPT_CNT	(sizeof (vd_options) / sizeof (vd_option_t))

#define	VD_WRITE	0
#define	VD_READ		1
vd_rw_t vd_wr[2] = {{"WR", (void *)VDWrite}, {"RD", (void *)VDRead}};

/* vd_ log is a separate global because it lives across the fork */
vdisk_log_t vd_log;


static int daemon(int nochdir, int noclose);

static void vd_usage(FILE *stream);
static int vd_query(char *vdiskpath, char *option);
static void vd_setup_privs();
static void vd_run(char *vdiskpath, char *xpvpath);
static int vd_req_rw(vd_state_t *st, blkif_request_t *req, vd_rw_t *rw);
static int vd_req_write_barrier(vd_state_t *st, blkif_request_t *req);
static int vd_req_flush(vd_state_t *st, blkif_request_t *req);
static int vd_resp_push(vd_state_t *st, uint64_t id, uint8_t operation,
    int16_t status);
static void vd_cleanup(int signo);



/*
 * main()
 */
int
main(int argc, char *argv[])
{
	char *vdiskpath;
	char *xpvpath;
	char *pidpath;
	char *query;
	pid_t pid;
	FILE *fd;
	int opt;
	int rc;


	/* run as user xvm with restricted privileges */
	vd_setup_privs();

	/* option defaults */
	vdiskpath = NULL;
	xpvpath = NULL;
	pidpath = NULL;
	query = NULL;

	while ((opt = getopt(argc, argv, "h?x:f:p:q:")) != -1) {
		switch (opt) {
		/* option to query */
		case 'q':
			query = optarg;
			break;
		/* path to vdisk file */
		case 'f':
			vdiskpath = optarg;
			break;
		/* path to xpvtap device minor node */
		case 'x':
			xpvpath = optarg;
			break;
		/* optional file path to write pid to */
		case 'p':
			pidpath = optarg;
			break;
		case 'h':
		case '?':
			vd_usage(stdout);
			exit(0);
		default:
			vd_usage(stderr);
			exit(-1);
		}
	}

	/* make sure the manditory options were passed in */
	if ((vdiskpath == NULL) ||
	    ((xpvpath == NULL) && (query == NULL)) ||
	    ((xpvpath != NULL) && (query != NULL))) {
		vd_usage(stderr);
		exit(-1);
	}

	if (query != NULL) {
		return (vd_query(vdiskpath, query));
	}

	/* setup logging */
	rc = vdisk_log_init(&vd_log);
	if (rc != 0) {
		fprintf(stderr, "%s % d\n",
		    gettext("ERROR: unable to initialize logging"), rc);
	}

	/* Daemonize and, if asked to, store pid */
	rc = daemon(0, 0);
	if (rc != 0) {
		VDISK_LOG(vd_log, VDISK_LFLG_ERR, "%s % d\n",
		    gettext("ERROR: unable to fork process"), rc);
		vdisk_log_fini(&vd_log);
		exit(-1);
	}
	if (pidpath != NULL) {
		pid = getpid();
		fd = fopen(pidpath, "w");
		if (fd == NULL) {
			VDISK_LOG(vd_log, VDISK_LFLG_ERR, "%s: %s\n",
			    gettext("ERROR: unable to create pid file"),
			    pidpath);
			vdisk_log_fini(&vd_log);
			exit(-1);
		}
		fprintf(fd, "%d", (uint_t)pid);
		fclose(fd);
	}

	vd_run(vdiskpath, xpvpath);

	vdisk_log_fini(&vd_log);
	return (0);
}


/*
 * daemon()
 *   turns the caller into a daemon
 */
static int
daemon(int nochdir, int noclose)
{
	int pid;
	int fd;
	int e;

	/* fork and exit if parent */
	pid = fork();
	if (pid == -1) {
		return (-1);
	}
	if (pid != 0) {
		exit(0);
	}

	/* create a new session */
	e = setsid();
	if (e == -1) {
		return (-1);
	}

	/* if nochdir is not set, chdir to root */
	if (!nochdir) {
		e = chdir("/");
		if (e != 0) {
			return (-1);
		}
	}

	/* if noclose is not set, divert std* to /dev/null */
	if (!noclose) {
		fd = open("/dev/null", O_RDWR);
		if (fd == -1) {
			return (-1);
		}
		(void) dup2(fd, STDIN_FILENO);
		(void) dup2(fd, STDOUT_FILENO);
		(void) dup2(fd, STDERR_FILENO);
		(void) close(fd);
	}

	return (0);
}


/*
 * vd_usage()
 */
static void
vd_usage(FILE *stream)
{
	fprintf(stream, "\n%s\n\n",
	    gettext("USAGE: vdisk -f <vdiskpath> "
	    "-x <xpvtap path> [-p <pidfile path>]"));
}


/*
 * vd_setup_privs()
 *    setup app to run as xvm user with restricted privledges.
 */
static void
vd_setup_privs()
{
	if (__init_daemon_priv(PU_RESETGROUPS | PU_CLEARLIMITSET,
	    60, 60, PRIV_XVM_CONTROL, NULL)) {
		VDISK_LOG(vd_log, VDISK_LFLG_ERR, "%s\n",
		    gettext("ERROR: additional privileges are required"));
		exit(1);
	}

	if (priv_set(PRIV_OFF, PRIV_ALLSETS, PRIV_FILE_LINK_ANY, PRIV_PROC_INFO,
	    PRIV_PROC_SESSION, PRIV_PROC_EXEC, NULL)) {
		VDISK_LOG(vd_log, VDISK_LFLG_ERR, "%s\n",
		    gettext("ERROR: failed to set reduced privileges"));
		exit(1);
	}
}


/*
 * vd_query()
 *   query disk options
 */
static int
vd_query(char *vdiskpath, char *option)
{
	int i;

	for (i = 0; i < VD_OPT_CNT; i++) {
		if (strcmp(option, vd_options[i].v_name) == 0) {
			return (vd_options[i].v_cmd(vdiskpath));
		}
	}

	return (-1);
}


/*
 * vd_query_sectors()
 */
static int
vd_query_sectors(char *vdiskpath)
{
	const int sector_size = 512;
	void *vdh;

	vdh = vdisk_open(vdiskpath);
	if (vdh == NULL) {
		fprintf(stderr, "%s: \"%s\"\n",
		    gettext("ERROR: unable to open vdisk"), vdiskpath);
		return (-1);
	}
	printf("%lld\n", (long long)(vdisk_get_size(vdh) / sector_size));

	(void) vdisk_setflags(vdh, VD_NOFLUSH_ON_CLOSE);
	vdisk_close(vdh);
	return (0);
}


/*
 * vd_run()
 */
static void
vd_run(char *vdiskpath, char *xpvpath)
{
	struct sigaction sigact;
	blkif_request_t *req;
	vd_state_t *st;
	int rc;


	/* allocate and init global state */
	vd_statep = malloc(sizeof (*vd_statep));
	bzero(vd_statep, sizeof (*vd_statep));
	st = vd_statep;
	st->vdiskpath = vdiskpath;
	st->xpvpath = xpvpath;

	/* open vdisk */
	st->vdh = vdisk_open(vdiskpath);
	if (st->vdh == NULL) {
		VDISK_LOG(vd_log, VDISK_LFLG_ERR, "%s: \"%s\"\n",
		    gettext("ERROR: Unable to open vdisk"),
		    vdiskpath);
		return;
	}
	st->vboxh = ((vd_handle_t *)st->vdh)->hdd;

	sigact.sa_flags = 0;
	sigact.sa_handler = vd_cleanup;
	rc = sigemptyset(&sigact.sa_mask);
	if (rc != 0) {
		VDISK_LOG(vd_log, VDISK_LFLG_ERR, "%s\n",
		    gettext("ERROR: sigemptyset() failed"));
		goto out;
	}
	rc = sigaction(SIGHUP, &sigact, NULL);
	if (rc != 0) {
		VDISK_LOG(vd_log, VDISK_LFLG_ERR, "%s\n",
		    gettext("ERROR: sigaction(SIGHUP) failed"));
		goto out;
	}
	rc = sigaction(SIGTERM, &sigact, NULL);
	if (rc != 0) {
		VDISK_LOG(vd_log, VDISK_LFLG_ERR, "%s\n",
		    gettext("ERROR: sigaction(SIGTERM) failed"));
		goto out;
	}

	/* Open blktap */
	st->xfd = open(xpvpath, O_RDWR);
	if (st->xfd == -1) {
		VDISK_LOG(vd_log, VDISK_LFLG_ERR, "%s: \"%s\"\n",
		    gettext("ERROR: Unable to open blktap"),
		    xpvpath);
		goto out;
	}

	/* map in and init shared ring between xpvtap and daemon */
	st->sringp = (blkif_sring_t *)mmap((caddr_t)0, PAGESIZE,
	    PROT_READ | PROT_WRITE, MAP_SHARED, st->xfd, 0);
	if (st->sringp == MAP_FAILED) {
		VDISK_LOG(vd_log, VDISK_LFLG_ERR, "%s\n",
		    gettext("ERROR: Unable to mmap ring"));
		goto out;
		return;
	}
	BACK_RING_INIT(&st->ring, st->sringp, PAGESIZE);

	/* map in gref buf */
	st->grefbuf = mmap((caddr_t)0, VD_GREF_BUFSIZE,
	    PROT_READ | PROT_WRITE, MAP_SHARED, st->xfd, PAGESIZE);
	if (st->grefbuf == MAP_FAILED) {
		VDISK_LOG(vd_log, VDISK_LFLG_ERR, "%s\n",
		    gettext("ERROR: Unable to mmap gref buf"));
		goto out;
		return;
	}

	FD_ZERO(&st->fds);
	FD_SET(st->xfd, &st->fds);
	st->running = 1;

	VDISK_LOG(vd_log, VDISK_LFLG_INFO, "starting up\n");
	while (st->running) {
		while (RING_HAS_UNCONSUMED_REQUESTS(&st->ring)) {
			req = NULL;
			req = RING_GET_REQUEST(&st->ring, st->ring.req_cons);
			if (req == NULL) {
				continue;
			}
			st->ring.req_cons++;

			switch (req->operation) {
			case BLKIF_OP_WRITE:
				rc = vd_req_rw(st, req, &vd_wr[VD_WRITE]);
				if (rc != 0) {
					st->running = 0;
				}
				st->flush_flag = 1;
				break;

			case BLKIF_OP_WRITE_BARRIER:
				rc = vd_req_write_barrier(st, req);
				if (rc != 0) {
					st->running = 0;
				}
				break;

			case BLKIF_OP_FLUSH_DISKCACHE:
				rc = vd_req_flush(st, req);
				if (rc != 0) {
					st->running = 0;
				}
				break;

			case BLKIF_OP_READ:
				rc = vd_req_rw(st, req, &vd_wr[VD_READ]);
				if (rc != 0) {
					st->running = 0;
				}
				break;

			default:
				st->running = 0;
			}
		}

		if (!st->running) {
			break;
		}

		/* sleep until the driver tells us there are more requests */
		(void) select(st->xfd + 1, &st->fds, 0, 0, NULL);
	}
	VDISK_LOG(vd_log, VDISK_LFLG_INFO, "shutting down\n");

out:
	/*
	 * vdisk drivers call flush during close even if no writes
	 * have occurred to the file.  Set noflush_on_close flag
	 * if no writes indicating to not flush during the close.
	 */
	if (st->flush_flag == 0) {
		rc = vdisk_setflags(st->vdh, VD_NOFLUSH_ON_CLOSE);
		if (rc != 0)
			VDISK_LOG(vd_log, VDISK_LFLG_ERR, "%s\n",
			    gettext("ERROR: Unable to set noflush_on_close"));
	}
	vdisk_close(st->vdh);
}


/*
 * vd_req_rw()
 */
static int
vd_req_rw(vd_state_t *st, blkif_request_t *req, vd_rw_t *rw)
{
	uint64_t cur_offset;
	uint_t num_segs;
	int16_t status;
	uint64_t size;
	uint64_t off;
	void *addr;
	int rc;
	int i;


	status = BLKIF_RSP_OKAY;

	/*
	 * Setup the offset into the file for the first segment's first
	 * valid sector & the associated guest buffer address.
	 */
	off = VD_REQ_OFFSET(req);
	addr = VD_REQ_ADDR(st, req, 0);
	size = 0;

	VDISK_DLOG(vd_log, VDISK_LFLG_HDRS,
	    "%s:i=%llx;sc=%llx;nm=%x;a=%p;o=%llx\n",
	    rw->rw_str, (long long)req->id, (long long)req->sector_number,
	    (int)req->nr_segments, addr, (long long)off);

#ifdef DEBUG
	if (vdisk_log_enable & VDISK_LFLG_DATA) {
		{
			char *b;
			b = (char *)addr;
			VDISK_DLOG(vd_log, VDISK_LFLG_DATA,
			    "%s:%02x %02x %02x %02x %02x %02x %02x %02x\n",
			    rw->rw_str, b[0], b[1], b[2], b[3], b[4], b[5],
			    b[6], b[7]);
		}
	}
#endif

	/*
	 * run through all the segments. There are 8 (0-7) sectors per segment.
	 * On some OSes (e.g. Linux), there may be empty gaps between segments.
	 * (i.e. the first segment may end on sector 6 and the second segment
	 * start on sector 4).
	 */
	cur_offset = off;
	num_segs = (uint_t)req->nr_segments;
	for (i = 0; i < num_segs; i++) {

		VDISK_DLOG(vd_log, VDISK_LFLG_SEGS, "%s:seg=%d,fi=%d,la=%d\n",
		    rw->rw_str, i, (uint_t)req->seg[i].first_sect,
		    (uint_t)req->seg[i].last_sect);

		/*
		 * if we have previous segments still to R/W, and the current
		 * segment doesn't start at sector 0, R/W the previous segment
		 * and start fresh with the current segment.
		 */
		if ((size > 0) && (req->seg[i].first_sect != 0)) {
			VDISK_DLOG(vd_log, VDISK_LFLG_SEGS,
			    "%s:off=%llx;addr=%p;size=%llx\n",
			    rw->rw_str, (long long)off, addr, (long long)size);
			rc = (*rw->rw_func)(st->vboxh, off, addr, size);
			if (!VBOX_SUCCESS(rc)) {
				status = BLKIF_RSP_ERROR;
				break;
			}
			/*
			 * update off, addr, and clear size for current
			 * segment.
			 */
			off = cur_offset;
			addr = VD_REQ_ADDR(st, req, i);
			size = 0;
		}

		/*
		 * Add in current segment's size and update current offset
		 * into buffer.
		 */
		size += VD_SEG_SIZE(req, i);
		cur_offset += VD_SEG_SIZE(req, i);

		/*
		 * if this is the last segment, or this segment doesn't end
		 * at the last sector within the segment, R/W up to and
		 * including the current segment.
		 */
		if (((i + 1) == num_segs) || (req->seg[i].last_sect != 7)) {
			VDISK_DLOG(vd_log, VDISK_LFLG_SEGS,
			    "%s:off=%llx;addr=%p;size=%llx\n",
			    rw->rw_str, (long long)off, addr, (long long)size);
			rc = (*rw->rw_func)(st->vboxh, off, addr, size);
			if (!VBOX_SUCCESS(rc)) {
				status = BLKIF_RSP_ERROR;
				break;
			}
			/*
			 * if this is not the last segment, update off, addr,
			 * and clear size for current segment.
			 */
			if ((i + 1) != num_segs) {
				off = cur_offset;
				addr = VD_REQ_ADDR(st, req, i + 1);
				size = 0;
			}
		}
	}

	/* log any errors */
	if (status == BLKIF_RSP_ERROR) {
		VDISK_LOG(vd_log, VDISK_LFLG_ERR,
		    "%s failed:sc=%llx;nm=%x;fi=%x;st=%x\n",
		    rw->rw_str, (long long)req->sector_number,
		    (int)req->nr_segments, (int)req->seg[0].first_sect,
		    (int)status);
	}

	/* send response */
	rc = vd_resp_push(st, req->id, req->operation, status);
	if (rc != 0) {
		VDISK_LOG(vd_log, VDISK_LFLG_ERR, "%s %s\n", rw->rw_str,
		    gettext("ERROR: Unable to send response"));
		return (-1);
	}

	return (0);
}


/*
 * vd_req_write_barrier()
 */
static int
vd_req_write_barrier(vd_state_t *st, blkif_request_t *req)
{
	uint16_t status;
	int rc;


	rc = VDFlush(st->vboxh);
	if (!VBOX_SUCCESS(rc)) {
		status = BLKIF_RSP_ERROR;
	} else {
		status = BLKIF_RSP_OKAY;
	}

	VDISK_DLOG(vd_log, VDISK_LFLG_HDRS, "wb:i=0x%llx;st=0x%x\n",
	    (long long)req->id, (int)status);

	rc = vd_resp_push(st, req->id, req->operation, status);
	if (rc != 0) {
		VDISK_LOG(vd_log, VDISK_LFLG_ERR, "%s\n",
		    gettext("ERROR: Unable to send write barrier response"));
		return (-1);
	}

	return (0);
}


/*
 * vd_req_flush()
 */
static int
vd_req_flush(vd_state_t *st, blkif_request_t *req)
{
	uint16_t status;
	int rc;


	rc = VDFlush(st->vboxh);
	if (!VBOX_SUCCESS(rc)) {
		status = BLKIF_RSP_ERROR;
	} else {
		status = BLKIF_RSP_OKAY;
	}

	VDISK_DLOG(vd_log, VDISK_LFLG_HDRS, "fl:i=0x%llx;st=0x%x\n",
	    (long long)req->id, (int)status);

	rc = vd_resp_push(st, req->id, req->operation, status);
	if (rc != 0) {
		VDISK_LOG(vd_log, VDISK_LFLG_ERR, "%s\n",
		    gettext("ERROR: Unable to send flush response"));
		return (-1);
	}

	return (0);
}


/*
 * vd_resp_push()
 */
static int
vd_resp_push(vd_state_t *st, uint64_t id, uint8_t operation, int16_t status)
{
	blkif_response_t *resp;
	int rc;


	resp = RING_GET_RESPONSE(&st->ring, st->ring.rsp_prod_pvt);
	resp->id = id;
	resp->operation = operation;
	resp->status = status;
	st->ring.rsp_prod_pvt++;
	RING_PUSH_RESPONSES(&st->ring);

	rc = ioctl(st->xfd, XPVTAP_IOCTL_RESP_PUSH);
	if (rc != 0) {
		return (-1);
	}

	return (0);
}


/*
 * vd_cleanup()
 *    cleanup and exit after signal
 */
static void
vd_cleanup(int signo)
{
	int rc;

	if (vd_statep->flush_flag == 0) {
		rc = vdisk_setflags(vd_statep->vdh, VD_NOFLUSH_ON_CLOSE);
		if (rc != 0)
			VDISK_LOG(vd_log, VDISK_LFLG_ERR, "%s\n",
			    gettext("ERROR: Unable to set noflush_on_close"));
	}
	vdisk_close(vd_statep->vdh);
	VDISK_LOG(vd_log, VDISK_LFLG_INFO, "shutting down\n");
	vdisk_log_fini(&vd_log);
	exit(0);
}
