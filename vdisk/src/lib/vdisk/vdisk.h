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

#ifndef _VDISK_H
#define	_VDISK_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <libxml/tree.h>

typedef struct vd_handle
{
	void *hdd;			/* pointer to hdd */
	boolean_t unmanaged;		/* unmanaged disk */
	xmlDocPtr doc;			/* pointer to xml doc */
	xmlNodePtr disk_root;		/* pointer to disk element */
	xmlNodePtr diskprop_root;	/* pointer to diskprop element */
	xmlNodePtr snap_root;		/* pointer to snapshot element */
	xmlDtdPtr dtd;			/* pointer to dtd */
	xmlNodePtr userprop_root;	/* pointer to userprop element */
} vd_handle_t;

/* Base name to give to virtual disk files */
#define	VD_BASE "vdisk"

/* Flags used by vdisk command */
#define	VD_NOFLUSH_ON_CLOSE	1

/*
 * Flags used to ignore and not ignore the read-only flag in the
 * property declaration structure.
 * VD_PROP_IGN_RO only should be used to set properties values due to
 * snapshot, rename, move, etc. that aren't allowed to be set
 * directly from command line.
 * VD_PROP_NORMAL is the default behavior to observe the read-only flag.
 */
#define	VD_PROP_IGN_RO	1
#define	VD_PROP_NORMAL	0

void *vdisk_open(const char *vdisk_path);
ssize_t vdisk_read(void *vdh,  uint64_t uoffset, void *pvbuf, size_t cbread);
ssize_t vdisk_write(void *vdh,  uint64_t uoffset, void *pvbuf, size_t cbwrite);
int vdisk_flush(void *vdh);
int64_t vdisk_get_size(void *vdh);
void vdisk_close(void *vdh);
int vdisk_setflags(void *vdh, uint_t flags);

int vdisk_check_vdisk(const char *vdisk_path);

int vdisk_print_files(vd_handle_t *vdh, char *vdname, int parsable_output,
    int print_store, int print_extents, int print_snaps);

int vdisk_load_snapshots(vd_handle_t *vdh, char *pszformat, char *vdname,
    int openflag);
int vdisk_find_snapshots(vd_handle_t *vdh, char *pszrmfilename,
    int *image_number, int *total_image_number);
int vdisk_move_snapshots(vd_handle_t *vdh, char *pszformat, char *vdname,
    char *new_dir);
char *vdisk_find_snapshot_name(vd_handle_t *vdh, int image_number);
int vdisk_find_create_storepath(const char *name, char *vdname, char *snapname,
    char *extname, char **pszformat, int create_flag, vd_handle_t **vdhp);

int vdisk_find_format_ext(vd_handle_t *vdh, char *vdname, char *file,
    char **pszformat, char *extname, int use_store);
int vdisk_format2ext(char *pszformat, char *extname);
int vdisk_ext2format(char *extname, char *pszformat);

int vdisk_create_tree(char *vdname, char *vtype, int sparse, char *parent,
    char *create_epoch, char *size_str, char *size_bytes, char *desc,
    char *owner, vd_handle_t **vdhp);
void vdisk_free_tree(vd_handle_t *vdh);
int vdisk_read_tree(vd_handle_t **vdh, char *vdname);
int vdisk_write_tree(vd_handle_t *vdh, char *vdname);
int vdisk_add_snap(vd_handle_t *vdh, char *snapname, char *filename);
int vdisk_rename_snap(vd_handle_t *vdh, char *property, char *old_string,
    char *new_string);
int vdisk_delete_snap(vd_handle_t *vdh, char *snapname);
int vdisk_add_prop_str(vd_handle_t *vdh, const char *property, char *string);
int vdisk_del_prop_str(vd_handle_t *vdh, const char *property);
int vdisk_get_prop_rw(char *property, char **rw_string);
int vdisk_get_prop_bool(vd_handle_t *vdh, const char *property, int *);
int vdisk_set_prop_bool(vd_handle_t *vdh, const char *property, int val,
    int override);
int vdisk_get_prop_val(vd_handle_t *vdh, const char *property, int *val);
int vdisk_set_prop_val(vd_handle_t *vdh, const char *property, int val,
    int override);
int vdisk_get_prop_str(vd_handle_t *vdh, const char *name, char **str);
int vdisk_set_prop_str(vd_handle_t *vdh, const char *property, char *str,
    int override);
void vdisk_print_prop_all(vd_handle_t *vdh, int longlist);
int vdisk_lock(char *vdname);
int vdisk_unlock(int lockfd, char *vdname);
void vdisk_get_xmlfile_suffix(char *xmlname, char *file, int len);
void vdisk_get_xmlfile(char *xmlname, char *vdname, int len);
void vdisk_get_vdfilebase(vd_handle_t *, char *vdfilebase, char *vdname,
    int len);
void vdisk_get_vdname(char *vdname, const char *name, int len);

#ifdef	__cplusplus
}
#endif
#endif /* _VDISK_H */
