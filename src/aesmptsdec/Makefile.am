##########################################################################
# Copyright 2021 RDK Management
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

SUBDIRS = 
AM_CPPFLAGS = -pthread -Wall -std=c++14
plugin_LTLIBRARIES = libgstaesmptsdecrypt.la
libgstaesmptsdecrypt_la_SOURCES = aes_transform_openssl.cpp gstaesmptsdecrypt.cpp scte52_processor.cpp 
libgstaesmptsdecrypt_la_CPPFLAGS =  $(GST_CFLAGS)
libgstaesmptsdecrypt_la_LDFLAGS = $(GST_LIBS) $(GSTBASE_LIBS) -lcrypto
libgstaesmptsdecrypt_la_LDFLAGS += -module -avoid-version
