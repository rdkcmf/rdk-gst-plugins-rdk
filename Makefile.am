##########################################################################
# Copyright 2014 RDK Management
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
##########################################################################

SUBDIRS = @SUBDIRS@
DIST_SUBDIRS = cfg @SUBDIRS@

GST_BASE_CFLAGS ?=
GST_BASE_LIBS ?=
LIB_SUB_DIR=    gstreamer-0.10

## IPK Generation Support
gst_plugins_rdk_IPK_OUTDIR = $(abs_top_builddir)/ipk
gst_plugins_rdk_IPK_STAGING_DIR=$(abs_top_builddir)/staging_dir
gst_plugins_rdk_IPK_TMP_DIR=$(gst_plugins_rdk_IPK_STAGING_DIR)/ipk

gst_plugins_rdk_CONTROL_FILES = control
gst_plugins_rdk_IPK_FILES  = debian-binary control.tar.gz data.tar.gz
gst_plugins_rdk_IPK_OUTPUT = gst-plugins-rdk.ipk

package_ipk:
	@echo "Generating IPK Package"
	$(MAKE) prefix=$(gst_plugins_rdk_IPK_STAGING_DIR) install
	$(mkdir_p) $(gst_plugins_rdk_IPK_TMP_DIR)/usr/lib/$(LIB_SUB_DIR)
	cp -aRf $(gst_plugins_rdk_IPK_STAGING_DIR)/lib/$(LIB_SUB_DIR)/*.so $(gst_plugins_rdk_IPK_TMP_DIR)/usr/lib/$(LIB_SUB_DIR)
	tar -czvf $(gst_plugins_rdk_IPK_OUTDIR)/data.tar.gz -C $(gst_plugins_rdk_IPK_TMP_DIR) usr
	tar -czvf $(gst_plugins_rdk_IPK_OUTDIR)/control.tar.gz -C $(gst_plugins_rdk_IPK_OUTDIR) $(gst_plugins_rdk_CONTROL_FILES)
	cd $(gst_plugins_rdk_IPK_OUTDIR) && ar cr $(gst_plugins_rdk_IPK_OUTPUT) $(gst_plugins_rdk_IPK_FILES) && cd -

distcleancheck_listfiles = *-libtool
