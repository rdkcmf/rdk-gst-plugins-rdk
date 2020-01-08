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

dnl                                               -*- Autoconf -*-
dnl Process this file with autoconf to produce a configure script.

dnl AC_PREREQ([2.69])
AC_INIT(gst-plugins-rdk, 1.0, karunakaran_amirthalingam@cable.comcast.com,gst-plugins-rdk)
AC_CONFIG_SRCDIR([src/httpsink/gsthttpsink.h])
AM_CONFIG_HEADER(cfg/config.h)
AC_CONFIG_MACRO_DIR([cfg])
AC_CONFIG_AUX_DIR([cfg])
AM_INIT_AUTOMAKE([foreign no-dist-gzip dist-bzip2 1.9])
LT_INIT

dnl subdirectories.
SUBDIRS="src"

dnl Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_CPP
AC_PROG_CXXCPP

dnl use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
  [AM_DEFAULT_VERBOSITY=1
   AC_SUBST(AM_DEFAULT_VERBOSITY)])




dnl **********************************
dnl checks for dependencies
dnl **********************************
AC_HEADER_STDC

dnl Checks for header files.
AC_CHECK_HEADERS([limits.h memory.h stdlib.h string.h sys/socket.h unistd.h])

dnl Checks for typedefs, structures, and compiler characteristics.
dnl AC_CHECK_HEADER_STDBOOL
AC_TYPE_UINT8_T

dnl Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([memset strdup strerror])

PLUGIN_SRC=" "
DVR_LFLAGS=" "
BUILD_DVR_COMPONENTS="no"

dnl GStreamer version. Use 0.10 by default
AC_ARG_ENABLE(gstreamer1,
              AS_HELP_STRING([--enable-gstreamer1],
                             [Use GStreamer 1.x]), ,
              [enable_gstreamer1=auto])

AC_MSG_NOTICE([Look for Gstreamer-1.4])
PKG_CHECK_MODULES([GST], [gstreamer-1.0 >= 1.4], have_gst1="yes", have_gst1="no")
AS_IF([test "x$have_gst1" = "xyes"], [
    GST_MAJORMINOR=1.0
    PKG_CHECK_MODULES([GSTBASE], [gstreamer-base-1.0 >= 1.4])
    AC_DEFINE(USE_GST1, 1, [Build with GStreamer 1.x])
    ], [])

AS_IF([test "x$have_gst1" != "xyes"], [
  AC_MSG_NOTICE([GStreamer-1.4 not found; Looking for GStreamer-0.10 as alternate])
  GST_MAJORMINOR=0.10
  PKG_CHECK_MODULES([GST], [gstreamer-0.10 >= 0.10.28])
  PKG_CHECK_MODULES([GSTBASE], [gstreamer-base-0.10 >= 0.10.28])
], [])

plugindir="\$(libdir)/gstreamer-$GST_MAJORMINOR"

PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 2.22.0])

PKG_CHECK_MODULES([CURL], [libcurl >= 7.19.6])

AC_ARG_ENABLE([dtcpdec],
              AS_HELP_STRING([--enable-dtcpdec],[enable dtcpdecrypt plugin (default is no)]),
              [
      		case "${enableval}" in
        	 yes) PLUGIN_SRC="$PLUGIN_SRC dtcpdecrypt";;
        	 no)  AC_MSG_ERROR([dtcpdecrypt plugin is disabled]) ;;
        	 *) AC_MSG_ERROR([bad value ${enableval} for --enable-dtcpdec]) ;;
       		esac
    	      ],
    	      [echo "dtcpdecrypt plugin is disabled"])

AC_ARG_ENABLE([dtcpenc],
              AS_HELP_STRING([--enable-dtcpenc],[enable dtcpencrypt plugin (default is no)]),
              [
      		case "${enableval}" in
        	 yes) PLUGIN_SRC="$PLUGIN_SRC dtcpencrypt";;
        	 no)  echo "dtcpencrypt plugin is disabled" ;;
        	 *) AC_MSG_ERROR([bad value ${enableval} for --enable-dtcpenc]) ;;
       		esac
    	      ],
    	      [echo "dtcpencrypt plugin is disabled"])

AC_ARG_ENABLE([httpsrc],
              AS_HELP_STRING([--enable-httpsrc],[enable httpsrc plugin (default is no)]),
              [
      		case "${enableval}" in
        	 yes) PLUGIN_SRC="$PLUGIN_SRC httpsrc";;
        	 no)  echo "httpsrc plugin is disabled" ;;
        	 *) AC_MSG_ERROR([bad value ${enableval} for --enable-httpsrc]) ;;
       		esac
    	      ],
    	      [echo "httpsrc plugin is disabled"])

AC_ARG_ENABLE([httpsink],
              AS_HELP_STRING([--enable-httpsink],[enable httpsink plugin (default is no)]),
              [
      		case "${enableval}" in
        	 yes) PLUGIN_SRC="$PLUGIN_SRC httpsink";;
        	 no)  echo "httpsink plugin is disabled" ;;
        	 *) AC_MSG_ERROR([bad value ${enableval} for --enable-httpsink]) ;;
       		esac
    	      ],
    	      [echo "httpsink plugin is disabled"])

AC_ARG_ENABLE([rbifilter],
              AS_HELP_STRING([--enable-rbifilter],[enable rbifilter plugin (default is no)]),
              [
      		case "${enableval}" in
        	 yes) PLUGIN_SRC="$PLUGIN_SRC rbifilter";;
        	 no)  echo "rbifilter plugin is disabled" ;;
        	 *) AC_MSG_ERROR([bad value ${enableval} for --enable-rbifilter]) ;;
       		esac
    	      ],
    	      [echo "rbifilter plugin is disabled"])

#RMFMeta can be built only for GST-1.0
AC_SUBST(PLUGIN_SRC)
AC_SUBST(SUBDIRS)
AC_SUBST(GST_MAJORMINOR)
AC_SUBST(plugindir)
AC_SUBST(DVR_LFLAGS)
AC_CONFIG_FILES([Makefile
                 src/Makefile
                 cfg/Makefile
                 src/dtcpdecrypt/Makefile
                 src/dtcpencrypt/Makefile
                 src/httpsink/Makefile
                 src/httpsrc/Makefile
                 src/rbifilter/Makefile])
AC_OUTPUT
