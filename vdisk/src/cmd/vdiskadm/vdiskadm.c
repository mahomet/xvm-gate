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
#include <math.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>

#include <locale.h>
#include <libintl.h>
#include <bsm/libbsm.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "VBox/VBoxHDD.h"
#include "iprt/string.h"

#include "vdisk.h"

#define	VDI_MAX_BACKENDS	15
static VDBACKENDINFO vdi_backend_info[VDI_MAX_BACKENDS];

typedef struct vdi_cmd_s {
	const char	*cl_name;
	boolean_t	cl_private;
	int		(*cl_cmd)(int argc, char *argv[]);
	const char	*cl_desc;
	const char	*cl_help;
} vdi_cmd_t;

/*
 * Utility to create and manage virtual disks.
 * Main entry points are:
 *	vdi_help_cmd
 *	vdi_list_cmd - list base image and snapshots
 *	vdi_create_cmd - create a virtual disk
 *	vdi_destroy_cmd - destroy a virtual disk or snapshot
 *	vdi_import_cmd - import a virtual disk
 *	vdi_export_cmd - export a virtual disk
 *	vdi_convert_cmd - convert a virtual disk
 *	vdi_translate_cmd - translate a virtual disk data file
 *	vdi_snapshot_cmd - create a snapshot image of virtual disk
 *	vdi_rollback_cmd - rollback virtual disk to a snapshot
 *	vdi_clone_cmd - clone a virtual disk
 *	vdi_verify_cmd - verify that path is a virtual disk
 *	vdi_refinc_cmd - increment reference count on virtual disk
 *	vdi_refdec_cmd - decrement reference count on virtual disk
 *	vdi_propadd_cmd - add a user defined property to virtual disk
 *	vdi_propdel_cmd - delete a user defined property
 *	vdi_propget_cmd - get a native or user define property
 *	vdi_propset_cmd - set a native or user define property
 *	vdi_move_cmd - move a virtual disk to another directory
 *	vdi_rename_cmd - rename a virtual disk or snapshot
 */

static int vdi_help_cmd(int argc, char *argv[]);
static int vdi_list_cmd(int argc, char *argv[]);
static int vdi_create_cmd(int argc, char *argv[]);
static int vdi_destroy_cmd(int argc, char *argv[]);
static int vdi_import_cmd(int argc, char *argv[]);
static int vdi_export_cmd(int argc, char *argv[]);
static int vdi_convert_cmd(int argc, char *argv[]);
static int vdi_translate_cmd(int argc, char *argv[]);
static int vdi_snapshot_cmd(int argc, char *argv[]);
static int vdi_rollback_cmd(int argc, char *argv[]);
static int vdi_clone_cmd(int argc, char *argv[]);
static int vdi_verify_cmd(int argc, char *argv[]);
static int vdi_refinc_cmd(int argc, char *argv[]);
static int vdi_refdec_cmd(int argc, char *argv[]);
static int vdi_propadd_cmd(int argc, char *argv[]);
static int vdi_propdel_cmd(int argc, char *argv[]);
static int vdi_propget_cmd(int argc, char *argv[]);
static int vdi_propset_cmd(int argc, char *argv[]);
static int vdi_move_cmd(int argc, char *argv[]);
static int vdi_rename_cmd(int argc, char *argv[]);

const char vdi_help_desc[] = "List commands or show help on a command\n";
const char vdi_help_help[] =
	"usage: vdiskadm help [command]\n"
	"   command - individual command to display help info\n\n";

const char vdi_list_desc[] = "list images in a virtual disk\n";
const char vdi_list_help[] =
	"USAGE:\n"
	"  vdiskadm list [-pf] vdname\n\n"
	" EXAMPLE:\n"
	"  vdiskadm list -f /export/guests/winxp/winxp-001\n";

const char vdi_create_desc[] = "create a virtual disk\n";
const char vdi_create_help[] =
	"USAGE:\n"
	" vdiskadm create -s <size> [-t <type[:opt],[opt]>] "
	"[-c <comment>] vdname\n\n"
	"  TYPES AND OPTIONS SUPPORTED\n"
	"    vmdk:sparse\n"
	"    vmdk:fixed\n"
	"    vdi:sparse\n"
	"    vdi:fixed\n"
	"    vhd:sparse\n"
	"    vhd:fixed\n"
	"    raw\n"
	"EXAMPLE:\n"
	"  vdiskadm create -t vmdk:sparse -s 8g -c \"My Disk\" "
	"/export/guests/winxp/winxp-001\n";

const char vdi_destroy_desc[] = "destroy a virtual disk\n";
const char vdi_destroy_help[] =
	"USAGE:\n"
	"  vdiskadm destroy [-r] vdname[@snap_name]\n\n"
	"EXAMPLE:\n"
	"  vdiskadm destroy /export/guests/winxp/winxp-001.snap4\n";

const char vdi_import_desc[] = "import a zvol, raw file, or virtual disk\n";
const char vdi_import_help[] =
	"USAGE:\n"
	" vdiskadm import [-fnpqm] [-x <type>] -d <file|zvol|dsk> "
	"[-t <type[:opt]>] vdname\n\n"
	"EXAMPLE:\n"
	"  vdiskadm import -d /downloads/image.vmdk /export/new_guests/disk1\n";

const char vdi_export_desc[] = "export virtual disk to raw file, disk, zvol\n";
const char vdi_export_help[] =
	"USAGE:\n"
	" vdiskadm export -x <type>[:opt] -d <file|zvol|dsk> vdname\n\n"
	"EXAMPLE:\n"
	"  vdiskadm export -x raw -d /dev/zvol/dsk/pool/t1 "
	" /export/new_guests/disk1\n";

const char vdi_convert_desc[] = "convert a virtual disk to different type\n";
const char vdi_convert_help[] =
	"USAGE:\n"
	"  vdiskadm convert [-t <type[:opt]>] vdname\n\n"
	"  TYPES AND OPTIONS SUPPORTED\n"
	"    vmdk:sparse\n"
	"    vmdk:fixed\n"
	"    vdi:sparse\n"
	"    vdi:fixed\n"
	"    vhd:sparse\n"
	"    vhd:fixed\n"
	"    raw\n"
	"EXAMPLE:\n"
	"  vdiskadm convert -t vmdk:fixed /guests/winxp/winxp-001\n";

const char vdi_translate_desc[] = "translate virtual disk data to a "
	"different type\n";
const char vdi_translate_help[] =
	"USAGE:\n"
	"  vdiskadm translate [-i type[:opt]] -I input_file -x type[:opt] "
	    "-d output_file\n\n"
	"  TYPES AND OPTIONS SUPPORTED\n"
	"    vmdk:sparse\n"
	"    vmdk:fixed\n"
	"    vdi:sparse\n"
	"    vdi:fixed\n"
	"    vhd:sparse\n"
	"    vhd:fixed\n"
	"    raw\n"
	"EXAMPLE:\n"
	"  vdiskadm translate -I app.vmdk -x raw -d /dev/zvol/dsk/pool1/t1\n";

const char vdi_snapshot_desc[] = "create a snapshot of a virtual disk\n";
const char vdi_snapshot_help[] =
	"USAGE:\n"
	" vdiskadm snapshot vdname@snap_name\n\n"
	"EXAMPLE:\n"
	"  vdiskadm snapshot /export/guests/winxp/winxp-001@snap1";

const char vdi_rollback_desc[] = "revert to a snapshot of a virtual disk\n";
const char vdi_rollback_help[] =
	"USAGE:\n"
	"  vdiskadm rollback [-r] vdname[@snap_name]\n\n"
	"EXAMPLE:\n"
	"  vdiskadm rollback /export/guests/winxp/winxp-001@snap4\n";

const char vdi_clone_desc[] = "clone a snapshot\n";
const char vdi_clone_help[] =
	"USAGE:\n"
	" vdiskadm clone [-c <comment>] vdname[@snap_name] vdname\n\n"
	"EXAMPLE:\n"
	"  vdiskadm clone -c \"Cloned Disk\" "
	"/export/guests/winxp/winxp-001@snap1 /export/new_guests/winxp-clone\n";

const char vdi_verify_desc[] = "verify a disk is valid\n";
const char vdi_verify_help[] =
	"USAGE:\n"
	" vdiskadm verify vdname\n\n";

const char vdi_refinc_desc[] = "try to increment a rw or ro reference count "
	"on a virtual disk\n";
const char vdi_refinc_help[] =
	"USAGE:\n"
	"  vdiskadm ref-inc [-r] vdname\n\n"
	"EXAMPLE: add a rw reference on vdisk (fails if rwcnt or rocnt > 0)\n"
	"  vdiskadm ref-inc /export/guests/winxp/winxp-001\n"
	"EXAMPLE: add a ro reference on vdisk (fails if rwcnt > 0)\n"
	"  vdiskadm ref-inc -r /export/guests/winxp/winxp-001\n";

const char vdi_refdec_desc[] = "try to decrement a rw or ro reference count\n";
const char vdi_refdec_help[] =
	"USAGE:\n"
	"  vdiskadm ref-dec vdname\n\n"
	"EXAMPLE: decrement the rw or ro reference on the vdisk\n"
	"  vdiskadm ref-dec /export/guests/winxp/winxp-001\n";

const char vdi_propadd_desc[] = "add a user property to the disk state\n";
const char vdi_propadd_help[] =
	"USAGE:\n"
	"  vdiskadm prop-add -p <property>=<value> vdname\n\n"
	"EXAMPLE: Add a user property named user: with value 5\n"
	"  vdiskadm prop-add -p user:=5 /export/guests/winxp/winxp-001\n";

const char vdi_propdel_desc[] = "delete a user property from the disk state\n";
const char vdi_propdel_help[] =
	"USAGE:\n"
	"  vdiskadm prop-del -p <property> vdname\n\n"
	"EXAMPLE: Delete a user property named user:\n"
	"  vdiskadm prop-del -p user: /export/guests/winxp/winxp-001\n";

const char vdi_propget_desc[] = "read a property from the disk state\n";
const char vdi_propget_help[] =
	"USAGE:\n"
	"  vdiskadm prop-get -p <property> vdname\n\n"
	"EXAMPLE:\n"
	"  vdiskadm prop-get -p all /export/guests/winxp/winxp-001\n";

const char vdi_propset_desc[] = "set a property in the disk state\n";
const char vdi_propset_help[] =
	"USAGE:\n"
	"  vdiskadm prop-set -p <property>=<value> vdname\n\n"
	"EXAMPLE:\n"
	"  vdiskadm prop-set -p owner=xvm /export/guests/winxp/winxp-001\n";

const char vdi_move_desc[] = "move a virtual disk to a different location\n";
const char vdi_move_help[] =
	"USAGE:\n"
	"  vdiskadm move vdname <new_dir>\n\n"
	"EXAMPLE:\n"
	"  vdiskadm move /export/guests/winxp/winxp-001 /export/new_dir\n";

const char vdi_rename_desc[] = "rename a virtual disk or snapshot\n";
const char vdi_rename_help[] =
	"USAGE:\n"
	"  vdiskadm rename vdname vdname\n\n"
	"EXAMPLE:\n"
	"  vdiskadm rename /guests/winxp/winxp-001 /guests/winxp/winxp-002\n";

static vdi_cmd_t vdi_cmd_list[] = {
	{"help", B_FALSE,
	    vdi_help_cmd, vdi_help_desc, vdi_help_help},

	{"list", B_FALSE,
	    vdi_list_cmd, vdi_list_desc, vdi_list_help},

	{"create", B_FALSE,
	    vdi_create_cmd, vdi_create_desc, vdi_create_help},
	{"destroy", B_FALSE,
	    vdi_destroy_cmd, vdi_destroy_desc, vdi_destroy_help},
	{"import", B_FALSE,
	    vdi_import_cmd, vdi_import_desc, vdi_import_help},
	{"export", B_FALSE,
	    vdi_export_cmd, vdi_export_desc, vdi_export_help},
	{"convert", B_FALSE,
	    vdi_convert_cmd, vdi_convert_desc, vdi_convert_help},
	{"translate", B_FALSE,
	    vdi_translate_cmd, vdi_translate_desc, vdi_translate_help},

	{"move", B_FALSE,
	    vdi_move_cmd, vdi_move_desc, vdi_move_help},
	{"rename", B_FALSE,
	    vdi_rename_cmd, vdi_rename_desc, vdi_rename_help},

	{"snapshot", B_FALSE,
	    vdi_snapshot_cmd, vdi_snapshot_desc, vdi_snapshot_help},
	{"rollback", B_FALSE,
	    vdi_rollback_cmd, vdi_rollback_desc, vdi_rollback_help},
	{"clone", B_FALSE,
	    vdi_clone_cmd, vdi_clone_desc, vdi_clone_help},

	{"verify", B_FALSE,
	    vdi_verify_cmd, vdi_verify_desc, vdi_verify_help},

	{"ref-inc", B_TRUE,
	    vdi_refinc_cmd, vdi_refinc_desc, vdi_refinc_help},
	{"ref-dec", B_TRUE,
	    vdi_refdec_cmd, vdi_refdec_desc, vdi_refdec_help},

	{"prop-add", B_FALSE,
	    vdi_propadd_cmd, vdi_propadd_desc, vdi_propadd_help},
	{"prop-del", B_FALSE,
	    vdi_propdel_cmd, vdi_propdel_desc, vdi_propdel_help},
	{"prop-get", B_FALSE,
	    vdi_propget_cmd, vdi_propget_desc, vdi_propget_help},
	{"prop-set", B_FALSE,
	    vdi_propset_cmd, vdi_propset_desc, vdi_propset_help},
};
#define	VDI_CMD_CNT	(sizeof (vdi_cmd_list) / sizeof (vdi_cmd_t))



#define	MIN(a, b)	((a) < (b) ? (a) : (b))

static uint_t vdi_heads[] = {16, 32, 64, 128};
#define	VDI_HEADS_CNT	(sizeof (vdi_heads) / sizeof (uint_t))


/* Helper routines */
static void vdi_usage();
static vdi_cmd_t *vdi_cmd_lookup(char *name);
static int vdi_cmd_print_help(FILE *stream, char *str);
static int zfs_nicestrtonum(const char *value, uint64_t *num);
static int str2shift(const char *buf);
static void copy_add_ext(char *name_ext, char *name, char *ext);
static int vdi_get_type_flags(VDBACKENDINFO *backend, char *optarg,
    uint_t *type_flags);
int check_vdisk_in_use(vd_handle_t *vdh, char *print_name);

/* print help */
static int
vdi_help_cmd(int argc, char *argv[])
{
	vdi_cmd_t *cmd;
	int rc;
	int i;

	if (argc == 2) {
		rc = vdi_cmd_print_help(stdout, argv[1]);
		if (rc != 0) {
			goto helpfail_usage;
		}
		return (0);
	}

	if (argc != 1) {
		goto helpfail_usage;
	}

	(void) fprintf(stdout, gettext("\nVirtual Disk Tool\n"
	    "  vdiskadm help [command] - for individual command help\n\n"
	    "List of commands:\n\n"));
	for (i = 0; i < VDI_CMD_CNT; i++) {
		cmd = &vdi_cmd_list[i];
		if (!cmd->cl_private) {
			(void) fprintf(stdout, "%-10s - %s", cmd->cl_name,
			    gettext(cmd->cl_desc));
		}
	}
	(void) fprintf(stdout, "\n");

	return (0);

helpfail_usage:
	(void) fprintf(stderr, gettext("usage: vdiskadm help [command]\n"));
	(void) fprintf(stderr, gettext("'vdiskadm help' lists the commands\n"));
	return (-1);
}

/*
 * Opens a virtual disk and print the list of images.
 * -f option: gives a full list including extents and store file
 * -p option: prints files in parsable list
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_list_cmd(int argc, char *argv[])
{
	char vdname[MAXPATHLEN];	/* path to virtual disk */
	char extname[MAXPATHLEN];	/* extension type of virtual disk */
	char *pszformat = NULL;		/* VBox's extension type of disk */
	vd_handle_t *vdh = NULL;
	int rc;
	int c;
	int print_all = 0;
	int parsable_output = 0;
	PVBOXHDD pdisk;

	while ((c = getopt(argc, argv, "fp")) != -1) {
		switch (c) {
		case 'f':
			print_all = 1;
			break;

		case 'p':
			parsable_output = 1;
			break;

		case '?':
			(void) fprintf(stderr, "\n%s: \"%c\"\n\n",
			    gettext("ERROR: invalid option"), optopt);
			vdi_usage();
			return (-1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr,
		    gettext("ERROR: missing file argument\n"));
		(void) vdi_cmd_print_help(stderr, "list");
		goto fail;
	}

	if (vdisk_find_create_storepath(argv[0], vdname, NULL,
	    extname, &pszformat, 0, &vdh) == -1) {
		goto fail;
	}

	/* Alloc handle space */
	rc = VDCreate(NULL, &pdisk);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}
	vdh->hdd = pdisk;

	if ((vdisk_load_snapshots(vdh, pszformat, vdname,
	    VD_OPEN_FLAGS_READONLY)) == -1) {
		goto fail;
	}

	RTStrFree(pszformat);

	/* Print files (with snapshots) associated with virtual disk */
	vdisk_print_files(vdh, vdname, parsable_output, print_all,
	    print_all, 1);

	/* Close all and free pdisk */
	VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);
	return (0);

fail:
	if (pszformat)
		RTStrFree(pszformat);
	if ((vdh != NULL) && (vdh->hdd != NULL))
		VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);
	return (-1);
}

/*
 * Creates a virtual disk.
 * -s option: give the size
 * -t option: give type or type:option
 * -c option: give comment or description of virtual disk
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_create_cmd(int argc, char *argv[])
{
	PDMMEDIAGEOMETRY PCHSGeometry;
	PDMMEDIAGEOMETRY LCHSGeometry;
	char vdname[MAXPATHLEN];	/* path to virtual disk */
	char vdfilebase[MAXPATHLEN];	/* path to virtual disk file */
	char vdname_ext[MAXPATHLEN];	/* virtual disk name with extension */
	char extname[MAXPATHLEN];	/* extension type of virtual disk */
	char create_time_str[MAXPATHLEN];
	char *disk_size_str;
	char disk_size_bytes_str[MAXPATHLEN];
	char sector_str[MAXPATHLEN];
	char none[] = "none";
	char *rm_name = NULL;
	unsigned uImageFlags;
	struct stat64 stat64buf;
	unsigned uOpenFlags;
	uint64_t disk_size = 0;
	uint64_t sector_size = 0;
	boolean_t sparse;
	uint_t cylinders;
	char *pszformat;
	PVBOXHDD pdisk = NULL;
	char *comment;
	uint_t cnt;
	int rc;
	int c;
	int i;
	size_t backend_len, optarg_len;
	char *colon = NULL;
	vd_handle_t *vdh = NULL;
	struct passwd *pw;
	char *at, *slash;

	/* default values if options not specified */
	sparse = B_FALSE;
	pszformat = "VMDK";
	uOpenFlags = VD_OPEN_FLAGS_NORMAL; /* normal, readonly, honor_zeroes */
	uImageFlags = VD_IMAGE_FLAGS_NONE; /* sparse */

	comment = NULL;
	cnt = 0;

	rc = VDBackendInfo(VDI_MAX_BACKENDS, vdi_backend_info, &cnt);
	if (rc != VINF_SUCCESS) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Unable to load formats"));
		exit(-1);
	}

	while ((c = getopt(argc, argv, "s:t::c:")) != -1) {
		switch (c) {
		case 's':
			if (zfs_nicestrtonum(optarg, &disk_size) != 0) {
				(void) fprintf(stderr, "%s %s\n",
				    gettext("ERROR: Bad size"), optarg);
				(void) vdi_cmd_print_help(stderr, "create");
				exit(-1);
			}
			disk_size_str = optarg;

			break;

		case 'c':
			comment = optarg;
			break;

		case 't':
			/* Get length of type not including option */
			colon = strchr(optarg, ':');
			if (colon)
				optarg_len = colon - optarg;
			else
				optarg_len = strlen(optarg);
			/* Check type against available backends */
			for (i = 0; i < cnt; i++) {
				backend_len = strlen
				    (vdi_backend_info[i].pszBackend);
				/* Check no further if lengths don't match */
				if (optarg_len != backend_len)
					continue;
				rc = strncasecmp(vdi_backend_info[i].pszBackend,
				    optarg, backend_len);
				if (rc == NULL) {
					pszformat = (char *)
					    vdi_backend_info[i].pszBackend;
					break;
				}
			}
			if (i >= cnt) {
				(void) fprintf(stderr, "\n%s: %s\n\n",
				    gettext("ERROR: Unsupported format "
				    "specified"), optarg);
				(void) vdi_cmd_print_help(stderr, "create");
				exit(-1);
			}

			rc = vdi_get_type_flags(&vdi_backend_info[i], optarg,
			    &uImageFlags);
			if (rc != 0) {
				(void) fprintf(stderr, "\n%s: %s\n\n",
				    gettext("ERROR: Unsupported option "
				    "specified"), optarg);
				(void) vdi_cmd_print_help(stderr, "create");
				exit(-1);
			}
			break;

		case ':':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Missing argument for option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "create");
			exit(-1);

		case '?':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Invalid option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "create");
			exit(-1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Missing name argument"));
		vdi_usage();
		(void) vdi_cmd_print_help(stderr, "create");
		exit(-1);
	}

	/* Verify that snapshot name isn't given on create */
	at = strrchr(argv[0], '@');
	if (at) {
		/* Check if '@' is in vdisk name but not in path */
		slash = strrchr(argv[0], '/');
		if (slash)
			at = strrchr(slash, '@');
		if (at) {
			(void) fprintf(stderr, "\n%s\n\n",
			    gettext("ERROR: Cannot give snapshot on create"));
			vdi_usage();
			(void) vdi_cmd_print_help(stderr, "create");
			exit(-1);
		}
	}

	/* Check if mandatory disk size arg was given */
	if (disk_size == 0) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Missing disk size argument"));
		vdi_usage();
		(void) vdi_cmd_print_help(stderr, "create");
		exit(-1);
	}

	snprintf(disk_size_bytes_str, MAXPATHLEN, "%lld",
	    (long long)disk_size);

	sector_size = disk_size / 512;
	snprintf(sector_str, MAXPATHLEN, "%lld", (long long)sector_size);

	/*
	 * Setup geometry information for disks.
	 *
	 * PCHS = 16383/16/63 (for disks smaller than about 8GByte reduce
	 * the cylinder count appropriately)
	 */
	cylinders = disk_size / (16 * 63 * 512);
	PCHSGeometry.cCylinders =  MIN(16383, cylinders);
	PCHSGeometry.cHeads = 16;
	PCHSGeometry.cSectors = 63;

	/*
	 * LCHS=c/h/63 for h=16/32/64/128, whatever value makes c <= 1024.
	 * Otherwise use h=255 and the corresponding value for c (clipped to
	 * 1024).
	 */
	LCHSGeometry.cSectors = 63;
	for (i = 0; i < VDI_HEADS_CNT; i++) {
		cylinders = disk_size / (vdi_heads[i] * 63 * 512);
		if (cylinders <= 1024) {
			LCHSGeometry.cCylinders = cylinders;
			LCHSGeometry.cHeads = vdi_heads[i];
			break;
		}
	}
	if (i >= VDI_HEADS_CNT) {
		LCHSGeometry.cCylinders = 1024;
		LCHSGeometry.cHeads = 255;
	}

	/*
	 * Get pathname to virtual disk.
	 * Set flag to create store path and file.
	 */
	if (vdisk_find_create_storepath(argv[0], vdname, NULL,
	    NULL, NULL, 1, NULL) == -1) {
		return (-1);
	}

	/* Find extension name given format name */
	if (vdisk_format2ext(pszformat, extname) == -1)
		return (-1);

	/* Alloc handle space */
	rc = VDCreate(NULL, &pdisk);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}

	vdisk_get_vdfilebase(NULL, vdfilebase, vdname, MAXPATHLEN);
	copy_add_ext(vdname_ext, vdfilebase, extname);

	/* verify disk is not present first */
	rc = stat64(vdname_ext, &stat64buf);
	if (rc != -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: File already exists"), vdname);
		strerror(errno);
		goto fail;
	}

	/* Create the base (i.e. non-COW) virtual disk */
	rc = VDCreateBase(pdisk, pszformat, vdname_ext, disk_size,
	    uImageFlags, comment, &PCHSGeometry, &LCHSGeometry, NULL,
	    uOpenFlags, NULL, NULL);
	if (!(VBOX_SUCCESS(rc))) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to create virtual disk"),
		    vdname);
		goto fail;
	}

	/* get create time of virtual disk */
	rc = stat64(vdname_ext, &stat64buf);
	if (rc == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to stat virtual disk file "),
		    vdname_ext);
		strerror(errno);
		goto fail;
	}
	snprintf(create_time_str, MAXPATHLEN, "%ld", stat64buf.st_ctime);
	pw = getpwuid(stat64buf.st_uid);

	if (comment == NULL)
		comment = none;
	if (vdisk_create_tree(vdname, extname,
	    (uImageFlags & VD_IMAGE_FLAGS_FIXED), NULL, create_time_str,
	    disk_size_bytes_str, sector_str, comment, pw->pw_name,
	    &vdh) == -1) {
		goto fail;
	}
	vdh->hdd = pdisk;

	/* Write new snapshot list to store */
	if (vdisk_write_tree(vdh, vdname) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to update store file"), vdname);
		goto fail;
	}

	/* Close all and free pdisk */
	VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);
	return (0);

fail:
	if (pdisk) {
		VDDestroy(pdisk);
	}

	/* unlink vdfile */
	(void) unlink(vdname_ext);

	/* unlink store file */
	vdisk_get_xmlfile(vdfilebase, vdname, MAXPATHLEN);
	(void) unlink(vdfilebase);

	/* Remove directory */
	rm_name = strrchr(vdfilebase, '/');
	if (rm_name) {
		*rm_name = '\0';
		(void) rmdir(vdfilebase);
	}
	vdisk_free_tree(vdh);
	return (-1);
}

/*
 * Destroys a virtual disk or snapshot.
 * -r option: allows the removal of a snapshot besides the latest
 * 	or the entire virtual disk.
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_destroy_cmd(int argc, char *argv[])
{
	char vdname[MAXPATHLEN];	/* path to virtual disk */
	char snapname[MAXPATHLEN];	/* snapshot of disk */
	char extname[MAXPATHLEN];	/* extension type of virtual disk */
	char *pszformat = NULL;		/* VBox's extension type of disk */
	char snapname_ext[MAXPATHLEN];	/* snapshot with extension */
	char vdname_ext[MAXPATHLEN];	/* virtual disk name with extension */
	char storename[MAXPATHLEN];
	char *rm_name, *rm_name_ext;
	vd_handle_t *vdh = NULL;
	int rc;
	int rm_image_number = 0, total_image_number;
	int destroy_children = 0;
	int c;
	int i;
	char *snapshot;
	PVBOXHDD pdisk;

	while ((c = getopt(argc, argv, "r")) != -1) {
		switch (c) {
		case 'r':
			destroy_children = 1;
			break;

		case '?':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Invalid option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "destroy");
			exit(-1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Missing name argument"));
		(void) vdi_cmd_print_help(stderr, "destroy");
		exit(-1);
	}

	if (vdisk_find_create_storepath(argv[0], vdname, snapname,
	    extname, &pszformat, 0, &vdh) == -1) {
		goto fail;
	}

	if (check_vdisk_in_use(vdh, argv[0]))
		goto fail;

	/* Alloc handle space */
	rc = VDCreate(NULL, &pdisk);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}
	vdh->hdd = pdisk;


	/* Set file to be removed to either a snapshot or the base */
	if (snapname[0] == '\0') {
		/* If entire vd to be destroyed, open snaps readonly */
		if ((vdisk_load_snapshots(vdh, pszformat, vdname,
		    VD_OPEN_FLAGS_READONLY)) == -1) {
			goto fail;
		}
		vdisk_get_vdfilebase(vdh, vdname_ext, vdname, MAXPATHLEN);
		(void) strlcat(vdname_ext, ".", MAXPATHLEN);
		(void) strlcat(vdname_ext, extname, MAXPATHLEN);
		rm_name_ext = vdname_ext;
		rm_name = vdname;
	} else {
		if ((vdisk_load_snapshots(vdh, pszformat, vdname,
		    VD_OPEN_FLAGS_HONOR_SAME)) == -1) {
			goto fail;
		}
		(void) strlcpy(snapname_ext, snapname, MAXPATHLEN);
		(void) strlcat(snapname_ext, ".", MAXPATHLEN);
		(void) strlcat(snapname_ext, extname, MAXPATHLEN);
		rm_name_ext = snapname_ext;
		rm_name = snapname;
	}
	RTStrFree(pszformat);
	pszformat = NULL;

	if ((vdisk_find_snapshots(vdh, rm_name_ext, &rm_image_number,
	    &total_image_number)) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to find file to destroy"), rm_name);
		goto fail;
	}

	/* Not allowed to remove base file if any shapshots exist. */
	if (total_image_number == (rm_image_number + 1)) {
		if ((total_image_number > 1) && (destroy_children == 0)) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n",
			    gettext("ERROR: Cannot destroy file; file "
			    "has children"), rm_name);
			(void) fprintf(stderr, "\n%s\n",
			    gettext("ERROR: Use -r to destroy file and "
			    "all snapshots\n"));
			/* don't yet list snapshots like zfs does */
			goto fail;
		} else {
			/* Remove all images and extent files */
			for (i = 0; i < total_image_number; i++) {
				(void) VDClose(vdh->hdd, true);
			}
			/* Remove storepath file */
			vdisk_get_xmlfile(storename, vdname, MAXPATHLEN);
			(void) unlink(storename);
			vdisk_free_tree(vdh);
			/* Remove directory */
			rm_name = strrchr(storename, '/');
			if (rm_name) {
				*rm_name = '\0';
				(void) rmdir(storename);
			}
			return (0);
		}
	}

	/* find snapshot name before VDMerge removes it */
	snapshot = vdisk_find_snapshot_name(vdh, rm_image_number);
	if (snapshot == NULL) {
		(void) fprintf(stderr, "\n%s: \"%d\"\n\n",
		    gettext("ERROR: Unable to delete image number in "
		    "store file"), rm_image_number);
		goto fail;
	}

	/* Only removing a snapshot so merge it */
	rc = VDMerge(vdh->hdd, (rm_image_number), (rm_image_number + 1),
	    NULL);
	if (!(VBOX_SUCCESS(rc))) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to remove image"), rm_name);
		goto fail;
	}

	/* remove snapshot from xml tree */
	if (vdisk_delete_snap(vdh, snapshot) == -1) {
		goto fail;
	}

	/* Write new snapshot list to store */
	if (vdisk_write_tree(vdh, vdname) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to update store file"), vdname);
		goto fail;
	}

	/* Close all and free pdisk */
	VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);

	return (0);

fail:
	if (pszformat)
		RTStrFree(pszformat);
	if ((vdh != NULL) && (vdh->hdd != NULL))
		VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);
	return (-1);
}

/*
 * Imports a virtual disk and places it under vdiskadm control.
 * -f option: gives a full list including extents and store file
 * -n option: dryrun
 * -p option: prints files in parsable list
 * -m option: moves the given file instead of copying it
 * -q option: runs in quiet mode with no output to stdout
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_import_cmd(int argc, char *argv[])
{
	char vdname[MAXPATHLEN];	/* path to virtual disk */
	char vdfilename_ext[MAXPATHLEN];	/* virtual disk with ext */
	char vdfilebase[MAXPATHLEN];	/* virtual disk with ext */
	char extname[MAXPATHLEN];	/* extension type of virtual disk */
	char comment[MAXPATHLEN];
	char *special_char;
	char *pszformat_in = NULL;	/* VBox's extension type of disk */
	int free_pszformat_in = 0;	/* set if need to free */
	char *pszformat_out = NULL;	/* VBox's extension type of disk */
	PVBOXHDD pdisk;
	PVBOXHDD pdisk_import = NULL;
	uint_t uimageflags_imp = 0;
	uint_t uimageflags;
	int print_files = 0;
	int quiet = 0;
	int parsable_output = 0;
	int dry_run = 0;
	int mv_not_cpy = 0;
	int c, i;
	int rc;
	uint64_t disk_size = 0;
	uint64_t sector_size = 0;
	char create_time_str[MAXPATHLEN];
	char disk_size_str[MAXPATHLEN];
	char sector_str[MAXPATHLEN];
	struct stat64 stat64buf;
	vd_handle_t *vdh = NULL;
	struct passwd *pw;
	char none[] = "none";
	char *com_ptr;
	char system_command[MAXPATHLEN];
	char old_dir[MAXPATHLEN];
	char *slash;
	char inplace_path[MAXPATHLEN];
	uint_t cnt;
	char *import_file = NULL;
	int import_block_dev = 0;
	int import_reg_file = 0;
	size_t backend_len, optarg_len;
	char *colon = NULL;

	uimageflags = VD_IMAGE_FLAGS_NONE;
	pszformat_out = "VMDK";
	vdname[0] = '\0';

	rc = VDBackendInfo(VDI_MAX_BACKENDS, vdi_backend_info, &cnt);
	if (rc != VINF_SUCCESS) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Unable to load formats"));
		exit(-1);
	}

	while ((c = getopt(argc, argv, "fnpqmx::d:t::")) != -1) {
		switch (c) {
		case 'f':
			print_files = 1;
			break;

		case 'n':
			dry_run = 1;
			break;

		case 'p':
			parsable_output = 1;
			break;

		case 'q':
			quiet = 1;
			break;

		case 'm':
			mv_not_cpy = 1;
			break;

		case 'x':
			/* Check type against available backends */
			colon = strchr(optarg, ':');
			if (colon)
				optarg_len = colon - optarg;
			else
				optarg_len = strlen(optarg);
			for (i = 0; i < cnt; i++) {
				backend_len = strlen
				    (vdi_backend_info[i].pszBackend);
				/* Check no further if lengths don't match */
				if (optarg_len != backend_len)
					continue;
				rc = strncasecmp(vdi_backend_info[i].pszBackend,
				    optarg, backend_len);
				if (rc == NULL) {
					pszformat_in = (char *)
					    vdi_backend_info[i].pszBackend;
					break;
				}
			}
			if (i >= cnt) {
				(void) fprintf(stderr, "\n%s: %s\n\n",
				    gettext("ERROR: Unsupported import format "
				    "specified"), optarg);
				(void) vdi_cmd_print_help(stderr, "import");
				exit(-1);
			}

			/*
			 * Don't need uimageflags_imp, but don't allow bogus
			 * option to be specificed.
			 */
			rc = vdi_get_type_flags(&vdi_backend_info[i], optarg,
			    &uimageflags_imp);
			if (rc != 0) {
				(void) fprintf(stderr, "\n%s: %s\n\n",
				    gettext("ERROR: Unsupported import option "
				    "specified"), optarg);
				(void) vdi_cmd_print_help(stderr, "import");
				exit(-1);
			}

			break;

		case 'd':
			import_file = optarg;
			break;

		case 't':
			/* Get length of type not including option */
			colon = strchr(optarg, ':');
			if (colon)
				optarg_len = colon - optarg;
			else
				optarg_len = strlen(optarg);
			/* Check type against available backends */
			for (i = 0; i < cnt; i++) {
				backend_len = strlen
				    (vdi_backend_info[i].pszBackend);
				/* Check no further if lengths don't match */
				if (optarg_len != backend_len)
					continue;
				rc = strncasecmp(vdi_backend_info[i].pszBackend,
				    optarg, backend_len);
				if (rc == NULL) {
					pszformat_out = (char *)
					    vdi_backend_info[i].pszBackend;
					break;
				}
			}
			if (i >= cnt) {
				(void) fprintf(stderr, "\n%s: %s\n\n",
				    gettext("ERROR: Unsupported vdisk format "
				    "specified"), optarg);
				(void) vdi_cmd_print_help(stderr, "import");
				exit(-1);
			}

			rc = vdi_get_type_flags(&vdi_backend_info[i], optarg,
			    &uimageflags);
			if (rc != 0) {
				(void) fprintf(stderr, "\n%s: %s\n\n",
				    gettext("ERROR: Unsupported vdisk option "
				    "specified"), optarg);
				(void) vdi_cmd_print_help(stderr, "import");
				exit(-1);
			}
			break;

		case ':':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Missing argument for option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "import");
			exit(-1);

		case '?':
			(void) fprintf(stderr, "\n%s: \"%c\"\n\n",
			    gettext("ERROR: Invalid option"), optopt);
			vdi_usage();
			return (-1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr,
		    gettext("ERROR: missing virtual disk argument\n"));
		vdi_usage();
		return (-1);
	}

	/* vdisk should not exist */
	rc = stat64(argv[0], &stat64buf);
	if (rc != -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Virtual disk path must not exist"),
		    argv[0]);
		goto fail;
	}

	/* get create time and size of virtual disk */
	if (import_file == NULL) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: import file must be given"));
		goto fail;
	}

	rc = stat64(import_file, &stat64buf);
	if (rc == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\": %s\n\n",
		    gettext("ERROR: Unable to access file to import"),
		    import_file, strerror(errno));
		goto fail;
	}

	/* Allocation vd handle needed to call libvdisk */
	vdh = malloc(sizeof (vd_handle_t));
	if (vdh == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	bzero(vdh, sizeof (vd_handle_t));

	/* Alloc handle space */
	rc = VDCreate(NULL, &pdisk);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}
	vdh->hdd = pdisk;

	/*
	 * Attempt to detect format type.
	 * If a block device is given then assume raw.
	 * If an iso image is given then assume raw.
	 * If a file ending in .raw is given then assume raw.
	 * Try to get the format type from the file otherwise.
	 * If can't determine format, user will have to specify.
	 */
	if (stat64buf.st_mode & S_IFBLK) {
		import_block_dev = 1;
		if (pszformat_in == NULL)
			pszformat_in = "raw";
	} else if (pszformat_in == NULL) {
		special_char = strrchr(import_file, '.');
		if ((special_char) && strcasecmp(".iso", special_char) == 0)
			pszformat_in = "raw";
		else if ((special_char) && strcasecmp(".raw",
		    special_char) == 0)
			pszformat_in = "raw";
		else {
			rc = VDGetFormat(import_file, &pszformat_in);
			free_pszformat_in = 1;
			if (!(VBOX_SUCCESS(rc))) {
				(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
				    gettext("ERROR: Unable to determine import "
				    "type, please specify with -x option"),
				    import_file);
				goto fail;
			}
		}
	}

	/*
	 * Open import file readonly since a sparse, optimized stream
	 * vmdk file can only be opened read-only and must always be
	 * copied to a new format to be used.
	 */
	rc = VDOpen(vdh->hdd, pszformat_in, import_file,
	    VD_OPEN_FLAGS_READONLY, NULL);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: File present but open failed."
		    " Corrupt file?"));
		goto fail;
	}

	if (strcasecmp(pszformat_in, "raw") == 0) {
		import_reg_file = 1;
	}

	if (print_files) {
		if ((import_block_dev == 1) || (import_reg_file == 1)) {
			if (parsable_output)
				(void) printf("file:%s\n", import_file);
			else
				(void) printf("%s\n", import_file);
		} else {
			/*
			 * Print files associated with importing virtual disk.
			 * No snapshots are printed since snapshots are not
			 * yet supported on import.
			 * If parsable_output is set, then also print the
			 * store file name (even though it doesn't exist yet)
			 * so that the rest of the import infrastructure
			 * can find the name of the store file.
			 * Print files with real names as given in argv[0]
			 * not in vdisk format.  File names will be changed
			 * during the import.
			 */
			vdisk_print_files(vdh, import_file, parsable_output,
			    parsable_output, 1, 0);
		}
	}

	/* If dry_run is set, then cleanup and exit */
	if (dry_run) {
		if ((free_pszformat_in) & (pszformat_in != NULL))
			RTStrFree(pszformat_in);
		VDDestroy(vdh->hdd);
		free(vdh);
		return (0);
	}

	/* Create the store file */
	if (vdisk_find_create_storepath(argv[0], vdname, NULL,
	    NULL, NULL, 1, NULL) == -1) {
		goto fail;
	}


	disk_size = VDGetSize(vdh->hdd, 0);
	snprintf(disk_size_str, MAXPATHLEN, "%lld", (long long)disk_size);
	sector_size = disk_size / 512;
	snprintf(sector_str, MAXPATHLEN, "%lld", (long long)sector_size);

	if (mv_not_cpy == 1) {
		if ((import_block_dev == 1) || (import_reg_file == 1)) {
			(void) fprintf(stderr, "%s\n", gettext(
			    "ERROR: Must copy block device or regular file."));
			goto fail;
		}
		if (strcasecmp(pszformat_in, pszformat_out) != 0) {
			(void) fprintf(stderr, "%s\n", gettext(
			    "ERROR: Input and output formats must "
			    "match to move instead of copy."));
			goto fail;
		}
		(void) VDGetImageFlags(vdh->hdd, 0, &uimageflags_imp);
		if (uimageflags_imp & VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED) {
			(void) fprintf(stderr, "%s: %s\n", gettext(
			    "ERROR: Optimized stream vmdk input image "
			    "must be copied to be used"), import_file);
			goto fail;
		}
	} else {
		/* Create new handle to copy vd file(s) to new directory */
		rc = VDCreate(NULL, &pdisk_import);
		if (!VBOX_SUCCESS(rc)) {
			(void) fprintf(stderr, "%s\n", gettext(
			    "ERROR: Unable to allocate handle space."));
			goto fail;
		}
	}

	/* Copy given image into virtual disk name in new directory */
	vdisk_get_vdfilebase(NULL, vdfilebase, vdname, MAXPATHLEN);
	if (vdisk_format2ext(pszformat_out, extname) == -1) {
			(void) fprintf(stderr, "%s\n", gettext(
			    "ERROR: Unable to handle type."));
			goto fail;
	}
	copy_add_ext(vdfilename_ext, vdfilebase, extname);

	if (mv_not_cpy) {
		/* Rename to same path, but to file vdisk.<ext> */
		(void) strlcpy(vdfilebase, argv[0], MAXPATHLEN);
		slash = strrchr(vdfilebase, '/');
		if (slash) {
			*++slash = '\0';
		} else {
			slash = vdfilebase;
			*slash = '\0';
		}
		(void) strlcpy(slash, VD_BASE, MAXPATHLEN);
		copy_add_ext(inplace_path, vdfilebase, extname);

		rc = VDCopy(vdh->hdd, 0, vdh->hdd, pszformat_in, inplace_path,
		    true, 0, uimageflags, NULL, NULL, NULL, NULL);
		if (!(VBOX_SUCCESS(rc))) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to rename file"), argv[0]);
			goto fail;
		}

		(void) strlcpy(old_dir, argv[0], MAXPATHLEN);
		slash = strrchr(old_dir, '/');
		if (slash) {
			*slash = '\0';
		} else {
			(void) strlcpy(old_dir, ".", MAXPATHLEN);
		}

		/* Now move file into given directory */
		snprintf(system_command, MAXPATHLEN, "mv %s/vdisk* %s\n",
		    old_dir, vdname);
		rc = system(system_command);
		if (rc != 0) {
			(void) fprintf(stderr, "\n%s: %s\n\n",
			    gettext("ERROR: Unable to move virtual disk: \n"
			    "Retry import with filename vdisk.vmdk"), argv[1]);
			goto fail;
		}
	} else {
		rc = VDCopy(vdh->hdd, 0, pdisk_import, pszformat_out,
		    vdfilename_ext, false, 0, uimageflags, NULL, NULL,
		    NULL, NULL);
		if (!(VBOX_SUCCESS(rc))) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to copy file"),
			    import_file);
			goto fail;
		}
	}

	/* get create time of virtual disk */
	rc = stat64(vdfilename_ext, &stat64buf);
	if (rc == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\": %s\n\n",
		    gettext("ERROR: Unable to stat virtual disk file "),
		    vdfilename_ext, strerror(errno));
		goto fail;
	}
	snprintf(create_time_str, MAXPATHLEN, "%ld", stat64buf.st_ctime);
	pw = getpwuid(stat64buf.st_uid);
	(void) VDGetComment(vdh->hdd, 0, comment, MAXPATHLEN);

	if (strcmp(comment, "") == 0)
		com_ptr = none;
	else
		com_ptr = comment;

	if (vdisk_create_tree(vdname, extname,
	    (uimageflags & VD_IMAGE_FLAGS_FIXED), NULL, create_time_str,
	    disk_size_str, sector_str, com_ptr, pw->pw_name, &vdh) == -1) {
		goto fail;
	}

	/* Close all and free original pdisk */
	VDDestroy(vdh->hdd);
	vdh->hdd = NULL;

	/* Write new snapshot list to store */
	if (vdisk_write_tree(vdh, vdname) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to update store file"), vdname);
		goto fail;
	}

	/* Close all and free pdisk_export */
	VDDestroy(pdisk_import);
	vdisk_free_tree(vdh);

	if ((free_pszformat_in) & (pszformat_in != NULL))
		RTStrFree(pszformat_in);

	return (0);

fail:
	if ((vdh != NULL) && (vdh->hdd != NULL))
		VDDestroy(vdh->hdd);
	/* won't hurt to call if vdh was malloc'd directly */
	vdisk_free_tree(vdh);
	if (pdisk_import)
		VDDestroy(pdisk_import);

	/* unlink store file */
	vdisk_get_xmlfile(vdfilebase, vdname, MAXPATHLEN);
	(void) unlink(vdfilebase);

	if ((free_pszformat_in) & (pszformat_in != NULL))
		RTStrFree(pszformat_in);

	/* Remove directory */
	if (vdname[0] != '\0')
		(void) rmdir(vdname);

	return (-1);
}

/*
 * Copies virtual disk in the same format as base image.
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_export_cmd(int argc, char *argv[])
{
	char vdname[MAXPATHLEN];	/* path to virtual disk */
	char extname_in[MAXPATHLEN];	/* extension type of virtual disk */
	char *pszformat_in = NULL;	/* VBox's extension type of disk */
	char *pszformat_out = NULL;	/* VBox's extension type of export */
	vd_handle_t *vdh = NULL;
	vd_handle_t *vdh_export = NULL;
	int rc;
	PVBOXHDD pdisk, pdisk_export = NULL;
	char c;
	size_t backend_len, optarg_len;
	char *colon = NULL;
	int i;
	char *export_file = NULL;
	uint_t uimageflags_exp;
	uint_t cnt;
	struct stat64 stat64buf;
	uint64_t disk_size_in = 0;
	uint64_t disk_size_out = 0;
	int export_block_dev = 0;

	uimageflags_exp = VD_IMAGE_FLAGS_NONE;
	pszformat_out = "VMDK";

	rc = VDBackendInfo(VDI_MAX_BACKENDS, vdi_backend_info, &cnt);
	if (rc != VINF_SUCCESS) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Unable to load formats"));
		exit(-1);
	}

	while ((c = getopt(argc, argv, "x::d:")) != -1) {
		switch (c) {
		case 'd':
			export_file = optarg;
			break;

		case 'x':
			/* Get length of type not including option */
			colon = strchr(optarg, ':');
			if (colon)
				optarg_len = colon - optarg;
			else
				optarg_len = strlen(optarg);
			/* Check type against available backends */
			for (i = 0; i < cnt; i++) {
				backend_len = strlen
				    (vdi_backend_info[i].pszBackend);
				/* Check no further if lengths don't match */
				if (optarg_len != backend_len)
					continue;
				rc = strncasecmp(vdi_backend_info[i].pszBackend,
				    optarg, backend_len);
				if (rc == NULL) {
					pszformat_out = (char *)
					    vdi_backend_info[i].pszBackend;
					if (strcasecmp(optarg, "raw") == 0)
						uimageflags_exp =
						    VD_IMAGE_FLAGS_FIXED;
					break;
				}
			}
			if (i >= cnt) {
				(void) fprintf(stderr, "\n%s: %s\n\n",
				    gettext("ERROR: Unsupported export format "
				    "specified"), optarg);
				(void) vdi_cmd_print_help(stderr, "export");
				exit(-1);
			}

			rc = vdi_get_type_flags(&vdi_backend_info[i], optarg,
			    &uimageflags_exp);
			if (rc != 0) {
				(void) fprintf(stderr, "\n%s: %s\n\n",
				    gettext("ERROR: Unsupported export option "
				    "specified"), optarg);
				(void) vdi_cmd_print_help(stderr, "export");
				exit(-1);
			}
			break;

		case ':':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Missing argument for option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "export");
			exit(-1);

		case '?':
			(void) fprintf(stderr, "\n%s: \"%c\"\n\n",
			    gettext("ERROR: Invalid option"), optopt);
			vdi_usage();
			return (-1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr,
		    gettext("ERROR: missing file or virtual disk argument\n"));
		vdi_usage();
		return (-1);
	}

	if (pszformat_out == NULL) {
		(void) fprintf(stderr,
		    gettext("ERROR: missing -x export file type option\n"));
		vdi_usage();
		return (-1);
	}

	if (export_file == NULL) {
		(void) fprintf(stderr,
		    gettext("ERROR: missing -d export file path\n"));
		vdi_usage();
		return (-1);
	}

	if (vdisk_find_create_storepath(argv[0], vdname, NULL,
	    extname_in, &pszformat_in, 0, &vdh) == -1) {
		goto fail;
	}

	if (check_vdisk_in_use(vdh, argv[0]))
		goto fail;

	/* Alloc handle space */
	rc = VDCreate(NULL, &pdisk);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}
	vdh->hdd = pdisk;

	/* Open virtual disk readonly since copying to the exported file */
	if ((vdisk_load_snapshots(vdh, pszformat_in, vdname, 0)) == -1) {
		goto fail;
	}

	vdh_export = malloc(sizeof (vd_handle_t));
	if (vdh_export == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	bzero(vdh_export, sizeof (vd_handle_t));

	/* Alloc handle space */
	rc = VDCreate(NULL, &pdisk_export);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}
	vdh_export->hdd = pdisk_export;

	rc = stat64(export_file, &stat64buf);
	if (rc == 0) {
		if (stat64buf.st_mode & S_IFBLK) {
			if (strcasecmp(pszformat_out, "raw") != 0) {
				(void) fprintf(stderr, "%s\n", gettext(
				    "ERROR: Block devices only support "
				    "raw type."));
				goto fail;
			}
			export_block_dev = 1;
			rc = VDOpen(vdh_export->hdd, pszformat_out,
			    export_file, VD_OPEN_FLAGS_NORMAL, NULL);

			disk_size_out = VDGetSize(vdh_export->hdd, 0);
			disk_size_in = VDGetSize(vdh->hdd, 0);
			if (disk_size_in > disk_size_out) {
				(void) fprintf(stderr, "%s\n", gettext(
				    "ERROR: Block device isn't large enough."));
				goto fail;
			}
			(void) VDClose(vdh_export->hdd, false);
		} else {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Export file must not exist"),
			    export_file);
		}
	}

	rc = VDCopy(vdh->hdd, 0, pdisk_export, pszformat_out, export_file,
	    false, 0, uimageflags_exp, NULL, NULL, NULL, NULL);
	if (!(VBOX_SUCCESS(rc)) && (rc != VERR_VD_IMAGE_READ_ONLY)) {
		if (export_block_dev)
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Check status of block device"),
			    export_file);
		else
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to create export file"),
			    export_file);
		goto fail;
	}

	VDDestroy(pdisk_export);
	vdisk_free_tree(vdh);

	/* Close all and free pdisk */
	VDDestroy(vdh->hdd);

	return (0);

fail:
	if ((vdh != NULL) && (vdh->hdd != NULL))
		VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);
	if (pdisk_export)
		VDDestroy(pdisk_export);
	if (vdh_export)
		free(vdh_export);
	return (-1);
}

/*
 * Convert virtual disk image to another format.
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_convert_cmd(int argc, char *argv[])
{
	char vdname[MAXPATHLEN];	/* path to virtual disk */
	char vdname_ext[MAXPATHLEN];	/* path to virtual disk with ext */
	char vdname_conv[MAXPATHLEN];	/* path to virtual disk */
	char extname[MAXPATHLEN];	/* extension type of virtual disk */
	char extname_conv[MAXPATHLEN];	/* extension type of virt conv disk */
	char *pszformat = NULL;		/* VBox's extension type of orig disk */
	char *pszformat_conv = NULL;	/* VBox's extension type of conv disk */
	char vdfilebase_conv[MAXPATHLEN];	/* path to converted vdisk */
	char name_conv[MAXPATHLEN];	/* path to temporary vdisk */
	vd_handle_t *vdh = NULL;
	vd_handle_t *vdh_conv = NULL;
	int rc;
	PVBOXHDD pdisk, pdisk_conv = NULL;
	uint_t cnt;
	size_t backend_len, optarg_len;
	char *colon = NULL;
	int c, i;
	char *at, *slash;
	char vdname_conv_ext[MAXPATHLEN]; /* convert disk name with ext */
	uint_t uimageflags_conv, uimageflags_in;
	char *size_bytes = NULL, *sector_str = NULL;
	char *desc = NULL, *owner = NULL, *name = NULL;
	char *create_time = NULL;
	int rm_image_number = 0, total_image_number;
	char storename[MAXPATHLEN];
	char *rm_name = NULL;

	/* Setup defaults for type and option for converted image */
	uimageflags_conv = VD_IMAGE_FLAGS_NONE;
	pszformat_conv = "VMDK";
	rc = VDBackendInfo(VDI_MAX_BACKENDS, vdi_backend_info, &cnt);
	if (rc != VINF_SUCCESS) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Unable to load formats"));
		exit(-1);
	}

	while ((c = getopt(argc, argv, "t::")) != -1) {
		switch (c) {
		case 't':
			/* Get length of type not including option */
			colon = strchr(optarg, ':');
			if (colon)
				optarg_len = colon - optarg;
			else
				optarg_len = strlen(optarg);
			/* Check type against available backends */
			for (i = 0; i < cnt; i++) {
				backend_len = strlen
				    (vdi_backend_info[i].pszBackend);
				/* Check no further if lengths don't match */
				if (optarg_len != backend_len)
					continue;
				rc = strncasecmp(vdi_backend_info[i].pszBackend,
				    optarg, backend_len);
				if (rc == NULL) {
					pszformat_conv = (char *)
					    vdi_backend_info[i].pszBackend;
					break;
				}
			}
			if (i >= cnt) {
				(void) fprintf(stderr, "\n%s: %s\n\n",
				    gettext("ERROR: Unsupported format "
				    "specified"), optarg);
				(void) vdi_cmd_print_help(stderr, "convert");
				exit(-1);
			}

			rc = vdi_get_type_flags(&vdi_backend_info[i], optarg,
			    &uimageflags_conv);
			if (rc != 0) {
				(void) fprintf(stderr, "\n%s: %s\n\n",
				    gettext("ERROR: Unsupported option "
				    "specified"), optarg);
				(void) vdi_cmd_print_help(stderr, "convert");
				exit(-1);
			}
			break;

		case ':':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Missing argument for option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "convert");
			exit(-1);

		case '?':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Invalid option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "convert");
			exit(-1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Missing virtual disk argument"));
		vdi_usage();
		(void) vdi_cmd_print_help(stderr, "convert");
		exit(-1);
	}

	if (pszformat_conv == NULL) {
		(void) fprintf(stderr,
		    gettext("ERROR: missing -t convert type option\n"));
		vdi_usage();
		return (-1);
	}

	/* Verify that snapshot name isn't given on convert */
	at = strrchr(argv[0], '@');
	if (at) {
		/* Check if '@' is in vdisk name but not in path */
		slash = strrchr(argv[0], '/');
		if (slash)
			at = strrchr(slash, '@');
		if (at) {
			(void) fprintf(stderr, "\n%s\n\n",
			    gettext("ERROR: Cannot give snapshot on convert"));
			vdi_usage();
			(void) vdi_cmd_print_help(stderr, "convert");
			exit(-1);
		}
	}

	if (vdisk_find_create_storepath(argv[0], vdname, NULL,
	    extname, &pszformat, 0, &vdh) == -1) {
		goto fail_noremove;
	}

	if (check_vdisk_in_use(vdh, argv[0]))
		goto fail_noremove;

	if (vdisk_get_prop_str(vdh, "owner", &owner) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to get owner of file"), argv[0]);
		goto fail_noremove;
	}
	if (vdisk_get_prop_str(vdh, "creation-time-epoch", &create_time)
	    == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to get create time of file"),
		    argv[0]);
		goto fail_noremove;
	}
	if (vdisk_get_prop_str(vdh, "max-size", &size_bytes) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to get max-size of file"), argv[0]);
		goto fail_noremove;
	}
	if (vdisk_get_prop_str(vdh, "sectors", &sector_str) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to get sectors of file"), argv[0]);
		goto fail_noremove;
	}
	if (vdisk_get_prop_str(vdh, "description", &desc) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to get description of file"),
		    argv[0]);
		goto fail_noremove;
	}
	if (vdisk_get_prop_str(vdh, "name", &name) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to get name of file"),
		    argv[0]);
		goto fail_noremove;
	}


	/* Alloc handle space */
	rc = VDCreate(NULL, &pdisk);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail_noremove;
	}
	vdh->hdd = pdisk;

	/* Open virtual disk readonly since copying the exported file */
	if ((vdisk_load_snapshots(vdh, pszformat, vdname, 0)) == -1) {
		goto fail_noremove;
	}

	/* If types are the same don't convert */
	if (strcmp(pszformat, pszformat_conv) == NULL) {
		(void) VDGetImageFlags(vdh->hdd, 0, &uimageflags_in);
		if (uimageflags_in == uimageflags_conv) {
			if (uimageflags_in == VD_IMAGE_FLAGS_NONE)
				(void) fprintf(stderr, "\n%s: %s:sparse\n\n",
			 	   gettext("ERROR: Image already in type"),
				    pszformat_conv);
			else
				(void) fprintf(stderr, "\n%s: %s:fixed\n\n",
			 	   gettext("ERROR: Image already in type"),
				    pszformat_conv);
			goto fail_noremove;
		}
	}

	/* Alloc handle space for converted file */
	rc = VDCreate(NULL, &pdisk_conv);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail_noremove;
	}

	/* Concoct temp name using vdname + ".conv_" + type */
	if (vdisk_format2ext(pszformat_conv, extname_conv) == -1)
		goto fail_noremove;
	(void) strlcpy(name_conv, vdname, MAXPATHLEN);
	(void) strlcat(name_conv, ".conv_", MAXPATHLEN);
	(void) strlcat(name_conv, extname_conv, MAXPATHLEN);

	/* Now start convert vdisk create */
	if (vdisk_find_create_storepath(name_conv, vdname_conv, NULL,
	    NULL, NULL, 1, NULL) == -1) {
		goto fail;
	}

	vdisk_get_vdfilebase(NULL, vdfilebase_conv, vdname_conv, MAXPATHLEN);
	copy_add_ext(vdname_conv_ext, vdfilebase_conv, extname_conv);

	rc = VDCopy(vdh->hdd, 0, pdisk_conv, pszformat_conv, vdname_conv_ext,
	    false, 0, uimageflags_conv, NULL, NULL, NULL, NULL);

	if (!(VBOX_SUCCESS(rc)) && (rc != VERR_VD_IMAGE_READ_ONLY)) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to convert file"), argv[1]);
		goto fail;
	}

	/* Create xml file of new type */
	if (vdisk_create_tree(vdname_conv, extname_conv,
	    (uimageflags_conv & VD_IMAGE_FLAGS_FIXED), NULL, create_time,
	    size_bytes, sector_str, desc, owner, &vdh_conv) == -1) {
		goto fail;
	}
	vdh_conv->hdd = pdisk_conv;

	/* Write xml list to new convert store */
	if (vdisk_write_tree(vdh_conv, vdname_conv) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to create store file"),
		    "test_convert");
		goto fail;
	}

	/* Change vdisk name in store file to new name */
	if (vdisk_set_prop_str(vdh_conv, "name", name, VD_PROP_IGN_RO) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to set name in store"), vdname_conv);
		goto fail;
	}
	/* keep filename and name in sync - xvmserver uses filename */
	if (vdisk_set_prop_str(vdh_conv, "filename", name,
	    VD_PROP_IGN_RO) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to set name in store"), vdname_conv);
		goto fail;
	}

	/* Write out store to old name since before rename of directory */
	if (vdisk_write_tree(vdh_conv, vdname_conv) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to update store file"), vdname_conv);
		goto fail;
	}

	vdisk_get_vdfilebase(vdh, vdname_ext, vdname, MAXPATHLEN);
	(void) strlcat(vdname_ext, ".", MAXPATHLEN);
	(void) strlcat(vdname_ext, extname, MAXPATHLEN);

	if ((vdisk_find_snapshots(vdh, vdname_ext, &rm_image_number,
	    &total_image_number)) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to find count of images"), vdname);
		goto fail;
	}

	/* Remove all images and extent files */
	for (i = 0; i < total_image_number; i++) {
		(void) VDClose(vdh->hdd, true);
	}
	/* Remove storepath file */
	vdisk_get_xmlfile(storename, vdname, MAXPATHLEN);
	(void) unlink(storename);
	/* Remove directory */
	rm_name = strrchr(storename, '/');
	if (rm_name) {
		*rm_name = '\0';
		(void) rmdir(storename);
	}

	/* Move converted vdisk to original vdisk name */
	rc = rename(vdname_conv, vdname);
	if (rc != 0) {
		(void) fprintf(stderr, "\n%s: %s: %s\n\n",
		    gettext("ERROR: Unable to rename temp vdisk to vdname"),
		    vdname_conv, strerror(errno));
		/* Don't remove temp dir if rename fails since orig is gone */
		goto fail_noremove;
	}

	VDDestroy(pdisk_conv);
	if (pszformat)
		RTStrFree(pszformat);

	/* Close all and free pdisk */
	free(size_bytes);
	free(sector_str);
	free(desc);
	free(owner);
	free(create_time);
	free(name);
	vdisk_free_tree(vdh);

	return (0);

fail:
	if (vdname_conv[0] != '\0') {
		vdisk_get_xmlfile(storename, vdname_conv, MAXPATHLEN);
		(void) unlink(storename);
		if ((vdh_conv != NULL) && (vdh_conv->hdd != NULL))
			(void) VDClose(vdh_conv->hdd, true);
		rmdir(vdname_conv);
	}
fail_noremove:
	if (pszformat)
		RTStrFree(pszformat);
	if ((vdh != NULL) && (vdh->hdd != NULL))
		VDDestroy(vdh->hdd);
	if (pdisk_conv)
		VDDestroy(pdisk_conv);
	if (vdh != NULL)
		vdisk_free_tree(vdh);
	if (size_bytes)
		free(size_bytes);
	if (sector_str)
		free(sector_str);
	if (desc)
		free(desc);
	if (owner)
		free(owner);
	if (create_time)
		free(create_time);
	if (name)
		free(name);
	return (-1);
}

/*
 *  Translates virtual disk data to a different type and stores in a new
 * data file or block device.  No vdisk is created during a translate
 * operation.
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_translate_cmd(int argc, char *argv[])
{
	char *special_char;
	char *pszformat_in = NULL;	/* VBox's extension type of disk */
	int free_pszformat_in = 0;	/* set if need to free */
	char *pszformat_out = NULL;	/* VBox's extension type of disk */
	PVBOXHDD pdisk_in = NULL;
	PVBOXHDD pdisk_out = NULL;
	uint_t uimageflags_in = 0;
	uint_t uimageflags_out = VD_IMAGE_FLAGS_NONE;
	char *in_file = NULL;
	char *out_file = NULL;
	int c, i;
	int rc;
	uint_t cnt;
	int in_block_dev = 0;
	int out_block_dev = 0;
	size_t backend_len, optarg_len;
	uint64_t disk_size_in = 0;
	uint64_t disk_size_out = 0;
	struct stat64 stat64buf;
	char *colon = NULL;

	rc = VDBackendInfo(VDI_MAX_BACKENDS, vdi_backend_info, &cnt);
	if (rc != VINF_SUCCESS) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Unable to load formats"));
		exit(-1);
	}

	while ((c = getopt(argc, argv, "i::I:x::d:")) != -1) {
		switch (c) {
		case 'i':
			/* Check type against available backends */
			colon = strchr(optarg, ':');
			if (colon)
				optarg_len = colon - optarg;
			else
				optarg_len = strlen(optarg);
			for (i = 0; i < cnt; i++) {
				backend_len = strlen
				    (vdi_backend_info[i].pszBackend);
				/* Check no further if lengths don't match */
				if (optarg_len != backend_len)
					continue;
				rc = strncasecmp(vdi_backend_info[i].pszBackend,
				    optarg, backend_len);
				if (rc == NULL) {
					pszformat_in = (char *)
					    vdi_backend_info[i].pszBackend;
					break;
				}
			}
			if (i >= cnt) {
				(void) fprintf(stderr, "\n%s: %s\n\n",
				    gettext("ERROR: Unsupported input format "
				    "specified"), optarg);
				(void) vdi_cmd_print_help(stderr, "translate");
				exit(-1);
			}

			/*
			 * Don't need uimageflags_in, but don't allow bogus
			 * option to be specificed.
			 */
			rc = vdi_get_type_flags(&vdi_backend_info[i], optarg,
			    &uimageflags_in);
			if (rc != 0) {
				(void) fprintf(stderr, "\n%s: %s\n\n",
				    gettext("ERROR: Unsupported input option "
				    "specified"), optarg);
				(void) vdi_cmd_print_help(stderr, "translate");
				exit(-1);
			}
			break;

		case 'x':
			/* Check type against available backends */
			colon = strchr(optarg, ':');
			if (colon)
				optarg_len = colon - optarg;
			else
				optarg_len = strlen(optarg);
			for (i = 0; i < cnt; i++) {
				backend_len = strlen
				    (vdi_backend_info[i].pszBackend);
				/* Check no further if lengths don't match */
				if (optarg_len != backend_len)
					continue;
				rc = strncasecmp(vdi_backend_info[i].pszBackend,
				    optarg, backend_len);
				if (rc == NULL) {
					pszformat_out = (char *)
					    vdi_backend_info[i].pszBackend;
					break;
				}
			}
			if (i >= cnt) {
				(void) fprintf(stderr, "\n%s: %s\n\n",
				    gettext("ERROR: Unsupported output format "
				    "specified"), optarg);
				(void) vdi_cmd_print_help(stderr, "translate");
				exit(-1);
			}

			rc = vdi_get_type_flags(&vdi_backend_info[i], optarg,
			    &uimageflags_out);
			if (rc != 0) {
				(void) fprintf(stderr, "\n%s: %s\n\n",
				    gettext("ERROR: Unsupported output option "
				    "specified"), optarg);
				(void) vdi_cmd_print_help(stderr, "translate");
				exit(-1);
			}
			break;

		case 'I':
			in_file = optarg;
			break;

		case 'd':
			out_file = optarg;
			break;

		case ':':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Missing argument for option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "translate");
			exit(-1);

		case '?':
			(void) fprintf(stderr, "\n%s: \"%c\"\n\n",
			    gettext("ERROR: Invalid option"), optopt);
			vdi_usage();
			return (-1);
		}
	}

	argc -= optind;
	argv += optind;

	if (in_file == NULL) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: input file is missing"));
		vdi_usage();
		return (-1);
	}

	if (out_file == NULL) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: output file is missing"));
		vdi_usage();
		return (-1);
	}

	if (pszformat_out == NULL) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: output type is missing"));
		vdi_usage();
		return (-1);
	}

	if (argc > 0) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: unknown argument on command line"),
		    argv[0]);
		vdi_usage();
		return (-1);
	}

	/* Alloc handle space */
	rc = VDCreate(NULL, &pdisk_in);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}

	/* Alloc handle space */
	rc = VDCreate(NULL, &pdisk_out);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}

	/*
	 * Attempt to detect input file format type.
	 * If a block device is given then assume raw.
	 * If an iso image is given then assume raw.
	 * If a file ending in .raw is given then assume raw.
	 * Try to get the format type from the file otherwise.
	 * If can't determine format, user will have to specify.
	 */
	rc = stat64(in_file, &stat64buf);
	if (rc == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\": %s\n\n",
		    gettext("ERROR: Unable to access input file"),
		    in_file, strerror(errno));
		goto fail;
	}

	if (stat64buf.st_mode & S_IFBLK) {
		in_block_dev = 1;
		if (pszformat_in == NULL)
			pszformat_in = "raw";
	} else if (pszformat_in == NULL) {
		special_char = strrchr(in_file, '.');
		if ((special_char) && strcasecmp(".iso", special_char) == 0)
			pszformat_in = "raw";
		else if ((special_char) && strcasecmp(".raw",
		    special_char) == 0)
			pszformat_in = "raw";
		else {
			rc = VDGetFormat(in_file, &pszformat_in);
			free_pszformat_in = 1;
			if (!(VBOX_SUCCESS(rc))) {
				(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
				    gettext("ERROR: Unable to determine input "
				    "type, please specify with -i option"),
				    in_file);
				goto fail;
			}
		}
	}

	rc = stat64(out_file, &stat64buf);
	if (rc == 0) {
		if (stat64buf.st_mode & S_IFBLK) {
			if (strcasecmp(pszformat_out, "raw") != 0) {
				(void) fprintf(stderr, "%s\n", gettext(
				    "ERROR: Block devices only support "
				    "raw type."));
				goto fail;
			}
			out_block_dev = 1;
			rc = VDOpen(pdisk_out, pszformat_out,
			    out_file, VD_OPEN_FLAGS_NORMAL, NULL);

			disk_size_out = VDGetSize(pdisk_out, 0);
			disk_size_in = VDGetSize(pdisk_in, 0);
			if (disk_size_in > disk_size_out) {
				(void) fprintf(stderr, "%s\n", gettext(
				    "ERROR: Block device isn't large enough."));
				goto fail;
			}
			(void) VDClose(pdisk_out, false);
		} else {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Output file must not exist"),
			    out_file);
		}
	}

	/*
	 * Open input file readonly since a sparse, optimized stream
	 * vmdk file can only be opened read-only and must always be
	 * copied to a new format to be used.
	 */
	rc = VDOpen(pdisk_in, pszformat_in, in_file,
	    VD_OPEN_FLAGS_READONLY, NULL);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: File present but open failed."
		    " Corrupt file?"));
		goto fail;
	}


	rc = VDCopy(pdisk_in, 0, pdisk_out, pszformat_out, out_file,
	    false, 0, uimageflags_out, NULL, NULL, NULL, NULL);
	if (!(VBOX_SUCCESS(rc)) && (rc != VERR_VD_IMAGE_READ_ONLY)) {
		if (out_block_dev)
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Check status of block device"),
			    out_file);
		else
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to create output file"),
			    out_file);
		goto fail;
	}

	/* Close all and free both pdisk_in and pdisk_out */
	VDDestroy(pdisk_in);
	VDDestroy(pdisk_out);

	if ((free_pszformat_in) & (pszformat_in != NULL))
		RTStrFree(pszformat_in);

	return (0);

fail:
	if (pdisk_in)
		VDDestroy(pdisk_in);
	if (pdisk_out)
		VDDestroy(pdisk_out);

	if ((free_pszformat_in) & (pszformat_in != NULL))
		RTStrFree(pszformat_in);

	return (-1);
}

/*
 * Take a snapshot of the virtual disk.
 * Snapshot name is of the form <virtual disk>@<snap name>
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_snapshot_cmd(int argc, char *argv[])
{
	char vdname[MAXPATHLEN];	/* path to virtual disk */
	char snapname[MAXPATHLEN];	/* snapshot of disk */
	char snaplistname[MAXPATHLEN];	/* name to use for list command */
	char extname[MAXPATHLEN];	/* extension type of virtual disk */
	char *pszformat = NULL;		/* VBox's extension type of disk */
	char snapname_ext[MAXPATHLEN];	/* snapshot with extension */
	char vdname_ext[MAXPATHLEN];	/* virtual disk name with extension */
	char vdfilebase[MAXPATHLEN];	/* virtual disk file base name */
	char *snap_loc;
	vd_handle_t *vdh = NULL;
	int rc;
	char *name_at;
	int mv_image_number = 0;
	uint_t cnt, i;
	PVBOXHDD pdisk;
	unsigned int uimageflags;

	if (argc < 2) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Missing name argument"));
		(void) vdi_cmd_print_help(stderr, "snapshot");
		exit(-1);
	}

	name_at = strchr(argv[1], '@');
	if ((name_at == NULL) || (strlen(name_at + 1) == 0)) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: name must be of snapshot form "
		    "<name@snapshot>: "), argv[1]);
		return (-1);
	}
	if (vdisk_find_create_storepath(argv[1], vdname, NULL,
	    extname, &pszformat, 0, &vdh) == -1) {
		goto fail;
	}

	if (check_vdisk_in_use(vdh, argv[1]))
		goto fail;

	/* Formulate snapshot name */
	snap_loc = strrchr(argv[1], '@');
	/* Check name length allowing for '.'  + 9 letter ext */
	if ((strlen(vdname) + strlen(snap_loc) + 10) > MAXPATHLEN) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Name is too long"), argv[1]);
		goto fail;
	}

	/* Get name of snapshot of vdname@snapname */
	(void) strlcpy(snaplistname, vdname, MAXPATHLEN);
	(void) strlcat(snaplistname, snap_loc, MAXPATHLEN);

	vdisk_get_vdfilebase(vdh, snapname, vdname, MAXPATHLEN);
	(void) strlcat(snapname, snap_loc, MAXPATHLEN);

	/* Finish setting up snapshot name adding extension */
	copy_add_ext(snapname_ext, snapname, extname);

	cnt = 0;
	rc = VDBackendInfo(VDI_MAX_BACKENDS, vdi_backend_info, &cnt);
	if (rc != VINF_SUCCESS) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to load formats"), argv[1]);
		goto fail;
	}

	for (i = 0; i < cnt; i++) {
		rc = strncasecmp(vdi_backend_info[i].pszBackend,
		    pszformat, (size_t)strlen(vdi_backend_info[i].pszBackend));
		if (rc == NULL) {
			break;
		}
	}

	if ((vdi_backend_info[i].uBackendCaps & VD_CAP_DIFF) == 0) {
		(void) fprintf(stderr, "\n%s: %s\n\n",
		    gettext("ERROR: Disk type does not support snapshots"),
		    pszformat);
		(void) vdi_cmd_print_help(stderr, "snapshot");
		goto fail;
	}

	/* Alloc handle space */
	rc = VDCreate(NULL, &pdisk);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "\n%s\n\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}
	vdh->hdd = pdisk;

	if ((vdisk_load_snapshots(vdh, pszformat, vdname, 0)) == -1) {
		goto fail;
	}

	vdisk_get_vdfilebase(vdh, vdfilebase, vdname, MAXPATHLEN);
	copy_add_ext(vdname_ext, vdfilebase, extname);

	if ((vdisk_find_snapshots(vdh, vdname_ext, &mv_image_number,
	    NULL)) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to find file to snapshot"),
		    vdname_ext);
		goto fail;
	}

	(void) VDGetImageFlags(vdh->hdd, 0, &uimageflags);
	/* Renaming current active file to snapshot name using VDCopy. */
	rc = VDCopy(vdh->hdd, mv_image_number, vdh->hdd, pszformat,
	    snapname_ext, true, 0, uimageflags, NULL, NULL, NULL, NULL);
	if (!(VBOX_SUCCESS(rc))) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to rename snapshot of file"),
		    vdname);
		goto fail;
	}

	rc = VDCreateDiff(vdh->hdd, pszformat, vdname_ext,
	    VD_IMAGE_FLAGS_NONE, "Snapshot image", NULL, NULL,
	    VD_OPEN_FLAGS_NORMAL, NULL, NULL);
	if (!(VBOX_SUCCESS(rc))) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to create snapshot of file"),
		    vdname);
		goto fail;
	}

	RTStrFree(pszformat);
	pszformat = NULL;

	/* Add snapshot to xml tree */
	if (vdisk_add_snap(vdh, snaplistname, snapname_ext) == -1) {
		goto fail;
	}

	/* Write new snapshot list to store */
	if (vdisk_write_tree(vdh, vdname) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to update store file"), vdname);
		goto fail;
	}

	/* Close all and free pdisk */
	VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);

	return (0);

fail:
	if (pszformat)
		RTStrFree(pszformat);
	if ((vdh != NULL) && (vdh->hdd != NULL))
		VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);
	return (-1);
}

/*
 * Rollback a virtual disk to a given snapshot.
 * -r option: Rollbacks to the given snapshot even if snapshot is
 *	not the latest.
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_rollback_cmd(int argc, char *argv[])
{
	char vdname[MAXPATHLEN];	/* path to virtual disk */
	char snapname[MAXPATHLEN];	/* snapshot of disk */
	char extname[MAXPATHLEN];	/* extension type of virtual disk */
	char *pszformat = NULL;		/* VBox's extension type of disk */
	char snapname_ext[MAXPATHLEN];	/* snapshot with extension */
	char vdname_ext[MAXPATHLEN];	/* virtual disk name with extension */
	char vdfilebase[MAXPATHLEN];	/* path to virtual disk file */
	vd_handle_t *vdh = NULL;
	int rc;
	uint_t cnt, i;
	int rb_image_number = 0, total_image_number;
	int rollback_far = 0;
	int c;
	char *name_at;
	char *snapshot;
	PVBOXHDD pdisk;
	unsigned save_open_flags;

	while ((c = getopt(argc, argv, "r")) != -1) {
		switch (c) {
		case 'r':
			rollback_far = 1;
			break;

		case '?':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Invalid option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "rollback");
			exit(-1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) printf(gettext("ERROR: missing snapshot argument\n"));
		(void) vdi_cmd_print_help(stderr, "rollback");
		exit(-1);
	}

	/* Check snapshot format */
	name_at = strchr(argv[0], '@');
	if (name_at == NULL) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n",
		    gettext("ERROR: name must be of snapshot "
		    "form <name@snapshot>"), argv[0]);
		return (-1);
	}
	if (vdisk_find_create_storepath(argv[0], vdname, snapname,
	    extname, &pszformat, 0, &vdh) == -1) {
		goto fail;
	}

	if (check_vdisk_in_use(vdh, argv[0]))
		goto fail;

	cnt = 0;
	rc = VDBackendInfo(VDI_MAX_BACKENDS, vdi_backend_info, &cnt);
	if (rc != VINF_SUCCESS) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Unable to load formats"));
		goto fail;
	}

	for (i = 0; i < cnt; i++) {
		rc = strncasecmp(vdi_backend_info[i].pszBackend,
		    pszformat, (size_t)strlen(vdi_backend_info[i].pszBackend));
		if (rc == NULL) {
			break;
		}
	}

	if ((vdi_backend_info[i].uBackendCaps & VD_CAP_DIFF) == 0) {
		(void) fprintf(stderr, "\n%s: %s\n\n",
		    gettext("ERROR: Disk type does not support snapshots"),
		    pszformat);
		(void) vdi_cmd_print_help(stderr, "rollback");
		goto fail;
	}

	/* Alloc handle space */
	rc = VDCreate(NULL, &pdisk);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}
	vdh->hdd = pdisk;

	if ((vdisk_load_snapshots(vdh, pszformat, vdname, 0)) == -1) {
		goto fail;
	}

	copy_add_ext(snapname_ext, snapname, extname);

	if ((vdisk_find_snapshots(vdh, snapname_ext,
	    &rb_image_number, &total_image_number)) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to find file to rollback"),
		    snapname);
		goto fail;
	}

	/* Only allow rollback of 1 image without -r flag */
	if ((total_image_number != (rb_image_number + 2)) &&
	    (rollback_far == 0)) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n",
		    gettext("ERROR: Cannot rollback file; more recent "
		    "snapshots exist"), snapname);
		(void) fprintf(stderr, "\n%s\n",
		    gettext("ERROR: Use -r to force deletion of the "
		    "intervening snapshots"));
		/*
		 * TODO list snapshots like zfs does
		 */
		goto fail;
	}

	/* If given top level image - shouldn't get there, but check anyway */
	if (total_image_number == (rb_image_number + 1)) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to rollback to top level snapshot"),
		    snapname);
		goto fail;
	}

	/* Remove snapshots at the rollback snapshot and above */
	for (i = (total_image_number - 1); i > rb_image_number; i--) {

		/*
		 * Turn on readonly flag or VDClose will try to write
		 * to deleted image.
		 */
		rc = VDGetOpenFlags(vdh->hdd, i, &save_open_flags);
		if (!(VBOX_SUCCESS(rc))) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to get open flags of file"),
			    snapshot);
			goto fail;
		}
		rc = VDSetOpenFlags(vdh->hdd, i,
		    (save_open_flags | VD_OPEN_FLAGS_READONLY));
		if (!(VBOX_SUCCESS(rc))) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to set readonly "
			    "mode on file"), snapshot);
			goto fail;
		}

		/* top level file isn't in snaplist */
		if (i != (total_image_number - 1)) {
			snapshot = vdisk_find_snapshot_name(vdh, i);
			if (snapshot == NULL) {
				(void) fprintf(stderr, "\n%s: \"%d\"\n\n",
				    gettext("ERROR: Unable to delete "
				    "image number in store file"), i);
				goto fail;
			}
			/* No snapname to remove for top level image */
			if (vdisk_delete_snap(vdh, snapshot) == -1) {
				goto fail;
			}
		}

		/* Now close that top level or snapshot image */
		(void) VDClose(vdh->hdd, true);
	}

	vdisk_get_vdfilebase(vdh, vdfilebase, vdname, MAXPATHLEN);
	copy_add_ext(vdname_ext, vdfilebase, extname);

	/* Create a new differencing (COW) file for top level image */
	rc = VDCreateDiff(vdh->hdd, pszformat, vdname_ext,
	    VD_IMAGE_FLAGS_NONE, "Snapshot image", NULL, NULL,
	    VD_OPEN_FLAGS_NORMAL, NULL, NULL);
	if (!(VBOX_SUCCESS(rc))) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to create top image of file"),
		    vdname);
		goto fail;
	}

	RTStrFree(pszformat);
	pszformat = NULL;

	/* Write new snapshot list to store */
	if (vdisk_write_tree(vdh, vdname) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to update store file"), vdname);
		goto fail;
	}

	/* Close all and free pdisk */
	VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);

	return (0);

fail:
	if (pszformat)
		RTStrFree(pszformat);
	if ((vdh != NULL) && (vdh->hdd != NULL))
		VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);
	return (-1);
}

/*
 * Clones the virtual disk.
 * -c option: give comment or description of new virtual disk
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_clone_cmd(int argc, char *argv[])
{
	char vdname[MAXPATHLEN];	/* path to virtual disk */
	char snapname[MAXPATHLEN];	/* snapshot of disk */
	char extname[MAXPATHLEN];	/* extension type of virtual disk */
	char *pszformat = NULL;		/* VBox's extension type of disk */
	char clone_vdname[MAXPATHLEN];	/* path to clone disk */
	char clone_vdname_ext[MAXPATHLEN]; /* clone disk name with ext */
	char snapname_ext[MAXPATHLEN];	/* snapshot with extension */
	char vdfilebase[MAXPATHLEN];	/* path to virtual disk file */
	char vdfilebaseclone[MAXPATHLEN];	/* path to virtual disk file */
	unsigned int save_open_flags;
	PVBOXHDD pdisk;
	PVBOXHDD pclonedisk = NULL;
	char *comment;
	uint_t cnt;
	int rc;
	int c;
	int i;
	int image_number = 0, total_image_number;
	char *disk_size_str = NULL, *disk_size_bytes_str = NULL;
	char *sector_str = NULL;
	char create_time_str[MAXPATHLEN];
	int create_flag;
	char *sparse = NULL;
	struct stat64 stat64buf;
	vd_handle_t *vdh = NULL, *vdh_clone = NULL;
	struct passwd *pw;
	char *slash = NULL;
	char *at = NULL;
	unsigned int uimageflags;
	char none[] = "none";

	comment = NULL;
	cnt = 0;
	clone_vdname[0] = '\0';

	while ((c = getopt(argc, argv, "c:")) != -1) {
		switch (c) {
		case 'c':
			comment = optarg;
			break;

		case ':':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Missing argument for option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "clone");
			exit(-1);

		case '?':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Invalid option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "clone");
			exit(-1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 2) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Missing name argument"));
		(void) vdi_cmd_print_help(stderr, "clone");
		exit(-1);
	}

	if (vdisk_find_create_storepath(argv[0], vdname, snapname,
	    extname, &pszformat, 0, &vdh) == -1) {
		goto fail;
	}

	if (check_vdisk_in_use(vdh, argv[0]))
		goto fail;

	cnt = 0;
	rc = VDBackendInfo(VDI_MAX_BACKENDS, vdi_backend_info, &cnt);
	if (rc != VINF_SUCCESS) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Unable to load formats"));
		goto fail;
	}

	for (i = 0; i < cnt; i++) {
		rc = strncasecmp(vdi_backend_info[i].pszBackend,
		    pszformat, (size_t)strlen(vdi_backend_info[i].pszBackend));
		if (rc == NULL) {
			break;
		}
	}
	if (i >= cnt) {
		(void) fprintf(stderr, "\n%s: %s\n\n",
		    gettext("ERROR: Unsupported format used"),
		    pszformat);
		(void) vdi_cmd_print_help(stderr, "clone");
		goto fail;
	}

	/* Alloc handle space */
	rc = VDCreate(NULL, &pdisk);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}
	vdh->hdd = pdisk;

	if ((vdisk_load_snapshots(vdh, pszformat, vdname, 0)) == -1) {
		goto fail;
	}

	/* Now start clone create */
	if (vdisk_find_create_storepath(argv[1], clone_vdname, NULL,
	    NULL, NULL, 1, NULL) == -1) {
		goto fail;
	}

	rc = VDCreate(NULL, &pclonedisk);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}

	/* Setup snapname to hold image to be cloned. */
	if (strcmp(snapname, "\0") == 0) {
		vdisk_get_vdfilebase(NULL, vdfilebase, vdname, MAXPATHLEN);
	} else {
		vdisk_get_vdfilebase(NULL, vdfilebase, snapname, MAXPATHLEN);
		at = strrchr(snapname, '@');
		if (at)
			(void) strlcat(vdfilebase, at, MAXPATHLEN);
	}
	copy_add_ext(snapname_ext, vdfilebase, extname);

	slash = strrchr(snapname_ext, '/');
	if (slash)
		slash++;
	else
		slash = snapname_ext;
	if ((vdisk_find_snapshots(vdh, slash, &image_number,
	    &total_image_number)) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to find file to clone"),
		    argv[0]);
		goto fail;
	}

	/* Turn off readonly flag since will be set for snapshots */
	rc = VDGetOpenFlags(vdh->hdd, image_number, &save_open_flags);
	if (!(VBOX_SUCCESS(rc))) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to get open flags of file"),
		    argv[0]);
		goto fail;
	}
	rc = VDSetOpenFlags(vdh->hdd, image_number,
	    (save_open_flags & ~VD_OPEN_FLAGS_READONLY));
	if (!(VBOX_SUCCESS(rc))) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to set write mode on file"),
		    argv[0]);
		goto fail;
	}

	/* Clone gets conventional name */
	vdisk_get_vdfilebase(NULL, vdfilebaseclone, clone_vdname, MAXPATHLEN);
	copy_add_ext(clone_vdname_ext, vdfilebaseclone, extname);

	/* Copy current active file to clone name using VDCopy. */
	(void) VDGetImageFlags(vdh->hdd, 0, &uimageflags);
	rc = VDCopy(vdh->hdd, image_number, pclonedisk, pszformat,
	    clone_vdname_ext, false, 0, uimageflags, NULL, NULL, NULL, NULL);
	if (!(VBOX_SUCCESS(rc))) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to create clone of file"),
		    argv[0]);
		goto fail;
	}

	RTStrFree(pszformat);
	pszformat = NULL;

	if (comment) {
		rc = VDSetComment(pclonedisk, 0, comment);
		if (!(VBOX_SUCCESS(rc))) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to add clone comment"),
			    clone_vdname);
			/* fall through since failure isn't catastrophic */
		}
	}

	/* get create time of virtual disk */
	rc = stat64(clone_vdname_ext, &stat64buf);
	if (rc == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to stat virtual disk file "),
		    clone_vdname_ext);
		strerror(errno);
		goto fail;
	}
	snprintf(create_time_str, MAXPATHLEN, "%ld", stat64buf.st_ctime);

	if (comment == NULL)
		comment = none;
	(void) vdisk_get_prop_str(vdh, "sparse", &sparse);
	(void) vdisk_get_prop_str(vdh, "max-size", &disk_size_str);
	(void) vdisk_get_prop_str(vdh, "sectors", &sector_str);
	create_flag = 0;
	if (strcmp(sparse, "false") == 0)
		create_flag = VD_IMAGE_FLAGS_FIXED;

	pw = getpwuid(stat64buf.st_uid);
	if (vdisk_create_tree(clone_vdname, extname, create_flag,
	    NULL, create_time_str, disk_size_str, sector_str,
	    comment, pw->pw_name, &vdh_clone) == -1) {
		goto fail;
	}
	vdh_clone->hdd = pclonedisk;

	free(sparse);
	sparse = NULL;
	free(disk_size_str);
	disk_size_str = NULL;
	free(disk_size_bytes_str);
	disk_size_bytes_str = NULL;

	/* Write snapshot list to new clone store */
	if (vdisk_write_tree(vdh_clone, clone_vdname) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to update store file"), vdname);
		goto fail;
	}

	/* Close all and free pdisk and pclonedisk */
	VDDestroy(vdh->hdd);
	VDDestroy(pclonedisk);
	vdisk_free_tree(vdh);
	vdisk_free_tree(vdh_clone);

	return (0);

fail:
	if (pszformat != NULL)
		RTStrFree(pszformat);
	if ((vdh != NULL) && (vdh->hdd != NULL))
		VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);
	if (pclonedisk)
		VDDestroy(pclonedisk);
	vdisk_free_tree(vdh_clone);
	if (sparse)
		free(sparse);
	if (disk_size_str)
		free(disk_size_str);
	if (disk_size_bytes_str)
		free(disk_size_bytes_str);
	if (clone_vdname[0] != '\0')
		rmdir(clone_vdname);
	return (-1);
}

/*
 * Verifies that the given name is a virtual disk.
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_verify_cmd(int argc, char *argv[])
{
	vd_handle_t *vdh = NULL;

	if (argc != 2) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Missing name argument"));
		(void) vdi_cmd_print_help(stderr, "verify");
		exit(-1);
	}

	vdh = vdisk_open(argv[1]);
	if ((vdh == NULL) || (vdh->hdd == NULL) || (vdh->unmanaged)) {
		return (-1);
	}

	vdisk_close(vdh);
	return (0);
}

/*
 * Increment the reference count of a virtual disk.
 * -w option: increment the write count (default if no option given)
 * -r option: increment the reader count
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_refinc_cmd(int argc, char *argv[])
{
	int rwcnt;
	int rocnt;
	char vdname[MAXPATHLEN];	/* path to virtual disk */
	char extname[MAXPATHLEN];	/* extension type of virtual disk */
	char *pszformat = NULL;		/* VBox's extension type of disk */
	int lockfd = -1;
	vd_handle_t *vdh = NULL;
	int c;
	int reader_flag = 0;
	int ret = 0;

	while ((c = getopt(argc, argv, "wr")) != -1) {
		switch (c) {
		case 'w':
			break;

		case 'r':
			reader_flag = 1;
			break;

		case '?':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Invalid option"),
			    optopt);
			exit(-1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Missing name argument"));
		(void) vdi_cmd_print_help(stderr, "refinc");
		exit(-1);
	}

	if (vdisk_find_create_storepath(argv[0], vdname, NULL,
	    extname, &pszformat, 0, &vdh) == -1) {
		ret = -1;
		goto fail;
	}

	if ((lockfd = vdisk_lock(vdname)) == -1) {
		ret = -1;
		goto fail;
	}

	if (vdisk_read_tree(&vdh, vdname) == -1) {
		ret = -1;
		goto fail;
	}

	if (vdisk_get_prop_val(vdh, "rwcnt", &rwcnt) == -1) {
		ret = -1;
		goto fail;
	}
	if (vdisk_get_prop_val(vdh, "rocnt", &rocnt) == -1) {
		ret = -1;
		goto fail;
	}
	if (reader_flag) {
		/* rwcnt must be 0 to grab reader lock */
		if (rwcnt != 0) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n",
			    gettext("ERROR: unable to grab reader lock"),
			    argv[0]);
			ret = -1;
			goto fail;
		}
		rocnt++;
		if (vdisk_set_prop_val(vdh, "rocnt", rocnt,
		    VD_PROP_NORMAL) == -1) {
			ret = -1;
			goto fail;
		}
	} else {
		/* To grab writer lock both rocnt and rwcnt should be 0 */
		if ((rwcnt != 0) || (rocnt != 0)) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n",
			    gettext("ERROR: unable to grab writer lock"),
			    argv[0]);
			ret = -1;
			goto fail;
		}
		rwcnt++;
		if (vdisk_set_prop_val(vdh, "rwcnt", rwcnt,
		    VD_PROP_NORMAL) == -1) {
			ret = -1;
			goto fail;
		}
	}
	(void) vdisk_write_tree(vdh, vdname);

fail:
	if (pszformat)
		RTStrFree(pszformat);
	vdisk_free_tree(vdh);
	if (lockfd != -1) {
		if (vdisk_unlock(lockfd, vdname) == -1) {
			ret = -1;
		}
	}
	return (ret);
}

/*
 * Decrement the reference count of a virtual disk.
 * Descrement either the reader or writer count whichever one is
 * non-zero.
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_refdec_cmd(int argc, char *argv[])
{
	int refcnt;
	char reftype_str[MAXPATHLEN];
	char vdname[MAXPATHLEN];	/* path to virtual disk */
	char extname[MAXPATHLEN];	/* extension type of virtual disk */
	char *pszformat = NULL;		/* VBox's extension type of disk */
	int lockfd = -1;
	vd_handle_t *vdh = NULL;
	int ret = 0;

	argc--;
	argv++;

	if (argc < 1) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Missing name argument"));
		(void) vdi_cmd_print_help(stderr, "refdec");
		exit(-1);
	}

	if (vdisk_find_create_storepath(argv[0], vdname, NULL,
	    extname, &pszformat, 0, &vdh) == -1) {
		ret = -1;
		goto fail;
	}

	if ((lockfd = vdisk_lock(vdname)) == -1) {
		ret = -1;
		goto fail;
	}

	if (vdisk_read_tree(&vdh, vdname) == -1) {
		ret = -1;
		goto fail;
	}

	/*
	 * rwcnt and rocnt are mutual exclusive (only one of these should be
	 * non zero). First, see if rwcnt is set to one. If so, we'll decrement
	 * rwcnt. else we'll try rocnt.
	 */
	(void) strlcpy(reftype_str, "rwcnt", MAXPATHLEN);
	if (vdisk_get_prop_val(vdh, reftype_str, &refcnt) == -1) {
		ret = -1;
		goto fail;
	}

	if (refcnt == 0) {
		(void) strlcpy(reftype_str, "rocnt", MAXPATHLEN);
		if (vdisk_get_prop_val(vdh, reftype_str, &refcnt) == -1) {
			ret = -1;
			goto fail;
		}
	}
	if (refcnt == 0) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n",
		    gettext("ERROR: reference counts are already zero"),
		    argv[0]);
		ret = -1;
		goto fail;
	}

	refcnt--;
	if (vdisk_set_prop_val(vdh, reftype_str, refcnt, VD_PROP_NORMAL)) {
		ret = -1;
		goto fail;
	}
	(void) vdisk_write_tree(vdh, vdname);

fail:
	if (pszformat)
		RTStrFree(pszformat);
	vdisk_free_tree(vdh);
	if (lockfd != -1) {
		if (vdisk_unlock(lockfd, vdname) == -1) {
			ret = -1;
		}
	}
	return (ret);
}

/*
 * Adds a user defined property to a virtual disk.
 * -p option: property to be added
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_propadd_cmd(int argc, char *argv[])
{
	char *property;
	vd_handle_t *vdh = NULL;
	int opt;
	char *pszformat = NULL;
	char vdname[MAXPATHLEN];
	char extname[MAXPATHLEN];
	int rc;
	char *value = NULL;
	char *tst_value = NULL;
	PVBOXHDD pdisk;

	/* option defaults */
	property = NULL;

	while ((opt = getopt(argc, argv, "p:")) != -1) {
		switch (opt) {
		case 'p':
			property = optarg;
			value = strrchr(optarg, '=');
			if (value == NULL) {
				(void) vdi_cmd_print_help(stderr, "prop-add");
				return (-1);
			}
			*value = 0x0;
			value += 1;
			break;

		case ':':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Missing argument for option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "prop-add");
			return (-1);

		case '?':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Invalid option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "prop-add");
			return (-1);
		}
	}

	argc -= optind;
	argv += optind;

	if ((property == NULL) || (value == NULL) || (argc != 1)) {
		(void) vdi_cmd_print_help(stderr, "prop-add");
		return (-1);
	}

	if (vdisk_find_create_storepath(argv[0], vdname, NULL, extname,
	    &pszformat, 0, &vdh) == -1) {
		goto fail;
	}

	/* allocate disk handle */
	rc = VDCreate(NULL, &pdisk);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}
	vdh->hdd = pdisk;

	if ((vdisk_load_snapshots(vdh, pszformat, vdname,
	    VD_OPEN_FLAGS_READONLY)) == -1) {
		goto fail;
	}

	RTStrFree(pszformat);
	pszformat = NULL;

	if (vdisk_get_prop_str(vdh, property, &tst_value) != -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Property already exists"), property);
		goto fail;
	}

	if (vdisk_add_prop_str(vdh, property, value) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to add property"), property);
		goto fail;
	}

	if (vdisk_write_tree(vdh, vdname) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to update store file"), vdname);
		goto fail;
	}

	if (tst_value)
		free(tst_value);
	VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);

	return (0);

fail:
	if (pszformat)
		RTStrFree(pszformat);
	if ((vdh != NULL) && (vdh->hdd != NULL))
		VDDestroy(vdh->hdd);
	if (tst_value)
		free(tst_value);
	vdisk_free_tree(vdh);
	return (-1);
}

/*
 * Removes a user defined property from a virtual disk.
 * -p option: property to be deleted
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_propdel_cmd(int argc, char *argv[])
{
	char *property;
	vd_handle_t *vdh = NULL;
	int opt;

	char *pszformat = NULL;
	char vdname[MAXPATHLEN];
	char extname[MAXPATHLEN];
	int rc;
	PVBOXHDD pdisk;

	/* option defaults */
	property = NULL;

	while ((opt = getopt(argc, argv, "p:")) != -1) {
		switch (opt) {
		case 'p':
			property = optarg;
			break;

		case ':':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Missing argument for option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "prop-del");
			return (-1);

		case '?':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Invalid option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "prop-del");
			return (-1);
		}
	}

	argc -= optind;
	argv += optind;

	if ((property == NULL) || (argc != 1)) {
		(void) vdi_cmd_print_help(stderr, "prop-del");
		return (-1);
	}

	if (strrchr(property, ':') == NULL) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n",
		    gettext("ERROR: Cannot delete native property"),
		    property);
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: User property must contain colon"));
		goto fail;
	}

	if (vdisk_find_create_storepath(argv[0], vdname, NULL, extname,
	    &pszformat, 0, &vdh) == -1) {
		goto fail;
	}

	/* allocate disk handle */
	rc = VDCreate(NULL, &pdisk);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}
	vdh->hdd = pdisk;

	if ((vdisk_load_snapshots(vdh, pszformat, vdname,
	    VD_OPEN_FLAGS_READONLY)) == -1) {
		goto fail;
	}

	RTStrFree(pszformat);
	pszformat = NULL;

	if (vdisk_del_prop_str(vdh, property) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to delete property"), property);
		goto fail;
	}
	if (vdisk_write_tree(vdh, vdname) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to update store file"), vdname);
		goto fail;
	}

	VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);

	return (0);

fail:
	if (pszformat)
		RTStrFree(pszformat);
	if ((vdh != NULL) && (vdh->hdd != NULL))
		VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);
	return (-1);
}

/*
 * Get and print properties.
 * returns 0 - success
 * -1 failure
 */
static int
vdi_propget_cmd(int argc, char *argv[])
{
	char *property;
	vd_handle_t *vdh = NULL;
	int opt;
	char *pszformat = NULL;
	char vdname[MAXPATHLEN];
	char vdname_ext[MAXPATHLEN];
	char extname[MAXPATHLEN];
	struct stat64 stat64buf_mod;
	uint64_t file_size;
	int rc;
	int i;
	int value;
	int last_image;
	PVBOXHDD pdisk;
	int longlist = 0;
	char *rw_str;
	char *val_str = NULL;
	char *create_time;
	time_t cr_time;

	/* option defaults */
	property = NULL;

	while ((opt = getopt(argc, argv, "p:l")) != -1) {
		switch (opt) {
		case 'p':
			property = optarg;
			break;

		case 'l':
			longlist = 1;
			break;

		case ':':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Missing argument for option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "prop-get");
			return (-1);

		case '?':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Invalid option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "prop-get");
			return (-1);
		}
	}

	argc -= optind;
	argv += optind;

	if ((property == NULL) || (argc != 1)) {
		(void) vdi_cmd_print_help(stderr, "prop-get");
		return (-1);
	}

	if (vdisk_find_create_storepath(argv[0], vdname, NULL, extname,
	    &pszformat, 0, &vdh) == -1) {
		goto fail;
	}

	/* allocate disk handle */
	rc = VDCreate(NULL, &pdisk);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}
	vdh->hdd = pdisk;

	/* only load the disk images if needed */
	if ((strcmp(property, "all") == 0) ||
	    (strcmp(property, "effective-size") == 0)) {
		if ((vdisk_load_snapshots(vdh, pszformat, vdname,
		    VD_OPEN_FLAGS_READONLY)) == -1) {
			goto fail;
		}
		/* Get size of all snapshots and extents */
		last_image = VDGetCount(vdh->hdd) - 1;
		file_size = 0;
		for (i = 0; i <= last_image; i++) {
			file_size += VDGetFileSize(vdh->hdd, i);
		}
	}

	RTStrFree(pszformat);
	pszformat = NULL;


	/* get path to real virtual disk format file */
	if (vdisk_get_prop_str(vdh, "vdfile", &val_str) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to retrieve "
		    "filename from store"), vdname);
		goto fail;
	}

	(void) strlcpy(vdname_ext, vdname, MAXPATHLEN);
	(void) strlcat(vdname_ext, "/", MAXPATHLEN);
	(void) strlcat(vdname_ext, val_str, MAXPATHLEN);
	free(val_str);
	val_str = NULL;

	/* Use top (last) image to get last modification time */
	rc = stat64(vdname_ext, &stat64buf_mod);
	if (rc == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Virtual disk doesn't exist"), argv[0]);
		strerror(errno);
		goto fail;
	}

	if (vdisk_get_prop_str(vdh, "creation-time-epoch", &create_time) == -1)
		goto fail;
	cr_time = (time_t)atoi((char *)create_time);

	if (strcmp(property, "all") == 0) {
		vdisk_print_prop_all(vdh, longlist);
		if (longlist) {
			(void) fprintf(stdout, "effective-size: %lld\tro\n",
			    (long long) file_size);
			(void) fprintf(stdout, "creation-time: %s\tro\n",
			    ctime(&cr_time));
			(void) fprintf(stdout, "modification-time: %s\tro\n",
			    ctime(&stat64buf_mod.st_mtime));
			(void) fprintf(stdout,
			    "modification-time-epoch: %ld\tro\n",
			    stat64buf_mod.st_mtime);
		} else {
			(void) fprintf(stdout, "effective-size: %lld\n",
			    (long long) file_size);
			(void) fprintf(stdout, "creation-time: %s",
			    ctime(&cr_time));
			(void) fprintf(stdout, "modification-time: %s",
			    ctime(&stat64buf_mod.st_mtime));
			(void) fprintf(stdout,
			    "modification-time-epoch: %ld\n",
			    stat64buf_mod.st_mtime);
		}
		return (0);
	}

	/* Handle boolean native properties first */
	if ((strcmp(property, "readonly") == 0) ||
	    (strcmp(property, "removable") == 0) ||
	    (strcmp(property, "cdrom") == 0) ||
	    (strcmp(property, "sparse") == 0)) {
		(void) vdisk_get_prop_bool(vdh, property, &value);
		if (longlist) {
			vdisk_get_prop_rw(property, &rw_str);
			if (value == 1) {
				(void) fprintf(stdout, "%s\t%s\n", "true",
				    rw_str);
			} else {
				(void) fprintf(stdout, "%s\t%s\n", "false",
				    rw_str);
			}
		}
		if (value == 1)
			(void) fprintf(stdout, "%s\n", "true");
		else
			(void) fprintf(stdout, "%s\n", "false");
		return (0);
	}
	if (strcmp(property, "effective-size") == 0) {
		if (longlist)
			(void) fprintf(stdout, "%lld\tro\n",
			    (long long)file_size);
		else
			(void) fprintf(stdout, "%lld\n",
			    (long long)file_size);
		return (0);
	}

	if (strcmp(property, "creation-time") == 0) {
		if (longlist)
			(void) fprintf(stdout, "creation-time: %s\tro",
			    ctime(&cr_time));
		else
			(void) fprintf(stdout, "creation-time: %s",
			    ctime(&cr_time));
		return (0);
	}
	if (strcmp(property, "creation-time-epoch") == 0) {
		if (longlist)
			(void) fprintf(stdout, "creation-time-epoch: %s\tro\n",
			    create_time);
		else
			(void) fprintf(stdout, "creation-time-epoch: %s\n",
			    create_time);
		return (0);
	}
	if (strcmp(property, "modification-time") == 0) {
		if (longlist)
			(void) fprintf(stdout, "modification-time: %s\tro",
			    ctime(&stat64buf_mod.st_mtime));
		else
			(void) fprintf(stdout, "modification-time: %s",
			    ctime(&stat64buf_mod.st_mtime));
		return (0);
	}
	if (strcmp(property, "modification-time-epoch") == 0) {
		if (longlist)
			(void) fprintf(stdout,
			    "modification-time-epoch: %ld\tro\n",
			    stat64buf_mod.st_mtime);
		else
			(void) fprintf(stdout,
			    "modification-time-epoch: %ld\n",
			    stat64buf_mod.st_mtime);
		return (0);
	}

	if (vdisk_get_prop_str(vdh, property, &val_str) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Property must exist"), property);
		goto fail;
	}

	if (longlist) {
		vdisk_get_prop_rw(property, &rw_str);
		(void) fprintf(stdout, "%s\t%s\n", val_str, rw_str);
	} else {
		(void) fprintf(stdout, "%s\n", val_str);
	}

	free(val_str);
	VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);

	return (0);

fail:
	if (pszformat)
		RTStrFree(pszformat);
	if ((vdh != NULL) && (vdh->hdd != NULL))
		VDDestroy(vdh->hdd);
	if (val_str)
		free(val_str);
	vdisk_free_tree(vdh);
	return (-1);
}

/*
 * Sets a user defined or native writeable property of a virtual disk.
 * -p option: property to be modified
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_propset_cmd(int argc, char *argv[])
{
	char *property;
	char *value;
	int opt;
	char xmlname[MAXPATHLEN];
	char extname[MAXPATHLEN];
	struct stat64 stat64buf;
	struct passwd *pw;
	char vdname[MAXPATHLEN];
	char pszformat_local[MAXPATHLEN];
	char *pszformat = NULL;
	int rc;
	int e;
	int switched_user = 0;
	vd_handle_t *vdh = NULL;
	PVBOXHDD pdisk;
	char *vtype;
	char *tst_value = NULL;

	property = NULL;
	value = NULL;

	/* option defaults */
	property = NULL;
	value = NULL;

	while ((opt = getopt(argc, argv, "p:")) != -1) {
		switch (opt) {
		case 'p':
			property = optarg;
			value = strrchr(optarg, '=');
			if (value == NULL) {
				(void) vdi_cmd_print_help(stderr, "prop-set");
				return (-1);
			}
			*value = 0x0;
			value += 1;
			break;

		case ':':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Missing argument for option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "prop-set");
			return (-1);

		case '?':
			(void) fprintf(stderr, "\n%s: %c\n\n",
			    gettext("ERROR: Invalid option"),
			    optopt);
			(void) vdi_cmd_print_help(stderr, "prop-set");
			return (-1);
		}
	}

	argc -= optind;
	argv += optind;

	if ((property == NULL) || (value == NULL) || (argc != 1)) {
		(void) vdi_cmd_print_help(stderr, "prop-set");
		return (-1);
	}

	/* 'owner' is handled differently since it must chown the files */
	if (strcmp(property, "owner") != 0) {
		if ((strcmp(property, "creation-time") == 0) ||
		    (strcmp(property, "modification-time") == 0) ||
		    (strcmp(property, "modification-time-epoch") == 0) ||
		    (strcmp(property, "effective-size") == 0)) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Property is readonly"), property);
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to set property"), property);
			goto fail;
		}

		if ((strcmp(property, "rwcnt") == 0) &&
		    (strcmp(value, "0") != 0)) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Can only set rwcnt to 0"),
			    property);
			goto fail;
		}

		if ((strcmp(property, "rocnt") == 0) &&
		    (strcmp(value, "0") != 0)) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Can only set rocnt to 0"),
			    property);
			goto fail;
		}

		if (vdisk_find_create_storepath(argv[0], vdname, NULL,
		    extname, &pszformat, 0, &vdh) == -1) {
			goto fail;
		}

		/* Can't change description on active files */
		if ((strcmp(property, "description") == 0) &&
		    (check_vdisk_in_use(vdh, argv[0])))
			goto fail;

		/* allocate disk handle */
		rc = VDCreate(NULL, &pdisk);
		if (!VBOX_SUCCESS(rc)) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Can only set rocnt to 0"),
			    property);
			goto fail;
		}
		vdh->hdd = pdisk;

		if ((vdisk_load_snapshots(vdh, pszformat, vdname,
		    VD_OPEN_FLAGS_READONLY)) == -1) {
			goto fail;
		}

		RTStrFree(pszformat);
		pszformat = NULL;

		if (vdisk_get_prop_str(vdh, property, &tst_value) == -1) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Property must exist"), property);
			goto fail;
		}
		free(tst_value);

		if (vdisk_set_prop_str(vdh, property, value,
		    VD_PROP_NORMAL) == -1) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to set property"), property);
			goto fail;
		}

		if (vdisk_write_tree(vdh, vdname) == -1) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to update store file"),
			    vdname);
			goto fail;
		}

		VDDestroy(vdh->hdd);
		vdisk_free_tree(vdh);

		return (0);
	}

	/* Handle owner property */
	pw = getpwnam(value);
	if (pw == NULL) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: owner invalid"), value);
		return (-1);
	}

	vdisk_get_vdname(vdname, argv[0], MAXPATHLEN);
	vdisk_get_xmlfile(xmlname, vdname, MAXPATHLEN);

	/* Check owner of store file and chown if necessary */
	rc = stat64(xmlname, &stat64buf);
	if (rc == -1) {
		goto fail;
	}
	if (pw->pw_uid != stat64buf.st_uid) {
		/* Change owner of store file */
		rc = chown(xmlname, pw->pw_uid, -1);
		if (rc != 0) {
			/* Switch to owner given and try chown again */
			e = setgroups(0, NULL);
			if (e != 0) {
				(void) fprintf(stderr, "\n%s\n",
				    gettext("unable to clear groups"));
				goto fail;
			}
			e = setregid(pw->pw_gid, pw->pw_gid);
			if (e != 0) {
				(void) fprintf(stderr, "\n%s: %d\n",
				    gettext("unable to switch to gid"),
				    (int)pw->pw_gid);
				goto fail;
			}
			e = setreuid(pw->pw_uid, pw->pw_uid);
			if (e != 0) {
				(void) fprintf(stderr, "\n%s: %s\n",
				    gettext("unable to switch to user"),
				    value);
				goto fail;
			}
			switched_user = 1;
			/* If chown succeeds, finish command as that uid */
			rc = chown(xmlname, pw->pw_uid, -1);
			if (rc != 0) {
				/* chown still fails - just fail */
				goto fail;
			}
		}
	}

	if (vdisk_read_tree(&vdh, vdname) == -1)
		goto fail;

	(void) vdisk_get_prop_str(vdh, "vtype", &vtype);
	if (vdisk_ext2format(vtype, pszformat_local) == -1) {
		free(vtype);
		goto fail;
	}

	if (vdisk_set_prop_str(vdh, "owner", value, VD_PROP_NORMAL)) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n",
		    gettext("ERROR: unable to store owner"), value);
		goto fail;
	}

	/* Write new snapshot list to store */
	if (vdisk_write_tree(vdh, vdname) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to update store file"), vdname);
		goto fail;
	}

	rc = VDCreate(NULL, &pdisk);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}
	vdh->hdd = pdisk;

	if ((vdisk_load_snapshots(vdh, pszformat_local, vdname,
	    VD_OPEN_FLAGS_READONLY)) == -1) {
		goto fail;
	}

	/* Change owner on format specific file and all extents */
	rc = VDSetAttr(vdh->hdd, "SetOwner", pw->pw_uid);
	if (rc != 0) {
		/* If switched owners and it fails -- done */
		if (switched_user)
			goto fail;
		/* Switch to owner given and try chown again */
		e = setgroups(0, NULL);
		if (e != 0) {
			(void) fprintf(stderr, "\n%s\n",
			    gettext("unable to clear groups"));
			goto fail;
		}
		e = setregid(pw->pw_gid, pw->pw_gid);
		if (e != 0) {
			(void) fprintf(stderr, "\n%s: %d\n",
			    gettext("unable to switch to gid"),
			    (int)pw->pw_gid);
			goto fail;
		}
		e = setreuid(pw->pw_uid, pw->pw_uid);
		if (e != 0) {
			(void) fprintf(stderr, "\n%s: %s\n",
			    gettext("unable to switch to user"),
			    value);
			goto fail;
		}
		/* If chown succeeds, finish command as that uid */
		rc = VDSetAttr(vdh->hdd, "SetOwner", pw->pw_uid);
		if (rc != 0) {
			/* chown still fails - just fail */
			goto fail;
		}
	}

	/* Check owner of vdname directory and chown if necessary */
	rc = stat64(vdname, &stat64buf);
	if (rc == -1) {
		goto fail;
	}
	if (pw->pw_uid != stat64buf.st_uid) {
		/* Change owner of store file */
		rc = chown(vdname, pw->pw_uid, -1);
		if (rc != 0) {
			goto fail;
		}
	}

	/* Close all and free hdd */
	VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);
	return (0);

fail:
	if ((vdh != NULL) && (vdh->hdd != NULL))
		VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);
	if (pszformat)
		RTStrFree(pszformat);
	if (tst_value)
		free(tst_value);
	return (-1);
}

/*
 * Move a virtual disk to a different directory.
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_move_cmd(int argc, char *argv[])
{
	char vdname[MAXPATHLEN];	/* path to virtual disk */
	char extname[MAXPATHLEN];	/* extension type of virtual disk */
	char checkname[MAXPATHLEN];	/* path to check for existing vdisk */
	char *pszformat = NULL;		/* VBox's extension type of disk */
	vd_handle_t *vdh = NULL;
	int rc;
	PVBOXHDD pdisk;
	char *at;
	struct stat64 stat64buf;

	if (argc < 3) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Missing name or path argument"));
		(void) vdi_cmd_print_help(stderr, "move");
		exit(-1);
	}

	at = strrchr(argv[1], '@');
	if (at) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Must use vdname not snapshot"));
		(void) vdi_cmd_print_help(stderr, "move");
		exit(-1);
	}

	/* Fail if dest dir is already a vdisk */
	(void) strlcpy(checkname, argv[2], MAXPATHLEN);
	(void) strlcat(checkname, "/vdisk.xml", MAXPATHLEN);
	rc = stat64(checkname, &stat64buf);
	if (rc != -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Directory cannot be a virtual disk"),
		    argv[2]);
		exit(-1);
	}

	if (vdisk_find_create_storepath(argv[1], vdname, NULL,
	    extname, &pszformat, 0, &vdh) == -1) {
		goto fail;
	}

	if (check_vdisk_in_use(vdh, argv[1]))
		goto fail;


	/* Alloc handle space */
	rc = VDCreate(NULL, &pdisk);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		goto fail;
	}
	vdh->hdd = pdisk;

	if ((vdisk_load_snapshots(vdh, pszformat, vdname, 0)) == -1) {
		goto fail;
	}

	if ((vdisk_move_snapshots(vdh, pszformat, vdname, argv[2]))
	    == -1) {
		goto fail;
	}

	RTStrFree(pszformat);
	pszformat = NULL;

	/* Close all and free pdisk */
	VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);

	return (0);

fail:
	if (pszformat)
		RTStrFree(pszformat);
	if ((vdh != NULL) && (vdh->hdd != NULL))
		VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);
	return (-1);
}

/*
 * Renames an entire virtual disk or a single snapshot.
 *
 * Returns:
 *	0: success
 *	-1: on failure
 */
static int
vdi_rename_cmd(int argc, char *argv[])
{
	char vdname_old[MAXPATHLEN];	/* path to virtual disk */
	char vdname_new[MAXPATHLEN];	/* path to virtual disk */
	char *slash_old;
	char *slash_new;
	char *at_old;
	char *at_new;
	struct stat64 stat64buf;
	vd_handle_t *vdh = NULL;
	int rc;
	int ret = 0;
	PVBOXHDD pdisk;
	int mv_image_number = 0;
	char snapname[MAXPATHLEN];		/* snapshot of disk */
	char old_snapname[MAXPATHLEN];		/* snapshot of disk */
	char new_snapname[MAXPATHLEN];		/* snapshot of disk */
	char oldsnapname_ext[MAXPATHLEN];	/* snapshot with extension */
	char newsnapname_ext[MAXPATHLEN];	/* snapshot with extension */
	char *snap_loc;
	char extname[MAXPATHLEN];	/* extension type of virtual disk */
	char *pszformat = NULL;		/* VBox's extension type of disk */
	unsigned int open_flags, open_flags_child;
	unsigned int uimageflags;

	if (argc < 3) {
		(void) fprintf(stderr, "\n%s\n\n",
		    gettext("ERROR: Missing name argument"));
		(void) vdi_cmd_print_help(stderr, "rename");
		return (-1);
	}

	/* verify that old path and new path have same ancestry */
	(void) strlcpy(vdname_old, argv[1], MAXPATHLEN);
	(void) strlcpy(vdname_new, argv[2], MAXPATHLEN);
	/* rename old path to new path */
	slash_old = strrchr(vdname_old, '/');
	slash_new = strrchr(vdname_new, '/');

	if (slash_old) {
		*slash_old = '\0';
		slash_old++;
	}

	if (slash_new) {
		*slash_new = '\0';
		slash_new++;
	}

	/* both paths should have paths or not */
	if (((slash_old == NULL) && (slash_new != NULL)) ||
	    ((slash_old != NULL) && (slash_new == NULL))) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n",
		    gettext("ERROR: old and new virtual disks must have "
		    "same path for rename"), argv[1]);
		(void) vdi_cmd_print_help(stderr, "rename");
		ret = -1;
		goto fail;
	}

	/* If old had a slash, both should have one, check for same path */
	if ((slash_old) && (strcmp(vdname_old, vdname_new) != 0)) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n",
		    gettext("ERROR: old and new virtual disks must have "
		    "same path for rename"), argv[1]);
		(void) vdi_cmd_print_help(stderr, "rename");
		ret = -1;
		goto fail;
	}

	/* both paths should have snapshots or not */
	at_old = strrchr(argv[1], '@');
	at_new = strrchr(argv[2], '@');
	if (((at_old == NULL) && (at_new != NULL)) ||
	    ((at_old != NULL) && (at_new == NULL))) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n",
		    gettext("ERROR: old and new must be both virtual disks "
		    "or snapshots"), argv[1]);
		(void) vdi_cmd_print_help(stderr, "rename");
		ret = -1;
		goto fail;
	}

	/* if at_old is non-NULL, then dealing with snapshots */
	if (at_old != NULL) {
		/* set vdname_[new/old] to respective vdisks */
		if (slash_old) {
			(void) strlcpy(vdname_new, slash_new, MAXPATHLEN);
			(void) strlcpy(vdname_old, slash_old, MAXPATHLEN);
		} else {
			(void) strlcpy(vdname_old, argv[1], MAXPATHLEN);
			(void) strlcpy(vdname_new, argv[2], MAXPATHLEN);
		}
		at_old = strrchr(vdname_old, '@');
		at_new = strrchr(vdname_new, '@');
		*at_old = '\0';
		*at_new = '\0';
		/* vdisk must be the same to rename a snapshot */
		if ((strcmp(vdname_old, vdname_new) != 0)) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n",
			    gettext("ERROR: old and new snapshots must have "
			    "same virtual disk for rename"), vdname_old);
			(void) vdi_cmd_print_help(stderr, "rename");
			ret = -1;
			goto fail;
		}

		if (vdisk_find_create_storepath(argv[1], vdname_old, NULL,
		    extname, &pszformat, 0, &vdh) == -1) {
			ret = -1;
			goto fail;
		}

		if (check_vdisk_in_use(vdh, vdname_old)) {
			ret = -1;
			goto fail;
		}

		/* Formulate snapshot name */
		snap_loc = strrchr(argv[1], '@');
		/* Check name length allowing for '.'  + 9 letter ext */
		if ((strlen(vdname_old) + strlen(snap_loc) + 10) > MAXPATHLEN) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Name is too long"), argv[1]);
			ret = -1;
			goto fail;
		}

		/* Get name of existing snapshot of form vdname@snapname */
		(void) strlcpy(old_snapname, snap_loc, MAXPATHLEN);

		/* Get actual file name of existing snapshot including ext */
		vdisk_get_vdfilebase(vdh, snapname, vdname_old, MAXPATHLEN);
		(void) strlcat(snapname, snap_loc, MAXPATHLEN);
		copy_add_ext(oldsnapname_ext, snapname, extname);

		/* Get name of new snapshot with base vdname_old */
		snap_loc = strrchr(argv[2], '@');
		(void) strlcpy(new_snapname, snap_loc, MAXPATHLEN);

		/* Get actual file name of new snapshot including ext */
		vdisk_get_vdfilebase(vdh, snapname, vdname_old, MAXPATHLEN);
		(void) strlcat(snapname, snap_loc, MAXPATHLEN);
		copy_add_ext(newsnapname_ext, snapname, extname);

		/* Alloc handle space */
		rc = VDCreate(NULL, &pdisk);
		if (!VBOX_SUCCESS(rc)) {
			(void) fprintf(stderr, "\n%s\n\n", gettext(
			    "ERROR: Unable to allocate handle space."));
			ret = -1;
			goto fail;
		}
		vdh->hdd = pdisk;

		if ((vdisk_load_snapshots(vdh, pszformat,
		    vdname_old, 0)) == -1) {
			ret = -1;
			goto fail;
		}

		if ((vdisk_find_snapshots(vdh, oldsnapname_ext,
		    &mv_image_number, NULL)) == -1) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to find file to snapshot"),
			    vdname_old);
			ret = -1;
			goto fail;
		}

		/* Switch snapshot being renamed to read/write mode */
		rc = VDGetOpenFlags(vdh->hdd, mv_image_number, &open_flags);
		if (!(VBOX_SUCCESS(rc))) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to get open state of file"),
			    vdname_old);
			ret = -1;
			goto fail;
		}

		rc = VDSetOpenFlags(vdh->hdd, mv_image_number,
		    (open_flags & ~VD_OPEN_FLAGS_READONLY));
		if (!(VBOX_SUCCESS(rc))) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to make snapshot writeable"),
			    vdname_old);
			ret = -1;
			goto fail;
		}

		/* Switch snapshot whose Hint will be changed to read/write */
		rc = VDGetOpenFlags(vdh->hdd, (mv_image_number + 1),
		    &open_flags_child);
		if (!(VBOX_SUCCESS(rc))) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to get open state of file"),
			    vdname_old);
			ret = -1;
			goto fail;
		}

		rc = VDSetOpenFlags(vdh->hdd, (mv_image_number + 1),
		    (open_flags_child & ~VD_OPEN_FLAGS_READONLY));
		if (!(VBOX_SUCCESS(rc))) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to make snapshot writeable"),
			    vdname_old);
			ret = -1;
			goto fail;
		}

		/* Change vdisk snapshot name in store file to new name */
		if (vdisk_rename_snap(vdh, "name", old_snapname,
		    new_snapname) == -1) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to update store file"),
			    vdname_old);
			/* revert open flags back for snapshot */
			(void) VDSetOpenFlags(vdh->hdd, mv_image_number,
			    open_flags);
			(void) VDSetOpenFlags(vdh->hdd, (mv_image_number + 1),
			    open_flags_child);
			ret = -1;
			goto fail;
		}

		/* Find old and new disk file names skipping over path */
		slash_old = strrchr(oldsnapname_ext, '/');
		if (slash_old)
			slash_old++;
		else
			slash_old = oldsnapname_ext;
		slash_new = strrchr(newsnapname_ext, '/');
		if (slash_new)
			slash_new++;
		else
			slash_new = newsnapname_ext;
		/* Change vdisk snapshot filename in store file to new name */
		if (vdisk_rename_snap(vdh, "vdfile", slash_old,
		    slash_new) == -1) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to update store file"),
			    vdname_old);
			/* revert open flags */
			(void) VDSetOpenFlags(vdh->hdd, mv_image_number,
			    open_flags);
			ret = -1;
			goto fail;
		}

		/* Write out store with new snapshot name */
		if (vdisk_write_tree(vdh, vdname_old) == -1) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to update store file"),
			    vdname_old);
			(void) VDSetOpenFlags(vdh->hdd, mv_image_number,
			    open_flags);
			ret = -1;
			goto fail;
		}

		/* Renaming snapshot using VDCopy. */
		(void) VDGetImageFlags(vdh->hdd, 0, &uimageflags);
		rc = VDCopy(vdh->hdd, mv_image_number, vdh->hdd, pszformat,
		    newsnapname_ext, true, 0, uimageflags, NULL, NULL,
		    NULL, NULL);
		if (!(VBOX_SUCCESS(rc))) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to rename snapshot of file"),
			    vdname_old);
			/* Since store was written, revert and write out */
			(void) vdisk_rename_snap(vdh, "name", old_snapname,
			    new_snapname);
			(void) vdisk_rename_snap(vdh, "vdfile",
			    oldsnapname_ext, newsnapname_ext);
			(void) vdisk_write_tree(vdh, vdname_old);
			/* Revert open flags */
			(void) VDSetOpenFlags(vdh->hdd, mv_image_number,
			    open_flags);
			ret = -1;
			goto fail;
		}



		RTStrFree(pszformat);
		VDDestroy(vdh->hdd);
		vdisk_free_tree(vdh);

		return (0);
	}

	if (vdisk_read_tree(&vdh, argv[1]) == -1) {
		ret = -1;
		goto fail;
	}

	if (check_vdisk_in_use(vdh, argv[1])) {
		ret = -1;
		goto fail;
	}

	/* just dealing with virtual disk */
	rc = stat64(argv[2], &stat64buf);
	if (rc != -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Virtual disk path already exists"),
		    argv[2]);
		ret = -1;
		goto fail;
	}
	(void) strlcpy(vdname_new, argv[2], MAXPATHLEN);
	slash_new = strrchr(vdname_new, '/');
	if (slash_new)
		slash_new++;
	else
		slash_new = vdname_new;
	(void) strlcpy(vdname_old, argv[1], MAXPATHLEN);

	/* Change vdisk name in store file to new name */
	if (vdisk_set_prop_str(vdh, "name", slash_new, VD_PROP_IGN_RO) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to set name in store"), vdname_new);
		ret = -1;
		goto fail;
	}
	/* keep filename and name in sync - xvmserver uses filename */
	if (vdisk_set_prop_str(vdh, "filename", slash_new,
	    VD_PROP_IGN_RO) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to set name in store"), vdname_new);
		ret = -1;
		goto fail;
	}

	/* Write out store to old name since before rename of directory */
	if (vdisk_write_tree(vdh, vdname_old) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to update store file"), vdname_old);
		ret = -1;
		goto fail;
	}

	rc = rename(argv[1], argv[2]);
	if (rc != 0) {
		(void) fprintf(stderr, "\n%s: %s: %s\n\n",
		    gettext("ERROR: Unable to rename virtual disk"),
		    argv[1], strerror(errno));

		/* rewrite store file with previous name */
		slash_old = strrchr(vdname_old, '/');
		if (slash_old == NULL)
			slash_old = vdname_old;
		else
			slash_old++;
		(void) vdisk_set_prop_str(vdh, "name", slash_old,
		    VD_PROP_IGN_RO);
		/* keep filename and name in sync - xvmserver uses filename */
		(void) vdisk_set_prop_str(vdh, "filename", slash_old,
		    VD_PROP_IGN_RO);
		(void) vdisk_write_tree(vdh, vdname_old);
		ret = -1;
		goto fail;
	}

fail:
	if (pszformat)
		RTStrFree(pszformat);
	if ((vdh != NULL) && (vdh->hdd != NULL))
		VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);
	return (ret);
}


/* *** HELPER ROUTINES *** */

static void
vdi_usage()
{
	(void) fprintf(stderr, gettext("usage: vdiskadm command args ...\n"));
	(void) fprintf(stderr,
	    gettext("'vdiskadm help' lists the commands and formats"
	    " supported\n\n"));
	/* XXX - this is wrong abort(); */
}

static vdi_cmd_t *
vdi_cmd_lookup(char *name)
{
	int i;

	for (i = 0; i < VDI_CMD_CNT; i++) {
		if (strcmp(name, vdi_cmd_list[i].cl_name) == 0) {
			return (&vdi_cmd_list[i]);
		}
	}

	return (NULL);
}

/*
 * Given a numeric suffix, convert the value into a number of bits that the
 * resulting value must be shifted.
 */
static int
str2shift(const char *buf)
{
	const char *ends = "BKMGTPEZ";
	int i;

	if (buf[0] == '\0')
		return (0);
	for (i = 0; i < strlen(ends); i++) {
		if (toupper(buf[0]) == ends[i])
			break;
	}
	if (i == strlen(ends)) {
		return (-1);
	}

	/*
	 * We want to allow trailing 'b' characters for 'GB' or 'Mb'.  But don't
	 * allow 'BB' - that's just weird.
	 */
	if (buf[1] == '\0' || (toupper(buf[1]) == 'B' && buf[2] == '\0' &&
	    toupper(buf[0]) != 'B'))
		return (10*i);

	return (-1);
}

/*
 * Convert a string of the form '100G' into a real number.  Used when setting
 * properties or creating a volume.  'buf' is used to place an extended error
 * message for the caller to use.
 */
static int
zfs_nicestrtonum(const char *value, uint64_t *num)
{
	char *end;
	int shift;

	*num = 0;

	/* Check to see if this looks like a number.  */
	if ((value[0] < '0' || value[0] > '9') && value[0] != '.') {
		return (-1);
	}

	/* Rely on stroll() to process the numeric portion.  */
	errno = 0;
	*num = strtoll(value, &end, 10);

	/*
	 * Check for ERANGE, which indicates that the value is too large to fit
	 * in a 64-bit value.
	 */
	if (errno == ERANGE) {
		return (-1);
	}

	/*
	 * If we have a decimal value, then do the computation with floating
	 * point arithmetic.  Otherwise, use standard arithmetic.
	 */
	if (*end == '.') {
		double fval = strtod(value, &end);

		if ((shift = str2shift(end)) == -1)
			return (-1);

		fval *= pow(2, shift);

		if (fval > UINT64_MAX) {
			return (-1);
		}

		*num = (uint64_t)fval;
	} else {
		if ((shift = str2shift(end)) == -1)
			return (-1);

		/* Check for overflow */
		if (shift >= 64 || (*num << shift) >> shift != *num) {
			return (-1);
		}

		*num <<= shift;
	}

	return (0);
}


int
main(int argc, char *argv[])
{
	struct passwd *pw;
	vdi_cmd_t *cmd;
	int e;


	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	if (argc < 2) {
		goto mainfail_usage;
	}

	if (argc >= 3) {
		if (strcmp(argv[1], "-u") == 0) {
			pw = getpwnam(argv[2]);
			if (pw == NULL) {
				(void) fprintf(stderr, "\n%s: %s\n",
				    gettext("invalid user"), argv[2]);
				goto mainfail_usage;
			}
			e = setgroups(0, NULL);
			if (e != 0) {
				(void) fprintf(stderr, "\n%s\n",
				    gettext("unable to clear groups"));
				goto mainfail_usage;
			}
			e = setregid(pw->pw_gid, pw->pw_gid);
			if (e != 0) {
				(void) fprintf(stderr, "\n%s: %d\n",
				    gettext("unable to switch to gid"),
				    (int)pw->pw_gid);
				goto mainfail_usage;
			}
			e = setreuid(pw->pw_uid, pw->pw_uid);
			if (e != 0) {
				(void) fprintf(stderr, "\n%s: %s\n",
				    gettext("unable to switch to user"),
				    argv[2]);
				goto mainfail_usage;
			}
			argc -= 3;
			argv += 3;
		} else {
			argc--;
			argv++;
		}
	} else {
		argc--;
		argv++;
	}

	cmd = vdi_cmd_lookup(argv[0]);
	if (cmd == NULL) {
		goto mainfail_usage;
	}

	e = cmd->cl_cmd(argc, argv);
	return (e);

mainfail_usage:
	vdi_usage();
	return (-1);
}


static int
vdi_cmd_print_help(FILE *stream, char *str)
{
	vdi_cmd_t *cmd;


	cmd = vdi_cmd_lookup(str);
	if (cmd == NULL) {
		return (-1);
	}
	(void) fprintf(stream, "\n%s - %s\n",
	    gettext(cmd->cl_name), gettext(cmd->cl_desc));
	(void) fprintf(stream, "%s\n", gettext(cmd->cl_help));

	return (0);
}

/*
 * Given a name and an extension return string of form "name.ext".
 *	name_ext - fills in name concatenated with '.' and extension.
 *	name - string containing virtual disk image or snapshot
 *	ext - string containing extension in local format
 */
static void
copy_add_ext(char *name_ext, char *name, char *ext)
{
	(void) strlcpy(name_ext, name, MAXPATHLEN);
	(void) strlcat(name_ext, ".", MAXPATHLEN);
	(void) strlcat(name_ext, ext, MAXPATHLEN);
}


/*
 * Get type of virtual disk to create given args.
 */
static int
vdi_get_type_flags(VDBACKENDINFO *backend, char *optarg, uint_t *uimageflags)
{
	char *option_str;

	option_str = strchr(optarg, ':');
	if (option_str == NULL) {
		if (strcasecmp(optarg, "raw") == 0) {
			*uimageflags = VD_IMAGE_FLAGS_FIXED;
		} else {
			if ((backend->uBackendCaps & VD_CAP_CREATE_DYNAMIC)) {
				*uimageflags = VD_IMAGE_FLAGS_NONE;
			} else {
				*uimageflags = VD_IMAGE_FLAGS_FIXED;
			}
		}
		return (0);
	}

	option_str++;
	if ((strcmp(option_str, "sparse")) == NULL) {
		if ((backend->uBackendCaps & VD_CAP_CREATE_DYNAMIC) == 0) {
			(void) fprintf(stderr, "\n%s: %s\n\n",
			    gettext("ERROR: Disk type does not support sparse"),
			    optarg);
			return (-1);
		} else {
			*uimageflags = VD_IMAGE_FLAGS_NONE;
		}
	} else if ((strcmp(option_str, "fixed")) == NULL) {
		*uimageflags = VD_IMAGE_FLAGS_FIXED;
	} else if (option_str) {
		return (-1);
	}

	return (0);
}

/*
 * Returns a 1 if disk in use; 0 otherwise
 */
int
check_vdisk_in_use(vd_handle_t *vdh, char *print_name)
{
	char *rwcnt_str = NULL;
	int rwcnt;
	char *rocnt_str = NULL;
	int rocnt;

	/* Check to see if virtual disk is open for reading or writing */
	(void) vdisk_get_prop_str(vdh, "rwcnt", &rwcnt_str);
	if (strcmp(rwcnt_str, "<unknown>") == 0) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Invalid rwcnt for file"), print_name);
		if (rwcnt_str)
			free(rwcnt_str);
		goto fail;
	}
	rwcnt = atoi((char *)rwcnt_str);
	free(rwcnt_str);
	if (rwcnt > 0) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Cannot modify in-use virtual disk"),
		    print_name);
		goto fail;
	}

	(void) vdisk_get_prop_str(vdh, "rocnt", &rocnt_str);
	if (strcmp(rocnt_str, "<unknown>") == 0) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Invalid rocnt for file"), print_name);
		if (rocnt_str)
			free(rocnt_str);
		goto fail;
	}
	rocnt = atoi((char *)rocnt_str);
	free(rocnt_str);
	if (rocnt > 0) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Cannot modify in-use virtual disk"),
		    print_name);
		goto fail;
	}

	return (0);

fail:
	return (1);
}
