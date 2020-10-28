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
 * @file    gstdtvsource.cpp
 * @date    November 2019
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <memory.h>

#include <linux/dvb/dmx.h>

#include "gstdtvsource.h"
#include "safec_lib.h"

#define SPTS_BUFFER_SIZE   (188 * 4096)
#define PAT_INTERVAL_MS    100


static void gst_dtv_source_dispose(GObject * object);
static void gst_dtv_source_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_dtv_source_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static gboolean gst_dtv_source_negotiate(GstBaseSrc *bsrc);
static gboolean gst_dtv_source_start(GstBaseSrc * bsrc);
static gboolean gst_dtv_source_stop(GstBaseSrc * bsrc);
static GstStateChangeReturn gst_dtv_source_change_state(GstElement *element, GstStateChange transition);
static GstFlowReturn gst_dtv_source_create(GstPushSrc *src, GstBuffer **buffer);
static gboolean gst_dtv_source_send_event(GstElement *element, GstEvent *event);
static gboolean gst_dtv_source_query(GstBaseSrc *src, GstQuery *query);
static gboolean gst_dtv_source_event(GstBaseSrc *src, GstEvent *event);
static gboolean gst_dtv_source_get_size(GstBaseSrc *bsrc, guint64 *size);
static gboolean gst_dtv_source_is_seekable(GstBaseSrc * bsrc);

static void* gst_dtv_read_data_thread(void *arg);

static void dtv_source_create_pat(guchar *pat, guint service_id, guint pmt_pid, guint8 version);
static void dtv_source_init_crc_table(void);
static guint32 dtv_source_calc_crc32(guchar *data, guint size);
static gint64 dtv_source_get_clock_msecs(void);


#define DTV_SOURCE_CAPS \
   "video/mpegts, " \
   "  systemstream=(boolean)true, " \
   "  packetsize=(int)188;"

static guint32 crc32_table[256];

static GstStaticPadTemplate gst_dtv_source_pad_template = GST_STATIC_PAD_TEMPLATE("src",
   GST_PAD_SRC,
   GST_PAD_ALWAYS,
   GST_STATIC_CAPS(DTV_SOURCE_CAPS));

GST_DEBUG_CATEGORY_STATIC(gst_dtv_source_debug);
#define GST_CAT_DEFAULT gst_dtv_source_debug

enum
{
   PROPERTY_TUNER = 1,
   PROPERTY_DEMUX,
   PROPERTY_SERV_ID,
   PROPERTY_PMT_PID
};

#define gst_dtv_source_parent_class parent_class
G_DEFINE_TYPE (GstDtvSource, gst_dtv_source, GST_TYPE_PUSH_SRC);


static void gst_dtv_source_class_init(GstDtvSourceClass *klass)
{
   GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
   GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);
   GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
   GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
   GstDtvSourceClass *gstdtvsource_class= GST_DTV_SOURCE_CLASS (klass);

   gobject_class->set_property = gst_dtv_source_set_property;
   gobject_class->get_property = gst_dtv_source_get_property;

   /* Install the properties supported by the plugin */
   g_object_class_install_property(gobject_class, PROPERTY_TUNER,
      g_param_spec_uint(
         "tuner",
         "number of the tuner to be used",
         "tuner number as a decimal integer",
         0, // min value
         255, // max value
         0, // default value
         (GParamFlags)G_PARAM_READWRITE));

   g_object_class_install_property(gobject_class, PROPERTY_DEMUX,
      g_param_spec_uint(
         "demux",
         "number of the demux to be used",
         "demux number as a decimal integer",
         0, // min value
         255, // max value
         0, // default value
         (GParamFlags)G_PARAM_READWRITE));

   g_object_class_install_property(gobject_class, PROPERTY_SERV_ID,
      g_param_spec_uint(
         "serv-id",
         "service ID in the SPTS",
         "service ID as a decimal integer",
         0, // min value
         65535, // max value
         0, // default value
         (GParamFlags)G_PARAM_READWRITE));

   g_object_class_install_property(gobject_class, PROPERTY_PMT_PID,
      g_param_spec_uint(
         "pmt-pid",
         "PID of the PMT for the service in the SPTS",
         "PMT PID as a decimal integer",
         0, // min value
         65535, // max value
         0, // default value
         (GParamFlags)G_PARAM_READWRITE));

   gobject_class->dispose = gst_dtv_source_dispose;

   gstbasesrc_class->negotiate= gst_dtv_source_negotiate;
   gstbasesrc_class->start = gst_dtv_source_start;
   gstbasesrc_class->stop = gst_dtv_source_stop;
   gstpushsrc_class->create = gst_dtv_source_create;
   gstbasesrc_class->get_size = gst_dtv_source_get_size;
   gstbasesrc_class->is_seekable = gst_dtv_source_is_seekable;
   gstbasesrc_class->event = gst_dtv_source_event;

   gstdtvsource_class->parent_query = gstbasesrc_class->query;
   gstbasesrc_class->query = gst_dtv_source_query;

   gstdtvsource_class->parent_send_event = gstelement_class->send_event;
   gstelement_class->send_event = gst_dtv_source_send_event;
   gstelement_class->change_state = gst_dtv_source_change_state;

   GST_DEBUG_CATEGORY_INIT(gst_dtv_source_debug, "dtvsource", 0, "dtvsource element");

   gst_element_class_add_pad_template(gstelement_class,
      gst_static_pad_template_get(&gst_dtv_source_pad_template));

   gst_element_class_set_static_metadata(gstelement_class,
      "DTV Source",
      "Source",
      "Outputs SPTS mpegts from a DVB tuner",
      "Ocean Blue Software Ltd");
}

static void gst_dtv_source_init(GstDtvSource *src)
{
   src->tuner = 0;
   src->demux = 0;
   src->service_id = 0;
   src->pmt_pid = 0;

   src->dvr_fd = -1;
   src->offset = 0;

   if ((src->buffer_start = malloc(SPTS_BUFFER_SIZE)) != NULL)
   {
      src->buffer_end = src->buffer_start + SPTS_BUFFER_SIZE;
      src->read_ptr = src->buffer_start;
      src->write_ptr = src->buffer_start;
      src->buffer_size = SPTS_BUFFER_SIZE;
      src->space_in_buffer = SPTS_BUFFER_SIZE;
   }
   else
   {
      src->read_ptr = NULL;
      src->write_ptr = NULL;
      src->buffer_size = 0;
      src->space_in_buffer = 0;
   }

   src->last_pat_timestamp = 0;
   src->pat_count = 0;
   src->pat_version = 0;

   dtv_source_init_crc_table();

   pthread_mutex_init(&src->buffer_mutex, 0);
   pthread_cond_init(&src->data_in_buffer, 0);

   gst_base_src_set_format(GST_BASE_SRC(src), GST_FORMAT_BYTES);
   gst_base_src_set_live(GST_BASE_SRC(src), TRUE);
}

static void gst_dtv_source_dispose(GObject *object)
{
   GstDtvSource *dtvsrc = GST_DTV_SOURCE(object);

   if (dtvsrc->buffer_start != NULL)
   {
      free(dtvsrc->buffer_start);
      dtvsrc->buffer_start = NULL;
   }

   if (dtvsrc->dvr_fd >= 0)
   {
      close(dtvsrc->dvr_fd);
      dtvsrc->dvr_fd = -1;
   }

   pthread_mutex_destroy(&dtvsrc->buffer_mutex);
   pthread_cond_destroy(&dtvsrc->data_in_buffer);

   G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void gst_dtv_source_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
   GstDtvSource *dtvsrc = GST_DTV_SOURCE(object);

   switch (prop_id)
   {
      case PROPERTY_TUNER:
         dtvsrc->tuner = g_value_get_uint(value);
         GST_INFO_OBJECT(dtvsrc, "gst_dtv_source_set_property: tuner=%u", dtvsrc->tuner);
         break;

      case PROPERTY_DEMUX:
         dtvsrc->demux = g_value_get_uint(value);
         GST_INFO_OBJECT(dtvsrc, "gst_dtv_source_set_property: demux=%u", dtvsrc->demux);
         break;

      case PROPERTY_SERV_ID:
         dtvsrc->service_id = g_value_get_uint(value);
         GST_INFO_OBJECT(dtvsrc, "gst_dtv_source_set_property: serv-id=%u", dtvsrc->service_id);
         break;

      case PROPERTY_PMT_PID:
         dtvsrc->pmt_pid = g_value_get_uint(value);
         GST_INFO_OBJECT(dtvsrc, "gst_dtv_source_set_property: pmt-pid=%u", dtvsrc->pmt_pid);
         break;

      default:
         G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
         break;
   }
}

static void gst_dtv_source_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
   GstDtvSource *dtvsrc = GST_DTV_SOURCE(object);

   switch (prop_id)
   {
      case PROPERTY_TUNER:
         g_value_set_uint(value, dtvsrc->tuner);
         break;

      case PROPERTY_DEMUX:
         g_value_set_uint(value, dtvsrc->demux);
         break;

      case PROPERTY_SERV_ID:
         g_value_set_uint(value, dtvsrc->service_id);
         break;

      case PROPERTY_PMT_PID:
         g_value_set_uint(value, dtvsrc->pmt_pid);
         break;

      default:
         G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
         break;
   }
}

static gboolean gst_dtv_source_negotiate(GstBaseSrc *bsrc)
{
   GstDtvSource *dtvsrc;
   GstCaps *caps;
   gboolean retval;

   dtvsrc = GST_DTV_SOURCE(bsrc);
   retval = TRUE;

   GST_INFO_OBJECT(dtvsrc, "gst_dtv_source_negotiate: setting video/mpegts mimetype");

   caps = gst_caps_new_simple("video/mpegts",
      "systemstream", G_TYPE_BOOLEAN, TRUE,
      "packetsize", G_TYPE_INT, 188, NULL);

   if (!gst_pad_set_caps(GST_BASE_SRC_PAD(bsrc), caps))
   {
      GST_ERROR_OBJECT(dtvsrc, "gst_dtv_source_negotiate: error in gst_pad_set_caps");
      retval = FALSE;
   }

   gst_caps_unref(caps);

   return retval;
}

static gboolean gst_dtv_source_start(GstBaseSrc * bsrc)
{
   GstDtvSource *dtvsrc = GST_DTV_SOURCE(bsrc);
   gboolean result;
   gchar dvr_name[64];
   int rc;
   errno_t safec_rc = -1;

   GST_DEBUG_OBJECT(dtvsrc, "gst_dtv_source_start: tuner %u, demux %u", dtvsrc->tuner, dtvsrc->demux);

    safec_rc = sprintf_s(dvr_name, sizeof(dvr_name), "/dev/dvb/adapter%u/dvr0", dtvsrc->tuner);
    if(safec_rc < EOK)
    {
       ERR_CHK(safec_rc);
       return FALSE;
    }
   if ((dtvsrc->dvr_fd = open(dvr_name, O_RDONLY)) >= 0)
   {
      dtvsrc->offset = 0;

      dtvsrc->last_pat_timestamp = 0;
      dtvsrc->pat_count = 0;

      dtvsrc->pat_version++;

      /* Create a PAT for the service */
      GST_INFO_OBJECT(dtvsrc, "gst_dtv_source_start: service=%u, pmt_pid=%u", dtvsrc->service_id, dtvsrc->pmt_pid);

      dtv_source_create_pat(dtvsrc->current_pat, dtvsrc->service_id, dtvsrc->pmt_pid, dtvsrc->pat_version);

      /* Start a task to read the data */
      dtvsrc->stop_thread = FALSE;

      if ((rc = pthread_create(&dtvsrc->read_thread, NULL, gst_dtv_read_data_thread, dtvsrc)) != 0)
      {
         GST_ELEMENT_ERROR(dtvsrc, CORE, THREAD,
            (("Failed to create thread to read SPTS data")),
            ("Failed to create thread to read SPTS data"));

         close(dtvsrc->dvr_fd);
         dtvsrc->dvr_fd = -1;

         result = FALSE;
      }
      else
      {
         result = TRUE;
      }
   }
   else
   {
      GST_ELEMENT_ERROR(dtvsrc, RESOURCE, OPEN_READ,
         (("Failed to open %s, errno %d"), dvr_name, errno),
         (("Failed to open the DVR device %s, errno %d"), dvr_name, errno));

      result = FALSE;
   }

   return result;
}

static gboolean gst_dtv_source_stop(GstBaseSrc * bsrc)
{
   GstDtvSource *dtvsrc = GST_DTV_SOURCE(bsrc);

   GST_DEBUG_OBJECT(dtvsrc, "gst_dtv_source_stop: dvr_fd=%d", dtvsrc->dvr_fd);

   if (dtvsrc->dvr_fd >= 0)
   {
      /* Stop the task reading the data and close the device */
      pthread_cond_signal(&dtvsrc->data_in_buffer);

      dtvsrc->stop_thread = TRUE;

      pthread_join(dtvsrc->read_thread, NULL);

      close(dtvsrc->dvr_fd);
      dtvsrc->dvr_fd = -1;
   }

   GST_DEBUG_OBJECT(dtvsrc, "gst_dtv_source_stop: stopped");

   return TRUE;
}

static GstFlowReturn gst_dtv_source_create(GstPushSrc *src, GstBuffer **buffer)
{
   GstDtvSource *dtvsrc;
   guint blocksize;
   guint bytes_available;
   GstBuffer *spts_buffer;
   GstFlowReturn retval;

   dtvsrc = GST_DTV_SOURCE(src);

   retval = GST_FLOW_ERROR;

   if (dtvsrc->dvr_fd >= 0)
   {
      /* Get the size of the data block that's expected to be returned */
      blocksize = gst_base_src_get_blocksize(GST_BASE_SRC(src));

      /* Wait for data to be available in the buffer */
      pthread_mutex_lock(&dtvsrc->buffer_mutex);
      pthread_cond_wait(&dtvsrc->data_in_buffer, &dtvsrc->buffer_mutex);

      if (dtvsrc->write_ptr >= dtvsrc->read_ptr)
      {
         bytes_available = dtvsrc->write_ptr - dtvsrc->read_ptr;
      }
      else
      {
         bytes_available = dtvsrc->buffer_end - dtvsrc->read_ptr;
      }

      if (bytes_available == 0)
      {
         /* Data is available but read == write which means the buffer is full */
         bytes_available = dtvsrc->buffer_size;
         GST_DEBUG_OBJECT(dtvsrc, "gst_dtv_source_create: Buffer is full");
      }

      if (bytes_available < blocksize)
      {
         blocksize = bytes_available;
      }
      else
      {
         /* Return as many whole blocks as are available */
         blocksize = (bytes_available / blocksize) * blocksize;
      }

      dtvsrc->space_in_buffer += blocksize;

      pthread_mutex_unlock(&dtvsrc->buffer_mutex);

      if (blocksize != 0)
      {
         spts_buffer = gst_buffer_new_wrapped_full((GST_MEMORY_FLAG_READONLY | GST_MEMORY_FLAG_NOT_MAPPABLE),
            dtvsrc->read_ptr, blocksize, 0, blocksize, NULL, NULL);
         if (spts_buffer != NULL)
         {
            GST_BUFFER_FLAG_SET(spts_buffer, GST_BUFFER_FLAG_LIVE);

            GST_BUFFER_OFFSET(spts_buffer) = dtvsrc->offset;
            dtvsrc->offset += blocksize;
            GST_BUFFER_OFFSET_END(spts_buffer) = dtvsrc->offset;

            pthread_mutex_lock(&dtvsrc->buffer_mutex);

            if ((dtvsrc->read_ptr += blocksize) == dtvsrc->buffer_end)
            {
               dtvsrc->read_ptr = dtvsrc->buffer_start;
            }

            pthread_mutex_unlock(&dtvsrc->buffer_mutex);

            *buffer = spts_buffer;
            retval = GST_FLOW_OK;
         }
         else
         {
            GST_ELEMENT_ERROR(dtvsrc, RESOURCE, NO_SPACE_LEFT,
               (("Failed to create a new buffer")),
               (("")));
         }
      }
      else
      {
         if (dtvsrc->stop_thread)
         {
            GST_DEBUG_OBJECT(dtvsrc, "gst_dtv_source_create: No data left in buffer, pushing EOS");
            gst_pad_push_event(dtvsrc->parent.parent.srcpad, gst_event_new_eos());
         }
         else
         {
            GST_DEBUG_OBJECT(dtvsrc, "gst_dtv_source_create: No data left in buffer");
         }

         GST_ELEMENT_ERROR(dtvsrc, RESOURCE, NOT_FOUND,
            (("No data in the buffer")),
            (("")));
      }
   }
   else
   {
      GST_ELEMENT_ERROR(dtvsrc, RESOURCE, OPEN_READ,
         (("Device %u not opened"), dtvsrc->demux),
         (("DVR device %u not opened"), dtvsrc->demux));
   }

   return retval;
}

static GstStateChangeReturn gst_dtv_source_change_state(GstElement *element, GstStateChange transition)
{
   GstDtvSource *dtvsrc = GST_DTV_SOURCE(element);
   GstStateChangeReturn retval;

   GST_INFO_OBJECT(dtvsrc, "gst_dtv_source_change_state: %d->%d",
      GST_STATE_TRANSITION_CURRENT(transition), GST_STATE_TRANSITION_NEXT(transition));

   switch (transition)
   {
      case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      case GST_STATE_CHANGE_PAUSED_TO_READY:
      case GST_STATE_CHANGE_READY_TO_NULL:
         break;

      case GST_STATE_CHANGE_NULL_TO_READY:
      case GST_STATE_CHANGE_READY_TO_PAUSED:
      case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
         break;
   }

   switch (transition)
   {
      case GST_STATE_CHANGE_NULL_TO_READY:
         break;
      case GST_STATE_CHANGE_READY_TO_PAUSED:
         break;
      case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
         break;
      case GST_STATE_CHANGE_PAUSED_TO_READY:
         break;
      default:
         break;
   }

   retval = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

   switch (transition)
   {
      case GST_STATE_CHANGE_READY_TO_PAUSED:
      case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
         GST_INFO_OBJECT(dtvsrc, "gst_dtv_source_change_state: PLAYING_TO_PAUSED");
         break;
      case GST_STATE_CHANGE_PAUSED_TO_READY:
         break;
      case GST_STATE_CHANGE_READY_TO_NULL:
         GST_INFO_OBJECT(dtvsrc, "gst_dtv_source_change_state: READY_TO_NULL");
         break;
      default:
         break;
   }

   GST_INFO_OBJECT(dtvsrc, "gst_dtv_source_change_state: %d->%d = %d",
      GST_STATE_TRANSITION_CURRENT(transition), GST_STATE_TRANSITION_NEXT(transition), retval);

   return retval;
}

static gboolean gst_dtv_source_send_event(GstElement *element, GstEvent *event)
{
   GstDtvSource *dtvsrc = GST_DTV_SOURCE(element);
   gboolean result= GST_DTV_SOURCE_GET_CLASS(dtvsrc)->parent_send_event(element, event);

   GST_INFO_OBJECT(dtvsrc, "gst_dtv_source_send_event: %d", GST_EVENT_TYPE(event));

   switch(GST_EVENT_TYPE(event))
   {
      case GST_EVENT_SEEK:
         result = FALSE;
         break;
      default:
         break;
   }

   return result;
}

static gboolean gst_dtv_source_query(GstBaseSrc *src, GstQuery *query)
{
   gboolean result;
   GstDtvSource *dtvsrc;

   dtvsrc = GST_DTV_SOURCE(src);

   switch (GST_QUERY_TYPE(query))
   {
      default:
         result = GST_DTV_SOURCE_GET_CLASS(dtvsrc)->parent_query(src, query);
         break;
   }

   GST_INFO_OBJECT(dtvsrc, "gst_dtv_source_query: \"%s\" = %u", GST_QUERY_TYPE_NAME(query), result);

   return result;
}

static gboolean gst_dtv_source_event(GstBaseSrc *src, GstEvent *event)
{
   gboolean result;
   GstDtvSource *dtvsrc;

   dtvsrc = GST_DTV_SOURCE(src);

   GST_DEBUG_OBJECT(dtvsrc, "gst_dtv_source_event: \"%s\"", GST_EVENT_TYPE_NAME(event));

   result = TRUE;

   switch(GST_EVENT_TYPE(event))
   {
      default:
         result = GST_BASE_SRC_CLASS(parent_class)->event(src, event);
         break;
   }

   return result;
}

static gboolean gst_dtv_source_get_size(GstBaseSrc *bsrc, guint64 *size)
{ 
   gboolean result;
   GstDtvSource *dtvsrc;

   dtvsrc = GST_DTV_SOURCE(bsrc);

   if (dtvsrc->dvr_fd >= 0)
   {
      // Return max value to avoid bsrc from thinking it hits the end and halting 
      *size= LLONG_MAX;
      GST_DEBUG_OBJECT(dtvsrc, "gst_dtv_source_get_size: size %llx", *size);
      result = TRUE;
   }
   else
   {
      result = FALSE;
   }

   return result;
}
      
static gboolean gst_dtv_source_is_seekable(GstBaseSrc * bsrc)
{
   GstDtvSource *dtvsrc;

   /* Source is streaming live data so seeking isn't possible */
   dtvsrc = GST_DTV_SOURCE(bsrc);
   GST_DEBUG_OBJECT(dtvsrc, "gst_dtv_source_is_seekable:");
   return FALSE;
}


//#define SPTS_FILENAME   "/media/spts.ts"

static void* gst_dtv_read_data_thread(void *arg)
{
   GstDtvSource *dtvsrc;
   guint blocksize;
   guint bytes_to_read;;
   int nbytes;
   gint64 current_time;
   errno_t rc = -1;
#ifdef SPTS_FILENAME
   FILE *fp;

   if ((fp = fopen(SPTS_FILENAME, "w")) == NULL)
   {
      g_message("%s: Failed to open /media/spts.ts, error %d", __FUNCTION__, errno);
   }
#endif

   dtvsrc = (GstDtvSource *)arg;

   dtvsrc->read_ptr = dtvsrc->buffer_start;
   dtvsrc->write_ptr = dtvsrc->buffer_start;

   blocksize = gst_base_src_get_blocksize(GST_BASE_SRC(dtvsrc));

   /* Keep reading data until requested to stop.
    * The data is read is 'blocksize' chunks as this is what will be used to fill the GstBuffers */
   while (!dtvsrc->stop_thread)
   {
      if (dtvsrc->write_ptr + blocksize > dtvsrc->buffer_end)
      {
         bytes_to_read = dtvsrc->buffer_end - dtvsrc->write_ptr;
      }
      else
      {
         bytes_to_read = blocksize;
      }

      if ((nbytes = read(dtvsrc->dvr_fd, dtvsrc->write_ptr, bytes_to_read)) > 0)
      {
#ifdef SPTS_FILENAME
         if (fp != NULL)
            fwrite(dtvsrc->write_ptr, 1, nbytes, fp);
#endif

         pthread_mutex_lock(&dtvsrc->buffer_mutex);

         if (dtvsrc->space_in_buffer < nbytes)
         {
            GST_DEBUG_OBJECT(dtvsrc, "%s: Buffer has OVERFLOWED!", __FUNCTION__);
            dtvsrc->space_in_buffer = 0;
         }
         else
         {
            dtvsrc->space_in_buffer -= nbytes;
         }

         dtvsrc->write_ptr += nbytes;
         if (dtvsrc->write_ptr == dtvsrc->buffer_end)
         {
            dtvsrc->write_ptr = dtvsrc->buffer_start;
         }

         current_time = dtv_source_get_clock_msecs();

         /* Check whether it's time to inject another PAT, but only do it this time if there's
          * enough space, otherwise it will be done next time */
         if (((current_time - dtvsrc->last_pat_timestamp) >= PAT_INTERVAL_MS) &&
            (dtvsrc->space_in_buffer >= TSPKT_SIZE) &&
            (dtvsrc->buffer_end - dtvsrc->write_ptr >= TSPKT_SIZE))
         {
            /* Time to inject a PAT.
             * Just need to update the continuity counter, before using it */
            dtvsrc->current_pat[3] = 0x10 | (dtvsrc->pat_count & 0x0F);

            rc = memcpy_s(dtvsrc->write_ptr, TSPKT_SIZE, dtvsrc->current_pat, TSPKT_SIZE);
            if(rc != EOK)
            {
                ERR_CHK(rc);
                return; 
            }

            dtvsrc->last_pat_timestamp = current_time;
            dtvsrc->pat_count++;

            dtvsrc->space_in_buffer -= TSPKT_SIZE;
            dtvsrc->write_ptr += TSPKT_SIZE;

            if (dtvsrc->write_ptr == dtvsrc->buffer_end)
            {
               dtvsrc->write_ptr = dtvsrc->buffer_start;
            }
         }

         pthread_mutex_unlock(&dtvsrc->buffer_mutex);

         if ((dtvsrc->buffer_size - dtvsrc->space_in_buffer) >= blocksize)
         {
            /* Signal that data is available to be read */
            pthread_cond_signal(&dtvsrc->data_in_buffer);
         }
      }
      else if (nbytes < 0)
      {
         GST_DEBUG_OBJECT(dtvsrc, "%s: read returned %d, errno %d", __FUNCTION__, nbytes, errno);
      }
   }

   GST_DEBUG_OBJECT(dtvsrc, "%s: stop requested", __FUNCTION__);

#ifdef SPTS_FILENAME
   if (fp != NULL)
      fclose(fp);
#endif

   return NULL;
}

static gint64 dtv_source_get_clock_msecs(void)
{
   /* Get the current time in microsecs and return millisecs */
   return(g_get_monotonic_time() / 1000);
}

static void dtv_source_create_pat(guchar *pat, guint service_id, guint pmt_pid, guint8 version)
{
   guint32 crc;
   guint i;

   pat[0] = 0x47; // Sync Byte
   pat[1] = 0x40; // TEI=no ; Payload Start=yes; Prio=0; 5 bits PId=0
   pat[2] = 0x00;
   pat[3] = 0x10; // 2 bits Scrambling = no; 2 bits adaptation field = no adaptation; 4 bits continuity counter

   pat[4] = 0x00; // Payload start=yes, hence this is the offset to start of section
   pat[5] = 0x00; // Start of section, Table ID = 0 (PAT)
   pat[6] = 0xB0; // 4 bits fixed = 1011; 4 bits MSB section length
   pat[7] = 0x0D; // 8 bits LSB section length (length = remaining bytes following this field including CRC)

   pat[8] = 0x00; // 2 bytes for transport ID, value doesn't matter
   pat[9] = 0x01;

   pat[10] = 0xC1 | ((version << 1) & 0x3E);

   pat[11] = 0x00; // Section #
   pat[12] = 0x00; // Last section #
   
   // 16 bit service ID
   pat[13] = (service_id >> 8) & 0xFF;
   pat[14] = service_id & 0xFF;

   // PMT PID, reserved 3 bits are set to 1
   pat[15] = 0xE0 | ((pmt_pid >> 8) & 0x1F);
   pat[16] = (guchar)(pmt_pid & 0xFF);

   crc = dtv_source_calc_crc32(&pat[5], 12);

   // 4 bytes of CRC
   pat[17] = (crc >> 24) & 0xFF;
   pat[18] = (crc >> 16) & 0xFF;
   pat[19] = (crc >> 8) & 0xFF;
   pat[20] = crc & 0xFF;

   // Fill stuffing bytes for rest of TS packet
   for (i = 21; i < TSPKT_SIZE; i++)
   {
      pat[i] = 0xFF;
   }
}

static void dtv_source_init_crc_table(void)
{
   guint32 i, j, k;

   for (i = 0; i < 256; i++)
   {
      k = 0;

      for (j = (i << 24) | 0x800000; j != 0x80000000; j <<= 1)
      {
         k = (k << 1) ^ (((k ^ j) & 0x80000000) ? 0x04c11db7 : 0);
      }

      crc32_table[i] = k;
   }
}

static guint32 dtv_source_calc_crc32(guchar *data, guint size)
{
   guint i;
   guint32 result = 0xffffffff;

   for (i = 0; i < size; i++)
   {
      result = (result << 8) ^ crc32_table[(result >> 24) ^ data[i]];
   }

   return result;
}

static gboolean dtvsource_init(GstPlugin *plugin)
{
   gst_element_register(plugin, "dtvsource", GST_RANK_NONE, gst_dtv_source_get_type());

   return TRUE;
}

#ifndef PACKAGE
#define PACKAGE "gstdtvsource"
#endif

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dtvsource,
    "Outputs SPTS data from a DVB tuner",
    dtvsource_init,
    "1.0", //VERSION,
    "LGPL",
    "dtvkit-gst-plugins",
    "https://dtvkit.org")

