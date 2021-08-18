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
 * SECTION:element-dtcpdec
 *
 * Initiate AKE with source at the time of element make and do decrypt data. 
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! dtcpdec dtcp_src_ip=<dtcp_source_ip> dtcp_port=<dtcp_port> ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */



/**
* @defgroup gst-plugins-rdk
* @{
* @defgroup dtcpdecrypt
* @{
**/


#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <stdlib.h>
#include <string.h>
#include "safec_lib.h"

#include "gstdtcpdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_dtcp_dec_debug);
#define GST_CAT_DEFAULT gst_dtcp_dec_debug
#define GST_DTCPDEC_ERR_EVENT 0x0800
#define MAX_ERR_STR_LENGTH  64

//#define DATA_BUF_SIZE 5*1024

static GstStateChangeReturn gst_dtcp_dec_change_state (GstElement *element, GstStateChange transition);

static unsigned int packet_count;
static GMutex* packet_count_mutex;

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT,
  PROP_AKE_RESULT,
  PROP_DTCP_SRC_IP,
  PROP_DTCP_PORT,
  PROP_BUFFER_SIZE,
  PROP_LOG_LEVEL
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY"));
    

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ( "video/vnd.dlna.mpeg-tts, "
                      "  mpegversion = (int) 2,"
                      "  systemstream = (boolean) true,"
                      "  packetsize = (int) 192;"
                      "video/mpegts, "
                      "  mpegversion = (int) 2,"
                      "  systemstream = (boolean) true,"
                      "  packetsize = (int) 188"));


#ifdef USE_GST1
#define gst_dtcp_dec_parent_class parent_class
G_DEFINE_TYPE (GstDtcpDec, gst_dtcp_dec, GST_TYPE_ELEMENT);
#else
GST_BOILERPLATE (GstDtcpDec, gst_dtcp_dec, GstElement,
    GST_TYPE_ELEMENT);
#endif

static void gst_dtcp_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dtcp_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void dtcp_error(char *err, char *str)
{
    errno_t rc = -1;

    rc = strcpy_s(err, MAX_ERR_STR_LENGTH, str);
    if (rc != EOK)
    {
        ERR_CHK(rc);
    }
}

#ifdef USE_GST1
static GstFlowReturn gst_dtcp_dec_chain (GstPad * pad, GstObject* parent,
    GstBuffer * buf);
#else
static GstFlowReturn gst_dtcp_dec_chain (GstPad * pad, GstBuffer * buf);
#endif

#define DTCP_ERR_DECRYPT (-15)

static int getErrCode(dtcp_result_t result)
{
	int retVal = GST_DTCPDEC_ERR_EVENT;
	switch (result)
	{
		case DTCP_ERR_NOT_INITIALIZED:     /**< DTCP Manager not initialized yet.    */
			retVal = GST_DTCPDEC_ERR_EVENT + 1;
			break;
    	case DTCP_ERR_INVALID_PARAM:       /**< Invalid parameter supplied.          */
			retVal = GST_DTCPDEC_ERR_EVENT + 2;
			break;
    	case DTCP_ERR_GENERAL:             /**< General unspecified error.           */
			retVal = GST_DTCPDEC_ERR_EVENT + 3;
			break;
    	case DTCP_ERR_MEMORY_ALLOC:        /**< Memory allocation failure.           */
			retVal = GST_DTCPDEC_ERR_EVENT + 4;
			break;
    	case DTCP_ERR_OUT_OF_SESSIONS:     /**< Too many active sessions.            */
			retVal = GST_DTCPDEC_ERR_EVENT + 5;
			break;
    	case DTCP_ERR_INVALID_CERTIFICATE: /**< Invalid certificate.                 */
			retVal = GST_DTCPDEC_ERR_EVENT + 6;
			break;
    	case DTCP_ERR_AKE:                 /**< Authorization/Key Exchange error.    */
			retVal = GST_DTCPDEC_ERR_EVENT + 7;
			break;
    	case DTCP_ERR_CONT_KEY_REQ:        /**< Content key error.                   */
			retVal = GST_DTCPDEC_ERR_EVENT + 8;
			break;
    	case DTCP_ERR_INVALID_IP_ADDRESS:  /**< Invalid IP address supplied.         */
			retVal = GST_DTCPDEC_ERR_EVENT + 9;
			break;
    	case DTCP_ERR_SERVER_NOT_REACHABLE: /**< DTCP Server not reachable.           */ 
			retVal = GST_DTCPDEC_ERR_EVENT + 10;
			break;
		case DTCP_ERR_DECRYPT:
			retVal = GST_DTCPDEC_ERR_EVENT + 11;
			break;
		default:
			retVal = GST_DTCPDEC_ERR_EVENT;
			break;
	}
	return retVal;
}

static void getErrStr(dtcp_result_t result, char *err_str)
{
	errno_t rc = -1;
	switch (result)
	{
		case DTCP_ERR_NOT_INITIALIZED:     /**< DTCP Manager not initialized yet.    */
			dtcp_error(err_str, "DTCP_ERR_NOT_INITIALIZED");
			break;
    	case DTCP_ERR_INVALID_PARAM:       /**< Invalid parameter supplied.          */
			dtcp_error(err_str, "DTCP_ERR_INVALID_PARAM");
			break;
    	case DTCP_ERR_GENERAL:             /**< General unspecified error.           */
			dtcp_error(err_str, "DTCP_ERR_GENERAL");
			break;
    	case DTCP_ERR_MEMORY_ALLOC:        /**< Memory allocation failure.           */
			dtcp_error(err_str, "DTCP_ERR_MEMORY_ALLOC");
			break;
    	case DTCP_ERR_OUT_OF_SESSIONS:     /**< Too many active sessions.            */
			dtcp_error(err_str, "DTCP_ERR_OUT_OF_SESSIONS");
			break;
    	case DTCP_ERR_INVALID_CERTIFICATE: /**< Invalid certificate.                 */
			dtcp_error(err_str, "DTCP_ERR_INVALID_CERTIFICATE");
			break;
    	case DTCP_ERR_AKE:                 /**< Authorization/Key Exchange error.    */
			dtcp_error(err_str, "DTCP_ERR_AKE");
			break;
    	case DTCP_ERR_CONT_KEY_REQ:        /**< Content key error.                   */
			dtcp_error(err_str, "DTCP_ERR_CONT_KEY_REQ");
			break;
    	case DTCP_ERR_INVALID_IP_ADDRESS:  /**< Invalid IP address supplied.         */
			dtcp_error(err_str, "DTCP_ERR_INVALID_IP_ADDRESS");
			break;
    	case DTCP_ERR_SERVER_NOT_REACHABLE: /**< DTCP Server not reachable.           */
			dtcp_error(err_str, "DTCP_ERR_SERVER_NOT_REACHABLE");
			break;
    	case DTCP_ERR_DECRYPT: 
			dtcp_error(err_str, "DTCP_PCP_HEADER_DECODE_FAILURE");
			break;
		default:
			dtcp_error(err_str, "DTCP_ERROR");
	}
}

static void onError(GstDtcpDec *filter, int err_code, char* err_string)
{
	GError *error = g_error_new (GST_CORE_ERROR, err_code, err_string);
	if(error == NULL)
	{
		GST_ERROR_OBJECT(filter, "error null");
		return;
	}

	GstElement *parent= GST_ELEMENT_PARENT(filter);
	GstMessage *message = gst_message_new_error (GST_OBJECT(parent), error, err_string);	
	if(message == NULL)
	{
		GST_ERROR_OBJECT(filter, "DTCP Error message creation failed");
		if(error)
			g_error_free(error);
		return;
	}
	if(GST_MESSAGE_TYPE (message) != GST_MESSAGE_ERROR)
		GST_ERROR_OBJECT(filter, "DTCP Error message type failed");
	if (GST_MESSAGE_SRC (message) == NULL)
		GST_ERROR_OBJECT(filter, "DTCP Error message src not found");
	if (gst_element_post_message(GST_ELEMENT (filter), message) == FALSE)
	{
		GST_ERROR_OBJECT(filter, "This element has no bus, therefore no message sent!");
	}
	if(error)
		g_error_free (error);
}

#ifdef USE_GST1
static gboolean gst_dtcp_dec_sink_event (GstPad *pSinkPad, GstObject *pParentObj, GstEvent *pEvent)
{
    GstDtcpDec *filter = GST_DTCPDEC (pParentObj);
    gboolean rc = TRUE;
    /* The following event switch case is just to add debug logs. Not to take any action
     * at this point in time. Let the default handler handle it
     */
    switch(GST_EVENT_TYPE(pEvent))
    {
        case GST_EVENT_EOS:
            GST_WARNING_OBJECT(filter, "DTCP Decrypt received EOS event and letting Gstreamer to handle it");
            /* Fall thro' */
        default:
            rc = gst_pad_event_default(pSinkPad, pParentObj, pEvent);
            break;
    }
    return rc;
}
#endif

static GstStateChangeReturn
gst_dtcp_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GstPad *pad =  gst_element_get_static_pad (element, "sink");
  GstDtcpDec *filter = GST_DTCPDEC (GST_OBJECT_PARENT (pad));

  GST_INFO_OBJECT(element, "change state from %s to %s\n",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  ret = GST_STATE_CHANGE_SUCCESS;
  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
        GST_INFO_OBJECT(element, "GST_STATE_CHANGE_NULL_TO_READY\n");
		if(0 == filter->pDtcpSession)
		{
			//Create DTCPSinkSession
        	GST_INFO_OBJECT(element, "Creating DTCP Session - initiating AKE to server %s:%d\n", filter->dtcp_src_ip, filter->dtcp_port);
			dtcp_result_t retVal = DTCPMgrCreateSinkSession(filter->dtcp_src_ip, filter->dtcp_port, FALSE, filter->buffersize, &filter->pDtcpSession);
			if(DTCP_SUCCESS != retVal)
			{
				filter->pDtcpSession = 0;
				filter->dtcp_element_bypass = TRUE;
				char err_str[MAX_ERR_STR_LENGTH];
				getErrStr(retVal, err_str);
				onError(filter, getErrCode(retVal), err_str);
				ret = GST_STATE_CHANGE_FAILURE;
				goto failure;
			}
		} 
		g_mutex_lock (packet_count_mutex);
		packet_count = 0;
		g_mutex_unlock (packet_count_mutex);
		filter->isFirstPacket = TRUE;
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
         GST_INFO_OBJECT(element, "GST_STATE_CHANGE_READY_TO_PAUSED\n");
      break;
    }

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
         GST_INFO_OBJECT(element, "GST_STATE_CHANGE_PAUSED_TO_PLAYING\n");
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
    GST_ERROR_OBJECT(element, "change state failure\n");
    goto failure;
  }     
  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
         GST_INFO_OBJECT(element, "GST_STATE_CHANGE_PLAYING_TO_PAUSED\n");
      break;
  
    case GST_STATE_CHANGE_PAUSED_TO_READY:
         GST_INFO_OBJECT(element, "GST_STATE_CHANGE_PAUSED_TO_READY\n");
      break;
  
    case GST_STATE_CHANGE_READY_TO_NULL:
         GST_INFO_OBJECT(element, "GST_STATE_CHANGE_READY_TO_NULL\n");
	     if(NULL != filter && 0 != filter->pDtcpSession)
         {
         	GST_INFO_OBJECT(element, "destroying DTCPSession...\n");
            DTCPMgrDeleteDTCPSession (filter->pDtcpSession);
            filter->pDtcpSession = 0;
         }
	 g_mutex_lock (packet_count_mutex);
	 //GST_INFO_OBJECT(element, "packet count = %lld\n", packet_count);
	 g_print ("packet count = %d\n", packet_count);
	 g_mutex_unlock (packet_count_mutex);
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

/* GObject vmethod implementations */

#ifndef USE_GST1
static void
gst_dtcp_dec_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple(element_class,
    "DtcpDec",
    "DTCP decryption plugin: Generic",
    "DTCP Decryption plugin: Generic Template Element",
    "kumar <<user@hostname.org>>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}
#endif

/* initialize the dtcpdec's class */
static void
gst_dtcp_dec_class_init (GstDtcpDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_dtcp_dec_set_property;
  gobject_class->get_property = gst_dtcp_dec_get_property;
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_dtcp_dec_change_state);

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, (GParamFlags)G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_AKE_RESULT,
      g_param_spec_boolean ("AKE_Result", "AKE_Result", "Check whether AKE is successfull or not",
          FALSE, (GParamFlags)G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_DTCP_SRC_IP,
      g_param_spec_string ("dtcp_src_ip", "Dtcp_Src_Ip", "DTCP source IP Address for Initiating AKE request",
          "127.0.0.1", (GParamFlags)G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_DTCP_PORT,
      g_param_spec_int ("dtcp_port", "Dtcp_Port", "DTCP port number on which DTCP Source will listen the AKE commands",
          5000, 10000, 5000, (GParamFlags)G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_BUFFER_SIZE,
      g_param_spec_int ("buffersize", "BufferSize", "Gst buffer size (DTCP Buffer size) Default:4096",
          0, 1048576, 4096, (GParamFlags)G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_LOG_LEVEL,
      g_param_spec_int ("loglevel", "LogLevel", "DTCP Manager log level Default:3(INFO)",
          0, 5, 3, (GParamFlags)G_PARAM_READWRITE));

#ifdef USE_GST1
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_static_metadata (gstelement_class, "DTCP Decryption",
      "Transform",
      "Accepts and forwards mpegts data applying optional DTCP decryption",
      "Comcast");
#endif
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
#ifdef USE_GST1
gst_dtcp_dec_init (GstDtcpDec * filter)
#else
gst_dtcp_dec_init (GstDtcpDec * filter,
    GstDtcpDecClass * gclass)
#endif
{
  GST_INFO_OBJECT(filter, "%s:: Begin\n", __FUNCTION__);
  dtcp_result_t retVal = DTCPMgrInitialize();
  if(DTCP_SUCCESS != retVal)
  {
	char err_str[MAX_ERR_STR_LENGTH];
	getErrStr(retVal, err_str);
    onError(filter, getErrCode(retVal), err_str);
  }

  const char *envVarValue = getenv("DTCP_DEBUG");
  int dtcpLogLevel = 3;
  if (NULL != envVarValue)
  {
      dtcpLogLevel = atoi(envVarValue);
  }
  GST_INFO_OBJECT(filter, "%s::dtcpLogLevel = %d\n", __FUNCTION__, dtcpLogLevel);
  DTCPMgrSetLogLevel(dtcpLogLevel); //INFO
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");

#ifndef USE_GST1
  gst_pad_set_setcaps_function (filter->sinkpad, GST_DEBUG_FUNCPTR(gst_pad_proxy_setcaps));
  gst_pad_set_getcaps_function (filter->sinkpad, GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));
#endif
  gst_pad_set_chain_function (filter->sinkpad, GST_DEBUG_FUNCPTR(gst_dtcp_dec_chain));

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");

#ifdef USE_GST1
  GST_PAD_SET_PROXY_CAPS (filter->srcpad);
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
#else
  gst_pad_set_setcaps_function (filter->srcpad, GST_DEBUG_FUNCPTR(gst_pad_proxy_setcaps));
  gst_pad_set_getcaps_function (filter->srcpad, GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));
#endif

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  filter->silent = TRUE;
  filter->ake_result = FALSE;
  errno_t rc = -1;
  rc = strcpy_s(filter->dtcp_src_ip, sizeof(filter->dtcp_src_ip), "127.0.0.1");
  if(rc != EOK)
  {
      ERR_CHK(rc);
      return;
  }
  filter->dtcp_port = 5000;
  filter->buffersize = 4096; //Default buffer size of HNSrc
//  filter->ake_init_ready = FALSE;
  filter->dtcp_element_bypass = FALSE;
  filter->loglevel = dtcpLogLevel;
  packet_count_mutex = g_mutex_new();

#ifdef USE_GST1
  /* Register a event handler */
  gst_pad_set_event_function(filter->sinkpad, GST_DEBUG_FUNCPTR(gst_dtcp_dec_sink_event));
#endif
}

static void
gst_dtcp_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDtcpDec *filter = GST_DTCPDEC (object);
  errno_t rc = -1;

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    case PROP_AKE_RESULT:
      break;
    case PROP_DTCP_SRC_IP:
      rc = strncpy_s(filter->dtcp_src_ip, sizeof(filter->dtcp_src_ip), g_value_get_string (value), 49);
      if(rc != EOK)
      {
         ERR_CHK(rc);
         return;
      }

/*
      if(filter->ake_init_ready)
      {
          //Create DTCPSinkSession
          filter->pDtcpSession = 0;
          if( 0 < DTCPMgrCreateSinkSession(filter->dtcp_src_ip, filter->dtcp_port, FALSE, filter->buffersize, &filter->pDtcpSession))
          {
              filter->pDtcpSession = 0;
              filter->dtcp_element_bypass = TRUE;
          }
      }
      filter->ake_init_ready = TRUE;
*/
      break;
    case PROP_DTCP_PORT:
      filter->dtcp_port = g_value_get_int (value);
/*
      if(filter->ake_init_ready)
      {
          //Create DTCPSinkSession
          filter->pDtcpSession = 0;
          if( 0 < DTCPMgrCreateSinkSession(filter->dtcp_src_ip, filter->dtcp_port, FALSE, filter->buffersize, &filter->pDtcpSession))
          {
              filter->pDtcpSession = 0;
              filter->dtcp_element_bypass = TRUE;
          }
      }
      filter->ake_init_ready = TRUE;
*/
      break;
    case PROP_BUFFER_SIZE:
      filter->buffersize = g_value_get_int (value);
      break;
    
    case PROP_LOG_LEVEL:
      filter->loglevel = g_value_get_int (value);
      DTCPMgrSetLogLevel(filter->loglevel);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dtcp_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDtcpDec *filter = GST_DTCPDEC (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    case PROP_AKE_RESULT:
      g_value_set_boolean (value, filter->ake_result);
      break;
    case PROP_DTCP_SRC_IP:
      g_value_set_string(value, filter->dtcp_src_ip);
      break;
    case PROP_DTCP_PORT:
      g_value_set_int (value, filter->dtcp_port);
      break;
    case PROP_BUFFER_SIZE:
      g_value_set_int (value, filter->buffersize);
      break;
    case PROP_LOG_LEVEL:
      g_value_set_int (value, filter->loglevel);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* this function handles the link with other elements */
static void gst_dtcpdecrypt_buf_delete( void *packet )
{
  if (!packet)
    return;

  GST_LOG("packet to delete = %p", packet);
  DTCPMgrReleasePacket(packet);

  g_mutex_lock (packet_count_mutex);
  packet_count--;
  g_mutex_unlock (packet_count_mutex);

  g_slice_free (DTCPIP_Packet, packet);
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
#ifdef USE_GST1
gst_dtcp_dec_chain (GstPad * pad, GstObject *parent, GstBuffer * buf)
#else
gst_dtcp_dec_chain (GstPad * pad, GstBuffer * buf)
#endif
{
  GstDtcpDec *filter;
  void *physicalAddress= NULL;
  void *virtualAddress= NULL;
  int dataLen;
  DTCPIP_Packet *packet = NULL;
  GstFlowReturn  ret = GST_FLOW_OK;

#ifdef USE_GST1
  GstMapInfo readWriteMap;
#endif

  filter = GST_DTCPDEC (GST_OBJECT_PARENT (pad));

  if(filter->dtcp_element_bypass)
  {
      return gst_pad_push (filter->srcpad, buf);
  }

#ifdef USE_GST1
  gst_buffer_map (buf, &readWriteMap, GST_MAP_READWRITE);

  virtualAddress = readWriteMap.data;
  dataLen = readWriteMap.size;
#else
  virtualAddress= GST_BUFFER_DATA(buf);
  dataLen= GST_BUFFER_SIZE(buf);
#endif

  if ((0 == dataLen) || (NULL == virtualAddress))
  {
    GST_WARNING_OBJECT (filter, "%s ::The Incoming buffer for Decryption is either NULL or Empty... \n", __FUNCTION__);  //CID:42329 Print_args
    ret = GST_FLOW_OK;
    goto out;
  }

  if(0 == filter->pDtcpSession)
  {
    /* just push out the incoming buffer without touching it */
    //return gst_pad_push (filter->srcpad, buf);
    GST_ERROR_OBJECT(filter, "%s:: DTCP Session is NULL\n", __FUNCTION__);
    ret = GST_FLOW_ERROR;
    goto out;
  }

  GST_LOG_OBJECT(filter, "%s::Decrypting... ", __FUNCTION__);

  if(filter->isFirstPacket && dataLen >= 16) {
    g_print("dtcpdec::Received first packet: ");
#ifdef USE_GST1
    gst_util_dump_mem (readWriteMap.data, 16);
#else
    gst_util_dump_mem (GST_BUFFER_DATA (buf), 16);
#endif

    filter->isFirstPacket = FALSE;
  }

  packet = g_slice_new0 (DTCPIP_Packet);

  if(NULL == packet)
  {
    GST_ERROR_OBJECT(filter, "%s::Error while allocating memory for dtcp packet of size %d... \n", __FUNCTION__, sizeof(DTCPIP_Packet));
    ret = GST_FLOW_ERROR;
    goto out;
  }
  packet->session= filter->pDtcpSession;
  packet->dataInPhyPtr = physicalAddress;
  packet->dataInPtr = virtualAddress;
  packet->dataLength = dataLen;
  packet->dataOutPtr = NULL;
  packet->dataOutPhyPtr = NULL;

  dtcp_result_t retVal = DTCPMgrProcessPacket(filter->pDtcpSession, packet);
  if(DTCP_SUCCESS != retVal)
  {
    GST_ERROR_OBJECT(filter, "%s::Error while decrypting... \n", __FUNCTION__);
	char err_str[MAX_ERR_STR_LENGTH];
	getErrStr(DTCP_ERR_DECRYPT, err_str);
    onError(filter, getErrCode(DTCP_ERR_DECRYPT), err_str);
    /* DTCPMgrReleasePacket() shouldn't be called if DTCPMgrProcessPacket()
     * failed. */
    g_slice_free (DTCPIP_Packet, packet);
    packet = NULL;
    ret = GST_FLOW_ERROR;
    goto out;
  }

  if(0 == packet->dataLength)
  {
    ret = GST_FLOW_OK;
    goto out;
  }

  if(packet->dataOutPtr == packet->dataInPtr)
  {
    //in-place decryption.... push the same buffer to downstream elements...
    GST_LOG_OBJECT(filter, "%s::decryption happened on the same buffer and pushing the buffer to downstream elements...\n", __FUNCTION__);
    ret = gst_pad_push (filter->srcpad, buf);
    //Buffer transferred downstream
    buf = NULL;
    ret = GST_FLOW_OK;
    goto out;
  }

  //no in-place decryption, create a new buffer with decrypted dtcp packet and push it downstream
#ifdef USE_GST1
  GstBuffer *dtcpDataBuf = gst_buffer_new_wrapped_full (
                              0,
                              packet->dataOutPtr,
                              packet->dataLength,
                              0,
                              packet->dataLength,
                              packet,
                              gst_dtcpdecrypt_buf_delete);
#else
  GstBuffer *dtcpDataBuf = gst_buffer_new();
  GST_BUFFER_DATA(dtcpDataBuf)= packet->dataOutPtr;
  GST_BUFFER_MALLOCDATA(dtcpDataBuf)= packet;
  GST_BUFFER_FREE_FUNC(dtcpDataBuf)= gst_dtcpdecrypt_buf_delete;
  GST_BUFFER_SIZE(dtcpDataBuf)= packet->dataLength;
  GST_BUFFER_OFFSET(dtcpDataBuf)= GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END(dtcpDataBuf)= GST_BUFFER_OFFSET_NONE;
#endif

  /* We pass the ownership of 'packet' to the buffer */
  packet = NULL;

  g_mutex_lock (packet_count_mutex);
  packet_count++;
  g_mutex_unlock (packet_count_mutex);

#ifndef USE_GST1
  gst_buffer_copy_metadata (dtcpDataBuf, buf, GST_BUFFER_COPY_CAPS);
#endif

  ret = gst_pad_push (filter->srcpad, dtcpDataBuf);

  if(ret < GST_FLOW_OK)
  {
    GST_WARNING_OBJECT(filter, "%s::gst pad push failed with retCode=%d...\n", __FUNCTION__, ret);
  }

out:
#ifdef USE_GST1
  gst_buffer_unmap (buf, &readWriteMap);
#endif
  if (buf)
    gst_buffer_unref (buf);

  if (packet) {
    DTCPMgrReleasePacket(packet);
    g_slice_free (DTCPIP_Packet, packet);
  }

  return ret;
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
dtcpdec_init (GstPlugin * dtcpdec)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template dtcpdec' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_dtcp_dec_debug, "dtcpdec",
     (GST_DEBUG_BOLD | GST_DEBUG_FG_RED), "dtcpdecrypt element");

  return gst_element_register (dtcpdec, "dtcpdec", GST_RANK_NONE,
      GST_TYPE_DTCPDEC);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "dtcpdec"
#endif

/* gstreamer looks for this structure to register dtcpdecs
 *
 * exchange the string 'Template dtcpdec' with your dtcpdec description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
#ifdef USE_GST1
    dtcpdec,
#else
    "dtcpdec",
#endif
    "create dtcp stream and encrypt the data : dtcpenc",
    dtcpdec_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)


/** @} */
/** @} */
