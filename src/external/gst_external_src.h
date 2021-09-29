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

#ifndef GST_EXTERNAL_SRC_H
#define GST_EXTERNAL_SRC_H

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS

#define GST_EXTERNAL_SRC_TYPE          (gst_external_src_get_type())
#define GST_EXTERNAL_SRC(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_EXTERNAL_SRC_TYPE, GstExternalSrc))
#define GST_EXTERNAL_SRC_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass), GST_EXTERNAL_SRC_TYPE, GstExternalSrcClass))

typedef struct _GstExternalSrc           GstExternalSrc;
typedef struct _GstExternalSrcClass      GstExternalSrcClass;
typedef struct _GstExternalSrcPrivate    GstExternalSrcPrivate;

GType gst_external_src_get_type(void);

struct _GstExternalSrc {
  GstPushSrc parent;

  /*< private >*/
  GstExternalSrcPrivate *priv;

  /*< private >*/
  gpointer     _gst_reserved[GST_PADDING];
};

struct _GstExternalSrcClass {
  GstPushSrcClass parent_class;
};

G_END_DECLS

#endif /* GST_EXTERNAL_SRC_H */
