#
# Copyright (C) 2021 RDK Management
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation, version 2
# of the license.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.
#

AUTOMAKE_OPTIONS = subdir-objects

SUBDIRS = 

GST_BASE_CFLAGS ?=
GST_BASE_LIBS ?=
LIB_SUB_DIR= gstreamer-1.0

AM_CFLAGS = $(GST_CFLAGS)

AM_LDFLAGS = $(GST_LIBS) $(WAYLANDLIB) -avoid-version

plugin_LTLIBRARIES = libgstexternal.la

libgstexternal_la_SOURCES = gst_external_src.c gst_external_sink.c gst_external_plugin.c

libgstexternal_la_CFLAGS= \
   $(AM_CFLAGS) \
   $(GST_CFLAGS)

libgstexternal_la_LDFLAGS= \
   $(AM_LDFLAGS) \
   $(GST_LIBS)  $(GSTBASE_LIBS) $(WAYLANDLIB) -avoid-version \
   -lwesteros_compositor \
   -lwesteros_simplebuffer_client \
   -lwesteros_simpleshell_client

distcleancheck_listfiles = *-libtool

## IPK Generation Support
libgstexternal_IPK_OUTDIR = $(abs_top_builddir)/ipk
libgstexternal_IPK_STAGING_DIR=$(abs_top_builddir)/staging_dir
libgstexternal_IPK_TMP_DIR=$(libgstexternal_IPK_STAGING_DIR)/ipk

libgstexternal_CONTROL_FILES = control
libgstexternal_IPK_FILES  = debian-binary control.tar.gz data.tar.gz
libgstexternal_IPK_OUTPUT = libgstexternal.ipk

package_ipk:
	@echo "Generating IPK Package"
	$(MAKE) prefix=$(libgstexternal_IPK_STAGING_DIR) install
	$(mkdir_p) $(libgstexternal_IPK_TMP_DIR)/usr/lib/$(LIB_SUB_DIR)
	cp -aRf $(libgstexternal_IPK_STAGING_DIR)/lib/$(LIB_SUB_DIR)/*.so $(libgstexternal_IPK_TMP_DIR)/usr/lib/$(LIB_SUB_DIR)
	tar -czvf $(libgstexternal_IPK_OUTDIR)/data.tar.gz -C $(libgstexternal_IPK_TMP_DIR) usr
	tar -czvf $(libgstexternal_IPK_OUTDIR)/control.tar.gz -C $(libgstexternal_IPK_OUTDIR) $(libgstexternal_CONTROL_FILES)
	cd $(libgstexternal_IPK_OUTDIR) && ar cr $(libgstexternal_IPK_OUTPUT) $(libgstexternal_IPK_FILES) && cd -

