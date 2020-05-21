/*
 * Copyright 2020 The DTVKit Open Software Foundation Ltd
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

/**
 * @brief   gstreamer DTV source plugin
 * @file    gstdtvsource.h
 * @date    November 2019
 */

#ifndef __GSTDTVSOURCE_H
#define __GSTDTVSOURCE_H

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

#include <pthread.h>

G_BEGIN_DECLS

#define GST_TYPE_DTV_SOURCE \
   (gst_dtv_source_get_type())
#define GST_DTV_SOURCE(obj) \
   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DTV_SOURCE,GstDtvSource))
#define GST_DTV_SOURCE_CLASS(klass) \
   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DTV_SOURCE,GstDtvSourceClass))
#define GST_DTV_SOURCE_GET_CLASS(obj) \
   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DTV_SOURCE, GstDtvSourceClass))
#define GST_IS_DTV_SOURCE(obj) \
   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DTV_SOURCE))
#define GST_IS_DTV_SOURCE_CLASS(klass) \
   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DTV_SOURCE))

#define TSPKT_SIZE         188

typedef struct
{
   GstPushSrc parent;

   guint tuner;
   guint demux;
   guint service_id;
   guint pmt_pid;

   int dvr_fd;
   long long offset;    /* Value set in the GstBuffer */

   guchar *buffer_start;
   guchar *buffer_end;
   guchar *read_ptr;
   guchar *write_ptr;
   guint buffer_size;
   guint space_in_buffer;

   guchar current_pat[TSPKT_SIZE];
   gint64 last_pat_timestamp;
   guint8 pat_count;
   guint8 pat_version;

   pthread_t read_thread;
   pthread_mutex_t buffer_mutex;
   pthread_cond_t data_in_buffer;
   gboolean stop_thread;
} GstDtvSource;


typedef gboolean (*send_event_func) (GstElement *element, GstEvent *event);
typedef gboolean (*query_func) (GstBaseSrc *src, GstQuery *query);

typedef struct
{
   GstPushSrcClass parent_class;
   send_event_func parent_send_event;
   query_func parent_query;
} GstDtvSourceClass;

GType gst_dtv_source_get_type(void);

G_END_DECLS

#endif // __GSTDTVSOURCE_H

