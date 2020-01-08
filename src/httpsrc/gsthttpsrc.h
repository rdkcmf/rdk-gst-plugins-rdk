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
  *  @defgroup HTTP_SRC   HTTP Source
  *  HTTP Source(httpsrc)  is a gstreamer source plugin which sends http request and receives and process the response.
  *  All RDK versions must support HTTP Source.
  *
  *  @b All Properties:
  *  - Location                   : URI to read from
  *  - automatic-redirect         : Automatically follow redirects
  *  - startPTS                   : Value of content starting PTS in seconds
  *  - endPTS                     : Value of content ending PTS in seconds
  *  - redirect-expected          : Should httpsrc expect a redirect when loading the current URL
  *  - disable-process-signaling  : Try and avoid use of signals
  *  - proxy                      : Proxy server to use, in the form HOSTNAME:PORT
  *  - user-agent                 : Value of the User-Agent HTTP request header field
  *  - trailer                    : Trailer data received
  *  - content-length             : Current duration of content in seconds
  *  - content type               : Value of content type received
  *  - http-status                : http status code received
  *  - user-id                    : User id for authentication
  *  - user-pw                    : User password for authentication
  *  - extra-headers              : User specified headers to append to the HTTP request
  *  - timeout                    : IO timeout in seconds where 0 means no timeout.
  *  - proxy-pw                   : Proxy user password for authentication
  *  - proxy-id                   : Proxy user id for authentication
  *  - extra-headers              : User specified headers to append to the HTTP request
  *  - GopSize                    : Number of frames in each GOP
  *  - is-live                    : Function as a live source
  *  - cookies                    : HTTP request cookies
  *  - numbframes                 : Number of B-frames in each GOP
  *
  *  @b Required capabilities:
  *  - Accept URL along with the query parameters
  *  - Set and Get HTTP header fields from HTTP Requests and Responses
  *  - Support Chunked encoding
  *  - Read Http Error codes
  *  - Support and Control buffer sizes
  *  - Set trick-mode property to instruct demux and other elements to go to trick-mode
  *  @ingroup  GST_PLUGINS
 **/

/**
* @defgroup gst-plugins-rdk
* @{
* @defgroup httpsrc
* @{
**/


#ifndef __GST_HTTPSRC_H__
#define __GST_HTTPSRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

#include <curl/curl.h>
#include <pthread.h>

G_BEGIN_DECLS

/**
 * @addtogroup HTTP_SRC
 * @{
**/

#define GST_TYPE_HTTP_SRC \
  (gst_http_src_get_type())
#define GST_HTTP_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HTTP_SRC,GstHttpSrc))
#define GST_HTTP_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HTTP_SRC,GstHttpSrcClass))
#define GST_HTTP_SRC_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_HTTP_SRC, GstHttpSrcClass))
#define GST_IS_HTTP_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HTTP_SRC))
#define GST_IS_HTTP_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HTTP_SRC))

typedef struct _GstHttpSrc GstHttpSrc;
typedef struct _GstHttpSrcClass GstHttpSrcClass;

typedef struct _GstHttpSrcBlockQueueElement
{
   struct _GstHttpSrcBlockQueueElement *m_next;
   guchar *m_block;
   gint m_blockSize;
} GstHttpSrcBlockQueueElement;


struct _GstHttpSrc
{
   GstPushSrc parent;
   
   gchar *m_location;                               /**< URI to read from, Source URI                                  */
   gboolean m_automaticRedirect;                    /**< Automatically follow redirects                                */
   gchar *m_userAgent;                              /**< Value of the HTTP request header User-Agent field             */
   gchar **m_cookies;                               /**< HTTP request cookies                                          */
   guint m_timeout;                                 /**< IO timeout in seconds where 0 means no timeout                */
   gchar *m_proxy;                                  /**< Proxy server URI                                              */
   gchar *m_proxyPassword;                          /**< Proxy user password for authentication                        */
   gchar *m_proxyId;                                /**< Proxy user id for authentication                              */
   GstStructure *m_extraHeaders;                    /**< User specified headers to append to the HTTP request          */
   gchar *m_userPassword;                           /**< User password for authentication                              */
   gchar *m_userId;                                 /**< User id for authentication                                    */
   gint m_httpStatus;                               /**< http status code received                                     */
   gchar *m_contentType;                            /**< Value of content type received                                */
   glong m_contentLength;                           /**< Current duration of content in seconds                        */
   gchar *m_trailer;                                /**< Trailer Data received                                         */
   gboolean m_haveHeaders;                          /**< Presence of header                                            */
   gboolean m_haveFirstData;                        /**< First Buffer Received from the Server                         */
   gboolean m_haveTrailers;                         /**< Boolean value indicates the presence of trailer               */
   gboolean m_waitTimeExceeded;                     /**< Wait Time expired                                             */
   gint m_numTrailerHeadersExpected;                /**< Trailer headers expected                                      */
   gulong m_gopSize;                                /**< Size of GOP                                                   */
   gulong m_numBFramesPerGOP;                       /**< Number of frames in each GOP                                  */
   gulong m_startPTS;                               /**< Value of content starting PTS in seconds                      */
   gulong m_endPTS;                                 /**< Value of content ending PTS in seconds                        */
   gboolean m_redirectExpected;                     /**< Should httpsrc expect a redirect when loading the current URL */
   gboolean m_disableProcessSignaling;              /**< Disable Process Signaling                                     */
   gboolean m_isSeekable;                           /**< Able to seek or not                                           */
   gboolean m_retry;                                /**< Retry  possible or not                                        */
   gboolean m_haveReceivedFirstBuffer;              /**< First Buffer Received from the Server is success or not       */
   gboolean m_haveSize;                             /**< checking for source length                                    */
   guint64 m_contentSize;                           /**< content size                                                  */
   guint64 m_requestPosition;                       /**< Position for seek                                             */
   guint64 m_readPosition;                          /**< Stores the current position                                   */
   pthread_t m_sessionThread;                       /**< Session thread                                                */
   gboolean m_threadStarted;                        /**< Indicates the thread start                                    */
   gboolean m_threadRunning;                        /**< Thread running or not                                         */
   gboolean m_threadStopRequested;                  /**< Request for stop                                              */
   gboolean m_sessionPaused;                        /**< Indicates session paused                                      */
   gboolean m_sessionUnPause;                       /**< Session unpaused state                                        */
   gboolean m_sessionError;                         /**< Session connection error                                      */
   pthread_t m_flowTimerThread;                     /**< Timer thread for gst_http_src_flow_timer_thread               */
   gboolean m_flowTimerThreadStarted;               /**< Indicates the start of timer thread                           */
   gboolean m_eosEventPushed;                       /**< EoS event pushed to DownStream element                        */
   CURL *m_curl;                                    /**< Http CURL command                                             */
   struct curl_slist *m_slist;                      /**< CURL list                                                     */
   gchar m_work[1024];                              /**< Extra headers                                                 */
   pthread_mutex_t m_queueMutex;                    /**< Mutex FIFO variables                                          */
   pthread_mutex_t m_queueNotEmptyMutex;            /**< Mutex FIFO variables                                          */
   pthread_cond_t m_queueNotEmptyCond;              /**< Mutex FIFO variables  - Not empty condition                   */
   pthread_mutex_t m_queueNotFullMutex;             /**< Mutex FIFO variables                                          */
   pthread_cond_t m_queueNotFullCond;               /**< Mutex FIFO variables  - Not full condition                    */
   pthread_mutex_t m_flowTimerMutex;                /**< Mutex FIFO variables                                          */
   pthread_cond_t m_flowTimerCond;                  /**< Mutex FIFO variables                                          */
   GstHttpSrcBlockQueueElement *m_queueHead;        /**< Queue head                                                    */
   GstHttpSrcBlockQueueElement *m_queueTail;        /**< Queue tail                                                    */
   gint m_queuedByteCount;                          /**< Block size                                                    */
   guchar *m_currBlock;                             /**< Current block                                                 */
   gint m_currBlockSize;                            /**< Current block size                                            */
   gint m_currBlockOffset;                          /**< Offset bytes                                                  */
   gint m_readDelay;                                /**< sleep up to half the CURL_MAX_WRITE_SIZE                     */
   gchar m_curlErrBuf[CURL_ERROR_SIZE];             /**< Curl http returned error                                      */

   GstCaps *m_caps;                                  /**< Structure describes the media types                          */
   int m_socketFD;
   gboolean m_isLowBitRateContent;
};

struct _GstHttpSrcClass
{
  GstPushSrcClass parent_class;
};

GType gst_http_src_get_type (void);                  /** Gtype element to register, Used with gst_element_register() */


#endif



/** @} */
/** @} */
/** @} */
