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
* @defgroup httpsrc
* @{
**/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gstelement.h>
#include "gsthttpsrc.h"
#include <assert.h>
#include <memory.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include "safec_lib.h"

#define ELEMENT_NAME "httpsrc"

#ifndef PACKAGE
#define PACKAGE ELEMENT_NAME
#endif

#define MAX_WAIT_TIME_MS (50)

/* Uncomment to enable increasing the socket low water mark to
   help reduce the occurences small data block sizes */
#if 1
#define ENABLE_SOCKET_LOWAT
#endif

/* Uncomment to enable sleeping for a dynamically determined amount
   to allow more data to collect in curl's receive buffer */
#if 1
#define ENABLE_READ_DELAY
#endif

#define CURL_EASY_SETOPT(curl, CURLoption, option)\
    if (curl_easy_setopt(curl, CURLoption, option) != 0) {\
          GST_LOG("GSTHTTPSRC : Failed at curl_easy_setopt ");\
    }  //CID:127573,127648,127985 - checked return

static gboolean httpsrc_init(GstPlugin *plugin)
{
   gst_element_register(plugin, ELEMENT_NAME, GST_RANK_NONE, gst_http_src_get_type ());

   return TRUE;
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
#ifdef USE_GST1
    httpsrc,
#else
    ELEMENT_NAME,
#endif
    "Supplies media data received over http",
    httpsrc_init, 
    VERSION, 
    "LGPL", 
    PACKAGE_NAME,
    GST_PACKAGE_ORIGIN )

static GstStaticPadTemplate srcPadTemplate= 
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (httpsrc_debug);
#define GST_CAT_DEFAULT httpsrc_debug

enum
{
  PROPERTY_LOCATION= 1,
  PROPERTY_AUTOMATIC_REDIRECT,
  PROPERTY_USER_AGENT,
  PROPERTY_IS_LIVE,
  PROPERTY_COOKIES,
  PROPERTY_TIMEOUT,
  PROPERTY_PROXY,
  PROPERTY_PROXY_PASSWORD,
  PROPERTY_PROXY_ID,
  PROPERTY_EXTRA_HEADERS,
  PROPERTY_USER_PASSWORD,
  PROPERTY_USER_ID,
  PROPERTY_HTTP_STATUS,
  PROPERTY_CONTENT_TYPE,
  PROPERTY_CONTENT_LENGTH,
  PROPERTY_TRAILER,
  PROPERTY_GOPSIZE,
  PROPERTY_NUMBFRAMESPERGOP,
  PROPERTY_STARTPTS,
  PROPERTY_ENDPTS,
  PROPERTY_REDIRECT_EXPECTED,
  PROPERTY_LOWBITRATE_CONTENT,
  PROPERTY_DISABLE_PROCESS_SIGNALING
};

#define DEFAULT_USER_AGENT "RMF httpsrc "
#define DEFAULT_HTTP_STATUS 0
#define DEFAULT_CONTENT_LENGTH -1
#define DEFAULT_TIMEOUT 0
#define DEFAULT_GOPSIZE 15
#define DEFAULT_NUMBFRAMESPERGOP 8
#define DEFAULT_STARTPTS 0
#define DEFAULT_ENDPTS 0
#define DEFAULT_SO_RCVLOWAT 1
#define FLOW_TIMER_WAIT_MAX_US 2*1000*1000 //2 seconds
#define FLOW_TIMER_WAIT_SLEEP_INTERVAL_US 10000 //10 ms
#define FLOW_TIMER_MAX_WAIT_ITERATIONS (FLOW_TIMER_WAIT_MAX_US/FLOW_TIMER_WAIT_SLEEP_INTERVAL_US) //200 iterations

static void gst_http_src_uri_handler_init(gpointer g_iface, gpointer iface_data);

#ifdef USE_GST1
static void gst_http_src_init(GstHttpSrc *src);
static gboolean gst_http_src_set_location(GstHttpSrc *src, const gchar *uri, GError **error);
#else
static void gst_http_src_base_init(gpointer g_class);
static void gst_http_src_init(GstHttpSrc *src, GstHttpSrcClass *g_class);
static gboolean gst_http_src_set_location(GstHttpSrc *src, const gchar *uri);
#endif

static void gst_http_src_class_init(GstHttpSrcClass *klass);
static void gst_http_src_finalize(GObject *gobject);

static void gst_http_src_reset(GstHttpSrc *src);
static void gst_http_src_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_http_src_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_http_src_set_proxy(GstHttpSrc *src, const gchar *uri);
static gboolean gst_http_src_set_trailer( GstHttpSrc *src, const gchar *trailer, gint len );
static gboolean gst_http_src_query(GstBaseSrc *bsrc, GstQuery *query);
static gboolean gst_http_src_is_seekable(GstBaseSrc *bsrc);
static gboolean gst_http_src_get_size(GstBaseSrc *bsrc, guint64 *size);
static gboolean gst_http_src_do_seek(GstBaseSrc *bsrc, GstSegment *segment);
static gboolean gst_http_src_unlock(GstBaseSrc *bsrc);
static gboolean gst_http_src_unlock_stop(GstBaseSrc *bsrc);
static gboolean gst_http_src_start(GstBaseSrc *bsrc);
static gboolean gst_http_src_stop(GstBaseSrc *bsrc);
static GstFlowReturn gst_http_src_create(GstPushSrc *pushsrc, GstBuffer **outbuf);
static void* gst_http_src_session_thread( void *arg );
static void* gst_http_src_flow_timer_thread( void *arg );
static curl_socket_t gst_http_src_opensocket_callback(void *clientp, curlsocktype purpose, struct curl_sockaddr *address);
static int gst_http_src_progress_callback(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow);
static size_t gst_http_src_header_callback(char *buffer, size_t size, size_t nitems, void *userData);
static size_t gst_http_src_data_received(void *ptr, size_t size, size_t nmemb, void *userData);
static gboolean gst_http_src_buffer_ready(GstHttpSrc *src, guchar *block, int blockSize, int blockOffset );
static void gst_http_src_flush_queue(GstHttpSrc *src);
static void gst_http_src_block_free(void *blockToFree);
static gboolean gst_http_src_append_extra_headers(GQuark field_id, const GValue *value, gpointer userData);
static void gst_http_src_session_trace(GstHttpSrc *src);

#ifdef USE_GST1
#define gst_http_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstHttpSrc, gst_http_src, GST_TYPE_PUSH_SRC,
  G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_http_src_uri_handler_init));
#else
static void _additional_init(GType type)
{
   static const GInterfaceInfo urihandlerInfo= { gst_http_src_uri_handler_init, NULL, NULL };

   g_type_add_interface_static(type, GST_TYPE_URI_HANDLER, &urihandlerInfo);

   GST_DEBUG_CATEGORY_INIT(httpsrc_debug, "httpsrc", 0, "HTTP src");
}

GST_BOILERPLATE_FULL(GstHttpSrc, gst_http_src, GstPushSrc,
    GST_TYPE_PUSH_SRC, _additional_init)
#endif

#ifdef USE_GST1
static gchar *gst_http_src_uri_get_uri(GstURIHandler * handler)
{
   GstHttpSrc *src= GST_HTTP_SRC(handler);

   return g_strdup (src->m_location);
}
#else
static const gchar *gst_http_src_uri_get_uri(GstURIHandler * handler)
{
   GstHttpSrc *src= GST_HTTP_SRC(handler);

   return src->m_location;
}
#endif

#ifdef USE_GST1
static gboolean gst_http_src_uri_set_uri(GstURIHandler * handler,
    const gchar * uri, GError **error)
{
   GstHttpSrc *src= GST_HTTP_SRC(handler);

   return gst_http_src_set_location(src, uri, error);
}
#else
static gboolean gst_http_src_uri_set_uri(GstURIHandler * handler, const gchar * uri)
{
   GstHttpSrc *src= GST_HTTP_SRC(handler);

   return gst_http_src_set_location(src, uri);
}
#endif

#ifdef USE_GST1
static const gchar * const *gst_http_src_uri_get_protocols(GType type)
{
   static const gchar *protocolsHandled[]= { "http", "https", NULL };
   return protocolsHandled;
}
#else
static gchar **gst_http_src_uri_get_protocols(void)
{
   static const gchar *protocolsHandled[]= { "http", "https", NULL };
   return (gchar **) protocolsHandled;
}
#endif

#ifdef USE_GST1
static GstURIType gst_http_src_uri_get_type(GType type)
#else
static guint gst_http_src_uri_get_type(void)
#endif
{
   return GST_URI_SRC;
}

static void gst_http_src_uri_handler_init(gpointer interface, gpointer interface_data)
{
   GstURIHandlerInterface *iface= (GstURIHandlerInterface *)interface;

   iface->get_uri= gst_http_src_uri_get_uri;
   iface->set_uri= gst_http_src_uri_set_uri;
   iface->get_protocols= gst_http_src_uri_get_protocols;
   iface->get_type= gst_http_src_uri_get_type;
}

#ifndef USE_GST1
static void gst_http_src_base_init(gpointer g_class)
{
   GstElementClass *element_class= GST_ELEMENT_CLASS(g_class);

   gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&srcPadTemplate));

   gst_element_class_set_details_simple(element_class, 
                                       "HTTP source",
                                       "Source",
                                       "Receives data over http",
                                       "Comcast");
}
#endif

static void gst_http_src_class_init(GstHttpSrcClass* klass)
{
   GObjectClass *gobject_class= G_OBJECT_CLASS(klass);
#ifdef USE_GST1
   GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
#endif
   GstBaseSrcClass *gstbasesrc_class= GST_BASE_SRC_CLASS(klass);
   GstPushSrcClass *gstpushsrc_class= GST_PUSH_SRC_CLASS (klass);

   gobject_class->set_property= gst_http_src_set_property;
   gobject_class->get_property= gst_http_src_get_property;
   gobject_class->finalize= gst_http_src_finalize;

   gstbasesrc_class->is_seekable= GST_DEBUG_FUNCPTR(gst_http_src_is_seekable);
   gstbasesrc_class->do_seek= GST_DEBUG_FUNCPTR(gst_http_src_do_seek);
   gstbasesrc_class->unlock= GST_DEBUG_FUNCPTR(gst_http_src_unlock);
   gstbasesrc_class->unlock_stop= GST_DEBUG_FUNCPTR(gst_http_src_unlock_stop);
   gstbasesrc_class->get_size= GST_DEBUG_FUNCPTR(gst_http_src_get_size);
   gstbasesrc_class->query= GST_DEBUG_FUNCPTR(gst_http_src_query);
   gstbasesrc_class->start= GST_DEBUG_FUNCPTR(gst_http_src_start);
   gstbasesrc_class->stop= GST_DEBUG_FUNCPTR(gst_http_src_stop);

   gstpushsrc_class->create= GST_DEBUG_FUNCPTR(gst_http_src_create);

   g_object_class_install_property 
     (gobject_class,
      PROPERTY_LOCATION,
      g_param_spec_string("location", "URI","URI to read from", "",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   g_object_class_install_property 
     (gobject_class,
      PROPERTY_AUTOMATIC_REDIRECT,
      g_param_spec_boolean("automatic-redirect", "automatic redirect", "Automatically follow redirects",
      TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   g_object_class_install_property 
     (gobject_class,
      PROPERTY_USER_AGENT,
      g_param_spec_string("user-agent", "User agent", "Value of the HTTP request header User-Agent field",
      DEFAULT_USER_AGENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   g_object_class_install_property 
     (gobject_class, 
      PROPERTY_IS_LIVE,
      g_param_spec_boolean("is-live", "is live", "Function as a live source",
      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   g_object_class_install_property
     (gobject_class, 
      PROPERTY_COOKIES,
      g_param_spec_boxed("cookies", "Cookies", "HTTP request cookies",
      G_TYPE_STRV, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   g_object_class_install_property
     (gobject_class, 
      PROPERTY_TIMEOUT,
      g_param_spec_uint("timeout", "timeout", "IO timeout in seconds where 0 means no timeout.", 0,
      3600, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   g_object_class_install_property
     (gobject_class,
      PROPERTY_PROXY,
      g_param_spec_string("proxy", "Proxy", "Proxy server URI", "",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   g_object_class_install_property
     (gobject_class, 
      PROPERTY_PROXY_PASSWORD,
      g_param_spec_string("proxy-pw", "proxy password", "Proxy user password for authentication", "",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   g_object_class_install_property
     (gobject_class, 
      PROPERTY_PROXY_ID,
      g_param_spec_string("proxy-id", "proxy-id", "Proxy user id for authentication", "",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   g_object_class_install_property
     (gobject_class, 
      PROPERTY_EXTRA_HEADERS,
      g_param_spec_boxed("extra-headers", "Extra Headers", "User specified headers to append to the HTTP request",
      GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   g_object_class_install_property
     (gobject_class, 
      PROPERTY_USER_PASSWORD,
      g_param_spec_string("user-pw", "user-pw", "User password for authentication", "",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   g_object_class_install_property
     (gobject_class,
      PROPERTY_USER_ID,
      g_param_spec_string("user-id", "user-id", "User id for authentication", "",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   g_object_class_install_property
     (gobject_class, 
      PROPERTY_HTTP_STATUS,
      g_param_spec_int("http-status", "http status", "http status code received.", 0,
      1000, DEFAULT_HTTP_STATUS,
      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

   g_object_class_install_property
     (gobject_class, 
      PROPERTY_CONTENT_TYPE,
      g_param_spec_string("content-type", "Content type", "Value of content type received", NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

   g_object_class_install_property
     (gobject_class, 
      PROPERTY_CONTENT_LENGTH,
      g_param_spec_ulong("content-length", "Content length", "Current duration of content in seconds",
      0, G_MAXULONG, DEFAULT_CONTENT_LENGTH, G_PARAM_READABLE));

   g_object_class_install_property
     (gobject_class,
      PROPERTY_TRAILER,
      g_param_spec_string("trailer", "Trailer", "Trailer data received", NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

   g_object_class_install_property
     (gobject_class, 
      PROPERTY_GOPSIZE,
      g_param_spec_ulong ("GopSize", "GopSize", "Number of frames in each GOP", 0,
      G_MAXULONG, DEFAULT_GOPSIZE,
      (GParamFlags)(G_PARAM_READWRITE)));

   g_object_class_install_property
     (gobject_class, 
      PROPERTY_NUMBFRAMESPERGOP,
      g_param_spec_ulong ("numbframes", "numbframes", "Number of B-frames in each GOP", 0,
      G_MAXULONG, DEFAULT_NUMBFRAMESPERGOP,
      (GParamFlags)(G_PARAM_READWRITE)));

   g_object_class_install_property
     (gobject_class, 
      PROPERTY_STARTPTS,
      g_param_spec_ulong ("startPTS", "startPTS", "Value of content starting PTS in seconds", 0,
      G_MAXULONG, DEFAULT_STARTPTS,
      (GParamFlags)(G_PARAM_READWRITE)));

   g_object_class_install_property
     (gobject_class, 
      PROPERTY_ENDPTS,
      g_param_spec_ulong ("endPTS", "endPTS", "Value of content ending PTS in seconds", 0,
      G_MAXULONG, DEFAULT_ENDPTS,
      (GParamFlags)(G_PARAM_READWRITE)));

   g_object_class_install_property
     (gobject_class,
      PROPERTY_REDIRECT_EXPECTED,
      g_param_spec_boolean("redirect-expected", "redirect-expected", "Should httpsrc expect a redirect when loading the current URL",
      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property
     (gobject_class,
      PROPERTY_LOWBITRATE_CONTENT,
      g_param_spec_boolean("low-bitrate-content", "low-bitrate-content", "httpsrc expect to pull a low bitrate content",
      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   g_object_class_install_property
     (gobject_class,
      PROPERTY_DISABLE_PROCESS_SIGNALING,
      g_param_spec_boolean("disable-process-signaling", "disable-process-signaling", "Try and avoid use of signals (to timeout name lookups, for example)",
      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

#ifdef USE_GST1
   gst_element_class_add_pad_template (gstelement_class, gst_static_pad_template_get(&srcPadTemplate));

   gst_element_class_set_static_metadata (gstelement_class, "HTTP source",
       "Source", "Receives data over http", "Comcast");

  GST_DEBUG_CATEGORY_INIT(httpsrc_debug, "httpsrc", 0, "HTTP src");
#endif
}

#ifdef USE_GST1
static void gst_http_src_init(GstHttpSrc *src)
#else
static void gst_http_src_init(GstHttpSrc *src, GstHttpSrcClass *g_class)
#endif
{
   const gchar *proxy;

   src->m_location= NULL;
   src->m_automaticRedirect= TRUE;
   src->m_userAgent= g_strdup(DEFAULT_USER_AGENT);
   src->m_cookies= NULL;
   src->m_proxyPassword= NULL;
   src->m_proxyId= NULL;
   src->m_extraHeaders= NULL;
   src->m_userPassword= NULL;
   src->m_userId= NULL;
   src->m_contentType= NULL;
   src->m_contentLength= DEFAULT_CONTENT_LENGTH;
   src->m_timeout= DEFAULT_TIMEOUT;
   src->m_trailer= NULL;
   src->m_sessionPaused= FALSE;
   src->m_sessionUnPause= FALSE;
   src->m_sessionError= FALSE;
   src->m_curl= 0;
   src->m_slist= 0;
   src->m_haveHeaders= FALSE;
   src->m_haveFirstData= FALSE;
   src->m_haveTrailers= FALSE;
   src->m_waitTimeExceeded= FALSE;
   src->m_numTrailerHeadersExpected= 0;
   src->m_readDelay= 0;
   src->m_socketFD = -1;
   src->m_isLowBitRateContent = FALSE;

   /*coverity[missing_lock]  CID-19225, 19226, 19357, 19358 Code annotation to ignore the Coevrity error*/
   pthread_mutex_init( &src->m_queueMutex, 0 );
   pthread_mutex_init( &src->m_queueNotEmptyMutex, 0 );
   pthread_cond_init( &src->m_queueNotEmptyCond, 0 );
   pthread_mutex_init( &src->m_queueNotFullMutex, 0 );
   pthread_cond_init( &src->m_queueNotFullCond, 0 );
   pthread_mutex_init( &src->m_flowTimerMutex, 0 );
   pthread_cond_init( &src->m_flowTimerCond, 0 );
   pthread_mutex_lock( &src->m_queueMutex );
   src->m_queueHead= 0;
   src->m_queueTail= 0;
   src->m_queuedByteCount= 0;
   src->m_queuedByteCount= 0;
   pthread_mutex_unlock( &src->m_queueMutex );
   /*coverity[missing_lock]  CID-19225, 19226, 19357, 19358 Code annotation to ignore the Coevrity error*/
   pthread_mutex_lock( &src->m_flowTimerMutex );
   src->m_currBlock= 0;
   src->m_currBlockSize= 0;
   src->m_currBlockOffset= 0;
   pthread_mutex_unlock( &src->m_flowTimerMutex );  //CID:136298,136353,136384,136598 - Missing lock

#ifdef USE_GST1
   gst_base_src_set_automatic_eos (GST_BASE_SRC (src), FALSE);
#endif

   gst_http_src_reset(src);
}

static void gst_http_src_reset(GstHttpSrc *src)
{
   src->m_threadStopRequested= FALSE;
   src->m_retry= FALSE;
   src->m_haveSize= FALSE;
   src->m_isSeekable= FALSE;
   src->m_contentSize= 0LL;
   src->m_requestPosition= 0LL;
   src->m_readPosition= 0LL;
   src->m_httpStatus= DEFAULT_HTTP_STATUS;
   src->m_startPTS= DEFAULT_STARTPTS;
   src->m_endPTS= DEFAULT_ENDPTS;
   src->m_redirectExpected= FALSE;
   src->m_disableProcessSignaling = FALSE;
   src->m_gopSize= DEFAULT_GOPSIZE;
   src->m_numBFramesPerGOP= DEFAULT_NUMBFRAMESPERGOP;
   src->m_haveReceivedFirstBuffer= FALSE;
   src->m_threadRunning= FALSE;
   src->m_threadStarted= FALSE;
   src->m_flowTimerThreadStarted= FALSE;
   src->m_eosEventPushed = FALSE;
   src->m_sessionPaused= FALSE;
   src->m_sessionUnPause= FALSE;
   src->m_sessionError= FALSE;
   src->m_haveHeaders= FALSE;
   src->m_haveFirstData= FALSE;
   src->m_haveTrailers= FALSE;
   src->m_waitTimeExceeded= FALSE;
   src->m_numTrailerHeadersExpected=0;
   src->m_readDelay= 0;
   src->m_socketFD = -1;
   src->m_isLowBitRateContent = FALSE;
   errno_t rc = -1;
   rc = memset_s(src->m_curlErrBuf, CURL_ERROR_SIZE, '\0', CURL_ERROR_SIZE);
   ERR_CHK(rc);
   
   if ( src->m_curl )
   {
      curl_easy_cleanup(src->m_curl);
      src->m_curl= 0;
   }
   
   if ( src->m_slist )
   {
      curl_slist_free_all(src->m_slist);
      src->m_slist= 0;
   }
   
   gst_caps_replace(&src->m_caps, NULL);
   
   gst_http_src_flush_queue(src);
}

static void gst_http_src_finalize(GObject *gobject)
{
   GstHttpSrc *src= GST_HTTP_SRC(gobject);

   GST_DEBUG_OBJECT(src, "finalize");

   if ( src->m_location )
   {
      g_free(src->m_location);
      src->m_location= NULL;
   }

   if ( src->m_userAgent )
   {
      g_free(src->m_userAgent);
      src->m_userAgent= NULL;
   }

   g_strfreev(src->m_cookies);
  
   if ( src->m_proxy ) 
   {
      g_free(src->m_proxy);
      src->m_proxy= NULL;
   }

   if ( src->m_proxyPassword )
   {
      g_free(src->m_proxyPassword);
      src->m_proxyPassword= NULL;
   }

   if ( src->m_proxyId )
   {
      g_free(src->m_proxyId);
      src->m_proxyId= NULL;
   }

   if ( src->m_extraHeaders )
   {
      gst_structure_free(src->m_extraHeaders);
      src->m_extraHeaders= NULL;
   }
  
   if ( src->m_userPassword )
   {
      g_free(src->m_userPassword);
      src->m_userPassword= NULL;
   }
    
   if ( src->m_userId )
   {
      g_free(src->m_userId);
      src->m_userId= NULL;
   }

   if ( src->m_contentType )
   {
      g_free(src->m_contentType);
      src->m_contentType= NULL;
   }

   if ( src->m_trailer ) 
   {
       g_free(src->m_trailer);
       src->m_trailer= NULL;
   }

   gst_http_src_flush_queue(src);

   pthread_mutex_destroy( &src->m_queueMutex );
   pthread_mutex_destroy( &src->m_queueNotEmptyMutex );   
   pthread_cond_destroy( &src->m_queueNotEmptyCond );
   pthread_mutex_destroy( &src->m_queueNotFullMutex );   
   pthread_cond_destroy( &src->m_queueNotFullCond );
   pthread_mutex_destroy( &src->m_flowTimerMutex );   
   pthread_cond_destroy( &src->m_flowTimerCond );

   G_OBJECT_CLASS(parent_class)->finalize(gobject);
}

static void gst_http_src_set_property(GObject *object, 
                                      guint property_id, 
                                      const GValue *value, 
                                      GParamSpec *pspec)
{
   GstHttpSrc *src= GST_HTTP_SRC(object);

   switch (property_id) 
   {
      case PROPERTY_LOCATION:
      {
         const gchar *location;

         location= g_value_get_string(value);
         if (location == NULL) 
         {
            GST_ERROR_OBJECT(src, "location cannot be NULL");
            goto done;
         }
#ifdef USE_GST1
         if ( !gst_http_src_set_location(src, location, NULL) )
#else
         if ( !gst_http_src_set_location(src, location) )
#endif
         {
            GST_ERROR_OBJECT(src, "location URI format error");
            goto done;
         }
      }
      break;
      
      case PROPERTY_AUTOMATIC_REDIRECT:
         src->m_automaticRedirect= g_value_get_boolean(value);
         break;
         
      case PROPERTY_USER_AGENT:
         if (src->m_userAgent)
         {
            g_free(src->m_userAgent);
         }
         src->m_userAgent= g_value_dup_string(value);
      break;
      
      case PROPERTY_IS_LIVE:
         gst_base_src_set_live(GST_BASE_SRC(src), g_value_get_boolean(value));
      break;

      case PROPERTY_COOKIES:
         g_strfreev(src->m_cookies);
         src->m_cookies= g_strdupv(g_value_get_boxed(value));
      break;

      case PROPERTY_TIMEOUT:
         if (src->m_timeout != g_value_get_uint(value)) 
         {
            src->m_timeout = g_value_get_uint(value);
            if (src->m_curl != NULL) 
            {
               CURL_EASY_SETOPT(src->m_curl, CURLOPT_CONNECTTIMEOUT, src->m_timeout);
               CURL_EASY_SETOPT(src->m_curl, CURLOPT_LOW_SPEED_TIME, src->m_timeout);

               if (src->m_timeout == 0) {
                  CURL_EASY_SETOPT(src->m_curl, CURLOPT_LOW_SPEED_LIMIT, 0);
               }
               else {
                  CURL_EASY_SETOPT(src->m_curl, CURLOPT_LOW_SPEED_LIMIT, 100);
               }

               GST_WARNING_OBJECT(src, "GSTHTTPSRC: Changing timeout - m_timeout %d", src->m_timeout);
            }
            else 
            {
               GST_ERROR_OBJECT(src, "GSTHTTPSRC: Invalid Curl handle while changing timeout");
            }
         }
      break;

      case PROPERTY_PROXY:
      {
         const gchar *proxy;

         proxy= g_value_get_string(value);

         if (proxy == NULL) 
         {
            GST_ERROR_OBJECT(src, "proxy cannot be NULL");
            goto done;
         }
         if ( !gst_http_src_set_proxy(src, proxy)) 
         {
            GST_ERROR_OBJECT(src, "proxy URI format error");
            goto done;
         }
      }
      break;

      case PROPERTY_PROXY_PASSWORD:
         if (src->m_proxyPassword)
         {
            g_free(src->m_proxyPassword);
         }
         src->m_proxyPassword= g_value_dup_string(value);
      break;

      case PROPERTY_PROXY_ID:
         if (src->m_proxyId)
         {
            g_free(src->m_proxyId);
         }
         src->m_proxyId= g_value_dup_string(value);
      break;

      case PROPERTY_EXTRA_HEADERS:
      {
         const GstStructure *s= gst_value_get_structure(value);

         if (src->m_extraHeaders)
         {
            gst_structure_free(src->m_extraHeaders);
         }
         if ( src->m_slist )
         {
            curl_slist_free_all(src->m_slist);
            src->m_slist= 0;
         }

         src->m_extraHeaders= s ? gst_structure_copy(s) : NULL;
      }
      break;

      case PROPERTY_USER_PASSWORD:
         if (src->m_userPassword)
         {
            g_free(src->m_userPassword);
         }
         src->m_userPassword= g_value_dup_string(value);
      break;

      case PROPERTY_USER_ID:
         if (src->m_userId)
         {
            g_free(src->m_userId);
         }
         src->m_userId= g_value_dup_string(value);
      break;

      case PROPERTY_HTTP_STATUS:
         src->m_httpStatus= g_value_get_int(value);
      break;

      case PROPERTY_GOPSIZE:
         src->m_gopSize= g_value_get_ulong(value);
      break;

      case PROPERTY_NUMBFRAMESPERGOP:
         src->m_numBFramesPerGOP= g_value_get_ulong(value);
      break;
      
      case PROPERTY_STARTPTS:
        src->m_startPTS= g_value_get_ulong(value);
      break;
      
      case PROPERTY_ENDPTS:
        src->m_endPTS= g_value_get_ulong(value);
      break;

      case PROPERTY_REDIRECT_EXPECTED:
         src->m_redirectExpected = g_value_get_boolean(value);
         GST_WARNING_OBJECT(src, "GSTHTTPSRC: Redirect Expected on URL %d", src->m_redirectExpected);
      break;

      case PROPERTY_LOWBITRATE_CONTENT:
      {
         src->m_isLowBitRateContent = g_value_get_boolean(value);
         GST_WARNING_OBJECT(src, "GSTHTTPSRC: Low Bitrate Content has been set to %d", src->m_isLowBitRateContent);
         if (src->m_socketFD > 0)
         {
             int rc = 0;
             int lowWaterMark = CURL_MAX_WRITE_SIZE/2;

             if (src->m_isLowBitRateContent)
                 lowWaterMark = CURL_MAX_WRITE_SIZE/8;

             /* m_redirectExpected takes the priority */
             if(src->m_redirectExpected == TRUE)
                 lowWaterMark = DEFAULT_SO_RCVLOWAT;

             rc = setsockopt(src->m_socketFD, SOL_SOCKET, SO_RCVLOWAT, &lowWaterMark, sizeof(int));
             if (rc != 0)
                 GST_ERROR_OBJECT( src, "setsockopt error %d setting SO_RCVLOWAT for socket_fd %d", rc, src->m_socketFD );
         }
      }
      break;

      case PROPERTY_DISABLE_PROCESS_SIGNALING:
         src->m_disableProcessSignaling = g_value_get_boolean(value);
         GST_WARNING_OBJECT(src, "GSTHTTPSRC: Disable Process Signaling %d", src->m_disableProcessSignaling);
      break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
done:
  return;
}

static void gst_http_src_get_property(GObject *object, 
                                      guint property_id,
                                      GValue *value, 
                                      GParamSpec *pspec)
{
   GstHttpSrc *src= GST_HTTP_SRC(object);

   switch (property_id) 
   {
      case PROPERTY_LOCATION:
         g_value_set_string(value, src->m_location);
      break;

      case PROPERTY_AUTOMATIC_REDIRECT:
         g_value_set_boolean(value, src->m_automaticRedirect);
      break;

      case PROPERTY_USER_AGENT:
         g_value_set_string(value, src->m_userAgent);
      break;
      
      case PROPERTY_IS_LIVE:
         g_value_set_boolean(value, gst_base_src_is_live(GST_BASE_SRC(src)));
      break;

      case PROPERTY_COOKIES:
         g_value_set_boxed(value, g_strdupv(src->m_cookies));
      break;
      
      case PROPERTY_TIMEOUT:
         g_value_set_uint(value, src->m_timeout);
      break;

      case PROPERTY_PROXY:
         if (src->m_proxy == NULL)
         {
            g_value_set_static_string(value, "");
         }
         else 
         {
            g_value_set_string(value, src->m_proxy);
         }
         break;

      case PROPERTY_PROXY_PASSWORD:
         g_value_set_string(value, src->m_proxyPassword);
      break;

      case PROPERTY_PROXY_ID:
         g_value_set_string(value, src->m_proxyId);
      break;

      case PROPERTY_EXTRA_HEADERS:
         gst_value_set_structure(value, src->m_extraHeaders);
      break;

      case PROPERTY_USER_PASSWORD:
         g_value_set_string(value, src->m_userPassword);
      break;

      case PROPERTY_USER_ID:
         g_value_set_string(value, src->m_userId);
      break;

      case PROPERTY_HTTP_STATUS:
         g_value_set_int(value, src->m_httpStatus);
      break;

      case PROPERTY_CONTENT_TYPE:
         g_value_set_string(value, src->m_contentType);
      break;

      case PROPERTY_CONTENT_LENGTH:
         g_value_set_ulong(value, src->m_contentLength);
      break;

      case PROPERTY_TRAILER:
          if (src->m_trailer == NULL)
          {
            g_value_set_static_string(value, "");
          }
          else
          {
            g_value_set_string(value, src->m_trailer);
          }
      break;

      case PROPERTY_GOPSIZE:
         g_value_set_ulong(value, src->m_gopSize);
      break;

      case PROPERTY_NUMBFRAMESPERGOP:
         g_value_set_ulong(value, src->m_numBFramesPerGOP);
      break;

      case PROPERTY_STARTPTS:
         g_value_set_ulong(value, src->m_startPTS);
      break;
      
      case PROPERTY_ENDPTS:
         g_value_set_ulong(value, src->m_endPTS);
      break;

      case PROPERTY_REDIRECT_EXPECTED:
         g_value_set_boolean(value, src->m_redirectExpected);
      break;

      case PROPERTY_DISABLE_PROCESS_SIGNALING:
         g_value_set_boolean(value, src->m_disableProcessSignaling);
      break;

      case PROPERTY_LOWBITRATE_CONTENT:
         g_value_set_boolean(value, src->m_isLowBitRateContent);
      break;

      default:
         G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
   }
}

#ifdef USE_GST1
static gboolean gst_http_src_set_location(GstHttpSrc *src, const gchar *uri,
    GError **error)
#else
static gboolean gst_http_src_set_location(GstHttpSrc *src, const gchar *uri)
#endif
{
   if (!uri || !g_strcmp0(uri, "") || (*uri == '\0')) 
   {
      GST_ERROR_OBJECT(src, "Empty or Invalid URI");
      return FALSE;
   }

   if (src->m_location) 
   {
      g_free(src->m_location);
      src->m_location= NULL;
   }
   src->m_location= g_strdup(uri);

   return TRUE;
}

static gboolean gst_http_src_set_proxy(GstHttpSrc *src, const gchar *uri)
{
   if (src->m_proxy) 
   {
      g_free(src->m_proxy);
      src->m_proxy= NULL;
   }
   
   if (
        (g_str_has_prefix(uri, "http://")) ||
        (g_str_has_prefix(uri, "https://"))
      ) 
   {
      src->m_proxy= g_strdup(uri);
   }
   else 
   {
      src->m_proxy= g_strconcat("http://", uri, NULL);;
   }

   return TRUE;
}

static gboolean gst_http_src_set_trailer( GstHttpSrc *src, const gchar *trailer, gint len )
{
   errno_t rc = -1;
   GST_DEBUG_OBJECT(src, "gst_http_src_set_trailer: (%.*s)", len, trailer );
   if ( !src->m_trailer )
   {
      src->m_trailer= g_strndup( trailer, len );
      GST_DEBUG_OBJECT(src, "First trailer: %s", src->m_trailer);
   }
   else
   {
      guint si, di, catlen;
      guint currTrailerLen, inTrailerLen;
      gchar *cat= 0;
      
      si= di= catlen= 0;
      currTrailerLen= strlen(src->m_trailer);
      inTrailerLen= len;
      
      GST_DEBUG_OBJECT(src, "Curr Trailer: %s", src->m_trailer);
      GST_DEBUG_OBJECT(src, "Incoming Trailer: %s", trailer);
      
      catlen= currTrailerLen+inTrailerLen+2;
      cat= g_malloc(catlen);
      if ( !cat )
      {
         GST_ERROR_OBJECT(src, "unable to allocate trailer buffer for %d bytes", catlen );
         return FALSE;
      }
      
      rc = memset_s( cat, catlen, '\0', catlen );
      ERR_CHK(rc);
      
      for( si= 0; si < currTrailerLen; ++di, ++si )
      {
         if ( (src->m_trailer[si] == '\r') || (src->m_trailer[si] == '\n') || (src->m_trailer[si] == '\0') )
         {
            break;
         }
         cat[di]= src->m_trailer[si];
      }
      
      cat[di++]= ':';

      for( si= 0; si < inTrailerLen; ++di, ++si )
      {
         if ( (trailer[si] == '\r') || (trailer[si] == '\n') || (trailer[si] == '\0') )
         {
            break;
         }
         cat[di]= trailer[si];
      }
      
      cat[di++]= '\0';
      
      g_free( src->m_trailer );
      
      src->m_trailer= cat;

      GST_DEBUG_OBJECT(src, "Concatenated Trailer: %s", src->m_trailer);
   }

   return TRUE;
}

static gboolean gst_http_src_query(GstBaseSrc *bsrc, GstQuery *query)
{
   GstHttpSrc *src= GST_HTTP_SRC(bsrc);
   gboolean ret;

   switch ( GST_QUERY_TYPE(query) ) 
   {
      case GST_QUERY_URI:
         gst_query_set_uri(query, src->m_location);
         ret= TRUE;
      break;
      default:
         ret= GST_BASE_SRC_CLASS(parent_class)->query(bsrc, query);
      break;
  }

  return ret;
}

static gboolean gst_http_src_is_seekable(GstBaseSrc *bsrc)
{
  GstHttpSrc *src= GST_HTTP_SRC(bsrc);

  return src->m_isSeekable;
}

static gboolean gst_http_src_get_size(GstBaseSrc *bsrc, guint64 *size)
{
   GstHttpSrc *src;

   src= GST_HTTP_SRC(bsrc);
   if (src->m_haveSize) 
   {
      GST_DEBUG_OBJECT(src, "get_size() contentSize= %" G_GUINT64_FORMAT, src->m_contentSize);
      
      *size= src->m_contentSize;
      return TRUE;
   }
   GST_DEBUG_OBJECT (src, "get_size() : size not yet available: return FALSE");
   return FALSE;
}

static gboolean gst_http_src_do_seek(GstBaseSrc *bsrc, GstSegment *segment)
{
   GstHttpSrc *src = GST_HTTP_SRC(bsrc);

   GST_DEBUG_OBJECT (src, "do_seek(%" G_GUINT64_FORMAT ")", segment->start);

   if (src->m_readPosition == segment->start) 
   {
      GST_DEBUG_OBJECT(src, "Seeking to current read position");
      return TRUE;
   }

   if (!src->m_isSeekable) 
   {
      GST_WARNING_OBJECT(src, "Element is not seekable");
      return FALSE;
   }

   src->m_requestPosition= segment->start;
   return TRUE;
}

static gboolean gst_http_src_unlock(GstBaseSrc *bsrc)
{
   GstHttpSrc *src;

   src= GST_HTTP_SRC(bsrc);
   GST_DEBUG_OBJECT(src, "unlock");

   src->m_threadStopRequested= TRUE;
   
   return TRUE;
}

static gboolean gst_http_src_unlock_stop(GstBaseSrc *bsrc)
{
   GstHttpSrc *src;

   src= GST_HTTP_SRC(bsrc);
   GST_DEBUG_OBJECT(src, "unlock stop");

   src->m_threadStopRequested= FALSE;

   return TRUE;
}

static gboolean gst_http_src_start(GstBaseSrc *bsrc)
{
   int rc;
   GstHttpSrc *src= GST_HTTP_SRC(bsrc);

   GST_DEBUG_OBJECT(src, "start (%s)", src->m_location);

   if (!src->m_location) 
   {
      GST_ERROR_OBJECT(src, "No location property set");
      return FALSE;
   }

   rc= pthread_create( &src->m_flowTimerThread, NULL, gst_http_src_flow_timer_thread, src );
   if ( rc != 0 )
   {
      GST_ERROR_OBJECT(src, "pthread create error %x for flow timer", rc);
      return FALSE;
   }

   rc= pthread_create( &src->m_sessionThread, NULL, gst_http_src_session_thread, src );
   if ( rc != 0 )
   {
      GST_ERROR_OBJECT(src, "pthread create error %x for session thread", rc);
      return FALSE;
   }
   src->m_threadStarted= TRUE;

   return TRUE;
}

static gboolean gst_http_src_stop(GstBaseSrc *bsrc)
{
   GstHttpSrc *src;

   src= GST_HTTP_SRC(bsrc);
   GST_DEBUG_OBJECT(src, "stop");
   
   if (src->m_threadStarted || src->m_threadRunning)
   {
      src->m_threadStopRequested= TRUE;
      src->m_threadRunning = FALSE;

      /* wakeup session thread before join */      
      pthread_mutex_lock( &src->m_queueMutex );
      pthread_mutex_lock( &src->m_queueNotFullMutex );
      pthread_mutex_unlock( &src->m_queueMutex );
      pthread_cond_signal( &src->m_queueNotFullCond );
      pthread_mutex_unlock( &src->m_queueNotFullMutex );

      pthread_join( src->m_sessionThread, NULL );
   }
   
   if (src->m_extraHeaders) 
   {
      gst_structure_free(src->m_extraHeaders);
      src->m_extraHeaders= NULL;
   }
  
   gst_http_src_reset(src);

   if (src->m_trailer) 
   {
       g_free(src->m_trailer);
       src->m_trailer= NULL;
   }

   return TRUE;
}

static GstFlowReturn gst_http_src_create(GstPushSrc *pushsrc, GstBuffer **outbuf)
{
   GstFlowReturn ret= GST_FLOW_ERROR;
   GstHttpSrc *src;
   GstBaseSrc *basesrc;
   GstHttpSrcBlockQueueElement *nextBlock= 0;
   GstBuffer *gstBuff= 0;
   int wasFull;
   gboolean lastBufPushed = FALSE;

   src= GST_HTTP_SRC(pushsrc);
   
   basesrc= GST_BASE_SRC_CAST(src);
   
   if ( src->m_threadStarted && !src->m_threadStopRequested && !src->m_haveTrailers )
   {
      if ( src->m_sessionPaused )
      {
         /* request session unpause */
         src->m_sessionUnPause= TRUE;
      }
      
      while( !nextBlock )
      {
         pthread_mutex_lock( &src->m_queueMutex );
         nextBlock= src->m_queueHead;
         if ( src->m_queueHead )
         {
            src->m_queueHead= src->m_queueHead->m_next;
            if ( !src->m_queueHead )
            {
               src->m_queueTail= 0;
            }
         }

         if ( nextBlock )
         {
            wasFull= (src->m_queuedByteCount > basesrc->blocksize*4 ? 1 : 0 );
            src->m_queuedByteCount -= nextBlock->m_blockSize;
            assert( src->m_queuedByteCount >= 0 );

            if ( wasFull )
            {
               /* signal queue is no longer full */            
               pthread_mutex_lock( &src->m_queueNotFullMutex );
               pthread_mutex_unlock( &src->m_queueMutex );
               pthread_cond_signal( &src->m_queueNotFullCond );
               pthread_mutex_unlock( &src->m_queueNotFullMutex );
            }
            else
            {
               pthread_mutex_unlock( &src->m_queueMutex );
            }
         }
         else         
         {
            if ( !src->m_threadStarted || src->m_threadStopRequested || src->m_sessionPaused )
            {
               pthread_mutex_unlock( &src->m_queueMutex );
               GST_ERROR_OBJECT(src, "gst_http_src_create A: no data: ts %d tsr %d sp %d", src->m_threadStarted, src->m_threadStopRequested, src->m_sessionPaused );
               break;
            }

            /* wait for queue to become not empty */
            pthread_mutex_lock( &src->m_queueNotEmptyMutex );
            pthread_mutex_unlock( &src->m_queueMutex );
            pthread_cond_wait( &src->m_queueNotEmptyCond, &src->m_queueNotEmptyMutex );
            pthread_mutex_unlock( &src->m_queueNotEmptyMutex );

            if ( !src->m_threadStarted || src->m_threadStopRequested || src->m_sessionPaused )
            {
               GST_ERROR_OBJECT(src, "gst_http_src_create B: no data: ts %d tsr %d sp %d", src->m_threadStarted, src->m_threadStopRequested, src->m_sessionPaused );
               break;
            }
         }
      }   

      if ( nextBlock )
      {
#ifdef USE_GST1
         gstBuff = gst_buffer_new_wrapped_full( 0,
                                                nextBlock->m_block,
                                                nextBlock->m_blockSize,
                                                0,
                                                nextBlock->m_blockSize,
                                                nextBlock->m_block,
                                                gst_http_src_block_free);
#else
         gstBuff= gst_buffer_new();
#endif

         if ( gstBuff )
         {

#ifndef USE_GST1
            GST_BUFFER_DATA(gstBuff)= nextBlock->m_block;
            GST_BUFFER_MALLOCDATA(gstBuff)= nextBlock->m_block;
            GST_BUFFER_SIZE(gstBuff)= nextBlock->m_blockSize;
            GST_BUFFER_FREE_FUNC(gstBuff)= gst_http_src_block_free;
#endif

            src->m_readPosition += nextBlock->m_blockSize;
            
            *outbuf= gstBuff;
            ret= GST_FLOW_OK;
         }
         else
         {
            GST_ERROR_OBJECT(src, "unable to alloc gst buffer");
         }
      }
   }
   
   if ( ret != GST_FLOW_OK )
   {
      if ( !src->m_sessionError )
      {
         GST_DEBUG_OBJECT(src, "gst_http_src_create: src->m_currBlock %x src->m_currBlockSize %d src->m_currBlockOffset %d", src->m_currBlock, src->m_currBlockSize, src->m_currBlockOffset);

         pthread_mutex_lock( &src->m_queueMutex );
         pthread_mutex_lock( &src->m_queueNotFullMutex );
         pthread_mutex_unlock( &src->m_queueMutex );
         src->m_threadStopRequested = TRUE;
         pthread_cond_signal( &src->m_queueNotFullCond );
         pthread_mutex_unlock( &src->m_queueNotFullMutex );

         pthread_mutex_lock( &src->m_flowTimerMutex );
         if ( src->m_currBlock  && ( src->m_currBlockOffset < src->m_currBlockSize ) )
         {
#ifdef USE_GST1
            gstBuff = gst_buffer_new_wrapped_full( 0,
                                                   src->m_currBlock,
                                                   src->m_currBlockOffset,
                                                   0,
                                                   src->m_currBlockOffset,
                                                   src->m_currBlock,
                                                   gst_http_src_block_free);

#else
            gstBuff = gst_buffer_new();
#endif
            if ( gstBuff )
            {

#ifndef USE_GST1
               GST_BUFFER_DATA(gstBuff)= src->m_currBlock;
               GST_BUFFER_MALLOCDATA(gstBuff)= src->m_currBlock;
               GST_BUFFER_SIZE(gstBuff)= src->m_currBlockOffset;
               GST_BUFFER_FREE_FUNC(gstBuff)= gst_http_src_block_free;
#endif

               src->m_readPosition += src->m_currBlockOffset;

               *outbuf= NULL;
               /* Pushes a buffer to the peer of pad.
                In all cases, success or failure, the caller loses its reference to buffer after calling this function. 
                Accessing local members of the block once the buffer is handed over might be unsafe, using a local copy of the pointer */
               if ( GST_FLOW_OK != gst_pad_push( src->parent.parent.srcpad, gstBuff ) ) 
               {
                  GST_ERROR_OBJECT(src, "gst_http_src_create: PUSHING BUFFER BEFORE EOS - FAILED");
               }   
               else 
               {
                  GST_WARNING_OBJECT(src, "gst_http_src_create: PUSHING BUFFER BEFORE EOS - SUCCESS");
               }

               lastBufPushed = TRUE;
               src->m_currBlock = NULL;
            }
            else
            {
               GST_ERROR_OBJECT(src, "unable to alloc gst buffer");
            }
         }
         pthread_mutex_unlock( &src->m_flowTimerMutex );

         if ( lastBufPushed == FALSE ) 
         {
            pthread_mutex_lock( &src->m_queueMutex );
            if ( src->m_queueHead && src->m_queueTail && ( src->m_queueHead == src->m_queueTail ) ) 
            {
#ifdef USE_GST1
               gstBuff = gst_buffer_new_wrapped_full( 0,
                                                      src->m_queueHead->m_block,
                                                      src->m_queueHead->m_blockSize,
                                                      0,
                                                      src->m_queueHead->m_blockSize,
                                                      src->m_queueHead->m_block,
                                                      gst_http_src_block_free);
#else
               gstBuff = gst_buffer_new();
#endif
               if ( gstBuff )
               {
#ifndef USE_GST1
                  GST_BUFFER_DATA(gstBuff)= src->m_queueHead->m_block;
                  GST_BUFFER_MALLOCDATA(gstBuff)= src->m_queueHead->m_block;
                  GST_BUFFER_SIZE(gstBuff)= src->m_queueHead->m_blockSize;
                  GST_BUFFER_FREE_FUNC(gstBuff)= gst_http_src_block_free;
#endif

                  src->m_readPosition += src->m_queueHead->m_blockSize; 

                  src->m_queueHead->m_blockSize = 0;

                  *outbuf= NULL;
                  /* Pushes a buffer to the peer of pad.
                     In all cases, success or failure, the caller loses its reference to buffer after calling this function. 
                     Accessing local members of the block once the buffer is handed over might be unsafe, using a local copy of the pointer */
                  if ( GST_FLOW_OK != gst_pad_push( src->parent.parent.srcpad, gstBuff ) )
                  {
                     GST_ERROR_OBJECT(src, "gst_http_src_create: Queued PUSHING BUFFER BEFORE EOS - FALIED");
                  }   
                  else 
                  {
                     GST_WARNING_OBJECT(src, "gst_http_src_create: Queued PUSHING BUFFER BEFORE EOS - SUCCESS");
                  }

                  lastBufPushed = TRUE;
                  src->m_queueHead= 0;
                  src->m_queueTail= 0;
                  src->m_queuedByteCount= 0;
                  src->m_currBlockOffset= 0;
               }
               else
               {
                  GST_ERROR_OBJECT(src, "Queued unable to alloc gst buffer");
               }
            }
            pthread_mutex_unlock( &src->m_queueMutex );
         }

         if ( !src->m_eosEventPushed && (src->m_threadStarted || src->m_sessionPaused) )
         {
            src->m_eosEventPushed = gst_pad_push_event( src->parent.parent.srcpad, gst_event_new_eos() );
            GST_WARNING_OBJECT(src, "gst_http_src_create: EoS event pushed to DownStream element - %d", src->m_eosEventPushed);
         }

         ret= GST_FLOW_CUSTOM_SUCCESS;
      }
   }
   
   return ret;
}

static void* gst_http_src_session_thread( void *arg )
{
   GstHttpSrc *src = (GstHttpSrc*)arg;
   errno_t rc = -1;

   src->m_threadRunning= TRUE;
   
   src->m_curl= curl_easy_init();
   if ( src->m_curl )
   {
      int flowTDelayIdx = 0;

      #ifdef ENABLE_SOCKET_LOWAT
      CURL_EASY_SETOPT(src->m_curl, CURLOPT_OPENSOCKETFUNCTION, gst_http_src_opensocket_callback);
      CURL_EASY_SETOPT(src->m_curl, CURLOPT_OPENSOCKETDATA, src);
      #endif
      CURL_EASY_SETOPT(src->m_curl, CURLOPT_PROGRESSFUNCTION, gst_http_src_progress_callback);
      CURL_EASY_SETOPT(src->m_curl, CURLOPT_PROGRESSDATA, src);
      CURL_EASY_SETOPT(src->m_curl, CURLOPT_NOPROGRESS, 0);
      CURL_EASY_SETOPT(src->m_curl, CURLOPT_HEADERFUNCTION, gst_http_src_header_callback);
      CURL_EASY_SETOPT(src->m_curl, CURLOPT_HEADERDATA, src);
      CURL_EASY_SETOPT(src->m_curl, CURLOPT_URL, src->m_location);
      CURL_EASY_SETOPT(src->m_curl, CURLOPT_WRITEFUNCTION, gst_http_src_data_received );
      CURL_EASY_SETOPT(src->m_curl, CURLOPT_WRITEDATA, src);
      CURL_EASY_SETOPT(src->m_curl, CURLOPT_FOLLOWLOCATION, (src->m_automaticRedirect ? 1 : 0));
      CURL_EASY_SETOPT(src->m_curl, CURLOPT_USERAGENT, src->m_userAgent);
      CURL_EASY_SETOPT(src->m_curl, CURLOPT_ERRORBUFFER, src->m_curlErrBuf);
      CURL_EASY_SETOPT(src->m_curl, CURLOPT_FAILONERROR, 1L); //this will make curl report an error when http code greater or equal than 400 is returned
      if ( src->m_cookies )
      {
         int i= 0;
         char *cookie= 0;
         
         do
         {
            cookie= src->m_cookies[i];
            if ( cookie )
            {
               CURL_EASY_SETOPT(src->m_curl, CURLOPT_COOKIELIST, cookie);
            }
            ++i;
         }
         while( cookie );
      }

      CURL_EASY_SETOPT(src->m_curl, CURLOPT_CONNECTTIMEOUT, src->m_timeout);
      CURL_EASY_SETOPT(src->m_curl, CURLOPT_LOW_SPEED_TIME, src->m_timeout);
      CURL_EASY_SETOPT(src->m_curl, CURLOPT_LOW_SPEED_LIMIT, 100);
      GST_WARNING_OBJECT(src, "GSTHTTPSRC: Setting timeout - m_timeout %d", src->m_timeout);

      if ( src->m_proxy )
      {
         CURL_EASY_SETOPT(src->m_curl, CURLOPT_PROXY, src->m_proxy);
         if ( src->m_proxyPassword )
         {
            CURL_EASY_SETOPT(src->m_curl, CURLOPT_PROXYPASSWORD, src->m_proxyPassword);
         }
         if ( src->m_proxyId )
         {
            CURL_EASY_SETOPT(src->m_curl, CURLOPT_PROXYUSERNAME, src->m_proxyId);
         }
      }
      if ( src->m_userPassword )
      {
         CURL_EASY_SETOPT(src->m_curl, CURLOPT_PASSWORD, src->m_userPassword);
      }
      if ( src->m_userId )
      {
         CURL_EASY_SETOPT(src->m_curl, CURLOPT_USERNAME, src->m_userId );
      }
      if ( src->m_extraHeaders )
      {
         if ( !src->m_slist )
         {
            gst_structure_foreach(src->m_extraHeaders, gst_http_src_append_extra_headers, src);
         }
         if ( src->m_slist )
         {
            CURL_EASY_SETOPT(src->m_curl, CURLOPT_HTTPHEADER, src->m_slist);
         }
      }
      if ( src->m_disableProcessSignaling )
      {
          GST_WARNING_OBJECT(src, "GSTHTTPSRC: Setting CURLOPT_NOSIGNAL = 1L");
          CURL_EASY_SETOPT(src->m_curl, CURLOPT_NOSIGNAL, 1L);
      }

      /* Added these prints just to get the Tune Trend; Will be removed once 2.0 is matured */
      GST_WARNING_OBJECT(src, "HTTPSrc: Sent HTTP GET request to the Server");

      CURLcode curl_code= curl_easy_perform(src->m_curl);

      GST_WARNING_OBJECT(src, "HTTPSrc: CURL EASY PERFORM COMPLETE");

      gst_http_src_session_trace(src);

      if ( !src->m_threadStopRequested ) 
      {
         if (curl_code) 
         {
            GST_ERROR_OBJECT(src, "curl session error %d", curl_code);

            if (curl_code == CURLE_HTTP_RETURNED_ERROR)
            {
               long status= 0;
               char httpErrStr[CURL_ERROR_SIZE * 2] = {'\0'};
               curl_easy_getinfo(src->m_curl, CURLINFO_RESPONSE_CODE, &status );

               GST_ERROR_OBJECT(src, "CURLE_HTTP_RETURNED_ERROR Http Err Code - %ld", status);
               GST_ERROR_OBJECT(src, "CURLE_HTTP_RETURNED_ERROR Curl Err Buf  - %s", src->m_curlErrBuf);

               src->m_sessionError= TRUE;
               rc = sprintf_s(httpErrStr, sizeof(httpErrStr), "Curl: Http Err Code - %ld : Curl Err Buf - %s ", status, src->m_curlErrBuf);
               if(rc < EOK)
               {
                  ERR_CHK(rc);
               }				   

               if (status == 404) 
               {
                  rc = strcat_s(httpErrStr,sizeof(httpErrStr), ": CA_ERROR File Not Found");
                  if(rc != EOK)
                  {
                     ERR_CHK(rc);
                  }
                  GST_ERROR_OBJECT(src, "CURLE_HTTP_RETURNED_ERROR - %s", httpErrStr);
                  GST_ELEMENT_ERROR(src, RESOURCE, NOT_FOUND, (httpErrStr), (src->m_curlErrBuf));
               }
               else 
               {
                  GST_ERROR_OBJECT(src, "CURLE_HTTP_RETURNED_ERROR - %s", httpErrStr);
                  GST_ELEMENT_ERROR(src, RESOURCE, FAILED, (httpErrStr), (src->m_curlErrBuf));
               }
            }
            else if (curl_code == CURLE_OPERATION_TIMEDOUT) 
            {
               src->m_sessionError= TRUE;
               src->m_threadStopRequested = TRUE;
               GST_ERROR_OBJECT(src, "CURLE_OPERATION_TIMEDOUT - %s", src->m_curlErrBuf);
               GST_ELEMENT_ERROR(src, RESOURCE, READ, ("A network error occured, or the server closed the connection unexpectedly."), (src->m_curlErrBuf));
            }
            else if (curl_code == CURLE_COULDNT_CONNECT)
            {
               src->m_sessionError= TRUE;
               src->m_threadStopRequested = TRUE;
               GST_ERROR_OBJECT(src, "CURLE_COULDNT_CONNECT - %s", src->m_curlErrBuf);
               GST_ELEMENT_ERROR(src, RESOURCE, OPEN_READ, ("NETWORK ERROR - Failed to connect() to host or proxy"), (src->m_curlErrBuf));
            }
            else if (curl_code == CURLE_COULDNT_RESOLVE_PROXY)
            {
               src->m_sessionError= TRUE;
               src->m_threadStopRequested = TRUE;
               GST_ERROR_OBJECT(src, "CURLE_COULDNT_RESOLVE_PROXY - %s", src->m_curlErrBuf);
               GST_ELEMENT_ERROR(src, RESOURCE, OPEN_READ, ("NETWORK ERROR - Failed to Resolve proxy"), (src->m_curlErrBuf));
            }
            else if (curl_code == CURLE_COULDNT_RESOLVE_HOST)
            {
               src->m_sessionError= TRUE;
               src->m_threadStopRequested = TRUE;
               GST_ERROR_OBJECT(src, "CURLE_COULDNT_RESOLVE_HOST - %s", src->m_curlErrBuf);
               GST_ELEMENT_ERROR(src, RESOURCE, OPEN_READ, ("NETWORK ERROR - Failed to Resolve Host"), (src->m_curlErrBuf));
            }
            else if (curl_code == CURLE_ABORTED_BY_CALLBACK)
            {
               /* libcurl returned a Abort callback Error - Ignore it and log it */
               GST_ERROR_OBJECT(src, "CURLE_ABORTED_BY_CALLBACK - Aborted by User : %s", src->m_curlErrBuf);
               src->m_threadStopRequested = TRUE;
            }
            else if (curl_code == CURLE_RECV_ERROR) 
            {
               /* Failure with receiving network data */ 
               GST_ERROR_OBJECT(src, "CURLE_RECV_ERROR - %s - Treating as EOS", src->m_curlErrBuf);
               src->m_threadStopRequested = TRUE;
            }
            else if (curl_code == CURLE_PARTIAL_FILE) 
            {
               /* A file transfer was shorter or larger than expected. This happens when the server first reports an expected transfer size, 
                  and then delivers data that doesn't match the previously given size. */
               GST_ERROR_OBJECT(src, "CURLE_PARTIAL_FILE - %s - Treating as EOS", src->m_curlErrBuf);
               src->m_threadStopRequested = TRUE;
            }
            else 
            {
               src->m_sessionError= TRUE;
               /*  Post other GST ELEMENT ERROR using error code reported by CURL */
            }
         }  

         /* Pause curl session as we have performed the operation requested of curl */      
         /* Report that the session has been Paused*/
         curl_easy_pause(src->m_curl, CURLPAUSE_ALL);      
         src->m_sessionPaused= TRUE;
      }

      pthread_mutex_lock( &src->m_flowTimerMutex );
      for ( flowTDelayIdx = 0; !src->m_flowTimerThreadStarted && (flowTDelayIdx < FLOW_TIMER_MAX_WAIT_ITERATIONS); flowTDelayIdx++ )
      {
         pthread_mutex_unlock( &src->m_flowTimerMutex ); //Release to allow m_flowTimerThreadStarted to be changed.
         if(0 == (flowTDelayIdx % (FLOW_TIMER_MAX_WAIT_ITERATIONS/4))) //Log 4 times during the wait.
            GST_WARNING_OBJECT(src, "GSTHTTPSRC: Yielding Session Thread as FlowTimer has not started");
         pthread_yield();
         usleep(FLOW_TIMER_WAIT_SLEEP_INTERVAL_US);
         // 10ms sleep looped 200 times.
         // Maximum delay for flowtimer to start 2 seconds
         pthread_mutex_lock( &src->m_flowTimerMutex );
      }
      if(FLOW_TIMER_MAX_WAIT_ITERATIONS == flowTDelayIdx)
         GST_WARNING_OBJECT(src, "%s: expect flow timer to block indefinitely.", __func__);
      /* wakeup flow timer thread before join */
      pthread_cond_signal( &src->m_flowTimerCond );
      pthread_mutex_unlock( &src->m_flowTimerMutex );

      pthread_join( src->m_flowTimerThread, NULL );

      src->m_flowTimerThreadStarted= FALSE;

      src->m_threadStarted= FALSE;
      
      /* wakeup src thread which might be blocked in create */      
      pthread_mutex_lock( &src->m_queueMutex );
      pthread_mutex_lock( &src->m_queueNotEmptyMutex );
      pthread_mutex_unlock( &src->m_queueMutex );
      pthread_cond_signal( &src->m_queueNotEmptyCond );
      pthread_mutex_unlock( &src->m_queueNotEmptyMutex );

      curl_easy_cleanup(src->m_curl);
      src->m_curl= 0;
   }
   else
   {
      GST_ERROR_OBJECT(src, "curl_easy_init failed");
   }
   
   return NULL;
} 

static void* gst_http_src_flow_timer_thread( void *arg )
{
   GstHttpSrc *src;

   src= (GstHttpSrc*)arg;
   
   pthread_mutex_lock( &src->m_flowTimerMutex );
   src->m_flowTimerThreadStarted= TRUE;
   if( !src->m_threadStopRequested )
   {
      /* wait for first chunk received */      
      pthread_cond_wait( &src->m_flowTimerCond, &src->m_flowTimerMutex );
   }
   pthread_mutex_unlock( &src->m_flowTimerMutex );
   
   
   while( !src->m_threadStopRequested )
   {
      struct timespec time;
      struct timeval curTime;
      int timeout= MAX_WAIT_TIME_MS;
      int rc;
      
      gettimeofday(&curTime, NULL);
      time.tv_nsec= curTime.tv_usec * 1000 + (timeout % 1000) * 1000000;
      time.tv_sec= curTime.tv_sec + (timeout / 1000);
      if (time.tv_nsec > 1000000000)
      {
         time.tv_nsec -= 1000000000;
         time.tv_sec++;
      }

      pthread_mutex_lock( &src->m_flowTimerMutex );
      rc= pthread_cond_timedwait(&src->m_flowTimerCond, &src->m_flowTimerMutex, &time);
      pthread_mutex_unlock( &src->m_flowTimerMutex );
      
      if ( rc == ETIMEDOUT )
      {
         src->m_waitTimeExceeded= TRUE;
         /* signal http thread that flow timer has expired */
         gst_http_src_data_received((void*)0, 0, 0, src);
      }
   }

   return NULL;
}

static curl_socket_t gst_http_src_opensocket_callback(void *clientp, curlsocktype purpose, struct curl_sockaddr *address)
{
   int socket_fd= -1;
   int lowWaterMark= CURL_MAX_WRITE_SIZE/2;
   int rc;
   GstHttpSrc *src;

   src= (GstHttpSrc*)clientp;

   /* Set the Water Mark as 2K for low Bitrate Content */
   if (src->m_isLowBitRateContent) {
      lowWaterMark= CURL_MAX_WRITE_SIZE/8;
   }

   if( src->m_redirectExpected == TRUE ) {
      lowWaterMark = DEFAULT_SO_RCVLOWAT;
   }
   
   GST_DEBUG_OBJECT(src, "gst_http_src_opensocket_callback: purpose %d family %d socktype %d protocol %d, LowWaterMark %d",
                    purpose, address->family, address->socktype, address->protocol, lowWaterMark);
   socket_fd= socket(address->family, address->socktype, address->protocol);
   if (socket_fd < 0) {    //CID : 18631 - Check for poitive value of socket_fd
        GST_ERROR_OBJECT( src, "socket error %d creating socket for socket_fd ", socket_fd );
   } else {
        GST_DEBUG_OBJECT(src, "gst_http_src_opensocket_callback: create socket_fd %d", socket_fd);
        GST_DEBUG_OBJECT(src, "gst_http_src_opensocket_callback: calling setsockopt SO_RCVLOWAT for socket_fd %d LowWaterMark %d", socket_fd, lowWaterMark);
        rc= setsockopt( socket_fd, SOL_SOCKET, SO_RCVLOWAT, &lowWaterMark, sizeof(int) );
        if (rc < 0) {
           GST_ERROR_OBJECT( src, "setsockopt error %d setting SO_RCVLOWAT for socket_fd %d", rc, socket_fd );
        }
   }
   src->m_socketFD = socket_fd;
   return socket_fd;
}

static int gst_http_src_progress_callback(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow)
{
   int rc= 0;
   GstHttpSrc *src;

   src= (GstHttpSrc*)clientp;
   
   if (src->m_threadStopRequested)
   {
      /* Trigger end of session */
      rc= -1;
   }   
   if ( src->m_sessionPaused && src->m_sessionUnPause )
   {
      /* resume session */
      // TODO: curl_easy_perform triggers progress callback
      // If needed to resume the session do it from somewhere else, not as part of the 
      // callback function which is triggered from curl_easy_perform
      // curl_easy_pause(src->m_curl, CURLPAUSE_ALL);      
      src->m_sessionPaused= FALSE;
      src->m_sessionUnPause= FALSE;
   }
   
   return rc;
}

static size_t gst_http_src_header_callback(char *buffer, size_t size, size_t nitems, void *userData)
{
   int len= size*nitems;
   GstHttpSrc *src;
   GstBaseSrc *basesrc;
   long status= 0;
   double contentLength= -1.0;
   gchar *value= 0;

   src= (GstHttpSrc*)userData;

   curl_easy_getinfo(src->m_curl, CURLINFO_RESPONSE_CODE, &status );

   GST_DEBUG_OBJECT(src, "gst_http_src_header_callback: status %ld, %d bytes", status, len );
   if ( buffer && len )
   {
      GST_DEBUG_OBJECT(src, "Header: %.*s", len, buffer );  
   }
   
   if ((status == 407) && src->m_proxyId && src->m_proxyPassword)
   {
      goto exit;
   }

   if (status == 401)
   {
      goto exit;
   }

   if ( src->m_haveHeaders || src->m_haveFirstData )
   {
      gboolean trailerOfInterest= FALSE;
      gchar *trailer;
      gint trailerDataLen= len;
      gboolean posted;
      
      trailer= g_strstr_len(buffer,len,":");
      if ( trailer )
      {
         ++trailer;;
         trailerDataLen= len-(trailer-buffer);
         if ( trailerDataLen )
         {
            GST_DEBUG_OBJECT(src, "Got Trailer");
            GST_DEBUG_OBJECT(src, "Trailer: Data = %.*s, Length = %d", trailerDataLen, trailer, trailerDataLen );
            
            if ( trailerDataLen > 0 )
            {
               gst_http_src_set_trailer(src, trailer, trailerDataLen);
               if ( src->m_numTrailerHeadersExpected > 0 )
               {
                  --src->m_numTrailerHeadersExpected;
                  if ( src->m_numTrailerHeadersExpected == 0)
                  {
                     trailerOfInterest= TRUE;
                  }
               }
            }

            if ( trailerOfInterest )
            {
               posted= gst_element_post_message( GST_ELEMENT(src), 
                                                      gst_message_new_custom(
                                                              GST_MESSAGE_ELEMENT, 
                                                              GST_OBJECT(src), 
                                                              gst_structure_new(
                                                                     "http/trailer",
                                                                     "valid", 
                                                                     G_TYPE_BOOLEAN, 
                                                                     trailerOfInterest,
                                                                     NULL
                                                                     )
                                                              )
                                                ); 
               if ( posted )
               {
                  GST_DEBUG_OBJECT(src, "Posted trailer message on bus");

                  /* Once trailer is posted on bus disable low speed timeout limit and set to inifite timeout */ 
                  /* Connect timeout need not be reset */
                  CURL_EASY_SETOPT(src->m_curl, CURLOPT_LOW_SPEED_TIME, 0);
                  CURL_EASY_SETOPT(src->m_curl, CURLOPT_LOW_SPEED_LIMIT, 0);
                  GST_WARNING_OBJECT(src, "GSTHTTPSRC: Changing timeout - m_timeout %d", 0);
               }
               
               curl_easy_pause(src->m_curl, CURLPAUSE_ALL);      
               src->m_sessionPaused= TRUE;
               src->m_haveTrailers= TRUE;
               
               /* wakeup src thread which might be blocked in create */      
               pthread_mutex_lock( &src->m_queueMutex );
               pthread_mutex_lock( &src->m_queueNotEmptyMutex );
               pthread_mutex_unlock( &src->m_queueMutex );
               pthread_cond_signal( &src->m_queueNotEmptyCond );
               pthread_mutex_unlock( &src->m_queueNotEmptyMutex );
            }
         }
      }      
   }
      
   if(buffer != NULL) {
      if ( (len == 2) && (buffer[0] == '\r') && (buffer[1] == '\n')  )
      {
         src->m_haveHeaders= TRUE;
      }
   }  //CID:18723 - Forward null
   
   curl_easy_getinfo(src->m_curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &contentLength );
   if ( contentLength >= 0 )
   {
      guint64 newSize;
      
      newSize= src->m_requestPosition + (guint64)contentLength;

      if ( !src->m_haveSize || (src->m_contentSize != newSize) ) 
      {
         src->m_contentSize= newSize;
         src->m_haveSize= TRUE;
         src->m_isSeekable= TRUE;
         GST_DEBUG_OBJECT(src, "size = %" G_GUINT64_FORMAT, src->m_contentSize);

         basesrc= GST_BASE_SRC_CAST(src);

#ifdef USE_GST1
         basesrc->segment.duration = src->m_contentSize;
#else
         gst_segment_set_duration(&basesrc->segment, GST_FORMAT_BYTES, src->m_contentSize);
#endif
         gst_element_post_message(GST_ELEMENT(src),
                                  gst_message_new_duration(GST_OBJECT(src), GST_FORMAT_BYTES, src->m_contentSize));
      }
   }

   if ( g_strstr_len(buffer, len, "Content-Type:") != NULL )
   {
      curl_easy_getinfo(src->m_curl, CURLINFO_CONTENT_TYPE, &value );
      if ( value )
      {
         char *match;

         if ( src->m_contentType )
         {
            g_free(src->m_contentType);
            src->m_contentType= NULL;
         }


         src->m_contentType= g_strndup(value, 255);     // RFC-4288

         GST_DEBUG_OBJECT(src, "Content-Type: %s", value);

         if (src->m_caps)
         {
            gst_caps_unref(src->m_caps);
         }

         match= g_strrstr( src->m_contentType, "video/" );
         if ( match )
         {
            if (g_ascii_strcasecmp(match, "video/vnd.dlna.mpeg-tts") == 0)
            {
               src->m_caps= gst_caps_new_simple ("video/vnd.dlna.mpeg-tts",
                                                  "systemstream", G_TYPE_BOOLEAN, TRUE,
                                                  "packetsize", G_TYPE_INT, 192,NULL);
#ifdef USE_GST1
               gst_base_src_set_caps (GST_BASE_SRC (src), src->m_caps);
#endif
            }
            else if (g_ascii_strcasecmp(match, "video/mpeg") == 0)
            {
               src->m_caps= gst_caps_new_simple ("video/mpegts",
                                                 "systemstream", G_TYPE_BOOLEAN, TRUE,
                                                 "packetsize", G_TYPE_INT, 188,NULL);
#ifdef USE_GST1
               gst_base_src_set_caps (GST_BASE_SRC (src), src->m_caps);
#endif
            }
            else
            {
               if (src->m_caps)
               {
#ifdef USE_GST1
                  src->m_caps = gst_caps_make_writable (src->m_caps);
                  gst_caps_set_simple( src->m_caps, "content-type", G_TYPE_STRING, src->m_contentType, NULL );
                  gst_base_src_set_caps (GST_BASE_SRC (src), src->m_caps);
#else
                  gst_caps_set_simple( src->m_caps, "content-type", G_TYPE_STRING, src->m_contentType, NULL );
#endif
               }
            }
         }
      }
   }

   if(buffer != NULL)
   {
      if ( strncmp( buffer, "PresentationTimeStamps.ochn.org:", 32 ) == 0 )
      {
         gulong startPTS = 0, endPTS = 0;
         if ( sscanf(&buffer[33], "startPTS=%08lx endPTS=%08lx", &startPTS, &endPTS) == 2 )
         {
            src->m_startPTS= startPTS;
            src->m_endPTS= endPTS;
         }
      }
   }   //CID:18723 - forward null 

   if ( strncmp( buffer, "FramesPerGOP.schange.com:", 25 ) == 0 )
   {
      src->m_gopSize= g_ascii_strtoull(&buffer[26],NULL,10) ;
   }

   if ( strncmp( buffer, "BFramesPerGOP.schange.com:", 26 ) == 0 )
   {
      src->m_numBFramesPerGOP= g_ascii_strtoull(&buffer[27],NULL,10) ;
   }

   if ( strncmp( buffer, "availableSeekRange.dlna.org:", 28 ) == 0 )
   {
      guint hour= 0, min= 0, sec= 0;
      if( sscanf(&buffer[29], "0 npt=00:00:00-%u:%u:%u", &hour, &min, &sec) == 3 )
      {
         src->m_contentLength= (hour * 60 * 60) + (min * 60) + sec;
      }
      GST_DEBUG_OBJECT(src, "content npt is: %.*s (parsed: %lu)", (len-29), &buffer[29], src->m_contentLength);
   }

   if ( strncmp( buffer, "Trailer:", 8 ) == 0 )
   {
      gchar *headerData= &buffer[9];
      gint headerLen= len-9;
      gchar *headerDataLeft= headerData;
      if ( headerLen )
      {
         src->m_numTrailerHeadersExpected= 1;
         while ( headerDataLeft && headerLen )
         {
            headerDataLeft= g_strstr_len( headerData, headerLen, "," );
            if ( headerDataLeft )
            {
               ++src->m_numTrailerHeadersExpected;
               headerLen -= (headerDataLeft-headerData);
               headerData= headerDataLeft+1;
            }
         }
      }
      GST_DEBUG_OBJECT(src, "expect %d trailer headers", src->m_numTrailerHeadersExpected );
   }
   
   if ( (status >= 400) && (status < 500) )
   {
      GST_ERROR_OBJECT(src, "client error %ld", status);
      len= 0;
   }
   else if ( (status >= 500) && (status < 600) )
   {
      GST_ERROR_OBJECT(src, "server error %ld", status);
      len= 0;
   }

exit:   
   return len;
}

static size_t gst_http_src_data_received(void *ptr, size_t size, size_t nmemb, void *userData)
{
   GstHttpSrc *src;
   GstBaseSrc *basesrc;
   int allocSize, hdrOffset;
   guchar *block= 0;
   int recvSize, recvOffset;
   int blockSize, blockOffset, blockAvail;
   int consumed, copySize;
   long long timeSinceLastPacket;
   errno_t rc = -1;
      
   src= (GstHttpSrc*)userData;
   basesrc= GST_BASE_SRC_CAST(src);

   pthread_mutex_lock( &src->m_flowTimerMutex );

   recvSize= size*nmemb;
   GST_TRACE_OBJECT(src, "gst_http_src_data_received: enter: %d bytes", recvSize);
   recvOffset= 0;

   consumed= 0;
   
   if ( ptr )
   {
      if ( src->m_threadStarted && !src->m_threadStopRequested )
      {
         if (!src->m_haveFirstData)
         {
            /* Added these prints just to get the Tune Trend; Will be removed once 2.0 is matured */
            GST_WARNING_OBJECT(src, "HTTPSrc: First Buffer Received from the Server");
            /* indicate to flow timer we received the first buffer */            
            pthread_cond_signal( &src->m_flowTimerCond );      
         }
         src->m_haveFirstData= TRUE;
         
         while( recvOffset < recvSize )
         {
            if ( src->m_currBlock )
            {
               block= src->m_currBlock;
               blockSize= src->m_currBlockSize;
               blockOffset= src->m_currBlockOffset;
               src->m_currBlock= 0;
            }
            else
            {
               blockSize= basesrc->blocksize;
               blockOffset= 0;
               
               hdrOffset= ((blockSize+15)&~15);
               allocSize= hdrOffset+sizeof(GstHttpSrcBlockQueueElement);
               
               block= (unsigned char*)malloc( allocSize*sizeof(unsigned char) );
               if ( !block )
               {
                  GST_ERROR_OBJECT(src, "unable to allocate block for %d (%d) bytes", blockSize, allocSize);
                  consumed= 0;
                  goto exit;
               }
            }
            
            blockAvail= blockSize-blockOffset;
            copySize= recvSize-recvOffset;
            if ( copySize > blockAvail )
            {
               copySize= blockAvail;
            }
            rc = memcpy_s( &block[blockOffset], blockAvail, &((guchar*)ptr)[recvOffset], copySize );
            if(rc != EOK)
            {
               ERR_CHK(rc);
               goto exit;
            }

            recvOffset += copySize;
            consumed += copySize;
            
            blockOffset += copySize;
            if ( blockOffset < blockSize )
            {
               assert( recvOffset == recvSize );
               src->m_currBlock= block;
               src->m_currBlockSize= blockSize;
               src->m_currBlockOffset= blockOffset;
            }
            else
            {
               if ( !gst_http_src_buffer_ready( src, block, blockSize, blockOffset ) )
               {
                  consumed= 0;
                  goto exit;
               }
               block= 0;
            }      
         }   
      }

      #ifdef ENABLE_READ_DELAY
      recvSize= size*nmemb;
      if ( recvSize == consumed )
      {
         /* 
          * For network interfaces with small MTU sizes, sleep here for up to
          * half the CURL_MAX_WRITE_SIZE interval to prevent getting a lot of
          * small blocks of data.
          */
         #define MAX_BITRATE_BYTES_PER_SEC ((15*1024*1024)/8.0)
         #define MAX_DELAY ((int) ((CURL_MAX_WRITE_SIZE*1.0E6) / (MAX_BITRATE_BYTES_PER_SEC*2.0)) )
         if ( recvSize >= CURL_MAX_WRITE_SIZE/2 )
         {
            if ( src->m_readDelay >= 100 )
            {
               src->m_readDelay -= 100;
            }
            else
            {
               src->m_readDelay= 0;
            }
         }
         else if ( (recvSize > 256) && (recvSize < 2000) )
         {
            if ( src->m_readDelay < MAX_DELAY )
            {
               src->m_readDelay += 100;
            }
         }
         if ( src->m_readDelay )
         {
            usleep( src->m_readDelay );
         }
      }
      #endif      
   }
   else if ( src->m_waitTimeExceeded )
   {
      /* flow timer has expired, consider current buffer, if any, complete */
      if ( src->m_currBlock )
      {
         block= src->m_currBlock;
         blockSize= src->m_currBlockSize;
         blockOffset= src->m_currBlockOffset;
         src->m_currBlock= 0;

         if  ( gst_http_src_buffer_ready( src, block, blockSize, blockOffset ) )
         {
            block= 0;
         }
      }      
      src->m_waitTimeExceeded= FALSE;
   }
   
exit:

   pthread_mutex_unlock( &src->m_flowTimerMutex );

   if ( consumed == 0 )
   {
      if ( block )
      {
         free( block );
      }

      if ( ptr && recvSize && src->m_threadStarted && src->m_threadStopRequested )
      {
         return recvSize;
      }
   }

   return consumed;
}

static gboolean gst_http_src_buffer_ready(GstHttpSrc *src, guchar *block, int blockSize, int blockOffset )
{
   gboolean rc= TRUE;
   GstBaseSrc *basesrc;
   int hdrOffset;
   int wasEmpty;
   GstHttpSrcBlockQueueElement *newBlock;

   basesrc= GST_BASE_SRC_CAST(src);

   /* indicate to flow timer we received a chunk */            
   pthread_cond_signal( &src->m_flowTimerCond );      
   
   hdrOffset= ((blockSize+15)&~15);
   newBlock= (GstHttpSrcBlockQueueElement*)(block+hdrOffset);
   newBlock->m_block= block;
   newBlock->m_blockSize= blockOffset;
   newBlock->m_next= 0;
   
   pthread_mutex_lock( &src->m_queueMutex );
   while ( src->m_queuedByteCount > basesrc->blocksize*4 )
   {
      if ( src->m_threadStopRequested )
      {
         pthread_mutex_unlock( &src->m_queueMutex );
         rc= FALSE;
         goto exit;
      }

      /* wait for queue to become not full */
      pthread_mutex_lock( &src->m_queueNotFullMutex );
      pthread_mutex_unlock( &src->m_queueMutex );
      pthread_cond_wait( &src->m_queueNotFullCond, &src->m_queueNotFullMutex );
      pthread_mutex_unlock( &src->m_queueNotFullMutex );
      
      if ( src->m_threadStopRequested )
      {
         rc= FALSE;
         goto exit;
      }

      pthread_mutex_lock( &src->m_queueMutex );
   }
   src->m_queuedByteCount += newBlock->m_blockSize;
   wasEmpty= src->m_queueTail ? 0 : 1;
   if ( src->m_queueTail )
   {
      src->m_queueTail->m_next= newBlock;
   }
   else
   {
      src->m_queueHead= newBlock;
   }
   src->m_queueTail= newBlock;

   if ( wasEmpty )
   {
      /* indicate queue is not empty */            
      pthread_mutex_lock( &src->m_queueNotEmptyMutex );
      pthread_mutex_unlock( &src->m_queueMutex );
      pthread_cond_signal( &src->m_queueNotEmptyCond );
      pthread_mutex_unlock( &src->m_queueNotEmptyMutex );
   }
   else
   {
      pthread_mutex_unlock( &src->m_queueMutex );
   }
   
exit:

   return rc;   
}

static void gst_http_src_flush_queue(GstHttpSrc *src)
{
   pthread_mutex_lock( &src->m_queueMutex );
   while ( src->m_queueHead )
   {
       GstHttpSrcBlockQueueElement *elmtFree= src->m_queueHead;
       src->m_queueHead= src->m_queueHead->m_next;
       if ( elmtFree->m_block ) 
       {
          free( elmtFree->m_block );
       }
   }
   src->m_queueHead= 0;
   src->m_queueTail= 0;
   src->m_queuedByteCount= 0;
   src->m_currBlockOffset= 0;
   pthread_mutex_unlock( &src->m_queueMutex );
}

static void gst_http_src_block_free(void *blockToFree)
{
   if (blockToFree) 
   {
      free(blockToFree);
   }
}

static gboolean gst_http_src_append_extra_headers(GQuark field_id, const GValue *value, gpointer userData)
{
   gboolean rc= TRUE;
   GstHttpSrc *src;
   int i, count= 1;
   const GValue *v;
   enum valueTypes { valueList= 0, valueArray= 1, valueOther= 2 };
   const GValue* (*get_value)(const GValue *val, guint n)= 0;
   const gchar *field_name= g_quark_to_string (field_id);
   gchar *field_content;
   errno_t safec_rc = -1;

   src= (GstHttpSrc*)userData;
   
   if ( G_VALUE_TYPE(value) == GST_TYPE_LIST )
   {
      count= gst_value_list_get_size(value);
      get_value= gst_value_list_get_value;
   }
   else if ( G_VALUE_TYPE(value) == GST_TYPE_ARRAY )
   {
      count= gst_value_array_get_size(value);
      get_value= gst_value_array_get_value; 
   }
   else
   {
      count= 1;
   }
   
   for( i= 0; i < count; ++i )
   {
      field_content= NULL;
      
      if ( get_value )
      {
         v= (*get_value)( value, i );
      }
      else
      {
         v= value;
      }

      if (G_VALUE_TYPE(v) == G_TYPE_STRING) 
      {
         field_content= g_value_dup_string(v);
      } 
      else 
      {
         GValue dest = { 0, };

         g_value_init(&dest, G_TYPE_STRING);
         if (g_value_transform(value, &dest)) 
         {
            field_content= g_value_dup_string(&dest);
         }
      }

      if (field_content == NULL) 
      {
         GST_ERROR_OBJECT(src, "extra-headers field '%s' contains no value or can't be converted to a string", field_name);
         rc= FALSE;
      }

      GST_DEBUG_OBJECT(src, "adding extra header: \"%s: %s\"", field_name, field_content);
      
      safec_rc = sprintf_s( src->m_work, sizeof(src->m_work), "%s: %s", field_name, field_content );
      if(safec_rc < EOK)
      {
          ERR_CHK(safec_rc);
          g_free(field_content);
          return FALSE;
      }
      src->m_slist= curl_slist_append(src->m_slist, src->m_work );
      
      g_free(field_content);
   }

   return rc;
}

static void gst_http_src_session_trace(GstHttpSrc *src)
{
   double total, connect, startTransfer, resolve, appConnect, preTransfer, redirect; 

   if (src && src->m_curl && src->m_location)
   {
      curl_easy_getinfo(src->m_curl, CURLINFO_NAMELOOKUP_TIME, &resolve);
      curl_easy_getinfo(src->m_curl, CURLINFO_CONNECT_TIME, &connect);
      curl_easy_getinfo(src->m_curl, CURLINFO_APPCONNECT_TIME, &appConnect);
      curl_easy_getinfo(src->m_curl, CURLINFO_PRETRANSFER_TIME, &preTransfer);
      curl_easy_getinfo(src->m_curl, CURLINFO_STARTTRANSFER_TIME, &startTransfer);
      curl_easy_getinfo(src->m_curl, CURLINFO_TOTAL_TIME , &total);
      curl_easy_getinfo(src->m_curl, CURLINFO_REDIRECT_TIME, &redirect);

      GST_WARNING_OBJECT(src,"HTTPSrc: HttpRequestEnd %s times={total=%g, connect=%g startTransfer=%g resolve=%g, appConnect=%g, preTransfer=%g, redirect=%g}", 
         src->m_location, total, connect, startTransfer, resolve, appConnect, preTransfer, redirect);
   }
}


/** @} */
/** @} */
