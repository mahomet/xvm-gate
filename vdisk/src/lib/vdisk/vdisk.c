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
#include <unistd.h>
#include <libgen.h>
#include <limits.h>

#include <locale.h>
#include <libintl.h>
#include <bsm/libbsm.h>
#include <pwd.h>
#include <libxml/parser.h>

#include "VBox/VBoxHDD.h"
#include "iprt/string.h"

#include "vdisk.h"
#include <errno.h>
int errno;

/*
 * Library for virtual disk routines.
 * Main entry points:
 *	vdisk_open
 * 	vdisk_close
 *	vdisk_read
 *	vdisk_write
 *	vdisk_flush
 *	vdisk_get_size
 *	vdisk_setflags
 * Virtual disk configuration is kept in a store file: vdisk.xml.
 * Virtual disks are created using vbox code and can be created of types
 * that are supported by the vbox code (vmdk, vdi, etc).
 * Virtual disk are created using the utility vdiskadm and then can
 * be accessed by the main entry routines.
 *
 * Rest of routines are helper routines needed by the main entry
 * points and vdiskadm.
 */

/* Elements that are children of the root vdisk */
typedef enum disk_element {
	VD_NAME,
	VD_VERSION,
	VD_PARENT,
	VD_DISKPROP,
	VD_SNAPSHOT,
} disk_element_t;

static const char *vddiskxml_elements[] = {
	"name",			/* VD_NAME */
	"version",		/* VD_VERSION */
	"parent",		/* VD_PARENT */
	"diskprop",		/* VD_DISKPROP */
	"snapshot",		/* VD_SNAPSHOT */
};

typedef struct prop_info {
	const char *prop_name;
	char *prop_rw;
} prop_info_t;

/* Used to print elements of vdisk and diskprop and if writable */
typedef enum prop_element {
	VD_E_NAME,
	VD_E_VERSION,
	VD_E_PARENT,
	VD_E_FILENAME,
	VD_E_VDFILE,
	VD_E_OWNER,
	VD_E_MAX_SIZE,
	VD_E_SECTORS,
	VD_E_DESCRIPTION,
	VD_E_USERPROP,
} prop_element_t;

static prop_info_t prop_element_info[] = {
	{"name", "ro"},			/* VD_E_NAME */
	{"version", "ro"},		/* VD_E_VERSION */
	{"parent", "ro"},		/* VD_E_PARENT */
	{"filename", "ro"},		/* VD_E_FILENAME */
	{"vdfile", "ro"},		/* VD_E_VDFILE */
	{"owner", "rw"},		/* VD_E_OWNER */
	{"max-size", "ro"},		/* VD_E_MAX_SIZE */
	{"sectors", "ro"},		/* VD_E_SECTORS */
	{"description", "rw"},		/* VD_E_DESCRIPTION */
	{"userprop", "rw"},		/* VD_E_USERPROP */
};

typedef enum prop_attr {
	VD_A_READONLY,
	VD_A_REMOVABLE,
	VD_A_CDROM,
	VD_A_CREATE_EPOCH,
	VD_A_VTYPE,
	VD_A_SPARSE,
	VD_A_RWCNT,
	VD_A_ROCNT
} prop_attribute_t;

/* Used to print attributes of vdisk and if writable */
static prop_info_t prop_attr_info[] = {
	{"readonly", "ro"},		/* VD_A_READONLY */
	{"removable", "ro"},		/* VD_A_REMOVABLE */
	{"cdrom", "ro"},		/* VD_A_CDROM */
	{"creation-time-epoch", "ro"},	/* VD_A_CREATE_EPOCH */
	{"vtype", "ro"},		/* VD_A_VTYPE */
	{"sparse", "ro"},		/* VD_A_SPARSE */
	{"rwcnt", "rw"},		/* VD_A_RWCNT */
	{"rocnt", "rw"},		/* VD_A_ROCNT */
};

struct VDIMAGE_small
{
	/*  Link to parent image descriptor, if any. */
	struct VDIMAGE  *pPrev;
	/*  Link to child image descriptor, if any. */
	struct VDIMAGE  *pNext;
	/*  Container base filename. (UTF-8) */
	char		*pszFilename;
	/* Data managed by the backend which keeps the actual info. */
	void		*pvBackendData;
	/* Cached sanitized image flags. */
	unsigned	uImageFlags;
};

struct VBOXHDD_small
{
	/* Structure signature (VBOXHDDDISK_SIGNATURE). */
	uint32_t	u32Signature;

	/* Number of opened images. */
	unsigned	cImages;

	/* Base image. */
	struct VDIMAGE	*pBase;
};


static int vdisk_is_unmanaged(const char *vdisk_path);
static vd_handle_t *vdisk_open_unmanaged(const char *vdisk_path);
static int vdisk_is_structured_file(const char *vdisk_name,
    const char **filetype);


char *vdisk_structured_files[] = {"vdi", "vmdk", "vhd"};


/*
 * vdisk_open opens a virtual disk and returns a handle that should
 * by used by subsequent calls to vdisk_read, vdisk_write, vdisk_flush,
 * vdisk_get_size and vdisk_close.
 *	vdisk_path - relative or full path to virtual disk.
 *
 * Returns:
 *	non-zero: handle to be used by subsequent calls.
 *	0: failure
 */
void *
vdisk_open(const char *vdisk_path)
{
	char vdname[MAXPATHLEN];
	char extname[MAXPATHLEN];
	char *pszformat = NULL;
	struct stat;
	int rc;
	vd_handle_t *vdh = NULL;
	PVBOXHDD pdisk;


	if (vdisk_is_unmanaged(vdisk_path)) {
		return (vdisk_open_unmanaged(vdisk_path));
	}

	if (vdisk_find_create_storepath(vdisk_path, vdname, NULL, extname,
	    &pszformat, 0, &vdh) == -1) {
		errno = ENOENT;
		goto fail;
	}

	/* allocate hard disk handle */
	rc = VDCreate(NULL, &pdisk);
	if (!VBOX_SUCCESS(rc)) {
		(void) fprintf(stderr, "%s\n", gettext(
		    "ERROR: Unable to allocate handle space."));
		errno = EIO;
		goto fail;
	}
	vdh->hdd = pdisk;
	vdh->unmanaged = B_FALSE;

	/* Read in store and load images */
	if ((vdisk_load_snapshots(vdh, pszformat, vdname, 0)) == -1) {
		errno = EIO;
		goto fail;
	}

	RTStrFree(pszformat);
	pszformat = NULL;

	return ((void *)vdh);

fail:
	if (pszformat)
		RTStrFree(pszformat);
	if ((vdh != NULL) && (vdh->hdd))
		VDDestroy(vdh->hdd);
	vdisk_free_tree(vdh);
	return (NULL);
}

/*
 * vdisk_read reads from a virtual disk.
 *	vdh: handle gotten from vdisk_open
 *	uoffset: offset from start of disk
 *	pvbuf: buffer to return data read
 *	cbread: number of bytes to be read.
 *
 * Returns:
 *	non-zero: number of bytes read.  Current vbox code assures that
 *		either all bytes or no bytes are returned from VDRead.
 *	0: failure
 */
ssize_t
vdisk_read(void *vdh, uint64_t uoffset, void *pvbuf, size_t cbread)
{
	int rc;

	uint64_t buf_start;
	uint64_t buf_end;
	uint64_t buf_len;
	char *tmp_buf;

	/*
	 * if the offset is sector (512 bytes) aligned, and the size is a
	 * whole multiple of the sector size, pass it right down to VDRead.
	 */
	if (((uoffset & 0x1FF) == 0) && ((cbread & 0x1FF) == 0)) {
		rc = VDRead(((vd_handle_t *)vdh)->hdd, uoffset, pvbuf, cbread);
		if (!VBOX_SUCCESS(rc)) {
			errno = EIO;
			return (-1);
		}
		return (cbread);
	}

	/* Handle unaligned reads */
	buf_start = uoffset & (~0x1ff);
	buf_end = uoffset + (uint64_t)cbread;
	if ((buf_end & 0x1FF) != 0)
		buf_end = (buf_end & ~0x1ff) + 0x200;
	buf_len = buf_end - buf_start;

	tmp_buf = malloc(buf_len);
	rc = VDRead(((vd_handle_t *)vdh)->hdd, buf_start, tmp_buf, buf_len);
	if (!VBOX_SUCCESS(rc)) {
		free(tmp_buf);
		errno = EIO;
		return (-1);
	}

	/* copy from tmp_buffer into real buffer */
	bcopy((tmp_buf + (uoffset % 0x200)), pvbuf, cbread);
	free(tmp_buf);

	return (cbread);
}

/*
 * vdisk_write writes data to a virtual disk.
 *	vdh: handle gotten from vdisk_open
 *	uoffset: offset from start of disk
 *	pvbuf: buffer of data to write
 *	cbread: number of bytes to write
 *
 * Returns:
 *	non-zero: number of bytes written.  Current vbox code assures that
 *		either all bytes or no bytes were written by VDWrite.
 *	0: failure
 */
ssize_t
vdisk_write(void *vdh, uint64_t uoffset, void *pvbuf, size_t cbwrite)
{
	int rc;

	/* Don't handle unaligned writes */
	if (((uoffset & 0x1FF) != 0) || ((cbwrite & 0x1FF) != 0)) {
		errno = EIO;
		return (-1);
	}

	rc = VDWrite(((vd_handle_t *)vdh)->hdd, uoffset, pvbuf, cbwrite);
	if (!VBOX_SUCCESS(rc)) {
		errno = EIO;
		return (-1);
	}
	return (cbwrite);
}

/*
 * vdisk_flush flushes writes to the virtual disk.
 *	vdh: handle gotten from vdisk_open
 *
 * Returns:
 *	0: success
 *	-1: failure
 */
int
vdisk_flush(void *vdh)
{
	int rc;

	rc = VDFlush(((vd_handle_t *)vdh)->hdd);
	if (!VBOX_SUCCESS(rc)) {
		errno = EIO;
		return (-1);
	}
	return (0);
}

/*
 * vdisk_get_size gets the size of the base image of the virtual disk.
 *	vdh: handle gotten from vdisk_open
 *
 * Returns:
 *	len: size of disk in bytes
 */
int64_t
vdisk_get_size(void *vdh)
{
	int64_t len;
	len = (int64_t)VDGetSize(((vd_handle_t *)vdh)->hdd, 0);
	return (len);
}

/*
 * vdisk_close releases the handle to the virtual disk
 *	vdh: handle gotten from vdisk_open
 */
void
vdisk_close(void *vdh)
{
	/* Close all and free hdd */
	VDDestroy(((vd_handle_t *)vdh)->hdd);
	vdisk_free_tree(vdh);
}

/*
 * vdisk_setflags calls the VBox VDSetOpenFlag routine with the
 * appropriate flag.
 *
 * Returns:
 *	0: success
 *	-1: failure
 */
int
vdisk_setflags(void *vdh, uint_t flags)
{
	int rc;

	if (flags == VD_NOFLUSH_ON_CLOSE) {
		rc = VDSetOpenFlags(((vd_handle_t *)vdh)->hdd, 0,
		    VD_OPEN_FLAGS_NOFLUSH_ON_CLOSE);
		if (!(VBOX_SUCCESS(rc)))
			return (-1);
		else
			return (0);
	}

	/* If no recognized flag given, then fail */
	return (-1);
}

/*
 * vdisk_check_vdisk checks if the given path is a virtual disk.
 *	vdisk_path - relative or full path to virtual disk.
 *
 * Returns:
 *	1: success; path is a virtual disk
 *	0: failure
 */
int
vdisk_check_vdisk(const char *vdisk_path)
{
	const char *state_file = "/vdisk.xml";
	char *state_path;

	if (vdisk_is_unmanaged(vdisk_path) &&
	    vdisk_is_structured_file(vdisk_path, NULL))
		return (1);

	state_path = malloc(PATH_MAX);
	strncpy(state_path, vdisk_path, PATH_MAX);
	if (strlen(state_path) > (PATH_MAX - strlen(state_file)))
		return (0);

	strcpy(&state_path[strlen(state_path)], state_file);
	if (access(state_path, F_OK) == 0)
		return (1);

	return (0);
}

/*
 * print_image_name_callback_parse is a callback that prints
 * an image name in a parsable format.  Image can be either a
 * file (base image) or a snapshot (differencing image).
 *	image_name: string containing the file name of the image.
 */
void
print_image_name_callback_parse(const char *image_name)
{
	if (strrchr(image_name, '@'))
		(void) printf("snapshot:%s\n", image_name);
	else
		(void) printf("file:%s\n", image_name);
}

/*
 * print_image_name_callback is a callback that prints
 * an image name.  Image can be either a file (base image)
 * or a snapshot (differencing image).
 *	image_name: string containing the file name of the image.
 */
void
print_image_name_callback(const char *image_name)
{
	(void) printf("%s\n", image_name);
}

/*
 * Print files associated with the virtual disk images.
 *	vdh - pointer to handle for virtual disk
 *	vdname - pointer to virtual disk
 *		may have extension on name if called from import
 *	parsable_output - prints easily parsable output
 *		if 1 then print "store:", "file:" or "snapshot" prefix
 *		if 0 just print file (don't print store file)
 *	print_store - if 1 print the store filename
 *	print_extents - if 1 print the extent filenames
 *	print_snaps - if print the snapshot filenames
 *
 * Returns:
 * 	0: success
 *	-1: failure
 */
int
vdisk_print_files(vd_handle_t *vdh, char *vdname, int parsable_output,
	int print_store, int print_extents, int print_snaps)
{
	char xmlname[MAXPATHLEN];
	char vdname_noext[MAXPATHLEN];
	char *pname;
	xmlNodePtr node, snap_node;
	xmlChar *fname = NULL;
	char *val_string;
	char *slash;
	int rc;
	char snapname[MAXPATHLEN];
	char vdname_local_noext[MAXPATHLEN];

	slash = strrchr(vdname, '/');
	if (slash == NULL)
		slash = vdname;
	else
		slash++;
	(void) strlcpy(vdname_local_noext, slash, MAXPATHLEN);

	/* May need to strip extension if called from import */
	(void) strlcpy(vdname_noext, vdname, MAXPATHLEN);
	pname = strrchr(vdname_noext, '.');
	if (pname)
		*pname = '\0';

	if (print_store) {
		vdisk_get_xmlfile(xmlname, vdname_noext, MAXPATHLEN);
		pname = strrchr(xmlname, '/');
		if (pname == NULL)
			pname = xmlname;
		else
			pname++;
		if (parsable_output) {
			(void) printf("store:%s\n", pname);
		} else {
			(void) printf("%s\n", pname);
		}
	}

	if (print_extents) {
		/* print base file, extents and snapshots */
		if (parsable_output) {
			VDDumpImages(vdh->hdd, 1,
			    print_image_name_callback_parse);
		} else {
			VDDumpImages(vdh->hdd, 1, print_image_name_callback);
		}
	}

	/* Print snapshots (and base) if directed and not already printed */
	if ((print_snaps) && (!print_extents) && (vdh->doc != NULL)) {
		for (node = vdh->snap_root; node != NULL; node = node->next) {
			for (snap_node = node->xmlChildrenNode;
			    snap_node != NULL; snap_node = snap_node->next) {
				if (xmlStrcmp(snap_node->name,
				    (xmlChar *)"name") != 0)
					continue;
				fname = xmlNodeListGetString(vdh->doc,
				    snap_node->xmlChildrenNode, 1);
				if (fname == NULL) {
					(void) fprintf(stderr, "\n%s:"
					    " \"%s\"\n\n",
					    gettext("ERROR: Unable to retrieve "
					    "snapshot name from store"),
					    vdname_local_noext);
					return (-1);
				}
				(void) strlcpy(snapname, vdname_local_noext,
				    MAXPATHLEN);
				(void) strlcat(snapname, (char *)fname,
				    MAXPATHLEN);

				if (parsable_output) {
					(void) printf("snapshot:%s\n",
					    snapname);
				} else {
					(void) printf("%s\n", snapname);
				}
				xmlFree(fname);
			}
		}

		rc = vdisk_get_prop_str(vdh, "name", &val_string);
		if (rc == -1) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to retrieve "
			    "name from store"), vdname_noext);
			free(val_string);
			return (-1);
		}

		pname = strrchr((char *)val_string, '/');
		if (pname)
			pname++;
		else
			pname = (char *)val_string;
		if (parsable_output) {
			(void) printf("file:%s\n", pname);
		} else {
			printf("%s\n", pname);
		}
		free(fname);
	}
	return (0);
}

/*
 * Load the virtual disk images from store file into hdd area.
 *	vdh - pointer to handle for virtual disk
 *	pszformat - string containing type of virtual disk
 *	vdname - string containing path to virtual disk
 *	open_flag - flag to open virtual disk in read-only mode
 *
 * Returns:
 * 	0: success
 *	-1: failure
 */
int
vdisk_load_snapshots(vd_handle_t *vdh, char *pszformat, char *vdname,
	int open_flag)
{
	struct stat64 statbuf;
	int rc;
	xmlChar *name = NULL;
	char vdpath[MAXPATHLEN];
	char fullname[MAXPATHLEN];
	xmlNodePtr  node, snap_node;
	char *val_string;

	(void) strlcpy(vdpath, vdname, MAXPATHLEN);
	(void) strlcat(vdpath, "/", MAXPATHLEN);

	/* verify snapshots exist and load them */
	for (node = vdh->snap_root; node != NULL; node = node->next) {
		for (snap_node = node->xmlChildrenNode;
		    snap_node != NULL; snap_node = snap_node->next) {
			if (xmlStrcmp(snap_node->name,
			    (xmlChar *)"vdfile") != 0)
				continue;
			name = xmlNodeListGetString(vdh->doc,
			    snap_node->xmlChildrenNode, 1);
			if (name == NULL) {
				(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
				    gettext("ERROR: Unable to retrieve "
				    "snapshot filename from store"), vdname);
				return (-1);
			}
			/* verify disk is present first */
			(void) strlcpy(fullname, vdpath, MAXPATHLEN);
			(void) strlcat(fullname, (char *)name, MAXPATHLEN);
			rc = stat64(fullname, &statbuf);
			if (rc == -1) {
				(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
				    gettext("ERROR: File does not exist"),
				    vdname);
				xmlFree(name);
				return (-1);
			}
			xmlFree(name);
			rc = VDOpen((PVBOXHDD)vdh->hdd, pszformat, fullname,
			    (VD_OPEN_FLAGS_NORMAL | open_flag), NULL);
			if (!(VBOX_SUCCESS(rc))) {
				(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
				    gettext("ERROR: Unable to open file"),
				    fullname);
				return (-1);
			}
		}
	}

	/* now verify and load vdname */
	if (vdisk_get_prop_str(vdh, "vdfile", &val_string) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to retrieve "
		    "filename from store"), vdname);
		return (-1);
	}

	(void) strlcpy(fullname, vdpath, MAXPATHLEN);
	(void) strlcat(fullname, (char *)val_string, MAXPATHLEN);
	rc = stat64(fullname, &statbuf);
	if (rc == -1) {
		fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: File does not exist"), vdname);
		return (-1);
	}

	rc = VDOpen((PVBOXHDD)vdh->hdd, pszformat, (char *)fullname,
	    (VD_OPEN_FLAGS_NORMAL | open_flag), NULL);
	if (!(VBOX_SUCCESS(rc))) {
		fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to open file"), vdname);
		return (-1);
	}
	return (0);
}

/*
 * Write xml tree to store file.
 *	vdh - pointer to virtual disk handle
 *	vdname - string containing path to virtual disk store file
 *
 * Returns:
 * 	0: success
 *	-1: failure
 */
int
vdisk_write_tree(vd_handle_t *vdh, char *vdname)
{
	char xmlname[MAXPATHLEN];

	if (vdh->doc == NULL) {
		fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: no tree exists"), vdname);
		return (-1);
	}

	vdisk_get_xmlfile(xmlname, vdname, MAXPATHLEN);

	(void) xmlSaveFormatFileEnc(xmlname, vdh->doc, NULL, 1);
	return (0);
}

/*
 * Find an image's associated image number given a file name.
 * Image numbers start at 0.
 *	vdh - pointer to handle for virtual disk
 *	pszrmfilename - string containing path of image to find
 *	image_number - contains image number on successful return
 *	total_image_number - if non-null contains the image count on return
 *
 * Returns:
 * 	0: success
 *	-1: failure
 */
int
vdisk_find_snapshots(vd_handle_t *vdh, char *pszrmfilename,
	int *image_number, int *total_image_number)
{
	struct VDIMAGE_small *pimage;
	struct VBOXHDD_small *pdisk_sm;
	int image_cnt = 0;
	int found_flag = 0;
	char *findfile, *imagefile;

	if (vdh == NULL)
		return (-1);

	pdisk_sm = (struct VBOXHDD_small *)vdh->hdd;

	/* Only search in store file for filename, not path */
	findfile = strrchr(pszrmfilename, '/');
	if (findfile)
		findfile++;
	else
		findfile = pszrmfilename;

	for (pimage = (struct VDIMAGE_small *)pdisk_sm->pBase; pimage;
	    pimage = (struct VDIMAGE_small *)pimage->pNext) {
		if (found_flag == 0) {
			imagefile = strrchr(pimage->pszFilename, '/');
			if (imagefile)
				imagefile++;
			else
				imagefile = pimage->pszFilename;

			if (strcmp(findfile, imagefile) == 0) {
				*image_number = image_cnt;
				found_flag = 1;
			}
		}
		image_cnt++;
	}

	if (total_image_number)
		*total_image_number = image_cnt;

	if (found_flag == 0)
		return (-1);

	return (0);
}

struct list_image {
	char			*list_image_name;
	struct list_image	*list_image_next;
};

struct list_image *list_image_head = NULL;

/*
 * list_image_name_callback is a callback that adds the given
 * image to a linked list of images for later use.
 *	image_name: string containing the file name of the image.
 */
void
list_image_name_callback(const char *image_name)
{
	int len;
	struct list_image li;
	struct list_image *lip;

	lip = malloc(sizeof (li));
	len = strlen(image_name) + 1;
	lip->list_image_name = malloc(len);
	strlcpy(lip->list_image_name, image_name, len);
	lip->list_image_next = list_image_head;
	list_image_head = lip;
}

/*
 * Move the virtual disk images to another directory.
 *	vdh - pointer to handle for virtual disk
 *	pszformat - format extension of file being moved
 *	vdname - name of virtual disk being moved
 *	new_dir - string containing new directory of virtual disk
 *
 * Returns:
 *	0: success
 *	-1: failure
 */
int
vdisk_move_snapshots(vd_handle_t *vdh, char *pszformat, char *vdname,
	char *new_dir)
{
	char vdname_newdir[MAXPATHLEN];
	char system_command[MAXPATHLEN];
	int rc;
	char *slash;
	struct stat64 statbuf;

	/* verify that new directory exists */
	rc = stat64(new_dir, &statbuf);
	if (rc == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Directory doesn't exist"), new_dir);
		return (-1);
	}
	(void) strlcpy(vdname_newdir, new_dir, MAXPATHLEN);
	slash = strrchr(vdname, '/');
	if (slash) {
		(void) strlcat(vdname_newdir, slash, MAXPATHLEN);
	} else {
		(void) strlcat(vdname_newdir, "/", MAXPATHLEN);
		(void) strlcat(vdname_newdir, vdname, MAXPATHLEN);
	}

	rc = rename(vdname, vdname_newdir);
	if (rc == 0) {
		return (0);
	}

	/* If failed for some other reason than cross-device link, fail */
	if ((rc == -1) && (errno != EXDEV)) {
		(void) fprintf(stderr, "\n%s: %s: %s\n\n",
		    gettext("ERROR: Unable to move virtual disk"),
		    vdname, strerror(errno));
		return (-1);
	}

	/* If rename can't be used, do a system mv instead. */
	snprintf(system_command, MAXPATHLEN, "mv %s %s\n",
	    vdname, vdname_newdir);
	rc = system(system_command);
	if (rc != 0) {
		(void) fprintf(stderr, "\n%s: %s: %s\n\n",
		    gettext("ERROR: Unable to move virtual disk"),
		    vdname, strerror(errno));
		return (-1);
	}

	return (0);
}

/*
 * Find or create the store file given a full or relative pathname to
 * the store file.  All passed in strings must be of at least
 * MAXPATHLEN in length.  extname, ppszformat and snapname can be NULL.
 *	name - full or relative pathname to store file
 *	vdname - fills in full pathname to verified virtual disk on return
 *	snapname - if non-null fills in snapshot if given name was a snapshot
 *	extname - if non-null fills in the extension name of virtual disk
 *	ppszformat - if non-null fills in the vbox ext name of virtual disk
 *	create_flag - if 0 - find the file; if 1 - create the file
 *	vdhp - will malloc handle and return on find (non-create) case;
 *		ignores on create case so can be null on create.
 *
 * Returns:
 * 	0: success
 *	-1: failure
 *
 * Note: If ppszformat is non-NULL, then caller
 * is responsible for freeing *ppszformat using RTStrFree().
 */
int
vdisk_find_create_storepath(const char *name, char *vdname, char *snapname,
    char *extname, char **ppszformat, int create_flag, vd_handle_t **vdhp)
{
	char fullpathreadname[MAXPATHLEN];
	char *working_name;
	char *special_char;
	char *snap;
	int rc;
	struct stat64 statbuf;
	int snap_flag = 0;
	char xmlname[MAXPATHLEN];
	xmlNodePtr node, snap_node;
	char *val_string = NULL;
	vd_handle_t *vdh = NULL;
	xmlChar *rname;

	/* Check given name length allowing for '.' plus 9 letter extension */
	if ((strlen(name) + 10) > MAXPATHLEN) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Name is too long"), name);
		return (-1);
	}

	vdisk_get_vdname(vdname, name, MAXPATHLEN);

	if (create_flag) {
		/* directory for virtual disk should not exist */
		rc = stat64(vdname, &statbuf);
		if (rc != -1) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Virtual disk "
			    "already exists"), vdname);
			return (-1);
		}

		/* create directory */
		if ((mkdirp(vdname, 0755) == -1) && (errno != EEXIST)) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Cannot create virtual disk"),
			    vdname);
			return (-1);
		}
	}

	/* If create, then setup empty strings and return */
	if (create_flag) {
		/* Set extensions and snapshot to empty string on create */
		if (extname)
			extname[0] = '\0';
		if (snapname)
			snapname[0] = '\0';
		if (ppszformat)
			(*ppszformat)[0] = '\0';
		return (0);
	}

	/* Non-creation case; finish setting up snapshot name */
	snap = strrchr(name, '@');
	if (snap == NULL) {
		/* snapshot name not given, set snapname to empty string */
		if (snapname)
			snapname[0] = '\0';
	} else {
		/* Set flag and pointer to later find snapshot pathname */
		if (snapname) {
			snap_flag = 1;
			working_name = snap + 1;
		}
	}

	/* parse store file to get extension */
	if ((extname == NULL) || (ppszformat == NULL)) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: No space to return format extension"),
		    vdname);
		return (-1);
	}

	rc = stat64(vdname, &statbuf);
	if (rc == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Virtual disk does not exist"),
		    vdname);
		return (-1);
	}

	vdisk_get_xmlfile(xmlname, vdname, MAXPATHLEN);
	rc = stat64(xmlname, &statbuf);
	if (rc == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Virtual disk store does not exist"),
		    xmlname);
		return (-1);
	}

	if (vdisk_read_tree(vdhp, vdname) == -1)
		return (-1);
	vdh = *vdhp;

	/* Get name of current file */
	if (vdisk_get_prop_str(vdh, "vdfile", &val_string) == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Retrieval of filename failed"),
		    vdname);
		vdisk_free_tree(vdh);
		return (-1);
	}

	(void) strlcpy(fullpathreadname, vdname, MAXPATHLEN);
	(void) strlcat(fullpathreadname, "/", MAXPATHLEN);
	(void) strlcat(fullpathreadname, val_string, MAXPATHLEN);
	free(val_string);

	if (vdisk_find_format_ext(vdh, vdname, fullpathreadname,
	    ppszformat, extname, 1)
	    == -1) {
		free(vdh);
		return (-1);
	}

	if (snap_flag) {
		node = vdh->snap_root;
		while (node) {
			snap_node = node->xmlChildrenNode;
			if (snap_node == NULL) {
				/* Continue with next snapshot */
				node = node->next;
				continue;
			}
			rname = NULL;
			if (xmlStrcmp(snap_node->name,
			    (xmlChar *)"name") == 0) {
				rname = xmlNodeListGetString(vdh->doc,
				    snap_node->xmlChildrenNode, 1);
			}
			/* compare given snapshot name to store file snaps */
			if (rname == NULL) {
				/* Continue with next snapshot */
				node = node->next;
				continue;
			}
			/* If found the snapshot -- then break out */
			special_char = strrchr((char *)rname, '@');
			if ((special_char) &&
			    (strcmp(working_name, (special_char + 1)) == 0)) {
				break;
			}

			/* Continue with next snapshot node. */
			xmlFree(rname);
			node = node->next;
		}
		/* If no more snapshots in store file - then fail */
		if (node == NULL) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to find snapshot"),
			    vdname);
			free(vdh);
			return (-1);
		}
		/* Copy entry from store file into snapname */
		vdisk_get_vdfilebase(*vdhp, snapname, vdname, MAXPATHLEN);
		(void) strlcat(snapname, "@", MAXPATHLEN);
		(void) strlcat(snapname, working_name, MAXPATHLEN);
		xmlFree(rname);
	}
	return (0);
}

/*
 * Given a path to a virtual disk file, return the extension name in
 * VBox and local format.
 *	vdh - virtual disk handle
 *	vdname - used to print error messages for virtual disk
 *	vdfile - path to actual virtual disk file including extension
 *	ppszformat - allocs and fills in VBox extension name on return
 *	extname - fills in local extension on return
 *	use_store - if set will look at store file first to determine
 *		extension type.
 *
 * Returns:
 * 	0: success
 *	-1: failure
 *
 * Note: If ppszformat is non-NULL, then caller
 * is responsible for freeing *ppszformat using RTStrFree().
 */
int
vdisk_find_format_ext(vd_handle_t *vdh, char *vdname, char *vdfile,
	char **ppszformat, char *extname, int use_store)
{
	int rc;
	char *special_char;
	char *type = NULL;
	char pszformat_local[MAXPATHLEN];

	special_char = strrchr(vdfile, '.');
	if (special_char == NULL) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Virtual disk file missing "
		    "format extension"), vdname);
		return (-1);
	}

	/* Handle special .iso case since no store entry for this */
	special_char++;
	if (strcasecmp("iso", special_char) == 0) {
		(void) strlcpy(extname, (special_char), MAXPATHLEN);
		/* Use RTStrDup since freed with RTStrFree */
		*ppszformat = RTStrDup("raw");
		if (*ppszformat == NULL) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Dup of type failed"), vdname);
			return (-1);
		}
		return (0);
	}

	/* Use store file, if possible */
	if ((use_store) && (vdh->disk_root != NULL)) {
		type = (char *)xmlGetProp(vdh->disk_root,
		    (const xmlChar *)"vtype");
		(void) strlcpy(extname, type, MAXPATHLEN);
		free(type);
		if (vdisk_ext2format(extname, pszformat_local) == -1)
			return (-1);
		/* Use vbox allocator so can be freed with RTStrFree */
		*ppszformat = RTStrDup(pszformat_local);
		if (*ppszformat == NULL) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Dup of type failed"), vdname);
			return (-1);
		}
		return (0);
	}

	/* Check for special raw case since VDGetFormat fails for raw */
	if (strcasecmp("raw", special_char) == 0) {
		(void) strlcpy(extname, "raw", MAXPATHLEN);
		/* Use RTStrDup since freed with RTStrFree */
		*ppszformat = RTStrDup("raw");
		if (*ppszformat == NULL) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Dup of type failed"), vdname);
			return (-1);
		}
		return (0);
	}

	/* Can't use store, must find type looking into file data */
	*ppszformat = NULL;
	rc = VDGetFormat(vdfile, ppszformat);
	if (!(VBOX_SUCCESS(rc))) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to get format of virtual disk"),
		    vdname);
		if (*ppszformat != NULL) {
			RTStrFree(*ppszformat);
		}
		return (-1);
	}

	if (vdisk_format2ext(*ppszformat, extname) == -1) {
		RTStrFree(*ppszformat);
		return (-1);
	}

	return (0);
}

/*
 * Return local format extension given VBox extension.
 * Typically, the local extension is a lower case version of the
 * VBox format.
 *	pszformat - VBox extension name
 *	extname - fills in local extension on return
 *
 * Returns:
 * 	0: success
 *	-1: failure
 */
int
vdisk_format2ext(char *pszformat, char *extname)
{
	int i;
	int len;

	len = strlen(pszformat);
	if (len >= MAXPATHLEN) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: File has invalid format"), pszformat);
		return (-1);
	}

	/* raw format type isn't capitalized */
	if (strcasecmp("raw", pszformat) == 0) {
		strlcpy(extname, "raw", MAXPATHLEN);
		return (0);
	}

	for (i = 0; i < len; i++) {
		extname[i] = tolower(pszformat[i]);
	}
	extname[i] = '\0';
	return (0);
}

/*
 * Return format extension given VBox extension.
 * format extension is returned into the pszformat string.
 * pszformat string must be MAXPATHLEN long.
 * Typically, the VBox format is an uppercase form of the
 * local extension name.
 *	extname - fills in local extension on return
 *	pszformat - VBox extension name
 *
 * Returns:
 * 	0: success
 *	-1: failure
 */
int
vdisk_ext2format(char *extname, char *pszformat)
{
	int i;
	int len;

	len = strlen(extname);
	if (len >= MAXPATHLEN) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: File has invalid format"), pszformat);
		return (-1);
	}

	/* raw format type isn't capitalized */
	if (strcasecmp("raw", extname) == 0) {
		strlcpy(pszformat, "raw", MAXPATHLEN);
		return (0);
	}

	for (i = 0; i < len; i++) {
		pszformat[i] = toupper(extname[i]);
	}
	pszformat[i] = '\0';
	return (0);
}

/*
 * Translate element names into numbers for easier comparison.
 *
 * Returns:
 * 	greater-than-zero: value associated with given string
 *	-1: failure
 */
int
vd_xlate_element(const unsigned char *tag, const char **element_array, int size)
{
	int i;

	for (i = 0; i < size / sizeof (char *); i++)
		if (xmlStrcmp(tag, (const xmlChar *)element_array[i]) == 0)
			return (i);

	return (-1);
}

/*
 * Creates and xml tree from the information given.
 *	vdname - used to print error messages for virtual disk
 *	vtype - virtual disk extension type
 *	fixed - boolean for fixed/flat (1) or sparse (0)
 *	parent - currently null since COW clones not yet supported
 * 	create_epoch - strings containing file creation in epoch form
 *	size_bytes - ascii string of the number of bytes in virtual disk
 *	sector_str - ascii string of the number of sectors in virtual disk
 *	desc - string containing a user designated description
 *	owner - string containing user id creating virtual disk
 *	vdhp - handle that is created and will be retuned
 *
 * Returns:
 * 	0: success
 *	-1: failure
 */
int
vdisk_create_tree(char *vdname, char *vtype, int fixed, char *parent,
    char *create_epoch, char *size_bytes, char *sector_str, char *desc,
    char *owner, vd_handle_t **vdhp)
{
	char filename[MAXPATHLEN];
	char *slash;
	char *rel_ptr;
	struct vd_handle *vdh = NULL;
	xmlDocPtr doc;
	xmlNodePtr disk_root;
	xmlNodePtr diskprop_root;

	LIBXML_TEST_VERSION;

	/* Allocation vd handle */
	vdh = malloc(sizeof (vd_handle_t));
	if (vdh == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	bzero(vdh, sizeof (vd_handle_t));
	*vdhp = vdh;

	/* Creates a new document, a node and set it as a root node */
	doc = vdh->doc = xmlNewDoc((xmlChar *)"1.0");

	/* Add DTD reference */
	xmlCreateIntSubset(doc, (xmlChar *) "vdisk",
	    (xmlChar *) "-//Sun Microsystems Inc//DTD xVM "
	    "Management All//EN",
	    (xmlChar *) "file:///usr/share/lib/xml/dtd/vdisk.dtd");

	disk_root = vdh->disk_root = xmlNewNode(NULL, (xmlChar *)"vdisk");
	(void) xmlDocSetRootElement(doc, disk_root);

	(void) xmlNewProp(disk_root, (xmlChar *)"readonly", (xmlChar *)"false");
	(void) xmlNewProp(disk_root, (xmlChar *)"removable",
	    (xmlChar *)"false");
	(void) xmlNewProp(disk_root, (xmlChar *)"cdrom", (xmlChar *)"false");
	(void) xmlNewProp(disk_root, (xmlChar *)"creation-time-epoch",
	    (xmlChar *)create_epoch);
	(void) xmlNewProp(disk_root, (xmlChar *)"vtype", (xmlChar *)vtype);
	if (fixed) {
		(void) xmlNewProp(disk_root, (xmlChar *)"sparse",
		    (xmlChar *)"false");
	} else {
		(void) xmlNewProp(disk_root, (xmlChar *)"sparse",
		    (xmlChar *)"true");
	}
	(void) xmlNewProp(disk_root, (xmlChar *)"rwcnt", (xmlChar *)"0");
	(void) xmlNewProp(disk_root, (xmlChar *)"rocnt", (xmlChar *)"0");


	slash = strrchr(vdname, '/');
	if (slash)
		rel_ptr = slash + 1;
	else
		rel_ptr = vdname;
	(void) xmlNewChild(disk_root, NULL, (xmlChar *)"name",
	    (xmlChar *)rel_ptr);
	(void) xmlNewChild(disk_root, NULL, (xmlChar *)"version",
	    (xmlChar *)"1.0");
	(void) xmlNewChild(disk_root, NULL, (xmlChar *)"parent",
	    (xmlChar *)"none");
	diskprop_root = vdh->diskprop_root = xmlNewChild(disk_root, NULL,
	    (xmlChar *)"diskprop", (xmlChar *)NULL);

	(void) xmlNewChild(diskprop_root, NULL, (xmlChar *)"filename",
	    (xmlChar *)rel_ptr);

	(void) strlcpy(filename, VD_BASE, MAXPATHLEN);
	(void) strlcat(filename, ".", MAXPATHLEN);
	(void) strlcat(filename, vtype, MAXPATHLEN);
	(void) xmlNewChild(diskprop_root, NULL, (xmlChar *)"vdfile",
	    (xmlChar *)filename);
	(void) xmlNewChild(diskprop_root, NULL, (xmlChar *)"owner",
	    (xmlChar *)owner);
	(void)  xmlNewChild(diskprop_root, NULL, (xmlChar *)"max-size",
	    (xmlChar *)size_bytes);
	(void) xmlNewChild(diskprop_root, NULL, (xmlChar *)"sectors",
	    (xmlChar *)sector_str);
	(void) xmlNewChild(diskprop_root, NULL, (xmlChar *)"description",
	    (xmlChar *)desc);

	return (0);
}

/*
 * Free xml tree given virtual disk handle.
 *	vdh: virtual disk handle
 */
void
vdisk_free_tree(vd_handle_t *vdh)
{
	if (vdh == NULL)
		return;

	if (vdh->doc) {
		xmlFreeDoc(vdh->doc);
		xmlCleanupParser();
	}

	free(vdh);
}

/*
 * Reads in the store file and returns handle to information.
 *	vdhp: virtual disk handle returned in pointer
 *	vdname: path to virtual disk
 *
 * Returns:
 * 	0: success
 *	-1: failure
 */
int
vdisk_read_tree(vd_handle_t **vdhp, char *vdname)
{
	char xmlname[MAXPATHLEN];
	xmlNodePtr node;
	int i;
	vd_handle_t *vdh;

	vdisk_get_xmlfile(xmlname, vdname, MAXPATHLEN);

	/*
	 * this initializes the library and check potential ABI mismatches
	 * between the version it was compiled for and the actual shared
	 * library used.
	 */
	LIBXML_TEST_VERSION

	/* Allocation vd handle */
	*vdhp = malloc(sizeof (vd_handle_t));
	if (*vdhp == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	vdh = *vdhp;
	bzero(vdh, sizeof (vd_handle_t));

	/* parse the file */
	vdh->doc = xmlReadFile(xmlname, NULL,
	    (XML_PARSE_NOBLANKS | XML_PARSE_DTDLOAD |
	    XML_PARSE_DTDATTR | XML_PARSE_DTDVALID));
	if (vdh->doc == NULL) {
		fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Open of virtual disk store file failed"),
		    xmlname);
		free(vdh);
		*vdhp = NULL;
		return (-1);
	}

	/* Get root element node and set diskprop_root and snap_root globals */
	vdh->disk_root = xmlDocGetRootElement(vdh->doc);
	for (node = vdh->disk_root->xmlChildrenNode; node != NULL;
	    node = node->next) {
		if ((xmlStrcmp(node->name, (xmlChar *)"text") == 0) ||
		    (xmlStrcmp(node->name, (xmlChar *)"comment") == 0))
			continue;

		i = vd_xlate_element(node->name, vddiskxml_elements,
		    sizeof (vddiskxml_elements));
		switch (i) {
		case VD_DISKPROP:
			if (vdh->diskprop_root == NULL)
				vdh->diskprop_root = node;
			continue;
		case VD_SNAPSHOT:
			if (vdh->snap_root == NULL)
				vdh->snap_root = node;
			continue;
		case VD_NAME:
		case VD_VERSION:
		case VD_PARENT:
			continue;
		default:
			printf("bad value %s\n", node->name);
		}
	}

	/* Set userprop_root global */
	for (node = vdh->diskprop_root->xmlChildrenNode; node != NULL;
	    node = node->next) {
		if ((xmlStrcmp(node->name, (xmlChar *)"text") == 0) ||
		    (xmlStrcmp(node->name, (xmlChar *)"comment") == 0))
			continue;

		if (xmlStrcmp(node->name, (xmlChar *)"userprop") == 0) {
			if (vdh->userprop_root == NULL)
				vdh->userprop_root = node;
			break;
		}
	}
	return (0);
}

/*
 * Add a snapshot element to the xml tree.
 *	vdh: virtual disk handle
 *	name: string containing name of snapshot
 *	filename: string containing filename of snapshot
 *
 * Returns:
 * 	0: success
 *	-1: failure
 */
int
vdisk_add_snap(vd_handle_t *vdh, char *name, char *filename)
{
	xmlNodePtr snap;
	struct stat64 stat64buf;
	char create_time_str[MAXPATHLEN];
	int rc;
	char *slash;
	char *at;
	char rel_name[MAXPATHLEN];

	at = strrchr(name, '@');
	if (at == NULL) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Malformed snapshot "), name);
		return (-1);
	}

	snap = xmlNewChild(vdh->disk_root, NULL, (xmlChar *)"snapshot",
	    (xmlChar *)NULL);
	if (vdh->snap_root == NULL) {
		vdh->snap_root = snap;
	}
	(void) xmlNewChild(snap, NULL, (xmlChar *)"name",
	    (xmlChar *)at);

	slash = strrchr(filename, '/');
	if (slash)
		(void) strlcpy(rel_name, (++slash), MAXPATHLEN);
	else
		(void) strlcpy(rel_name, filename, MAXPATHLEN);
	(void) xmlNewChild(snap, NULL, (xmlChar *)"vdfile",
	    (xmlChar *)rel_name);
	/* get create time of virtual disk */
	rc = stat64(filename, &stat64buf);
	if (rc == -1) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Unable to stat virtual disk file "),
		    filename);
		return (-1);
	}
	sprintf(create_time_str, "%ld", stat64buf.st_ctime);
	(void) xmlNewProp(snap, (xmlChar *)"creation-time-epoch",
	    (xmlChar *)create_time_str);

	return (0);
}

/*
 * Deletes a snapshot element from the xml tree.
 *	vdh: virtual disk handle
 *	name: string containing name of snapshot
 *
 * Returns:
 * 	0: success
 *	-1: failure
 */
int
vdisk_delete_snap(vd_handle_t *vdh, char *name)
{
	xmlNodePtr node, snap_node;
	xmlChar *fname;
	int cnt = 0;
	char *at, *dot;
	char local_name[MAXPATHLEN];

	/* strip extension if given */
	(void) strlcpy(local_name, name, MAXPATHLEN);
	dot = strrchr(local_name, '.');
	if (dot)
		*dot = '\0';

	/* look for just snapname */
	at = strrchr(local_name, '@');
	if (at == NULL) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Malformed snapshot "), name);
		return (-1);
	}

	for (node = vdh->snap_root; node != NULL; node = node->next, cnt++) {
		for (snap_node = node->xmlChildrenNode;
		    snap_node != NULL; snap_node = snap_node->next) {
			if (xmlStrcmp(snap_node->name,
			    (xmlChar *)"name") != 0)
				continue;
			fname = xmlNodeListGetString(vdh->doc,
			    snap_node->xmlChildrenNode, 1);
			if (fname == NULL)
				continue;
			if (strcmp(at, (char *)fname) == 0) {
				/* if snap_root is removed, move snap_root */
				if (cnt == 0) {
					vdh->snap_root = vdh->snap_root->next;
				}
				xmlUnlinkNode(node);
				xmlFreeNode(node);
				xmlFree(fname);
				return (0);
			}
			xmlFree(fname);
		}
	}
	(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
	    gettext("ERROR: Unable to find file in store to delete"), name);
	return (-1);
}

/*
 * Rename a snapshot property in the xml tree.
 *	vdh: virtual disk handle
 *	property: name of snapshot property to be renamed
 *	old_string: old string value of property
 *	new_string: new string value of property
 *
 * Returns:
 * 	0: success
 *	-1: failure
 */
int
vdisk_rename_snap(vd_handle_t *vdh, char *property, char *old_string,
    char *new_string)
{
	xmlNodePtr node, snap_node;
	xmlChar *fname;

	/* Find snapname and snapname_ext and replace them */
	for (node = vdh->snap_root; node != NULL; node = node->next) {
		for (snap_node = node->xmlChildrenNode;
		    snap_node != NULL; snap_node = snap_node->next) {
			if (xmlStrcmp(snap_node->name,
			    (xmlChar *)property) == 0) {
				fname = xmlNodeListGetString(vdh->doc,
				    snap_node->xmlChildrenNode, 1);
				if (fname == NULL)
					continue;
				if (strcmp(old_string, (char *)fname) != 0) {
					xmlFree(fname);
					continue;
				}
				xmlNodeSetContent(snap_node,
				    (xmlChar *)new_string);
				xmlFree(fname);
				return (0);
			}

		}
	}

	return (-1);
}

/*
 * Look into array of prop_info_t elements to find entry with
 * given property name.  Return corresponding r/w status string.
 *	property - property name
 *	array - array of elements to check
 *	size - size of array
 *	str - returns r/w status string.
 *
 * Returns:
 *	0 - success
 *	-1 - failure
 */
int
vdisk_get_rw(const char *property, prop_info_t *array, int size, char **str)
{
	int i;
	for (i = 0; i < size / sizeof (prop_info_t); i++) {
		if (xmlStrcmp((xmlChar *)property,
		    (xmlChar *)array[i].prop_name) == 0) {
			*str = array[i].prop_rw;
			return (0);
		}
	}
	return (-1);
}

/*
 * Find the r/w status of a given property.
 * On success rw_string will point to a constant string 'rw' or 'ro'.
 *	property - property name
 *	rw_string - returns r/w status string.
 *
 * Returns:
 *	0 - success
 *	-1 - failure
 */
int
vdisk_get_prop_rw(char *property, char **rw_string)
{
	/* User defined properties are all 'rw' */
	if (strrchr(property, ':') != NULL) {
		/* point to userprop 'rw' string */
		*rw_string = prop_element_info[VD_E_USERPROP].prop_rw;
		return (0);
	}

	/* Try attributes */
	if (vdisk_get_rw(property, prop_attr_info,
	    sizeof (prop_attr_info), rw_string) == 0) {
		return (0);
	}

	/* Try elements */
	if (vdisk_get_rw(property, prop_element_info,
	    sizeof (prop_element_info), rw_string) == 0) {
		return (0);
	}

	(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
	    gettext("ERROR: Unable to get property write status"),
	    property);
	return (-1);
}

/*
 * Find and return a boolean property element value that matches the given
 * property by first looking at disk_root attributes and then checking
 * the disk_prop nodes.
 *	vdh - virtual disk handle
 *	property - string containing property
 *	bool - pointer to boolean value to be returned
 *
 * Returns:
 * -1 - failure
 *  0 - success
 */
int
vdisk_get_prop_bool(vd_handle_t *vdh, const char *property, int *bool)
{
	xmlNodePtr node;
	char *rname = NULL;

	/* Check in disk node attributes first */
	rname = (char *)xmlGetProp(vdh->disk_root, (xmlChar *)property);
	if (rname != NULL)
		goto found;

	/* If not found, check in diskprop nodes */
	for (node = vdh->diskprop_root->xmlChildrenNode; node != NULL;
	    node = node->next) {
		if ((xmlStrcmp(node->name, (xmlChar *)property) == 0)) {
			rname = (char *)xmlNodeListGetString(vdh->doc,
			    node->xmlChildrenNode, 1);
		}
		if (rname)
			goto found;
	}

	/* property not found */
	return (-1);

found:
	if (strcmp(rname, "true") == 0)
		*bool = 1;
	else
		*bool = 0;

	xmlFree(rname);
	return (0);
}

/*
 * Return a value for a given property.  If property doesn't exist
 * or contains no data then return failure.
 *	vdh - virtual disk handle
 *	property - string containing property
 *	val - pointer to integer value to be returned
 *
 * Returns:
 * -1 - failure
 *  0 - success
 */
int
vdisk_get_prop_val(vd_handle_t *vdh, const char *property, int *val)
{
	xmlNodePtr node;
	char *rname = NULL;
	int ret = 0;

	rname = (char *)xmlGetProp(vdh->disk_root, (xmlChar *)property);
	if (rname != NULL)
		goto found;

	for (node = vdh->diskprop_root->xmlChildrenNode; node != NULL;
	    node = node->next) {
		if ((xmlStrcmp(node->name, (xmlChar *)property) == 0)) {
			rname = (char *)xmlNodeListGetString(vdh->doc,
			    node->xmlChildrenNode, 1);
		}
		if (rname)
			goto found;
	}

	return (-1);

found:
	*val = atoi((char *)rname);
	if (errno) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n",
		    gettext("ERROR: unable to locate integer property "),
		    property);
		ret = -1;
	}
	free(rname);
	return (ret);
}

/*
 * Return a string for a given property.  If property doesn't exist
 * or contains no data then return a string of "<unknown>".
 * Caller needs to free returned string via free().
 *	vdh - virtual disk handle
 *	property - string containing property
 *	val_string - pointer to string value to be returned
 *
 * Returns:
 * -1 - property not found, val_string set to "<unknown>"
 *  0 - success, val_string set to value of property
 */
int
vdisk_get_prop_str(vd_handle_t *vdh, const char *property, char **val_string)
{
	char unknown[] = "<unknown>";
	int len;
	xmlNodePtr node;
	xmlChar *rname = NULL;
	xmlChar *uname = NULL;
	xmlNodePtr userprop_node;

	/* Check if property is a user defined property */
	if (strrchr(property, ':') != NULL) {
		if (vdh->userprop_root == NULL)
			goto notfound;
		for (userprop_node = vdh->userprop_root; userprop_node != NULL;
		    userprop_node = userprop_node->next) {
			node = userprop_node->xmlChildrenNode;
			uname = xmlNodeListGetString(vdh->doc,
			    node->xmlChildrenNode, 1);
			if ((xmlStrcmp(uname, (xmlChar *)property) == 0)) {
				node = node->next;
				rname = xmlNodeListGetString(vdh->doc,
				    node->xmlChildrenNode, 1);
				goto found;
			}
		}
		goto notfound;
	}

	rname = xmlGetProp(vdh->disk_root, (xmlChar *)property);
	if (rname != NULL)
		goto found;

	for (node = vdh->disk_root->xmlChildrenNode; node != NULL;
	    node = node->next) {
		if ((xmlStrcmp(node->name, (xmlChar *)property) == 0)) {
			rname = xmlNodeListGetString(vdh->doc,
			    node->xmlChildrenNode, 1);
		}
		if (rname)
			goto found;
	}

	for (node = vdh->diskprop_root->xmlChildrenNode; node != NULL;
	    node = node->next) {
		if ((xmlStrcmp(node->name, (xmlChar *)property) == 0)) {
			rname = xmlNodeListGetString(vdh->doc,
			    node->xmlChildrenNode, 1);
		}
		if (rname)
			goto found;
	}

notfound:
	/* property not found */
	len = strlen(unknown) + 1;
	*val_string = malloc(len);
	(void) strlcpy(*val_string, unknown, len);
	return (-1);

found:
	len = strlen((char *)rname) + 1;
	*val_string = malloc(len);
	(void) strlcpy(*val_string, (char *)rname, len);
	xmlFree(rname);
	return (0);
}
/*
 * Sets a boolean for a property.
 *	vdh - virtual disk handle
 *	property - string containing property
 *	val - boolean value to be set
 *	ign_ro - flag to override value of read-only field for property.
 *
 * Returns 0 on success
 * Returns -1 on failure
 */
int
vdisk_set_prop_bool(vd_handle_t *vdh, const char *property, int val,
    int ign_ro)
{
	char *val_string;
	int ret = 0;

	val_string = malloc(MAXPATHLEN);
	if (val) {
		(void) strlcpy(val_string, "true", MAXPATHLEN);
	} else {
		(void) strlcpy(val_string, "false", MAXPATHLEN);
	}
	if (vdisk_set_prop_str(vdh, property, val_string, ign_ro)) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n",
		    gettext("ERROR: unable to store property "), property);
		ret = -1;
	}
	free(val_string);
	return (ret);
}

/*
 * Sets a value for a property.
 *	vdh - virtual disk handle
 *	property - string containing property
 *	val - integer value to be set
 *	ign_ro - flag to override value of read-only field for property.
 *
 * Returns 0 on success
 * Returns -1 on failure
 */
int
vdisk_set_prop_val(vd_handle_t *vdh, const char *property, int val,
    int ign_ro)
{
	char *val_string;
	int ret = 0;

	val_string = malloc(MAXPATHLEN);
	snprintf(val_string, MAXPATHLEN, "%d", val);
	if (vdisk_set_prop_str(vdh, property, val_string, ign_ro)) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n",
		    gettext("ERROR: unable to store property"), property);
		ret = -1;
	}
	free(val_string);
	return (ret);
}

/*
 * Sets a string value to an existing property or attribute.
 *	vdh - virtual disk handle
 *	property - string containing property
 *	string - string value to be set
 *	ign_ro - flag to override value of read-only field for property.
 *
 * Returns 0 if property is found and set.
 * -1 if property not found.
 */
int
vdisk_set_prop_str(vd_handle_t *vdh, const char *property, char *string,
    int ign_ro)
{
	xmlNodePtr node;
	char *rname = NULL;
	char *rw_string;
	int rc = 0;

	/* Check if property is a user defined property */
	if (strrchr(property, ':') != NULL) {
		for (node = vdh->userprop_root->xmlChildrenNode; node != NULL;
		    node = node->next) {
			if ((xmlStrcmp(node->name, (xmlChar *)property) == 0)) {
				/* All user defined properties are rw */
				xmlNodeSetContent(node, (xmlChar *)string);
				return (0);
			}
		}
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: Property must exist"), property);
		return (-1);
	}


	/* Try to set property as an attribute */
	rname = (char *)xmlGetProp(vdh->disk_root, (xmlChar *)property);
	/* If ign_ro is normal then check if readonly */
	if ((rname != NULL) && (ign_ro == VD_PROP_NORMAL)) {
		if (vdisk_get_rw(property, prop_attr_info,
		    sizeof (prop_attr_info), &rw_string) == -1) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Unable to get write status"),
			    property);
			xmlFree(rname);
			return (-1);
		}
		if (strcmp(rw_string, "ro") == 0) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Property is readonly"), property);
			xmlFree(rname);
			return (-1);
		}
	}
	if (rname != NULL) {
		/* Given property is an attribute */
		xmlFree(rname);
		(void) xmlSetProp(vdh->disk_root, (xmlChar *)property,
		    (xmlChar *)string);
		return (0);
	}

	/* Now try to set property as a child element of diskprop */
	for (node = vdh->diskprop_root->xmlChildrenNode; node != NULL;
	    node = node->next) {
		rc = xmlStrcmp(node->name, (xmlChar *)property);
		/* If ign_ro is normal then check if readonly */
		if ((rc == 0) && (ign_ro == VD_PROP_NORMAL)) {
			if (vdisk_get_rw(property, prop_element_info,
			    sizeof (prop_element_info), &rw_string) == -1) {
				(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
				    gettext("ERROR: Unable to get write "
				    "status"), property);
				return (-1);
			}
			if (strcmp(rw_string, "ro") == 0) {
				(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
				    gettext("ERROR: Property is readonly"),
				    property);
				return (-1);
			}
		}
		if (rc == 0) {
			xmlNodeSetContent(node, (xmlChar *)string);
			return (0);
		}
	}

	/* Now try to set property as a child element of root */
	for (node = vdh->disk_root->xmlChildrenNode; node != NULL;
	    node = node->next) {
		rc = xmlStrcmp(node->name, (xmlChar *)property);
		/* If ign_ro is normal then check if readonly */
		if ((rc == 0) && (ign_ro == VD_PROP_NORMAL)) {
			if (vdisk_get_rw(property, prop_element_info,
			    sizeof (prop_element_info), &rw_string) == -1) {
				(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
				    gettext("ERROR: Unable to get write "
				    "status"), property);
				return (-1);
			}
			if (strcmp(rw_string, "ro") == 0) {
				(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
				    gettext("ERROR: Property is readonly"),
				    property);
				return (-1);
			}
		}
		if (rc == 0) {
			xmlNodeSetContent(node, (xmlChar *)string);
			return (0);
		}
	}
	return (-1);
}

/*
 * Adds a property with a value of string.  Property is a
 * user defined property that must contain a colon.
 *	vdh - virtual disk handle
 *	property - string containing property
 *	string - string value to be added
 *	ign_ro - flag to override value of read-only field for property.
 *
 * Returns:
 * -1 - add failed
 *  0 - success
 */
int
vdisk_add_prop_str(vd_handle_t *vdh, const char *property, char *string)
{
	xmlNodePtr node;

	if (strrchr(property, ':') == NULL) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: User defined property must contain ':'"),
		    property);
		return (-1);
	}

	/* Check if user property already exists */
	if (vdh->userprop_root == NULL)
		goto notfound;

	for (node = vdh->userprop_root->xmlChildrenNode; node != NULL;
	    node = node->next) {
		if ((xmlStrcmp(node->name, (xmlChar *)property) == 0)) {
			(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
			    gettext("ERROR: Property already exists"),
			    property);
			return (-1);
		}
	}

notfound:
	if (vdh->userprop_root == NULL)
		node = vdh->userprop_root = xmlNewChild(vdh->diskprop_root,
		    NULL, (xmlChar *)"userprop", (xmlChar *)NULL);
	else
		node =  xmlNewChild(vdh->diskprop_root, NULL,
		    (xmlChar *)"userprop", (xmlChar *)NULL);

	(void) xmlNewChild(node, NULL,
	    (xmlChar *)"name", (xmlChar *)property);
	(void) xmlNewChild(node, NULL,
	    (xmlChar *)"value", (xmlChar *)string);

	return (0);
}

/*
 * Deletes a user defined property.
 *	vdh - virtual disk handle
 *	property - string containing property
 *
 * Returns:
 * -1 - add failed
 *  0 - success
 */
int
vdisk_del_prop_str(vd_handle_t *vdh, const char *property)
{
	xmlNodePtr node;
	xmlNodePtr userprop_node;
	xmlChar *fname = NULL;

	if (strrchr(property, ':') == NULL) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
		    gettext("ERROR: User defined property must contain ':'"),
		    property);
		return (-1);
	}

	/* Find user property and delete it */
	for (userprop_node = vdh->userprop_root; userprop_node != NULL;
	    userprop_node = userprop_node->next) {
		node = userprop_node->xmlChildrenNode;
		if ((xmlStrcmp(node->name, (xmlChar *)"name") != 0)) {
			continue;
		}
		fname = xmlNodeListGetString(vdh->doc,
		    node->xmlChildrenNode, 1);
		if (fname == NULL)
			continue;
		if (strcmp(property, (char *)fname) == 0) {
			xmlUnlinkNode(userprop_node);
			xmlFreeNode(userprop_node);
			xmlFree(fname);
			return (0);
		}
		xmlFree(fname);
	}

	/* Didn't find property to delete */
	(void) fprintf(stderr, "\n%s: \"%s\"\n\n",
	    gettext("ERROR: User defined property must exist"),
	    property);
	return (-1);
}

/*
 * Prints all properties associated with a virtual disk.
 *	vdh - virtual disk handle
 *	longlist - prints the read-only or read-write string for
 *		virtual disk.
 */
void
vdisk_print_prop_all(vd_handle_t *vdh, int longlist)
{
	int i;
	xmlNodePtr node;
	xmlNodePtr userprop_node;
	char *rname = NULL;
	char *uname = NULL;
	char *uval = NULL;

	/* Print the native attributes */
	for (i = 0; i < sizeof (prop_attr_info) / sizeof (prop_info_t); i++) {
		(void) vdisk_get_prop_str(vdh, prop_attr_info[i].prop_name,
		    &rname);
		if (longlist)
			printf("%s: %s\t%s\n", prop_attr_info[i].prop_name,
			    rname, prop_attr_info[i].prop_rw);
		else
			printf("%s: %s\n", prop_attr_info[i].prop_name, rname);
		free(rname);
	}

	/* Print the native properties */
	for (i = 0; i < sizeof (prop_element_info) /
	    sizeof (prop_info_t); i++) {
		/* Skip the base user prop node */
		if (i == VD_E_USERPROP)
			continue;
		(void) vdisk_get_prop_str(vdh,
		    prop_element_info[i].prop_name, &rname);
		if (longlist)
			printf("%s: %s\t%s\n", prop_element_info[i].prop_name,
			    rname, prop_element_info[i].prop_rw);
		else
			printf("%s: %s\n", prop_element_info[i].prop_name,
			    rname);
		free(rname);
	}

	/* Print the user properties */
	if (vdh->userprop_root == 0)
		return;

	for (userprop_node = vdh->userprop_root; userprop_node != NULL;
	    userprop_node = userprop_node->next) {
		node = userprop_node->xmlChildrenNode;
		uname = (char *)xmlNodeListGetString(vdh->doc,
		    node->xmlChildrenNode, 1);
		node = node->next;
		uval = (char *)xmlNodeListGetString(vdh->doc,
		    node->xmlChildrenNode, 1);
		if (longlist)
			printf("%s: %s\trw\n", uname, uval);
		else
			printf("%s: %s\n", uname, uval);
	}
}

/*
 * Given image number find and return the corresponding snapshot name.
 * NULL returned on finding no match.
 */
char *
vdisk_find_snapshot_name(vd_handle_t *vdh, int image_number)
{
	struct VDIMAGE_small *pimage;
	struct VBOXHDD_small *pdisk_sm = (struct VBOXHDD_small *)vdh->hdd;
	int image_cnt = 0;
	char *imagefile;

	for (pimage = (struct VDIMAGE_small *)pdisk_sm->pBase; pimage;
	    pimage = (struct VDIMAGE_small *)pimage->pNext) {
		if (image_cnt == image_number) {
			imagefile = strrchr(pimage->pszFilename, '/');
			if (imagefile)
				imagefile++;
			else
				imagefile = pimage->pszFilename;
			return (imagefile);
		}
		image_cnt++;
	}
	return (NULL);
}

/*
 * Create lock file for exclusive access
 * Returns 0 for success, -1 otherwise.
 */
int
vdisk_lock(char *vdname)
{
	char lockname[MAXPATHLEN];
	int lockfd;

	vdisk_get_vdfilebase(NULL, lockname, vdname, MAXPATHLEN);
	(void) strlcat(lockname, ".lock", MAXPATHLEN);
	if ((lockfd = open(lockname, (O_WRONLY | O_CREAT | O_EXCL), 0644))
	    < 0) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n",
		    gettext("ERROR: unable to create lockfile"), lockname);
		return (-1);
	}

	return (lockfd);
}

/*
 * Delete lock file for exclusive access
 * Returns 0 for success, -1 otherwise.
 */
int
vdisk_unlock(int lockfd, char *vdname)
{
	char lockname[MAXPATHLEN];
	int ret = 0;

	vdisk_get_vdfilebase(NULL, lockname, vdname, MAXPATHLEN);
	(void) strlcat(lockname, ".lock", MAXPATHLEN);
	if (close(lockfd) < 0) {
		(void) fprintf(stderr, "\n%s: \"%s\"\n",
		    gettext("ERROR: unable to close lockfile"), lockname);
		ret = -1;
	}
	unlink(lockname);
	return (ret);
}

/*
 * Just add xml suffix to given file name.
 * Used when printing store file name during import
 */
void
vdisk_get_xmlfile_suffix(char *xmlname, char *file, int len)
{
	(void) strlcpy(xmlname, file, len);
	(void) strlcat(xmlname, ".xml", len);
}

/*
 * Find xml pathname given vdname.
 * Used to write out data to xmlfile.
 */
void
vdisk_get_xmlfile(char *xmlname, char *vdname, int len)
{
	vdisk_get_vdfilebase(NULL, xmlname, vdname, len);
	(void) strlcat(xmlname, ".xml", len);
}

/*
 * Find the virtual disk base file given vdname.
 */
void
vdisk_get_vdname(char *vdname, const char *name, int len)
{
	char vdname_local[MAXPATHLEN];
	char *slash;
	char *at;

	(void) strlcpy(vdname_local, name, MAXPATHLEN);
	slash = strrchr(vdname_local, '/');

	/* Remove snapshot name if given; look after rightmost slash */
	if (slash) {
		at = strrchr(slash, '@');
	} else {
		at = strrchr(vdname_local, '@');
	}
	if (at)
		*at = '\0';
	(void) strlcpy(vdname, vdname_local, len);
}

/*
 * Given a vdname, find the corresponding vdfilebase name.
 * Ex: if vdname is /a/b/c  vdfilebase will be /a/b/c/vd
 */
void
vdisk_get_vdfilebase(vd_handle_t *vdh, char *vdfilebase, char *vdname, int len)
{
	char vdname_local[MAXPATHLEN];
	char *slash;
	char *dot;
	char *base;
	char default_base[] = VD_BASE;
	int alloc_base = 0;

	(void) strlcpy(vdname_local, vdname, MAXPATHLEN);
	slash = strrchr(vdname_local, '/');

	if (vdh) {
		(void) vdisk_get_prop_str(vdh, "vdfile", &base);
		alloc_base = 1;
		dot = strrchr(base, '.');
		if (dot)
			*dot = '\0';
	} else
		base = default_base;

	if (slash) {
		/* If a/x set vdfilebase to a/x/vdisk or a/x/<filename> */
		*slash = '\0';
		(void) strlcpy(vdfilebase, vdname_local, len);
		(void) strlcat(vdfilebase, "/", len);
		++slash;
		(void) strlcat(vdfilebase, slash, len);
		(void) strlcat(vdfilebase, "/", len);
		(void) strlcat(vdfilebase, base, len);
	} else {
		/* If x given set vdfilebase to x/vdisk or x/<filename> */
		(void) strlcpy(vdfilebase, vdname, len);
		(void) strlcat(vdfilebase, "/", len);
		(void) strlcat(vdfilebase, base, len);
	}
	if (alloc_base)
		free(base);
}

/*
 * Check to see if the disk passed in is *not* managed by vdiskadm.
 * returns 1 if it is not.
 */
static int
vdisk_is_unmanaged(const char *vdisk_path)
{
	struct stat stats;
	int rc;


	rc = stat(vdisk_path, &stats);
	if (rc != 0) {
		return (0);
	}
	/*
	 * If the file is a regular file, it is not managed by vdiskadm. This
	 * doesn't mean it is managed by vdiskadm though.
	 */
	if (stats.st_mode & S_IFREG) {
		return (1);
	}

	return (0);
}

/*
 * open the unmanaged disk (not managed by vdiskadm).  There won't be
 * a xml file, etc that goes along with the disk.
 * returns NULL if it can't open the file.
 */
static vd_handle_t *
vdisk_open_unmanaged(const char *vdisk_path)
{
	char *pszformat;
	vd_handle_t *vdh;
	int rc;


	pszformat = NULL;
	/* see if vbox understands the format */
	rc = VDGetFormat(vdisk_path, &pszformat);
	if (!(VBOX_SUCCESS(rc))) {
		if (pszformat != NULL) {
			RTStrFree(pszformat);
		}
		/*
		 * vbox doesn't understand the format, lets make sure we don't
		 * walk all over a file which we expect to not be a raw file.
		 */
		if (vdisk_is_structured_file(vdisk_path, NULL)) {
			return (NULL);
		}
		/* if we get this far, we are using the file as a raw file */
		pszformat = RTStrDup("raw");
		if (pszformat == NULL) {
			return (NULL);
		}
	}

	vdh = malloc(sizeof (vd_handle_t));
	if (vdh == NULL) {
		goto openunmanagefail_malloc;
	}
	vdh->unmanaged = B_TRUE;
	vdh->doc = NULL;

	/* allocate hard disk handle */
	rc = VDCreate(NULL, (PVBOXHDD *)&vdh->hdd);
	if (!VBOX_SUCCESS(rc)) {
		goto openunmanagefail_vdcreate;
	}

	rc = VDOpen((PVBOXHDD)vdh->hdd, pszformat, vdisk_path,
	    VD_OPEN_FLAGS_NORMAL, NULL);
	if (!(VBOX_SUCCESS(rc))) {
		/*
		 * if we can't open the file, try to open it read only. It
		 * would be better if vdisk_open() was passed a r/w flags,
		 * but it would only be used for unmanaged disks.
		 */
		rc = VDOpen((PVBOXHDD)vdh->hdd, pszformat, vdisk_path,
		    VD_OPEN_FLAGS_NORMAL | VD_OPEN_FLAGS_READONLY, NULL);
		if (!(VBOX_SUCCESS(rc))) {
			goto openunmanagefail_vdopen;
		}
	}

	return (vdh);

openunmanagefail_vdopen:
	VDDestroy(((vd_handle_t *)vdh)->hdd);
openunmanagefail_vdcreate:
	free(vdh);
openunmanagefail_malloc:
	RTStrFree(pszformat);

	return (NULL);
}


/*
 * checks to see if we expect the vbox code to recognize this file
 * as a structured disk image (i.e. it's not a raw file).
 * returns 1 if true.
 */
static int
vdisk_is_structured_file(const char *vdisk_name, const char **filetype)
{
	char *ext;
	int count;
	int i;


	/* Get the file extension if there is one */
	ext = strrchr(vdisk_name, '.');
	if (ext == NULL) {
		return (0);
	}
	ext++;

	/*
	 * check to see if the extension is in the list of structured disk
	 * types that vbox supports.
	 */
	count = sizeof (vdisk_structured_files) / sizeof (char *);
	for (i = 0; i < count; i++) {
		if (strcasecmp(ext, vdisk_structured_files[i]) == 0) {
			if (filetype != NULL) {
				*filetype = vdisk_structured_files[i];
			}
			return (1);
		}
	}

	return (0);
}
