#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2014 David Mackay. All rights reserved.
# This file also incorporates work licensed under the CDDL
# and covered by the following copyright notice:
#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#


check_proto() {
	[ -d "$proto" ] || err "proto area \"$proto\" doesn't exist"
}

# distutils doesn't support vendor-packages, hack it here
proto_vendor_packages() {
	check_proto

	msg "fixing for vendor-packages"

	pydir=$proto/usr/lib/python2.6/
	mkdir -p $pydir/vendor-packages
	(cd $pydir/site-packages/ && find . |
	    cpio -dumpa $pydir/vendor-packages/)
	rm -rf $pydir/site-packages/
}

proto_isaexec() {
	check_proto

	dir=$1
	file=$2

	mkdir -p $proto/$dir/$isaname
	mv -f $proto/$dir/$file $proto/$dir/$isaname/$file
	rm -f $proto/$dir/$file
	ln -s /usr/lib/isaexec $proto/$dir/$file
}

proto_isaexec_mv() {
	check_proto

	olddir=$1
	newdir=$2
	file=$3

	mkdir -p $proto/$newdir/$isaname
	mv -f $proto/$olddir/$file $proto/$newdir/$isaname/$file
	rm -f $proto/$newdir/$file
	ln -s /usr/lib/isaexec $proto/$newdir/$file
}

proto_isalib() {
	check_proto

	dir=$1
	file=$2

	if [ "$isa" = 64 ]; then
		mkdir -p $proto/$dir/$isaname
		mv -f $proto/$dir/$file $proto/$dir/$isaname/$file
	fi
}

proto_isapython() {
	check_proto

	file=$1
	pydir="$proto/usr/lib/python2.6/site-packages"

	if [ "$isa" = 64 ]; then
		mkdir -p "$pydir/64"
		mv -f "$pydir/$file" "$pydir/64/$file"
	fi
}

proto_rmrf() {
	check_proto

	for arg in "$@"; do
		rm -rf "$proto/$arg"
	done
}

proto_rmdir() {
	check_proto

	for arg in "$@"; do
		rmdir "$proto/$arg"
	done
}

proto_rm() {
	check_proto

	for arg in "$@"; do
		rm -f "$proto/$arg"
	done
}

proto_mkdir() {
	check_proto

	for arg in "$@"; do
		mkdir -p "$proto/$arg"
	done
}

proto_mv() {
	from=$1
	to=$2

	check_proto

	mv -f "$proto/$from" "$proto/$to"
}

proto_mvdir() {
	from=$1
	to=$2

	check_proto

	proto_rmrf "$to"
	mv -f "$proto/$from" "$proto/$to"
}
