/*
 * Copyright 2014 RDK Management
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
* @defgroup gst-plugins-rdk
* @{
* @defgroup httpsink
* @{
**/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsthttpsink.h"
#include "safec_lib.h"

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#define DATA_BUFFER_SIZE       32

#define GST_PACKAGE_ORIGIN "http://gstreamer.net/"

#define DEFAULT_SOURCE_TYPE "QAM_SRC"
#define DEFAULT_SOURCE_ID "ocap://0x0000"

static void
gst_http_sink_dispose (GObject * object);
static void
gst_http_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void
gst_http_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean
gst_http_sink_start (GstBaseSink * bsink);
static gboolean
gst_http_sink_stop (GstBaseSink * bsink);
static GstFlowReturn
gst_http_sink_render (GstBaseSink * sink, GstBuffer * buf);
static gboolean 
gst_http_sink_event(GstBaseSink *sink, GstEvent *event);
static gboolean
gst_http_sink_query (GstElement * element, GstQuery * query);
static gboolean
#ifdef USE_GST1
gst_http_sink_pad_query (GstPad * pad, GstObject *parent, GstQuery * query);
#else
gst_http_sink_pad_query (GstPad * pad, GstQuery * query);
#endif


static GstStaticPadTemplate gst_http_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

GST_DEBUG_CATEGORY_STATIC (gst_http_sink_debug);
#define GST_CAT_DEFAULT gst_http_sink_debug

enum
{
  PROP_0,
  PROP_SILENT,
  PROP_HTTP_OBJ,
  PROP_STREAM_TYPE,
  PROP_SENT_DATA_SIZE,
  PROP_SOURCE_TYPE,
  PROP_SOURCE_ID,
  PROP_SEND_DATA_TIME,
  PROP_SEND_STATUS,
};

#ifdef USE_GST1
#define gst_http_sink_parent_class parent_class
G_DEFINE_TYPE (GstHttpSink, gst_http_sink, GST_TYPE_BASE_SINK);
#else
GST_BOILERPLATE (GstHttpSink, gst_http_sink, GstBaseSink,
    GST_TYPE_BASE_SINK);
#endif

static GstStateChangeReturn
gst_http_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GstPad *pad =  gst_element_get_static_pad (element, "sink");
  GstHttpSink *httpsink = GST_HTTP_SINK (GST_OBJECT_PARENT (pad));

//  GST_INFO_OBJECT(element, "change state from %s to %s\n",
//      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
//      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  ret = GST_STATE_CHANGE_SUCCESS;
  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
//        GST_INFO_OBJECT(element, "GST_STATE_CHANGE_NULL_TO_READY\n");
		httpsink->isFirstPacket = TRUE;
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
		httpsink->sendError = FALSE;
//         GST_INFO_OBJECT(element, "GST_STATE_CHANGE_READY_TO_PAUSED\n");
      break;
    }

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
  		httpsink->sent_data_size= 0;
		httpsink->packetcount = 0;
		httpsink->sendError = FALSE;
//         GST_INFO_OBJECT(element, "GST_STATE_CHANGE_PAUSED_TO_PLAYING\n");
      break;
    }

    default:
      break;
  }
                
  /* Chain up to the parent class's state change function
   * Must do this before downward transitions are handled to safely handle
   * concurrent access by multiple threads */
  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
//    GST_ERROR_OBJECT(element, "change state failure\n");
    goto failure;
  }     
  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
//         GST_INFO_OBJECT(element, "GST_STATE_CHANGE_PLAYING_TO_PAUSED\n");
      break;
  
    case GST_STATE_CHANGE_PAUSED_TO_READY:
//         GST_INFO_OBJECT(element, "GST_STATE_CHANGE_PAUSED_TO_READY\n");
      break;
  
    case GST_STATE_CHANGE_READY_TO_NULL:
//         GST_INFO_OBJECT(element, "GST_STATE_CHANGE_READY_TO_NULL\n");
      break;
  
    default:
      break;
  }
        
  gst_object_unref(pad); 
  GST_INFO_OBJECT(element, "changed state from %s to %s\n",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));
      
  return ret;

failure: 
  gst_object_unref(pad); 
  GST_ERROR_OBJECT(element, "failed change state from %s to %s\n",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));
  ret = GST_STATE_CHANGE_FAILURE;
  return ret;
}

#ifndef USE_GST1
static void
gst_http_sink_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (gst_http_sink_debug, "httpsink", 0,
      "httpsink element");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_http_sink_pad_template));
  gst_element_class_set_details_simple (gstelement_class, "HTTP Sink",
      "Sink",
      "Supplies media data over http",
      "Comcast");
}
#endif

static void
gst_http_sink_class_init (GstHttpSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  klass->parent_dispose = gobject_class->dispose;
  klass->parent_event = gstbasesink_class->event;
  klass->parent_query = gstelement_class->query;
  
  gobject_class->dispose = gst_http_sink_dispose;
  gobject_class->set_property = gst_http_sink_set_property;
  gobject_class->get_property = gst_http_sink_get_property;
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_http_sink_change_state);

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          TRUE,(GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_HTTP_OBJ,
      g_param_spec_int ("http obj", "http obj", "Gets the http request instance from http server",
      	  -10000, 10000, -1, (GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_STREAM_TYPE,
      g_param_spec_boolean ("stream type", "stream type", "setting stream type: false-linear, true-chuncked (default: true)",
          TRUE,(GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SENT_DATA_SIZE,
      g_param_spec_uint64 ("sent_data_size", "sent data size", "total size of data written to socket",
      	  0, G_MAXUINT64, 0, (GParamFlags) G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_SOURCE_TYPE,
      g_param_spec_string ("source-type", "SOURCE TYPE", "Name of the source type used when logging first packet, like QAM, DVR, VOD, TSB, IPPV, VPOP",
      	  DEFAULT_SOURCE_TYPE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) ));

  g_object_class_install_property (gobject_class, PROP_SOURCE_ID,
      g_param_spec_string ("source-id", "SOURCE ID", "The source id info used when logging first packet, like ocap://0xXXXX, dvr://local/xxxx#0, vod://<string>",
      	  DEFAULT_SOURCE_ID, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) ));

  g_object_class_install_property (gobject_class, PROP_SEND_DATA_TIME,
      g_param_spec_uint64 ("send_data_time", "send data time", "last time data written to socket",
          0, G_MAXUINT64, 0, (GParamFlags) G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_SEND_STATUS,
      g_param_spec_boolean ("send_status", "send status", "current send status",
          FALSE, (GParamFlags) G_PARAM_READABLE));

  gstbasesink_class->get_times = 0;
  gstbasesink_class->start = gst_http_sink_start;
  gstbasesink_class->stop = gst_http_sink_stop;
  gstbasesink_class->render = gst_http_sink_render;
  
  gstelement_class->query = gst_http_sink_query;

  GST_DEBUG_CATEGORY_INIT (gst_http_sink_debug, "httpsink", 0,
      "httpsink element");

#ifdef USE_GST1
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_http_sink_pad_template));
  gst_element_class_set_static_metadata (gstelement_class, "HTTP Sink",
      "Sink",
      "Supplies media data over http",
      "Comcast");
#endif
}

static void
#ifdef USE_GST1
gst_http_sink_init (GstHttpSink *httpsink)
#else
gst_http_sink_init (GstHttpSink *httpsink, GstHttpSinkClass *g_class )
#endif
{
  GstPad *pad;
  pad = GST_BASE_SINK_PAD (httpsink);
  httpsink->silent= TRUE;
  httpsink->http_obj= -1;
  httpsink->is_chunked= TRUE;
  httpsink->last_timestamp= 0LL;
  httpsink->sent_data_size= 0;
  httpsink->sendError = FALSE;
  httpsink->last_send_time= 0LL;
  g_static_rec_mutex_init (&httpsink->http_obj_mutex);
  gst_pad_set_query_function (pad, GST_DEBUG_FUNCPTR (gst_http_sink_pad_query));
  gst_base_sink_set_sync (GST_BASE_SINK (httpsink), FALSE);
  gst_base_sink_set_async_enabled(GST_BASE_SINK (httpsink), FALSE);
}

static void
gst_http_sink_dispose (GObject * object)
{
  GstHttpSink *sink = GST_HTTP_SINK (object);

  if (sink->caps)
    gst_caps_unref (sink->caps);

  g_static_rec_mutex_free (&sink->http_obj_mutex);

  GST_HTTP_SINK_GET_CLASS(sink)->parent_dispose(object);
}

static void
gst_http_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstHttpSink *sink = GST_HTTP_SINK (object);

  errno_t rc = -1;

  switch (prop_id) {
    case PROP_SILENT:
      sink->silent = g_value_get_boolean (value);
      break;
    case PROP_HTTP_OBJ:
      g_static_rec_mutex_lock (&sink->http_obj_mutex);
      sink->http_obj = g_value_get_int (value);
      g_static_rec_mutex_unlock (&sink->http_obj_mutex);
      GST_INFO_OBJECT(sink, "HTTP_OBJ socket 0x%x", sink->http_obj);
      break;
    case PROP_STREAM_TYPE:
      sink->is_chunked = g_value_get_boolean (value);
      break;
    case PROP_SOURCE_TYPE:
      rc = strcpy_s( sink->source_type, sizeof(sink->source_type), g_value_get_string (value) );
      {
         ERR_CHK(rc);
         return;
      }
      GST_INFO_OBJECT(sink, "Source Type: %s", sink->source_type);
      break;
    case PROP_SOURCE_ID:
      rc = strcpy_s( sink->source_id, sizeof(sink->source_id), g_value_get_string (value) );
      {
         ERR_CHK(rc);
         return;
      }
      GST_INFO_OBJECT(sink, "Source Id: %s", sink->source_id);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_http_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstHttpSink *sink = GST_HTTP_SINK (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, sink->silent);
      break;
    case PROP_HTTP_OBJ:
      g_value_set_int (value, sink->http_obj);
      break;
    case PROP_STREAM_TYPE:
      g_value_set_boolean (value, sink->is_chunked);
      break;
    case PROP_SENT_DATA_SIZE:
      g_value_set_uint64 (value, sink->sent_data_size);
      break;
    case PROP_SOURCE_TYPE:
      g_value_set_string (value, sink->source_type);
      break;
    case PROP_SOURCE_ID:
      g_value_set_string (value, sink->source_id);
      break;
    case PROP_SEND_DATA_TIME:
      g_value_set_uint64 (value, sink->last_send_time);
      break;
    case PROP_SEND_STATUS:
      g_value_set_boolean (value, sink->is_blocked);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_http_sink_start (GstBaseSink * bsink)
{
  GstHttpSink *httpsink;

  httpsink = GST_HTTP_SINK (bsink);
  GST_DEBUG_OBJECT(httpsink, "gst_http_sink_start: enter");

  if ( httpsink->http_obj < 0 ) {
/*
    GST_ELEMENT_ERROR (httpsink, RESOURCE, OPEN_READ, (("No http_obj set.")),
        ("Missing http_obj property"));
     return FALSE;
*/
     GST_INFO_OBJECT(httpsink, "http_obj not set...");
  }

  GST_DEBUG_OBJECT(httpsink, "gst_http_sink_start: exit normal");

  return TRUE;
}

static gboolean
gst_http_sink_stop (GstBaseSink * bsink)
{
  GstHttpSink *httpsink;

  httpsink = GST_HTTP_SINK (bsink);
  GST_DEBUG_OBJECT(httpsink, "gst_http_sink_stop: enter");

  GST_DEBUG_OBJECT(httpsink, "gst_http_sink_stop: reset HTTP_OBJ socket to %x", httpsink->http_obj);

  GST_DEBUG_OBJECT(httpsink, "gst_http_sink_stop: exit normal");
  return TRUE;
}

void onError (GstHttpSink *sink, int err_code, char* err_string)
{
	GstMessage *message;
	GError *error = NULL;

	GST_INFO ("streaming error occurred : %s", err_string);

	error = g_error_new (GST_CORE_ERROR, err_code, "Client Closed Connection");
	if (error == NULL) {
		GST_ERROR("error null");
		return;
	}

	GstElement *parent = GST_ELEMENT_PARENT (sink);

	message = gst_message_new_error (GST_OBJECT (parent), error, "client closed");
	if (message == NULL) {
		GST_ERROR ("Bus message creation failed");
		goto done;
	}

	if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ERROR) {
		GST_ERROR ("Bus message type failed");
		goto done;
	}

	if (GST_MESSAGE_SRC (message) == NULL) {
		GST_ERROR ("Bus message src not found");
		goto done;
	}

	if (gst_element_post_message (GST_ELEMENT (sink), message) == FALSE) {
		GST_ERROR ("This element has no bus, therefore no message sent!");
		goto done;
	}

	sink->sendError = TRUE;
	sink->http_obj = -1;
done:
	g_clear_error (&error);
}

static GstFlowReturn
gst_http_sink_render (GstBaseSink * sink, GstBuffer * buf)
{
  GstHttpSink *httpsink;
  int fd;
  errno_t rc = -1;
#ifdef USE_GST1
  GstMapInfo map;
#endif

  httpsink = GST_HTTP_SINK (sink);
  g_static_rec_mutex_lock (&httpsink->http_obj_mutex);

  fd = httpsink->http_obj;

  if(buf == NULL)
  {
     GST_ERROR("NULL buf passed Error!!!");
     return GST_FLOW_ERROR;
  }

#if 0 
  FILE *fp = NULL;
  
  fp = fopen("/test_hs.ts", "a+"); 

  fwrite(buf->data, 1, buf->size, fp); 
  fclose(fp);
#endif //if 0

#ifdef USE_GST1
  gst_buffer_map (buf, &map, GST_MAP_READ);
#endif

  httpsink->last_timestamp= GST_BUFFER_TIMESTAMP(buf);
#ifdef USE_GST1
  if(httpsink->isFirstPacket && (map.size >=16))
#else
  if(httpsink->isFirstPacket && (buf->size >=16))
#endif
  {
#ifdef USE_GST1
    GST_INFO_OBJECT(httpsink, "httpsink::Received first packet[%d]: ", map.size);
#else
    GST_INFO_OBJECT(httpsink, "httpsink::Received first packet[%d]: ", buf->size);
#endif
	int ix;
    for(ix=0; ix<16; ix++)
#ifdef USE_GST1
      g_print("%02X ", map.data[ix]);
#else
      g_print("%02X ", buf->data[ix]);
#endif
    g_print("\n");
	httpsink->isFirstPacket = FALSE;
  }

  int sockRet=-1;
  if(!httpsink->is_chunked)
  {
		GST_DEBUG("Stream Type : Linear");
		//int n;

		/* How many bytes we send in this iteration */
		if (fd != -1) 
		{
			struct timeval timeout;
			timeout.tv_sec = 1;
			timeout.tv_usec = 0;
			if (setsockopt (httpsink->http_obj, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,  sizeof(timeout)) < 0)
			{
				GST_ERROR_OBJECT(httpsink,"Failed to set socket timeout for send()\n");
			}
			//n = write(fd, buf->data, buf->size);
#ifdef USE_GST1
			sockRet = send(fd, map.data, map.size, 0);
#else
			sockRet = send(fd, buf->data, buf->size, 0);
#endif
			if(sockRet == -1)
			{
            	GST_INFO_OBJECT(httpsink, "gst_http_sink_render: send B on socket %x fails err %X", fd, errno);
				onError(httpsink, GST_HTTPSINK_EVENT_CONNECTION_CLOSED, strerror(errno));
				goto error;
			}
		}
		httpsink->sent_data_size += sockRet;
		GST_LOG("Linear : sockRet = %d:%s: send returned = %d", errno, strerror(errno), sockRet );
  }
  else
  {
		//GST_DEBUG("Stream Type : Chunked");
		char data[DATA_BUFFER_SIZE];
		int  len;

		if (fd != -1 && (httpsink->sendError != TRUE))
		{
#ifdef ENABLE_SEND_TIMEOUT
			struct timeval timeout;
			timeout.tv_sec = 1;
			timeout.tv_usec = 0;
			if (setsockopt (httpsink->http_obj, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,  sizeof(timeout)) < 0)
			{
				GST_ERROR_OBJECT(httpsink,"Failed to set socket timeout for send()\n");
			}
#endif
#ifdef USE_GST1
			rc = sprintf_s(data, DATA_BUFFER_SIZE, "%X\r\n", map.size);
			if(rc < EOK)
			{
				ERR_CHK(rc);
				goto error;
			}
			len = rc;
#else
			rc = sprintf_s(data, DATA_BUFFER_SIZE, "%X\r\n", buf->size);
			if(rc < EOK)
			{
				ERR_CHK(rc);
				goto error;
			}
			len = rc;
#endif
			struct timeval time;
			gettimeofday( &time, NULL );
			httpsink->last_send_time = time.tv_sec;
			httpsink->is_blocked = TRUE;

			sockRet = send(fd, data, len, 0);
			if(sockRet == -1)
			{
            	//GST_INFO_OBJECT(httpsink, "gst_http_sink_render: send A on socket %x fails err %X", fd, errno);
				onError(httpsink, GST_HTTPSINK_EVENT_CONNECTION_CLOSED, strerror(errno));
				goto error;
			}
			httpsink->sent_data_size += sockRet;
		 	//GST_LOG("1. chunked : sockRet = %d:%s: send returned = %d", errno, strerror(errno), sockRet );	

#ifdef USE_GST1
			sockRet = send(fd, map.data, map.size, 0);
#else
			sockRet = send(fd, buf->data, buf->size, 0);
#endif
			if(sockRet == -1)
			{
            	//GST_INFO_OBJECT(httpsink, "gst_http_sink_render: send B on socket %x fails err %X", fd, errno);
				onError(httpsink, GST_HTTPSINK_EVENT_CONNECTION_CLOSED, strerror(errno));
				goto error;
			}
			httpsink->sent_data_size += sockRet;
			// GST_DEBUG("2. chunked : sockRet = %d:%s: send returned = %d", errno, strerror(errno), sockRet );	

			rc = sprintf_s(data, DATA_BUFFER_SIZE, "\r\n");
			if(rc < EOK)
			{
				ERR_CHK(rc);
				goto error;
			}
			len = rc;
			sockRet = send(fd, data, len, 0);
			if(sockRet == -1)
			{
            	//GST_INFO_OBJECT(httpsink, "gst_http_sink_render: send C on socket %x fails err %X", fd, errno);
				onError(httpsink, GST_HTTPSINK_EVENT_CONNECTION_CLOSED, strerror(errno));
				goto error;
			}
			httpsink->sent_data_size += sockRet;
			//GST_LOG("3. chunked : sockRet = %d:%s: send returned = %d", errno, strerror(errno), sockRet );	
		}
		//GST_DEBUG("Stream Type : Chunked3");

  }

  if (httpsink->packetcount < 5)
  {
	if (buf != NULL && (httpsink->packetcount == 0 || httpsink->packetcount == 4))
	{
#ifdef USE_GST1
		GST_INFO_OBJECT(httpsink, "Source Type: %s packet[%d] Source Id: %s sent [%d bytes] on ConnId [%d]",
          				httpsink->source_type, httpsink->packetcount+1, httpsink->source_id, map.size, fd);
#else
		int bufSize = buf->size;
		GST_INFO_OBJECT(httpsink, "Source Type: %s packet[%d] Source Id: %s sent [%d bytes] on ConnId [%d]",
          				httpsink->source_type, httpsink->packetcount+1, httpsink->source_id, bufSize, fd);
#endif
	}
	else if (buf == NULL)
	{
		GST_INFO_OBJECT(httpsink, "Source Type: %s packet[%d] Source Id: %s buf is NULL on ConnId [%d]",
						httpsink->source_type, httpsink->packetcount+1, httpsink->source_id, fd);
	}
    httpsink->packetcount++;
  }
  httpsink->is_blocked = FALSE;
  g_static_rec_mutex_unlock (&httpsink->http_obj_mutex);

#ifdef USE_GST1
  gst_buffer_unmap (buf, &map);
#endif
 
  return GST_FLOW_OK;

error:
  httpsink->is_blocked = FALSE;
  g_static_rec_mutex_unlock (&httpsink->http_obj_mutex);
#ifdef USE_GST1
  gst_buffer_unmap (buf, &map);
#endif
  //GST_INFO_OBJECT(httpsink, "gst_http_sink_render: dropped buffer");
  return GST_FLOW_OK;
}

static gboolean
gst_http_sink_query (GstElement * element, GstQuery * query)
{
  GstHttpSink *self;
  GstFormat format;

  self = GST_HTTP_SINK (element);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      gst_query_parse_position (query, &format, NULL);
      switch (format) {
        case GST_FORMAT_DEFAULT:
        case GST_FORMAT_TIME:
          gst_query_set_position (query, GST_FORMAT_TIME, self->last_timestamp);
         GST_INFO_OBJECT(self, "gst_http_sink_query: GST_FORMAT_TIME position %llx", self->last_timestamp);
          return TRUE;
        default:
          return FALSE;
      }

    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 2, GST_FORMAT_DEFAULT, GST_FORMAT_TIME);
      return TRUE;
    default:
      return GST_HTTP_SINK_GET_CLASS(self)->parent_query(element,query);
  }
}

static gboolean
#ifdef USE_GST1
gst_http_sink_pad_query (GstPad * pad, GstObject *parent, GstQuery * query)
#else
gst_http_sink_pad_query (GstPad * pad, GstQuery * query)
#endif
{
  GstHttpSink *self;
  GstFormat format;

  self = GST_HTTP_SINK (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      gst_query_parse_position (query, &format, NULL);
      switch (format) {
        case GST_FORMAT_DEFAULT:
        case GST_FORMAT_TIME:
          gst_query_set_position (query, GST_FORMAT_TIME, self->last_timestamp);
         GST_INFO_OBJECT(self, "gst_http_sink_pad_query: GST_FORMAT_TIME position %llx", self->last_timestamp);
          return TRUE;
        default:
          return FALSE;
      }

    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 2, GST_FORMAT_DEFAULT, GST_FORMAT_TIME);
      return TRUE;
    default:
#ifdef USE_GST1
      return gst_pad_query_default (pad, parent, query);
#else
      return gst_pad_query_default (pad, query);
#endif
  }
}

static gboolean
httpsink_init (GstPlugin * plugin)
{
  gst_element_register (plugin, "httpsink", GST_RANK_NONE,
      gst_http_sink_get_type ());

  return TRUE;
}

#ifndef PACKAGE
#define PACKAGE "myfirsthttpsink"
#endif

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
#ifdef USE_GST1
    httpsink,
#else
    "httpsink",
#endif
    "Supplies media data over http",
    httpsink_init, 
    VERSION, 
    "LGPL", 
    PACKAGE_NAME,
    GST_PACKAGE_ORIGIN )



/** @} */
/** @} */
