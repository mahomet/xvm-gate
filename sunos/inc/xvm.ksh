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


err() {
	echo "$@" >&2
	exit 1
}

derr() {
	echo "$@" >&2
	[ "$dryrun" = "y" ] || exit 1
}

msg() {
	echo "---- $@ ----"
}

warn() {
	echo "$@" >&2
}

ask() {
	msg=$1

	echo $msg
	read answer
	case "$answer" in
		y|Y|yes)
			return 1
			;;
		*)
			return 0
			;;
	esac
}

. sunos/inc/check_gate.ksh

prepare_cscope() {
	if [ "$1" != "-s" ]; then
		prepare_sfw_gates
		prepare_mq_gates
	fi
}

#
# Build cscope and tags for a given gate
#
build_gate_cscope() {
	gate=$1
	cd ${XVM_WS}${gate}

	msg "cscope for ${XVM_WS}${gate}"

	find . -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.S' \
	    -o -name '*.py' >cscope.files
	cscope -i cscope.files -b
	/usr/bin/ctags -wt `cat cscope.files`
	etags `cat cscope.files`
}

init_build() {
	unset PYTHONPATH || true
	unset MAKEFLAGS || true
	unset LD_LIBRARY_PATH || true

	if [ -d $XVM_WS ]
	then
		export proto=${XVM_WS}/proto/install
	else
		export proto=${XVM_WS}proto/install
	fi

	export PATH=/usr/bin/:/bin:/usr/sbin:/usr/sfw/bin:/usr/perl5/bin
	export MAKE=/usr/sfw/bin/gmake

	# Reset all locale-type variables
	export LANG=C
	export LC_COLLATE=C
	export LC_CTYPE=C
	export LC_MESSAGES=C
	export LC_MONETARY=C
	export LC_NUMERIC=C
	export LC_TIME=C

	if [ -z "$1" -o "$1" = "debug" ]; then
		export debug="y"
		export debugsuffix="-debug"
	else
		export debug="n"
		export debugsuffix=""
	fi

	if [ -z "$SOLARIS_BUILD_TOOLS" ]; then
		if [ -d "/ws/onnv-tools/onbld" ]; then
			export SOLARIS_BUILD_TOOLS="/ws/onnv-tools"
		elif [ -d "/opt/onbld" ]; then
			export SOLARIS_BUILD_TOOLS="/opt"
		fi
	fi
}

switch_isa() {
	export isa=$1

	msg "switching to $isa-bit build"

	if [ "$isa" = 32 ]; then
		export CFLAGS="$cflags_common $cflags_32"
		export LDFLAGS="$ldflags_common $ldflags_32"
		export isaname="i86"
		export isasuffix=""
		export staging="proto/staging/lib"
		export arch=x86_32
		export xen_pae=y
		export objdir="proto/staging/build$debugsuffix-32"
	else
		export CFLAGS="$cflags_common $cflags_64"
		export LDFLAGS="$ldflags_common $ldflags_64"
		export isaname="amd64"
		export isasuffix="amd64"
		export staging="proto/staging/lib/64"
		export arch=x86_64
		export xen_pae=y
		export objdir="proto/staging/build$debugsuffix-64"
	fi
}

do_getopts() {
	skip_prepare=""
	incremental=""

	while getopts m:is name
	do
		case $name in
			i)	incremental="-i"
                        	skip_prepare="-s"
				;;
                        s)	skip_prepare="-s"
				;;
			?)	usage
				;;
		esac
	done

	optind=$OPTIND

	export skip_prepare
	export incremenal
	export optind
}

no_prefix() {
	echo $@ | awk -F+ '{print $NF}'
}

append_slash() {
	if echo $1 | grep '/$' 2>&1 > /dev/null; then
	    echo $1
	else
	    echo $1/
	fi
}

if [ -d "${XVM_WS}" ] ; then
	export XVM_WS=${XVM_WS}/
fi

[ -n "$XVM_WS" -a -d "${XVM_WS}sunos/" ] || {
	echo "XVM_WS is not set correctly" >&2
	echo "${XVM_WS}sunos/ must exist" >&2
	exit 1
}

. sunos/inc/proto_ops.ksh
