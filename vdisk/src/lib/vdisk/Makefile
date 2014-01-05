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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

LIBRARY = libvdisk
OBJS = vdisk.o

CFLAGS += -g -Wall -pedantic -Wno-long-long -Wno-trigraphs -pipe
CFLAGS += -fno-omit-frame-pointer -fno-strict-aliasing
CFLAGS += -std=c99 -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS -D__EXTENSIONS__
CFLAGS += -DVBOX -DVBOX_OSE -DRT_OS_SOLARIS -D_FILE_OFFSET_BITS=64
CFLAGS += -DIN_RING3 -DGC_ARCH_BITS=32 -DLIBICONV_PLUG
CFLAGS += -DIN_RT_R3 -DIN_SUP_R3 -DLDR_WITH_NATIVE -DLDR_WITH_ELF32
CFLAGS += -DLDR_WITH_PE -DRT_WITH_VBOX
CFLAGS += -DRT_DONT_CONVERT_FILENAMES
CFLAGS += -DRT_NO_GIP
CFLAGS += -D_VDISK_

INCLUDE += -I$(XVM_WS)/proto/staging/include -I/usr/include/libxml2
CFLAGS += $(INCLUDE)

CC = gcc

all: $(LIBRARY)

install: $(LIBRARY) includes

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

includes:
	cp $(XVM_WS)/vdisk.hg/src/lib/vdisk/vdisk.h \
	    $(XVM_WS)/proto/staging/include/

$(LIBRARY): $(OBJS)
	$(AR) cr $(DESTLIB)/$@.a $(OBJS)

clean:
	@rm -rf *.o $(DESTLIB)/$(LIBRARY).a $(XVM_WS)/proto/staging/include/vdisk.h
