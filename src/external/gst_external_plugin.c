/*
 * Copyright 2021 RDK Management
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation, version 2
 * of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "gst_external_src.h"
#include "gst_external_sink.h"

#define PACKAGE_ORIGIN "http://example.com"
#define PACKAGE "externalsrc"

static gboolean gst_external_plugin_init (GstPlugin * plugin)
{
    if ( !gst_element_register (
             plugin,
             "externalsrc",
             GST_RANK_MARGINAL,
             gst_external_src_get_type()) )
        return FALSE;

    if ( !gst_element_register (
             plugin,
             "externalsink",
             GST_RANK_MARGINAL,
             gst_external_sink_get_type()) )
        return FALSE;

    return TRUE;
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    external,
    "reference external plugin",
    gst_external_plugin_init,
    "0.1",
    "LGPL",
    PACKAGE,
    PACKAGE_ORIGIN )
