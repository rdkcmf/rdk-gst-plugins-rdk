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
  *  @defgroup HTTP_SINK   HTTP Sink
  *  Httpsink  is a gstreamer sink element which supplies media data over http.
  *  It has only one Sink Pad from which it reads the media data.
  *  Properties Implemented:
  *  - http-obj       : Gets the http request instance from http server.
  *  - stream-type    : Setting stream type: true-linear, false-chuncked.
  *  - sent_data_size : Total size of data written to socket
  *  - source-type    : Name of the source type used when logging first packet, like QAM, DVR, VOD, TSB, IPPV, VPOP
  *  - source-id      : The source id info used when logging first packet, like ocap://0xXXXX, dvr://local/xxxx#0, vod://<string>
  *  - send_data_time : Last time data written to socket
  *  - send_status    : Current send status
  *  @ingroup  GST_PLUGINS
 **/

/**
* @defgroup gst-plugins-rdk
* @{
* @defgroup httpsink
* @{
**/


#ifndef __GST_HTTPSINK_H__
#define __GST_HTTPSINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS

/**
 *  @addtogroup HTTP_SINK   HTTP Sink
 *  @{
 */
 
#define GST_TYPE_HTTP_SINK \
  (gst_http_sink_get_type())
#define GST_HTTP_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HTTP_SINK,GstHttpSink))
#define GST_HTTP_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HTTP_SINK,GstHttpSinkClass))
#define GST_HTTP_SINK_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_HTTP_SINK, GstHttpSinkClass))
#define GST_IS_HTTP_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HTTP_SINK))
#define GST_IS_HTTP_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HTTP_SINK))

#define GST_HTTPSINK_EVENT_BASE    (0x0600)
#define GST_HTTPSINK_EVENT_CONNECTION_CLOSED (GST_HTTPSINK_EVENT_BASE + 1)

typedef struct _GstHttpSink GstHttpSink;
typedef struct _GstHttpSinkClass GstHttpSinkClass;

struct _GstHttpSink
{
  GstBaseSink parent;                /**<  Gstreamer Object                                         */

  gboolean silent;                   /**<  Indicates verbose output                                 */
  gint http_obj;                     /**<  Gets the http request instance from http server          */
  GStaticRecMutex http_obj_mutex;    /**<  Mutex object                                             */
  gboolean is_chunked;               /**<  Boolean value indicates packet is chunked or not         */
  long long last_timestamp;          /**<  Value from GST_BUFFER_TIMESTAMP                          */
  guint64 sent_data_size;            /**<  Total size of data written to socket                     */
  gint packetcount;                  /**<  Count of  number of packets received                     */
  gboolean isFirstPacket;            /**<  Boolean value checks the first packet                    */
  gchar source_type[32];             /**<  Name of the source type used when logging first packet,
                                           like QAM, DVR, VOD, TSB, IPPV, VPOP                      */
  gchar source_id[1024];             /**<  The source id info used when logging first packet,
										   like ocap://0xXXXX, dvr://local/xxxx#0, vod://<string>   */
  gboolean sendError;                /**<  Flag indicates error report to be send or not            */
  guint64 last_send_time;            /**<  Last time data written to socket                         */
  gboolean is_blocked;               /**<  Indicates data transfer is blocked                       */

  GstCaps *caps;                     /**<  For media types                                          */
};

typedef void (*dispose_func) (GObject * object);                            /**< Dispose parent    */
typedef gboolean (*event_func) (GstBaseSink *sink, GstEvent *event);        /**< httpsink event    */
typedef gboolean (*query_func) (GstElement *element, GstQuery *query);      /**< httpsink query    */

struct _GstHttpSinkClass
{
  GstBaseSinkClass parent_class;
  dispose_func parent_dispose;
  event_func parent_event;
  query_func parent_query;
};

GType gst_http_sink_get_type (void);  /**< Used for registering the http sink element                 */

G_END_DECLS

#endif /* __GST_HTTPSINK_H__ */


/** @} */   /** End of Doxygen tag  HTTP_SINK  */
/** @} */
/** @} */
