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
 * SECTION:element-dtcpenc
 *
 * create dtcp stream with the EMI(from cci property) and encrypt the data. 
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! dtcpenc ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */



/**
* @defgroup gst-plugins-rdk
* @{
* @defgroup dtcpencrypt
* @{
**/


#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <assert.h>

#include <gst/gst.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "gstdtcpenc.h"
#include "safec_lib.h"

GST_DEBUG_CATEGORY_STATIC (gst_dtcp_enc_debug);
#define GST_CAT_DEFAULT gst_dtcp_enc_debug

#define PACKET_SIZE (188)
#define TTS_PACKET_SIZE (192)
#define MAX_PIDS (8)

static unsigned int packet_count;
static GMutex* packet_count_mutex;

static unsigned long crc32_table[256];
static int crc32_initialized = 0;

static void init_crc32()
{
	if(crc32_initialized) return;
	unsigned int k, i, j;
	for(i = 0; i < 256; i++)
	{
		k = 0;
		for(j = (i << 24) | 0x800000; j != 0x80000000; j <<= 1)
		{
			k = (k << 1) ^ (((k ^ j) & 0x80000000) ? 0x04c11db7 : 0);
		}
		crc32_table[i] = k;
	}
	crc32_initialized = 1;
}

static unsigned long get_crc32(unsigned char *data, int size, int initial )
{
	int i;
	unsigned long int result = initial;
	init_crc32();
	for(i = 0; i < size; i++)
	{
		result = (result << 8) ^ crc32_table[(result >> 24) ^ data[i]];
	}
	return result;
}

static void putPmtByte( GstDtcpEnc * filter, unsigned char* pmt, int* index, unsigned char byte, int pmtPid )
{
   int ttsSize = 0;
   if(filter->tspacketsize == TTS_PACKET_SIZE)
   	  ttsSize = 4;

   pmt[(*index)++]= byte;
   if ( (*index) > (filter->tspacketsize-1) )
   {
      GST_INFO_OBJECT(filter, "putPmtByte :: pmt data exceeded %d bytes (index-%d) so continuing with sencond ts packet...\n", filter->tspacketsize, *index);
      pmt += filter->tspacketsize;
      pmt[0+ttsSize]= 0x47;
      pmt[1+ttsSize]= 0x20;
      pmt[1+ttsSize]= (unsigned char) ((pmtPid >> 8) & 0x1F);
      pmt[2+ttsSize]= (unsigned char) (0xFF & pmtPid);
      pmt[3+ttsSize]= 0x10;  // 2 bits Scrambling = no; 2 bits adaptation field = no adaptation; 4 bits continuity counter
      (*index) += filter->tspacketsize+ttsSize+4;
   }
}

static void dumpPacket( GstDtcpEnc * filter, unsigned char *packet )
{
   int i;
   char buff[1024];
   errno_t rc = -1;
   int col= 0;
   int buffPos= 0;
   int ts_packet_size = filter->tspacketsize;

   rc = sprintf_s(&buff[buffPos],sizeof(buff)-buffPos, "\n" );
   if(rc < EOK)
   {
       ERR_CHK(rc);
       return;
   }
   buffPos += rc;
   for( i= 0; i < ts_packet_size; ++i )
   {
        rc = sprintf_s(&buff[buffPos], sizeof(buff)-buffPos, "%02X ", packet[i] );
        if(rc < EOK)
        {
            ERR_CHK(rc);
            return;
        }
        buffPos += rc;
       ++col;
       if ( col == 8 )
       {
            rc = strcat_s(buff,sizeof(buff), " ");
            if(rc != EOK)
            {
                ERR_CHK(rc);
                return;
            }
          buffPos += 1;
       }
       if ( col == 16 )
       {
          rc = sprintf_s(&buff[buffPos], sizeof(buff)-buffPos, "\n" );
          if(rc < EOK)
          {
              ERR_CHK(rc);
              return;
          }
          buffPos += rc;
          col= 0;
       }
   }
   GST_TRACE_OBJECT( filter, "%s", buff );
}

static void dumpPackets( GstDtcpEnc * filter, unsigned char *packets, int len )
{
   int ts_packet_size = filter->tspacketsize;

   while( len > 0 )
   {
      dumpPacket( filter, packets );
      len -= ts_packet_size;
      packets += ts_packet_size;
   }
}

static GstStateChangeReturn gst_dtcp_enc_change_state (GstElement *element, GstStateChange transition);

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
  PROP_CCI,
  PROP_SRC_TYPE,
  PROP_BUFFER_SIZE,
  PROP_REMOTE_IP,
  PROP_KEY_LABEL,
  PROP_RESET
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
#ifdef USE_GST1
#define DTCP_ENC_CAPS \
           "video/mpegts, " \
           "  systemstream=(boolean)true, "\
           "  packetsize=(int)188;"\
           "video/vnd.dlna.mpeg-tts, " \
           "  systemstream=(boolean)true, "\
           "  packetsize=(int)192;"
#else
#define DTCP_ENC_CAPS \
           "video/mpegts, " \
           "  systemstream=(boolean)true, "\
           "  packetsize=(int)188;"\
           "video/vnd.dlna.mpeg-tts, " \
           "  systemstream=(boolean)true, "\
           "  packetsize=(int)192, "\
           "  preferInplace=(boolean)true;"
#endif


static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (DTCP_ENC_CAPS)
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

#ifdef USE_GST1
#define gst_dtcp_enc_parent_class parent_class
G_DEFINE_TYPE (GstDtcpEnc, gst_dtcp_enc, GST_TYPE_ELEMENT);
#else
GST_BOILERPLATE (GstDtcpEnc, gst_dtcp_enc, GstElement,
    GST_TYPE_ELEMENT);
#endif

static void gst_dtcp_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dtcp_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#ifndef USE_GST1
static gboolean gst_dtcp_enc_set_caps (GstPad * pad, GstCaps * caps);
#endif
static gboolean gst_dtcp_enc_generate_pmt( GstDtcpEnc *filter, 
                                              int videoComponentCount, int *videoPids, int *videoTypes,
                                              int audioComponentCount, int *audioPids, int *audioTypes, char **audioLanguages,
                                              int dataComponentCount, int *dataPids, int *dataTypes );
                                              
static gboolean gst_dtcp_enc_examine_buffer( GstDtcpEnc *filter, GstBuffer *buf );
#ifdef USE_GST1
static GstFlowReturn gst_dtcp_enc_chain (GstPad * pad, GstObject *parent, GstBuffer * buf);
#else
static GstFlowReturn gst_dtcp_enc_chain (GstPad * pad, GstBuffer * buf);

#endif

static GstStateChangeReturn
gst_dtcp_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GstPad *pad =  gst_element_get_static_pad (element, "sink");
  GstDtcpEnc *filter = GST_DTCPENC (GST_OBJECT_PARENT (pad));

  ret = GST_STATE_CHANGE_SUCCESS;
  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
        GST_DEBUG_OBJECT(element, "GST_STATE_CHANGE_NULL_TO_READY :: creating DTCPSession");
        if (NULL != filter)
        {
            if (0 == filter->buffersize)
            {
                if (QAMSRC == filter->srctype)
                    filter->buffersize = 128*1024; //Default buffer size of QAMSRC
                else
                    filter->buffersize = 4096; //Default buffer size of HNSRC
            }

            if ( 0 > DTCPMgrCreateSourceSession(filter->remoteip, filter->exchange_key_label, 0, filter->buffersize, &filter->pDtcpSession))
            {
                GST_ERROR_OBJECT(filter, "%s:: Error while creating DTCP SESSION.... \n",__FUNCTION__);
                filter->pDtcpSession = 0;
            }
            GST_DEBUG_OBJECT(filter, "%s:: filter->pDtcpSession = %lu", __FUNCTION__, filter->pDtcpSession);  //CID:28136 - Print args
            filter->tspacketsize=0;
            g_mutex_lock (packet_count_mutex);
            packet_count = 0;
            g_mutex_unlock (packet_count_mutex);
            filter->isFirstPacket = TRUE;
        }   //CID:18753 - Forward null

        break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
//         GST_INFO_OBJECT(element, "GST_STATE_CHANGE_READY_TO_PAUSED\n");
      break;
    }

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
//         GST_INFO_OBJECT(element, "GST_STATE_CHANGE_PAUSED_TO_PLAYING\n");
      break;
    }
    /*case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
      break;
    }
    case GST_STATE_CHANGE_READY_TO_NULL:
    {
      break;
    }*/
    default:
      break;
  }
                
  /* Chain up to the parent class's state change function
   * Must do this before downward transitions are handled to safely handle
   * concurrent access by multiple threads */
  GST_INFO_OBJECT(element, "calling parent_class change_state %p: transition %d\n", GST_ELEMENT_CLASS (parent_class)->change_state, transition);
  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT(element, "change state failure\n");
    goto failure;
  }     
  GST_DEBUG_OBJECT(element, "completed parent_class change_state %p: transition %d\n", GST_ELEMENT_CLASS (parent_class)->change_state, transition);
  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
//         GST_INFO_OBJECT(element, "GST_STATE_CHANGE_PLAYING_TO_PAUSED\n");
      break;
  
    case GST_STATE_CHANGE_PAUSED_TO_READY:
//         GST_INFO_OBJECT(element, "GST_STATE_CHANGE_PAUSED_TO_READY\n");
      break;
  
    case GST_STATE_CHANGE_READY_TO_NULL:
         GST_DEBUG_OBJECT(element, "GST_STATE_CHANGE_READY_TO_NULL:: destroying DTCP Stream and DTCPSession\n");

  	 if(NULL != filter && 0 != filter->pDtcpSession)
  	 {
  	     GST_INFO_OBJECT(element, "destroying DTCPSession\n");
		 if(0 > DTCPMgrDeleteDTCPSession(filter->pDtcpSession))
		 {
  			GST_ERROR_OBJECT(filter, "%s:: Error while deleting DTCP SESSION.... \n",__FUNCTION__);
		 }
	     filter->pDtcpSession = 0;
	  }
	 g_mutex_lock (packet_count_mutex);
	 g_print ("dtcpenc:: packet count = %d\n", packet_count);
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
gst_dtcp_enc_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple(element_class,
    "DtcpEnc",
    "DTCP encryption plugin: Generic",
    "DTCP Encryption plugin: Generic Template Element",
    "<<user@hostname.org>>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}
#endif

/* initialize the dtcpenc's class */
static void
gst_dtcp_enc_class_init (GstDtcpEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_dtcp_enc_set_property;
  gobject_class->get_property = gst_dtcp_enc_get_property;
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_dtcp_enc_change_state);

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ? Default:TRUE",
          TRUE, (GParamFlags)G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_CCI,
      g_param_spec_uchar ("cci", "Cci", "Stream CCI value for encrypting Default:0",
          0, G_MAXUINT8, 0, (GParamFlags)G_PARAM_READWRITE));
          
  g_object_class_install_property (gobject_class, PROP_SRC_TYPE,
      g_param_spec_int ("srctype", "SrcType", "Source Element Type (QAM or HN) Default:0",
          0, 1, 0, (GParamFlags)G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_REMOTE_IP,
      g_param_spec_string ("remoteip", "RemoteIp", "Remote IP (client ip) address",
          "0.0.0.0", (GParamFlags)G_PARAM_READWRITE));
          
  g_object_class_install_property (gobject_class, PROP_BUFFER_SIZE,
      g_param_spec_int ("buffersize", "BufferSize", "Gstreamer buffer size (DTCP Buffer Size) Default:131036",
          0, 192512, 131036, (GParamFlags)G_PARAM_READWRITE));
          
  g_object_class_install_property (gobject_class, PROP_KEY_LABEL,
      g_param_spec_uint ("keylabel", "KeyLabel", "DTCP Exchange Key Label Default:0",
          0, 0xFF, 0, (GParamFlags)G_PARAM_READWRITE));
          
  g_object_class_install_property (gobject_class, PROP_RESET,
      g_param_spec_boolean ("reset", "Reset", "Reset the dtcp session to clean up residue data default:FALSE",
          FALSE, (GParamFlags)G_PARAM_WRITABLE));

#ifdef USE_GST1
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_static_metadata (gstelement_class, "DTCP Encryption",
      "Transform",
      "Accepts and forwards mpegts data applying optional DTCP encryption",
      "Comcast");
#endif
  packet_count_mutex = g_mutex_new();

  init_crc32();
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
#ifdef USE_GST1
gst_dtcp_enc_init (GstDtcpEnc * filter)
#else
gst_dtcp_enc_init (GstDtcpEnc * filter,
    GstDtcpEncClass * gclass)
#endif
{
  GST_INFO_OBJECT(filter, "%s:: filter=%p packet_count_mutex=%p\n", __FUNCTION__, filter, packet_count_mutex);

  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
#ifndef USE_GST1
  gst_pad_set_setcaps_function (filter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_dtcp_enc_set_caps));
  gst_pad_set_getcaps_function (filter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));
#endif
  gst_pad_set_chain_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_dtcp_enc_chain));

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
#ifdef USE_GST1
  GST_PAD_SET_PROXY_CAPS (filter->srcpad);
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
#else
  gst_pad_set_getcaps_function (filter->srcpad,
                                GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));
#endif

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->pDtcpSession = 0;
  filter->silent = TRUE;
  filter->cci = 0;
  filter->srctype = 0; //QAMSRC
  filter->buffersize = 0; //131036; //Default buffer size of QAMSRC
  errno_t rc = -1;
  rc = strcpy_s(filter->remoteip, sizeof(filter->remoteip), "0.0.0.0");
  if(rc != EOK)
  {
      ERR_CHK(rc);
      return;
  }
  filter->havePAT= FALSE;
  filter->havePMT= FALSE;
  filter->haveNewPMT= FALSE;
  filter->newPMTSize= 0;
  filter->program= -1;
  filter->pmtPid= -1;
  filter->pcrPid= -1;
  filter->tspacketsize= 0;
}

static unsigned char gst_dtcp_enc_get_emi(unsigned char cci)
{
  unsigned char emi = 0;
  switch(cci & 0x03)
  {
	case 0x00: emi = 0x00; //Copy-Free
	  break;
	case 0x01: emi = 0x04; //Copy-No-More
	  break;
	case 0x02: emi = 0x0A; //Copy-One-Generation
	  break;
	case 0x03: emi = 0x0C; //Copy-Never
	  break;
	default: emi=0x00;	
  }
  emi = 0x04; //Setting EMI to Copy-No-More for all contents.
  return emi;
}

static void
gst_dtcp_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDtcpEnc *filter = GST_DTCPENC (object);
  errno_t rc = -1;

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    case PROP_CCI:
      filter->cci = g_value_get_uchar (value);
      filter->EMI = gst_dtcp_enc_get_emi (filter->cci);
      break;
    case PROP_SRC_TYPE:
      filter->srctype = g_value_get_int (value);
	  g_print("src type = %d\n", filter->srctype);
	  if(0 == filter->buffersize)
	  {
        filter->buffersize = 128*1024; //Default buffer size of QAMSRC
	  	if(HNSRC == filter->srctype)
		{
  			g_print("%s:: src type = HNSRC \n", __FUNCTION__);
        	filter->buffersize = 4096; //Default buffer size of HNSRC
		}
	  }
      break;
    case PROP_BUFFER_SIZE:
      filter->buffersize = g_value_get_int (value);
	  g_print("%s:: buffersize = %d\n", __FUNCTION__, filter->buffersize);
      break;
    case PROP_REMOTE_IP:
          rc = strcpy_s(filter->remoteip, sizeof(filter->remoteip), g_value_get_string (value));
          if(rc != EOK)
          {
             ERR_CHK(rc);
             return;
          }
	  filter->remoteip[49] = '\0';
	  g_print("%s:: RemoteIp = '%s'\n", __FUNCTION__, filter->remoteip);
      break;
    case PROP_KEY_LABEL:
      filter->exchange_key_label = g_value_get_uint (value);
	  g_print("%s:: exchange_key_label = %x\n", __FUNCTION__, filter->exchange_key_label);
      break;
    case PROP_RESET:
	  GST_INFO_OBJECT(filter, "%s:: RESETTING THE DTCP SESSION TO CLEANUP RESIDUE\n", __FUNCTION__);
  	  GST_INFO_OBJECT(filter, "destroying DTCPSession\n");
	  if(0 > DTCPMgrDeleteDTCPSession(filter->pDtcpSession))
	  {
  		GST_ERROR_OBJECT(filter, "%s:: Error while deleting DTCP SESSION.... \n",__FUNCTION__);
	  }
	  filter->pDtcpSession = 0;
  	  if( 0 > DTCPMgrCreateSourceSession(filter->remoteip, 0, 0, filter->buffersize, &filter->pDtcpSession))
  	  {
		GST_ERROR_OBJECT(filter, "%s:: Error while creating DTCP SESSION.... \n",__FUNCTION__);
		filter->pDtcpSession = 0;
  	  }
  	  GST_INFO_OBJECT(filter, "%s:: filter->pDtcpSession = %lu\n", __FUNCTION__, filter->pDtcpSession);  //CID:28857 - Print args
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dtcp_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDtcpEnc *filter = GST_DTCPENC (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    case PROP_CCI:
      g_value_set_uchar (value, filter->cci);
      break;
    case PROP_SRC_TYPE:
      g_value_set_int (value, filter->srctype);
      break;
    case PROP_BUFFER_SIZE:
      g_value_set_int (value, filter->buffersize);
      break;
    case PROP_REMOTE_IP:
      g_value_set_string(value, filter->remoteip);
      break;
    case PROP_KEY_LABEL:
      g_value_set_uint(value, filter->exchange_key_label);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

#ifndef GST_USE1
/* this function handles the link with other elements */
static gboolean
gst_dtcp_enc_set_caps (GstPad * pad, GstCaps * caps)
{
  GstDtcpEnc *filter;
  GstPad *otherpad;

  filter = GST_DTCPENC (gst_pad_get_parent (pad));
  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;
  gst_object_unref (filter);

  return gst_pad_set_caps (otherpad, caps);
}
#endif

static gboolean gst_dtcp_enc_generate_pmt( GstDtcpEnc *filter, 
                                              int videoComponentCount, int *videoPids, int *videoTypes,
                                              int audioComponentCount, int *audioPids, int *audioTypes, char **audioLanguages,
                                              int dataComponentCount, int *dataPids, int *dataTypes )
{
   gboolean result= TRUE;
   int i, pi, temp, pmtSize, dtcpDescSize;
   int pmtPacketCount, pmtSectionLen, pmtPid;
   unsigned char *crcData;
   int crc, crcLenTotal, crcLen;
   unsigned char byte;
   unsigned char *pmtPacket;
   errno_t rc = -1;
   
   pmtPid= filter->pmtPid;
   unsigned char cci = filter->cci;
   GST_INFO_OBJECT( filter, "CCI of the program is %X\n", cci);
   int ttsSize = 0;
   int ts_packet_size = filter->tspacketsize;
   if(filter->tspacketsize == TTS_PACKET_SIZE)
   {
	  ttsSize = 4;
   }
   
   dtcpDescSize= 6;

   pmtSize = ttsSize;
   pmtSize += 17;
   pmtSize += dtcpDescSize;
   pmtSize += videoComponentCount*5;
   for( i= 0; i < audioComponentCount; ++i )
   {
      pmtSize += 5;
      int nameLen= audioLanguages[i] ? strlen(audioLanguages[i]) : 0;
      if ( nameLen )
      {
         pmtSize += (3 + nameLen);
      }      
   }
   pmtSize += dataComponentCount*5;
   pmtSize += 4; //crc

   pmtPacketCount= 1;
   i= pmtSize-(ts_packet_size-17);
   while( i > 0 )
   {
   	  GST_INFO_OBJECT( filter, "%s::%d, pmt exceedes 1 TSPacketSize\n", __FUNCTION__, __LINE__);
      ++pmtPacketCount;
      i -= (ts_packet_size-8);
   }
   
   if ( pmtPacketCount > 3 )
   {
      GST_ERROR_OBJECT(filter, "%s:: pmt requires more than 3 TS packets\n",__FUNCTION__);
      return FALSE;
   }

   pmtPacket= filter->newPMT;
   
   pmtPacket[0+ttsSize]= 0x47;
   pmtPacket[1+ttsSize]= 0x60;
   pmtPacket[1+ttsSize] |= (unsigned char) ((filter->pmtPid >> 8) & 0x1F);
   pmtPacket[2+ttsSize]= (unsigned char) (0xFF & filter->pmtPid);
   pmtPacket[3+ttsSize]= 0x10; // 2 bits Scrambling = no; 2 bits adaptation field = no adaptation; 4 bits continuity counter

   pmtSectionLen= pmtSize-ttsSize-8;
   pmtPacket[4+ttsSize]= 0x00;
   pmtPacket[5+ttsSize]= 0x02;
   pmtPacket[6+ttsSize]= (0xB0 | ((pmtSectionLen>>8)&0xF));
   pmtPacket[7+ttsSize]= (pmtSectionLen & 0xFF); //lower 8 bits of Section length

   pmtPacket[8+ttsSize]= ((filter->program>>8)&0xFF);
   pmtPacket[9+ttsSize]= (filter->program&0xFF);

   temp= filter->versionPMT << 1;
   temp= temp & 0x3E; //Masking first 2 bits and last one bit : 0011 1110 (3E)
   pmtPacket[10+ttsSize]= 0xC1 | temp; //C1 : 1100 0001 : setting reserved bits as 1, current_next_indicator as 1

   pmtPacket[11+ttsSize]= 0x00;
   pmtPacket[12+ttsSize]= 0x00;

   pmtPacket[13+ttsSize]= 0xE0;
   pmtPacket[13+ttsSize] |= (unsigned char) ((filter->pcrPid >> 8) & 0x1F);
   pmtPacket[14+ttsSize]= (unsigned char) (0xFF & filter->pcrPid);
   pmtPacket[15+ttsSize]= 0xF0;
   pmtPacket[16+ttsSize]= 0x00; //pgm info length.

   pmtPacket[16+ttsSize]= dtcpDescSize & 0xFF; // Since desc_size is 6(110) only last 4 bits
   pmtPacket[16+ttsSize]= dtcpDescSize;

   //DTCP_descriptor...
   pmtPacket[17+ttsSize] = 0x88; //descriptor_tag
   pmtPacket[18+ttsSize] = 0x04; //descriptor_length
   pmtPacket[19+ttsSize] = 0x0f; //upper 8 bits of CA_System_ID
   pmtPacket[20+ttsSize] = 0xff; //lower 8 bits of CA_System_ID

   unsigned char aps = ((unsigned char)(cci & 0x20)) >> 5;
   unsigned char epn = !((unsigned char)(cci & 0x03)) & (aps & 0x01);
   unsigned char ict = (unsigned char)(cci & 0x10);

   if(epn)
	  filter->EMI = 0x0A; //Copy One Generation 

   pmtPacket[21+ttsSize] = 0xC0; //descriptor id(1 bit-1), retention_move_mode(1 bit-1), retention state(3 bits- 000)
   if(epn)
   	  pmtPacket[21+ttsSize] |= 0x04; //EPN(1 bit)
   pmtPacket[21+ttsSize] |= (unsigned char)(cci & 0x03); //dtcp_cci (2 bits)

   pmtPacket[22+ttsSize] = 0xF8; //reserved (3 bits-111), DOT(1 bit-1), AST(1 bit-1)
   if(ict)
	  pmtPacket[22+ttsSize] |= 0x04; //Image_Constraint_Token (1)
   pmtPacket[22+ttsSize] |= (unsigned char)(cci>>2 & 0x03); //APS(2)
   
   pi= 23+ttsSize;
   for( i= 0; i < videoComponentCount; ++i )
   {
      int videoPid= videoPids[i];
      putPmtByte( filter, pmtPacket, &pi, videoTypes[i], pmtPid ); 
      byte= (0xE0 | (unsigned char) ((videoPid >> 8) & 0x1F));
      putPmtByte( filter, pmtPacket, &pi, byte, pmtPid );
      byte= (unsigned char) (0xFF & videoPid);
      putPmtByte( filter, pmtPacket, &pi, byte, pmtPid ); 
      putPmtByte( filter, pmtPacket, &pi, 0xF0, pmtPid ); 
      putPmtByte( filter, pmtPacket, &pi, 0x00, pmtPid ); 
   }
   for( i= 0; i < audioComponentCount; ++i )
   {
      int audioPid= audioPids[i];
      int nameLen= audioLanguages[i] ? strlen(audioLanguages[i]) : 0;
      putPmtByte( filter, pmtPacket, &pi, audioTypes[i], pmtPid ); 
      byte= (0xE0 | (unsigned char) ((audioPid >> 8) & 0x1F));
      putPmtByte( filter, pmtPacket, &pi, byte, pmtPid );
      byte= (unsigned char) (0xFF & audioPid);
      putPmtByte( filter, pmtPacket, &pi, byte, pmtPid ); 
      putPmtByte( filter, pmtPacket, &pi, 0xF0, pmtPid );
      if ( nameLen )
      {
         putPmtByte( filter, pmtPacket, &pi, (3+nameLen), pmtPid ); 
         putPmtByte( filter, pmtPacket, &pi, 0x0A, pmtPid );
         putPmtByte( filter, pmtPacket, &pi, (1+nameLen), pmtPid ); 
		 int j;
         for( j= 0; j < nameLen; ++j )
         {
            putPmtByte( filter, pmtPacket, &pi, audioLanguages[i][j], pmtPid );
         } 
      }
      putPmtByte( filter, pmtPacket, &pi, 0x00, pmtPid ); 
   }
   for( i= 0; i < dataComponentCount; ++i )
   {
      int dataPid= dataPids[i];
      putPmtByte( filter, pmtPacket, &pi, dataTypes[i], pmtPid ); 
      byte= (0xE0 | (unsigned char) ((dataPid >> 8) & 0x1F));
      putPmtByte( filter, pmtPacket, &pi, byte, pmtPid );
      byte= (unsigned char) (0xFF & dataPid);
      putPmtByte( filter, pmtPacket, &pi, byte, pmtPid ); 
      putPmtByte( filter, pmtPacket, &pi, 0xF0, pmtPid ); 
      putPmtByte( filter, pmtPacket, &pi, 0x00, pmtPid ); 
   }

   // Calculate crc
   crcData= &pmtPacket[5+ttsSize];
   crcLenTotal= pmtSize-ttsSize-5-4;
   crcLen= ((crcLenTotal > (ts_packet_size-ttsSize-5)) ? (ts_packet_size-ttsSize-5) : crcLenTotal);
   crc= 0xffffffff;
   while( crcLenTotal )
   {
      crc= get_crc32( crcData, crcLen, crc);
      crcData += crcLen;
	  crcLen = crcLenTotal - crcLen;
      crcLenTotal = crcLen;
   }
   putPmtByte( filter, pmtPacket, &pi, ((crc >> 24) & 0xFF), pmtPid );
   putPmtByte( filter, pmtPacket, &pi, ((crc >> 16) & 0xFF), pmtPid );
   putPmtByte( filter, pmtPacket, &pi, ((crc >> 8) & 0xFF), pmtPid );
   putPmtByte( filter, pmtPacket, &pi, (crc & 0xFF), pmtPid );
   
   // Fill stuffing bytes for rest of TS packet
   rc = memset_s( pmtPacket+pi, MAX_PACKET_SIZE-pi, 0xFF, MAX_PACKET_SIZE-pi );
   ERR_CHK(rc);

   filter->newPMTSize= pmtSize;

   dumpPackets( filter, filter->newPMT, pmtSize);
            
   return result;
}                                              

static gboolean gst_dtcp_enc_examine_buffer( GstDtcpEnc *filter, GstBuffer *buf )
{
   gboolean result= TRUE;
   int size = 0;
   unsigned char *packet = NULL, *bufferEnd = NULL;
   int pid = 0, payloadStart = 0, adaptation = 0;
   int packetCount= 0;
   int ttsSize = 0;
   int ts_packet_size = filter->tspacketsize;
   errno_t rc = -1;
#ifdef USE_GST1
   GstMapInfo map =  {0};
#endif

   if(filter->tspacketsize == TTS_PACKET_SIZE)
   {
	  ttsSize = 4;
   }

#ifdef USE_GST1
   gst_buffer_map (buf, &map, GST_MAP_READWRITE);

   size = map.size;
   packet = map.data;
#else
   size = GST_BUFFER_SIZE (buf);   
   packet= GST_BUFFER_DATA (buf);
#endif

   if (NULL == packet)
   {
      GST_ERROR_OBJECT(filter, "%s:: Packet is NULL \n",__FUNCTION__);
#ifdef USE_GST1
    gst_buffer_unmap (buf, &map);
#endif
	  return FALSE;
   }
   
   // For the moment, insist on buffers being TS packet aligned
   if ( !((packet[0+ttsSize] == 0x47) && ((size%ts_packet_size) == 0)) )
   {
      GST_ERROR_OBJECT(filter, "%s:: data buffer not TS packet aligned tspacketsize = %d\n",__FUNCTION__, ts_packet_size);
#ifdef USE_GST1
    gst_buffer_unmap (buf, &map);
#endif
      //assert( false );
	  return FALSE;
   }

   bufferEnd= packet+size;
   while( packet < bufferEnd )
   {
      pid= (((packet[1+ttsSize] << 8) | packet[2+ttsSize]) & 0x1FFF);
      
      if ( pid == 0 )
      {
         payloadStart= (packet[1+ttsSize] & 0x40);

         if ( payloadStart )
         {
            adaptation= ((packet[3+ttsSize] & 0x30)>>4);
            if ( adaptation == 0x01 )
            {
               int tableid= packet[5+ttsSize];
               if ( tableid == 0x00 )
               {
                  int version= packet[10+ttsSize];
                  int current= (version & 0x01);
                  version= ((version >> 1)&0x1F);
                  
                  if ( !filter->havePAT || (current && (version != filter->versionPAT)) )
                  {
                     dumpPacket( filter, packet );
                     int length= ((packet[6+ttsSize]&0x0F)<<8)+(packet[7+ttsSize]);
                     if ( length == 13 )
                     {
                        filter->havePAT= TRUE;
                        filter->versionPAT= version;
                        filter->program= ((packet[13+ttsSize]<<8)+packet[14+ttsSize]);
                        filter->pmtPid= (((packet[15+ttsSize]&0x1F)<<8)+packet[16+ttsSize]);
                        GST_INFO_OBJECT( filter, "got PAT: program %X pmtPid %X\n", filter->program, filter->pmtPid );
                        if ( (filter->program != 0) && (filter->pmtPid != 0) )
                        {
                           if ( filter->havePMT )
                           {
                              GST_INFO_OBJECT( filter, "%s:: pmt change detected in pat", __FUNCTION__ );
                           }
                           filter->havePMT= FALSE;
                           filter->haveNewPMT= FALSE;
                           GST_DEBUG_OBJECT( filter, "%s:: acquired PAT version %d program %X pmt pid %X", 
                                             __FUNCTION__, version, filter->program, filter->pmtPid );
                        }
                        else
                        {
                           GST_WARNING_OBJECT( filter, "%s:: ignoring pid 0 TS packet with suspect program %x and pmtPid %x", 
                                               __FUNCTION__, filter->program, filter->pmtPid );
                           dumpPacket( filter, packet );
                           filter->program= -1;
                           filter->pmtPid= -1;
                        }
                     }
                     else
                     {
                        GST_WARNING_OBJECT( filter, "%s:: ignoring pid 0 TS packet with length of %d", __FUNCTION__, length );
                     }
                  }
               }
               else
               {
                  GST_WARNING_OBJECT( filter, "%s:: ignoring pid 0 TS packet with tableid of %x", __FUNCTION__, tableid );
               }
            }
            else
            {
               GST_WARNING_OBJECT( filter, "%s:: ignoring pid 0 TS packet with adaptation of %x", __FUNCTION__, adaptation );
            }
         }
         else
         {
            GST_WARNING_OBJECT( filter, "%s:: ignoring pid 0 TS without payload start indicator", __FUNCTION__ );
         }
      }
      else if ( pid == filter->pmtPid )
      {
         payloadStart= (packet[1+ttsSize] & 0x40);
         if ( payloadStart )
         {
            adaptation= ((packet[3+ttsSize] & 0x30)>>4);
            if ( adaptation == 0x01 )
            {
               int tableid= packet[5+ttsSize];
               if ( tableid == 0x02 )
               {
                  int program= ((packet[8+ttsSize]<<8)+packet[9+ttsSize]);
                  if ( program == filter->program )
                  {
                     int version= packet[10+ttsSize];
                     int current= (version & 0x01);
                     version= ((version >> 1)&0x1F);

                     if ( !filter->havePMT || (current && (version != filter->versionPMT)) )
                     {
                        dumpPacket( filter, packet );
                        if ( filter->havePMT && (version != filter->versionPMT) )
                        {
                           GST_INFO_OBJECT( filter, "%s:: pmt change detected: version %d -> %d", __FUNCTION__, filter->versionPMT, version );
                           filter->havePMT= FALSE;
                           filter->haveNewPMT= FALSE;
                        }

                        if ( !filter->havePMT )
                        {
                           int sectionLength= (((packet[6+ttsSize]&0x0F)<<8)+packet[7+ttsSize]);
                           // Check if pmt payload fits in one TS packet: raw packat size less tts header,
                           // less TS header, less fix size portion of pmt, minus trailing CRC
                           if ( sectionLength < (ts_packet_size-5-12-4-ttsSize) )
                           {
                              unsigned char *programInfo, *programInfoEnd;
                              int videoComponentCount, audioComponentCount, dataComponentCount;                           
                              int videoPids[MAX_PIDS], audioPids[MAX_PIDS], dataPids[MAX_PIDS];
                              int videoTypes[MAX_PIDS], audioTypes[MAX_PIDS], dataTypes[MAX_PIDS];
                              char* audioLanguages[MAX_PIDS];
                              int streamType, pid, len, langLen;
                              char work[32];
                              
                              int pcrPid= (((packet[13+ttsSize]&0x1F)<<8)+packet[14+ttsSize]);
                              int infoLength= (((packet[15+ttsSize]&0x0F)<<8)+packet[16+ttsSize]);
                              
                              videoComponentCount= audioComponentCount= dataComponentCount= 0;
                              programInfo= &packet[infoLength+17+ttsSize];
                              programInfoEnd= packet+ttsSize+8+sectionLength-4;
                              while ( programInfo < programInfoEnd )
                              {
                                 streamType= programInfo[0];
                                 pid= (((programInfo[1]&0x1F)<<8)+programInfo[2]);
                                 len= (((programInfo[3]&0x0F)<<8)+programInfo[4]);
                                 switch( streamType )
                                 {
                                    case 0x02: // MPEG2 Video
                                    case 0x80: // ATSC Video
                                       videoPids[videoComponentCount]= pid;
                                       videoTypes[videoComponentCount]= streamType;
                                       ++videoComponentCount;
                                       break;                                    
                                    case 0x03: // MPEG1 Audio                                    
                                    case 0x04: // MPEG2 Audio                                    
                                    case 0x0F: // MPEG2 AAC Audio                                    
                                    case 0x11: // MPEG4 LATM AAC Audio                                    
                                    case 0x81: // ATSC AC3 Audio                                    
                                    case 0x82: // HDMV DTS Audio                                    
                                    case 0x83: // LPCM Audio                                    
                                    case 0x84: // SDDS Audio                                    
                                    case 0x86: // DTS-HD Audio                                    
                                    case 0x87: // ATSC E-AC3 Audio                                    
                                    case 0x8A: // DTS Audio                                    
                                    case 0x91: // A52b/AC3 Audio                                    
                                    case 0x94: // SDDS Audio
                                       audioPids[audioComponentCount]= pid;
                                       audioTypes[audioComponentCount]= streamType;
                                       audioLanguages[audioComponentCount]= 0;
                                       if( len > 2 )
                                       {
                                          int descIdx, maxIdx;
                                          int descrTag, descrLen;
                                          
                                          descIdx= 5;
                                          maxIdx= descIdx+len;
                                          
                                          while ( descIdx < maxIdx )
                                          {
                                             descrTag= programInfo[descIdx];
                                             descrLen= programInfo[descIdx+1];
                                             
                                             switch ( descrTag )
                                             {
                                                // ISO_639_language_descriptor
                                                case 0x0A:
                                                   rc = memcpy_s( work, sizeof(work), &programInfo[descIdx+2], descrLen );
                                                   if(rc != EOK)
                                                   {
                                                       ERR_CHK(rc);
                                                       return FALSE;
                                                   }
                                                   work[descrLen]= '\0';
                                                   audioLanguages[audioComponentCount]= strdup(work);
                                                   break;
                                             }
                                             
                                             descIdx += (2+descrLen);
                                          }
                                       }
                                       ++audioComponentCount;
                                       break;
                                    case 0x01: // MPEG1 Video
                                    case 0x05: // ISO 13818-1 private sections
                                    case 0x06: // ISO 13818-1 PES private data
                                    case 0x07: // ISO 13522 MHEG
                                    case 0x08: // ISO 13818-1 DSM-CC
                                    case 0x09: // ISO 13818-1 auxiliary
                                    case 0x0a: // ISO 13818-6 multi-protocol encap	
                                    case 0x0b: // ISO 13818-6 DSM-CC U-N msgs
                                    case 0x0c: // ISO 13818-6 stream descriptors
                                    case 0x0d: // SO 13818-6 sections
                                    case 0x0e: // ISO 13818-1 auxiliary
                                    case 0x10: // MPEG4 Video
                                    case 0x12: // MPEG-4 generic
                                    case 0x13: // ISO 14496-1 SL-packetized
                                    case 0x14: // ISO 13818-6 Synchronized Download Protocol
                                    case 0x1B: // H.264 Video
                                    case 0x85: // ATSC Program ID
                                    case 0x92: // DVD_SPU vls Subtitle
                                    case 0xA0: // MSCODEC Video
                                    case 0xea: // Private ES (VC-1)
                                    default:
                                       if ( (streamType == 0x6) || (streamType >= 0x80) )
                                       {
                                          dataPids[dataComponentCount]= pid;
                                          dataTypes[dataComponentCount]= streamType;
                                          ++dataComponentCount;
                                       }
                                       else
                                       {
                                          GST_WARNING_OBJECT( filter, "%s:: pmt contains unsupported stream type %X", __FUNCTION__, streamType );
                                       }
                                       break;
                                 }
                                 programInfo += (5 + len);
                              }
                              
                              GST_INFO_OBJECT( filter, "%s:: found %d video, %d audio, and %d data pids in program %x with pcr pid %x",
                                       __FUNCTION__, videoComponentCount, audioComponentCount, dataComponentCount, filter->program, pcrPid );

                              filter->pcrPid= pcrPid;
                              filter->versionPMT= version;

                              result= gst_dtcp_enc_generate_pmt( filter,
                                                                    videoComponentCount, videoPids, videoTypes,
                                                                    audioComponentCount, audioPids, audioTypes, audioLanguages,
                                                                    dataComponentCount, dataPids, dataTypes );
                              if ( result )
                              {
                                 filter->haveNewPMT= TRUE;
                                 dumpPackets( filter, filter->newPMT, ts_packet_size );
                              }
                              
							  int i;
                              for( i= 0; i < audioComponentCount; ++i )
                              {
                                 if ( audioLanguages[i] )
                                 {
                                    free( audioLanguages[i] );
                                 }
                              }

                              filter->havePMT= TRUE;
                           }
                           else
                           {
                              GST_WARNING_OBJECT( filter, "%s:: ignoring multi-packet pmt (section length %d)", __FUNCTION__, sectionLength );
                           }
                        }
                     }                     
                  }
                  else
                  {
                     GST_WARNING_OBJECT( filter, "%s:: Warning: ignoring pmt TS packet with mismatched program of %x (expecting %x)", 
                                         __FUNCTION__, program, filter->program );
                  }                  
               }
               else
               {
                  GST_TRACE_OBJECT( filter, "%s:: Warning: ignoring pmt TS packet with tableid of %x", __FUNCTION__, tableid );
               }
            }
            else
            {
               GST_WARNING_OBJECT( filter, "%s:: Warning: ignoring pmt TS packet with adaptation of %x", __FUNCTION__, adaptation );
            }
            
            if ( filter->haveNewPMT )
            {
			   if(ttsSize)
			   {
                  filter->newPMT[0]= packet[0];
                  filter->newPMT[1]= packet[1];
                  filter->newPMT[2]= packet[2];
                  filter->newPMT[3]= packet[3];
			   }
               filter->newPMT[3+ttsSize]= packet[3+ttsSize];
               rc = memcpy_s( packet, ts_packet_size, filter->newPMT, ts_packet_size );
               if(rc != EOK)
               {
                   ERR_CHK(rc);
                   return FALSE;
               }
               dumpPackets( filter, packet, ts_packet_size );
            }
         }
         else
         {
            GST_WARNING_OBJECT( filter, "%s:: Warning: ignoring pmt TS without payload start indicator", __FUNCTION__ );
         }               
      }

      packet += ts_packet_size;
      ++packetCount;
   }

#ifdef USE_GST1
  gst_buffer_unmap (buf, &map);
#endif
   return result;
}

static void gst_dtcpencrypt_buf_delete( void *packet )
{
  if(packet)
  {
    GST_LOG("buffer to delete = %p\n", packet);
    DTCPMgrReleasePacket(packet);
    g_mutex_lock (packet_count_mutex);
    packet_count--;
    g_mutex_unlock (packet_count_mutex);
    g_slice_free (DTCPIP_Packet, packet);
  }
}

void onError(GstDtcpEnc* filter, int err_code, char* err_string)
{
    GstMessage *message;
    GError *error = NULL;

    error = g_error_new (GST_CORE_ERROR, err_code, err_string);
    if (error == NULL)
        GST_ERROR("error null");
    if(filter){
        GstElement *parent= GST_ELEMENT_PARENT (filter);    //CID:18616 - forward null
        message = gst_message_new_error (GST_OBJECT(parent), error, "DTCP AKE failed");
        if (message == NULL){
           GST_ERROR("DTCP error message creation failed");
        }
        else {
           if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ERROR)
               GST_ERROR("DTCP error message type failed");

           if (GST_MESSAGE_SRC (message) == NULL)
               GST_ERROR("DTCP error message src not found");

           if (gst_element_post_message (GST_ELEMENT (filter), message) == FALSE)
           {
               GST_ERROR("This element has no bus, therefore no message sent!");
           }
        }   //CID:18610 - Forward null
    }
    if (error)
        g_error_free (error);

    error = NULL;
}

#define ALIGNMENT_BUF_LEN (188*4)
/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
#ifdef USE_GST1
gst_dtcp_enc_chain (GstPad * pad, GstObject *parent, GstBuffer * buf)
#else
gst_dtcp_enc_chain (GstPad * pad, GstBuffer * buf)
#endif
{
  GstDtcpEnc *filter = NULL;
#ifdef USE_GST1
  GstMapInfo map;
#endif
  void *physicalAddress= NULL;
  void *virtualAddress= NULL;
  int dataLen;
  DTCPIP_Packet *packet = NULL;
  GstFlowReturn  ret = GST_FLOW_OK;
  errno_t rc = -1;

  if(NULL == pad) /*Coverity fix for issue 16638*/
  {
    GST_ERROR_OBJECT(filter, "%s:: DTCP GstPad passed is NULL\n", __FUNCTION__);
    onError( filter, GST_DTCPENC_EVENT_AKE_FAILED, "GstPad provided is NULL" );
    return GST_FLOW_ERROR;
  }

  filter = GST_DTCPENC (GST_OBJECT_PARENT (pad));
  if(NULL == filter || 0 == filter->pDtcpSession)
  {
    GST_ERROR_OBJECT(filter, "%s:: DTCP Session is NULL\n", __FUNCTION__);
    onError( filter, GST_DTCPENC_EVENT_AKE_FAILED, "DTCP session is null" );
    return GST_FLOW_ERROR;
  }

  if(!filter->tspacketsize)
  {
  	GstCaps *caps;
  	gint packetSize= 188;
  	
  	if ( pad ) {
  		GST_LOG_OBJECT (filter, "dtcpenc sink pad %p", pad );
#ifdef USE_GST1
		caps= gst_pad_get_current_caps( pad );
#else
		caps= gst_pad_get_negotiated_caps( pad );
#endif
		GST_LOG_OBJECT (filter, "dtcpenc caps %p", caps );
		if ( caps ) 
		{
			const GstStructure *str;
			str = gst_caps_get_structure (caps, 0);
			if ( !gst_structure_get_int (str, "packetsize", &packetSize) ) 
			{
				GST_WARNING_OBJECT( filter, "dtcpenc : caps do not specify TS packet size - assuming 188" );
			}
			gst_caps_unref(caps);
  	   }
  	}
	else
	{
    	GST_ERROR_OBJECT(filter, "%s:: GST PAD is NULL\n", __FUNCTION__);
	}
	filter->tspacketsize = packetSize;
  	GST_INFO_OBJECT (filter, "dtcpenc ts packet size = %d", filter->tspacketsize );
  }

#ifdef USE_GST1
 gst_buffer_map (buf, &map, (GstMapFlags) GST_MAP_READ);

  if(NULL == buf || NULL == map.data)
  {
    GST_ERROR_OBJECT(filter, "%s:: GST buffer / map.data is NULL\n", __FUNCTION__);
    ret = GST_FLOW_ERROR;
    goto out;
  }
#else
  if(NULL == buf || NULL == buf->data)
  {
    GST_ERROR_OBJECT(filter, "%s:: GST buffer / buf->data is NULL\n", __FUNCTION__);
    ret = GST_FLOW_ERROR;
    goto out;
  }
#endif

#ifndef XG5_GW 
//Temporarily disabling insert dtcpdesc for debugging DELIA-2022
/*
  if(QAMSRC == filter->srctype)
  {
  	GST_LOG_OBJECT(filter, "%s::inserting PMT\n", __FUNCTION__);
    if ( !gst_dtcp_enc_examine_buffer( filter, buf ) ) {
      GST_ERROR_OBJECT(filter, "%s:: Failed to add dtcp descriptors... returning....\n", __FUNCTION__);
      ret = GST_FLOW_ERROR;
      goto out;
    }
  }
*/
#endif

#ifdef USE_GST1
  virtualAddress = map.data;
  dataLen = map.size;
#else
  virtualAddress= GST_BUFFER_DATA(buf);
  dataLen= GST_BUFFER_SIZE(buf);
#endif

#ifdef USE_GST1
  GST_LOG_OBJECT(filter, "%s::Encrypting... with EMI = %d, bufferSize = %d\n", __FUNCTION__, filter->EMI, gst_buffer_get_size(buf));
#else
  GST_LOG_OBJECT(filter, "%s::Encrypting... with EMI = %d, bufferSize = %d\n", __FUNCTION__, filter->EMI, buf->size);
#endif

  if ((0 == dataLen) || (NULL == virtualAddress))
  {
    GST_WARNING_OBJECT (filter, "%s :: The Incoming buffer for Encryption is either NULL or Empty... \n", __FUNCTION__);  //CID:42330 - Print args
    ret = GST_FLOW_OK;
    goto out;
  }

  packet = g_slice_new0 (DTCPIP_Packet);
  if(packet == NULL) {
    GST_ERROR_OBJECT(filter, "Error while allocating memory for dtcp packet of size %d... \n", sizeof(DTCPIP_Packet));
    ret = GST_FLOW_ERROR;
    goto out;
  }

/* Use this GstBuffer flag to identify PSI Data */
#ifdef USE_GST1
  // Hack - Data can be received from remote streamer which runs gstreamer 0.10
  // GStreamer 0.10
  // gst/gstminiobject.h:  GST_MINI_OBJECT_FLAG_LAST  = (1<<4)
  // gst/gstbuffer.h:      GST_BUFFER_FLAG_MEDIA1     = (GST_MINI_OBJECT_FLAG_LAST << 5),
  // GStreamer 1.0
  // gst/gstminiobject.h:  GST_MINI_OBJECT_FLAG_LAST  = (1 << 4)
  // gst/gstbuffer.h:      GST_BUFFER_FLAG_MARKER     = (GST_MINI_OBJECT_FLAG_LAST << 5),
  if(GST_BUFFER_FLAG_IS_SET(buf,GST_BUFFER_FLAG_MARKER))
#else
  if(GST_BUFFER_FLAG_IS_SET(buf,GST_BUFFER_FLAG_MEDIA1))
#endif
  {
      packet->emi=0x00;
  }
  else
  {
      packet->emi=filter->EMI;
  }
  packet->session= filter->pDtcpSession;
  packet->dataInPhyPtr = physicalAddress;
  packet->dataInPtr = virtualAddress;
  packet->dataLength = dataLen;
  if(physicalAddress==NULL)
  {
    packet->dataOutPtr = NULL;
  }
  else
  {
    packet->dataOutPtr = virtualAddress;
  }
  packet->dataOutPhyPtr = physicalAddress;
  packet->pcpHeader = NULL;

  if(0 > DTCPMgrProcessPacket(filter->pDtcpSession, packet))
  {
    GST_ERROR_OBJECT(filter, "%s::Error while encrypting... \n", __FUNCTION__);
    /* Don't have to call DTCPMgrReleasePacket() if DTCPMgrProcessPacket()
     * failed */
    g_slice_free (DTCPIP_Packet, packet);
    packet = NULL;
    ret = GST_FLOW_ERROR;
    goto out;
  }

  if(filter->isFirstPacket)
  {
    if( packet->pcpHeaderOffset == 0 && packet->pcpHeader && packet->pcpHeaderLength )
    {
      GST_INFO_OBJECT (filter, "dtcpenc::Received first packet[%d]. header: ", dataLen);
      int ix;
      for(ix=0; ix<14; ix++)
        g_print("%02X ", packet->pcpHeader[ix]);
      g_print("\n");
    }
    else
    {
      GST_ERROR_OBJECT (filter, "dtcpenc::Received first packet. NO DTCP HEADER . ERROR ERROR ERROR \n");
    }
    filter->isFirstPacket = FALSE;
  }

  GstBuffer *dtcpHeaderBuf = NULL;
  GstBuffer *dtcpDataBuf = NULL;
  GstBuffer *dtcpDataBuf2 = NULL;
  GstBufferList *bufferlist_to_send = gst_buffer_list_new();
#ifndef USE_GST1
  GstBufferListIterator *it = gst_buffer_list_iterate (bufferlist_to_send);

  gst_buffer_list_iterator_add_group(it);
#endif

  if( packet->pcpHeaderOffset >= 0 && packet->pcpHeader && packet->pcpHeaderLength )
  {
#ifdef USE_GST1
    uint8_t *pcpHeaderBuf = (uint8_t *)g_malloc0(sizeof(uint8_t) * packet->pcpHeaderLength);
	if (NULL == pcpHeaderBuf) {
      GST_ERROR_OBJECT(filter, "Error while allocating pcpHeaderBuf of size %d.. \n", packet->pcpHeaderLength);
      gst_buffer_list_unref(bufferlist_to_send);
      ret = GST_FLOW_ERROR;
      goto out;
    }

    rc = memcpy_s(pcpHeaderBuf, sizeof(uint8_t) * packet->pcpHeaderLength, packet->pcpHeader, sizeof(uint8_t) * packet->pcpHeaderLength);
    if(rc != EOK)
    {
       ERR_CHK(rc);
       g_free(pcpHeaderBuf);
       gst_buffer_list_unref(bufferlist_to_send);
       ret = GST_FLOW_ERROR;
       goto out;
    }

    // pcpHeaderBuf g_malloc'd above will be free using g_free when dtcpHeaderBuf is unreffed
    dtcpHeaderBuf = gst_buffer_new_wrapped (pcpHeaderBuf, packet->pcpHeaderLength);
	if (NULL == dtcpHeaderBuf) {
      GST_ERROR_OBJECT(filter, "Error while allocating the GST buffer of size %d.. \n", packet->pcpHeaderLength);
      g_free(pcpHeaderBuf);
      gst_buffer_list_unref(bufferlist_to_send);
      ret = GST_FLOW_ERROR;
      goto out;
	}
#else
    dtcpHeaderBuf = gst_buffer_new_and_alloc (packet->pcpHeaderLength);

	if (NULL == dtcpHeaderBuf)
	{
      GST_ERROR_OBJECT(filter, "%s::Error while allocating the GST buffer of size %d.. \n", __FUNCTION__, packet->pcpHeaderLength);
      gst_buffer_list_iterator_free(it);
      gst_buffer_list_unref(bufferlist_to_send);
      ret = GST_FLOW_ERROR;
      goto out;
	}

      rc =  memcpy_s(GST_BUFFER_DATA(dtcpHeaderBuf), GST_BUFFER_SIZE(dtcpHeaderBuf), packet->pcpHeader, packet->pcpHeaderLength);
      if(rc != EOK)
      {
          ERR_CHK(rc);
          gst_buffer_list_iterator_free(it);
          gst_buffer_list_unref(bufferlist_to_send);
          ret = GST_FLOW_ERROR;
          goto out;
      }
	
#endif
  }

  if ( NULL == dtcpHeaderBuf ) 
  {
#ifdef USE_GST1
    dtcpDataBuf = gst_buffer_new_wrapped_full (0,
                                               packet->dataOutPtr,
                                               packet->dataLength,
                                               0,
                                               packet->dataLength,
                                               packet,
                                               gst_dtcpencrypt_buf_delete);

    gst_buffer_list_add (bufferlist_to_send, dtcpDataBuf);
#else
    dtcpDataBuf = gst_buffer_new();
    GST_BUFFER_DATA(dtcpDataBuf)= packet->dataOutPtr;
    GST_BUFFER_MALLOCDATA(dtcpDataBuf)= packet;
    GST_BUFFER_FREE_FUNC(dtcpDataBuf)= gst_dtcpencrypt_buf_delete;
    GST_BUFFER_SIZE(dtcpDataBuf)= packet->dataLength;
    GST_BUFFER_OFFSET(dtcpDataBuf)= GST_BUFFER_OFFSET_NONE;
    GST_BUFFER_OFFSET_END(dtcpDataBuf)= GST_BUFFER_OFFSET_NONE;

    gst_buffer_list_iterator_add (it, dtcpDataBuf);
#endif
  }
  else
  {
    // we may need to split data to 2 part to insert PCP header here,
    unsigned int first_section_size = (packet->pcpHeaderOffset > 0) ? packet->pcpHeaderOffset : 0;
    unsigned int second_section_size = (packet->dataLength > packet->pcpHeaderOffset) ? packet->dataLength - packet->pcpHeaderOffset : 0;
  
    if ( 0 != first_section_size ) 
	{
#ifdef USE_GST1
      if (0 == second_section_size)
      {
        /* No second section, pass ownership of 'packet' to the first buffer */
        dtcpDataBuf = gst_buffer_new_wrapped_full (0,
                                                   packet->dataOutPtr,
                                                   first_section_size,
                                                   0,
                                                   first_section_size,
                                                   packet,
                                                   gst_dtcpencrypt_buf_delete);
      }
      else
      {
        /* The second buffer will take owernship of 'packet' */
        dtcpDataBuf = gst_buffer_new_wrapped_full (0,
                                                   packet->dataOutPtr,
                                                   first_section_size,
                                                   0,
                                                   first_section_size,
                                                   NULL,
                                                   NULL);
      }

      //Inserting first section
      gst_buffer_list_add (bufferlist_to_send, dtcpDataBuf);
#else
      dtcpDataBuf = gst_buffer_new();
      GST_BUFFER_DATA(dtcpDataBuf)= packet->dataOutPtr;
      if(0 == second_section_size)
      {
        GST_BUFFER_MALLOCDATA(dtcpDataBuf)= packet;
        GST_BUFFER_FREE_FUNC(dtcpDataBuf)= gst_dtcpencrypt_buf_delete;
      }
      GST_BUFFER_SIZE(dtcpDataBuf)= first_section_size;
      GST_BUFFER_OFFSET(dtcpDataBuf)= GST_BUFFER_OFFSET_NONE;
      GST_BUFFER_OFFSET_END(dtcpDataBuf)= GST_BUFFER_OFFSET_NONE;

      //Inserting first section
      gst_buffer_list_iterator_add (it, dtcpDataBuf);
#endif
    } 

    //Inserting pcp header
#ifdef USE_GST1
    gst_buffer_list_add (bufferlist_to_send, dtcpHeaderBuf);
#else
    gst_buffer_list_iterator_add (it, dtcpHeaderBuf);
#endif
  
    if ( 0 != second_section_size ) 
	{
#ifdef USE_GST1
      dtcpDataBuf2 = gst_buffer_new_wrapped_full (0,
                                                  packet->dataOutPtr + first_section_size,
                                                  second_section_size,
                                                  0,
                                                  second_section_size,
                                                  packet,
                                                  gst_dtcpencrypt_buf_delete);

      //Inserting second section
      gst_buffer_list_add (bufferlist_to_send, dtcpDataBuf2);
#else
      dtcpDataBuf2 = gst_buffer_new();
      GST_BUFFER_DATA(dtcpDataBuf2)= packet->dataOutPtr + first_section_size;
      GST_BUFFER_SIZE(dtcpDataBuf2)= second_section_size;
      GST_BUFFER_OFFSET(dtcpDataBuf2)= GST_BUFFER_OFFSET_NONE;
      GST_BUFFER_OFFSET_END(dtcpDataBuf2)= GST_BUFFER_OFFSET_NONE;
//      if(0 == first_section_size)
      {
        GST_BUFFER_MALLOCDATA(dtcpDataBuf2)= packet;
        GST_BUFFER_FREE_FUNC(dtcpDataBuf2)= gst_dtcpencrypt_buf_delete;
      }

      //Inserting second section
      gst_buffer_list_iterator_add (it, dtcpDataBuf2);
#endif
    }        
  }

  /* We pass the ownership of 'packet' to the buffer */
  packet = NULL;

	g_mutex_lock (packet_count_mutex);
	packet_count++;
	g_mutex_unlock (packet_count_mutex);

  //Sending data
  ret = gst_pad_push_list(filter->srcpad, bufferlist_to_send);
  if(ret < GST_FLOW_OK)
  {
	if(ret == -2 /*GST_FLOW_FLUSHING*/) 
	  GST_LOG_OBJECT(filter, "%s::PEER PAD IS FLUSHING... \n", __FUNCTION__);
	GST_LOG_OBJECT(filter, "%s::gst_pad_push_list ret = %d\n", __FUNCTION__, ret);
  }

#ifndef USE_GST1
  gst_buffer_list_iterator_free(it);
#endif

out:
#ifdef USE_GST1
  gst_buffer_unmap (buf, &map);
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
dtcpenc_init (GstPlugin * dtcpenc)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template dtcpenc' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_dtcp_enc_debug, "dtcpenc",
      (GST_DEBUG_BOLD | GST_DEBUG_FG_RED), "dtcpencrypt element");

  return gst_element_register (dtcpenc, "dtcpenc", GST_RANK_NONE,
      GST_TYPE_DTCPENC);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "dtcpenc"
#endif

/* gstreamer looks for this structure to register dtcpencs
 *
 * exchange the string 'Template dtcpenc' with your dtcpenc description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
#ifdef USE_GST1
    dtcpenc,
#else
    "dtcpenc",
#endif
    "create dtcp stream and encrypt the data : dtcpenc",
    dtcpenc_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)


/** @} */
/** @} */
