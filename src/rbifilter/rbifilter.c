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
 
 /*
  * This file contains text from http://cgit.freedesktop.org/gstreamer/gst-plugins-base/tree/gst/playback/gstqueue2.c?id=RELEASE-0_10_13
  * It was added on 2013-08-07
  */



/**
* @defgroup gst-plugins-rdk
* @{
* @defgroup rbifilter
* @{
**/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

typedef unsigned char uint8_t;

#include "rbifilter.h"

#define GST_PACKAGE_ORIGIN "http://gstreamer.net/"

#define STATIC_CAPS \
           "video/mpegts;" \
           "video/mpegts, " \
           "  systemstream=(boolean)true, "\
           "  packetsize=(int)188;"\
           "video/mpegts, " \
           "  systemstream=(boolean)true, "\
           "  packetsize=(int)192;"\
           "video/vnd.dlna.mpeg-tts, " \
           "  systemstream=(boolean)true, "\
           "  packetsize=(int)192;"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(STATIC_CAPS));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(STATIC_CAPS));

GST_DEBUG_CATEGORY_STATIC (gst_rbifilter_debug);
#define GST_CAT_DEFAULT gst_rbifilter_debug


enum
{
  PROP_0,
  PROP_PACKET_IN_CALLBACK,
  PROP_PACKET_OUT_SIZE_CALLBACK,
  PROP_PACKET_OUT_DATA_CALLBACK,
  PROP_CONTEXT
};

#ifdef USE_GST1
#define gst_rbifilter_parent_class parent_class
G_DEFINE_TYPE (GstRBIFilter, gst_rbifilter, GST_TYPE_ELEMENT);
#else
#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_rbifilter_debug, "rbifilter", 0, "rbifilter element");

GST_BOILERPLATE_FULL (GstRBIFilter, gst_rbifilter, GstElement,
    GST_TYPE_ELEMENT, _do_init);
#endif

typedef enum _PendingItemType
{
   pendingItem_None,
   pendingItem_Event,
   pendingItem_Buffer,
} PendingItemType;

typedef gboolean (*packetInCB)( void *ctx, unsigned char *packets, int* len );
typedef int (*packetOutSizeCB)( void *ctx );
typedef int (*packetOutDataCB)( void *ctx, unsigned char *packets, int len );

static void gst_rbifilter_finalize (GObject * object);
static void gst_rbifilter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rbifilter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_rbifilter_change_state (GstElement * element,
    GstStateChange transition);
static gboolean
gst_rbifilter_set_caps (GstPad * pad, GstCaps * caps);
static void 
gst_rbifilter_lockItems( GstRBIFilter * rbifilter );
static void 
gst_rbifilter_unlockItems( GstRBIFilter * rbifilter );
static void 
gst_rbifilter_waitNotEmpty( GstRBIFilter * rbifilter );
static void 
gst_rbifilter_signalNotEmpty( GstRBIFilter * rbifilter );
static gboolean 
gst_rbifilter_add_event( GstRBIFilter *rbifilter, GstEvent *event );
static gboolean 
gst_rbifilter_add_buffer( GstRBIFilter *rbifilter, GstBuffer *buffer );
static void
gst_rbifilter_flush( GstRBIFilter *rbifilter );
static gboolean
#ifdef USE_GST1
gst_rbifilter_sink_event(GstPad *pad, GstObject *parent, GstEvent *event );
#else
gst_rbifilter_sink_event(GstPad *pad, GstEvent *event );
#endif
static GstFlowReturn
#ifdef USE_GST1
gst_rbifilter_chain (GstPad * pad, GstObject *parent, GstBuffer * buffer);
#else
gst_rbifilter_chain (GstPad * pad, GstBuffer * buffer);
#endif
static void
gst_rbifilter_loop( GstPad * pad );

#ifndef USE_GST1
static void
gst_rbifilter_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (gst_rbifilter_debug, "rbifilter", 0,
      "RBI filter element");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_set_details_simple (gstelement_class, "RBI Filter",
      "Transform",
      "Accepts and forwards mpegts data monitoring for RBI triggers.  When triggered inserts replacement stream data.",
      "Comcast");
}
#endif

static void
gst_rbifilter_finalize (GObject * object)
{
  GstRBIFilter *rbifilter;

  rbifilter = GST_RBIFILTER (object);
  
  #ifdef GLIB_VERSION_2_32 
  g_mutex_clear( &rbifilter->lockItems );
  g_cond_clear( &rbifilter->condNotEmpty );
  #else
  g_mutex_free( rbifilter->lockItems );
  g_cond_free( rbifilter->condNotEmpty );
  #endif  
  
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rbifilter_class_init (GstRBIFilterClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_rbifilter_set_property;
  gobject_class->get_property = gst_rbifilter_get_property;

  g_object_class_install_property (gobject_class, PROP_CONTEXT,
      g_param_spec_pointer (
          "rbi-context",
          "RBI Context",
          "Context to pass to RBI packet processor.",
          (GParamFlags)G_PARAM_READWRITE ));
  g_object_class_install_property (gobject_class, PROP_PACKET_IN_CALLBACK,
      g_param_spec_pointer (
          "rbi-packet-in-callback",
          "RBI packet input callback",
          "RBI packet input callback of form gboolean (*cb)( void *ctx, unsigned char* packets, int* len ).",
          (GParamFlags)G_PARAM_READWRITE ));
  g_object_class_install_property (gobject_class, PROP_PACKET_OUT_SIZE_CALLBACK,
      g_param_spec_pointer (
          "rbi-packet-out-size-callback",
          "RBI packet output size callback",
          "RBI packet output size callback of form int (*cb)( void *ctx ).",
          (GParamFlags)G_PARAM_READWRITE ));
  g_object_class_install_property (gobject_class, PROP_PACKET_OUT_DATA_CALLBACK,
      g_param_spec_pointer (
          "rbi-packet-out-data-callback",
          "RBI packet output data callback",
          "RBI packet output data callback of form int (*cb)( void *ctx, unsigned char* packets, int len ).",
          (GParamFlags)G_PARAM_READWRITE ));

  gobject_class->finalize = gst_rbifilter_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rbifilter_change_state);

#ifdef USE_GST1
  GST_DEBUG_CATEGORY_INIT (gst_rbifilter_debug, "rbifilter", 0,
      "RBI filter element");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_static_metadata (gstelement_class, "RBI Filter",
      "Transform",
      "Accepts and forwards mpegts data monitoring for RBI triggers.  When triggered inserts replacement stream data.",
      "Comcast");
#endif
}

static void
#ifdef USE_GST1
gst_rbifilter_init (GstRBIFilter * rbifilter)
#else
gst_rbifilter_init (GstRBIFilter * rbifilter, GstRBIFilterClass * g_class)
#endif
{
  rbifilter->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
#ifndef USE_GST1
  gst_pad_set_setcaps_function (rbifilter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_rbifilter_set_caps));
  gst_pad_set_getcaps_function (rbifilter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));
#endif
  gst_pad_set_chain_function (rbifilter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_rbifilter_chain));
                              
  rbifilter->srcpad = gst_pad_new_from_static_template (&srctemplate, "src");
#ifdef USE_GST1
  GST_PAD_SET_PROXY_CAPS (rbifilter->srcpad);
  GST_PAD_SET_PROXY_CAPS (rbifilter->sinkpad);
#else
  gst_pad_set_getcaps_function (rbifilter->srcpad,
                                GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));
#endif
  gst_pad_set_event_function (rbifilter->sinkpad, GST_DEBUG_FUNCPTR(gst_rbifilter_sink_event) );


  gst_element_add_pad (GST_ELEMENT (rbifilter), rbifilter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (rbifilter), rbifilter->srcpad);

  #ifdef GLIB_VERSION_2_32 
  g_cond_init( &rbifilter->condNotEmpty );
  g_mutex_init( &rbifilter->lockItems );
  #else
  rbifilter->condNotEmpty= g_cond_new();
  rbifilter->lockItems= g_mutex_new();
  #endif
  rbifilter->pendingItems= NULL;

  rbifilter->playing = FALSE;
  rbifilter->inserting = FALSE;
  rbifilter->srcRet= GST_FLOW_OK;
  rbifilter->rbiContext = 0;
  rbifilter->rbiPacketOutSizeCallback = 0;
  rbifilter->rbiPacketOutDataCallback = 0;
}

static void
gst_rbifilter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRBIFilter *rbifilter;

  rbifilter = GST_RBIFILTER (object);

  switch (prop_id) {
    case PROP_PACKET_IN_CALLBACK:
      rbifilter->rbiPacketInCallback= g_value_get_pointer (value);
      break;
    case PROP_PACKET_OUT_SIZE_CALLBACK:
      rbifilter->rbiPacketOutSizeCallback= g_value_get_pointer (value);
      break;
    case PROP_PACKET_OUT_DATA_CALLBACK:
      rbifilter->rbiPacketOutDataCallback= g_value_get_pointer (value);
      break;
    case PROP_CONTEXT:
      rbifilter->rbiContext= g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rbifilter_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRBIFilter *rbifilter;

  rbifilter = GST_RBIFILTER (object);

  switch (prop_id) {
    case PROP_PACKET_IN_CALLBACK:
      g_value_set_pointer (value, rbifilter->rbiPacketInCallback);
      break;
    case PROP_PACKET_OUT_SIZE_CALLBACK:
      g_value_set_pointer (value, rbifilter->rbiPacketOutSizeCallback);
      break;
    case PROP_PACKET_OUT_DATA_CALLBACK:
      g_value_set_pointer (value, rbifilter->rbiPacketOutDataCallback);
      break;
    case PROP_CONTEXT:
      g_value_set_pointer (value, rbifilter->rbiContext);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rbifilter_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRBIFilter *rbifilter = GST_RBIFILTER (element);

  (void)(rbifilter);

  GST_DEBUG_OBJECT(element, "rbifilter: change state from %s to %s\n", 
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      {
         if ( !rbifilter->rbiContext ) {
            GST_ERROR_OBJECT(rbifilter, "missing RBI context");
            ret= GST_STATE_CHANGE_FAILURE;
            goto failure;
         }

         if ( !rbifilter->rbiPacketInCallback ) {
            GST_ERROR_OBJECT(rbifilter, "missing RBI packet-in-callback");
            ret= GST_STATE_CHANGE_FAILURE;
            goto failure;
         }

         if ( !rbifilter->rbiPacketOutSizeCallback ) {
            GST_ERROR_OBJECT(rbifilter, "missing RBI packet-out-size-callback");
            ret= GST_STATE_CHANGE_FAILURE;
            goto failure;
         }

         if ( !rbifilter->rbiPacketOutDataCallback ) {
            GST_ERROR_OBJECT(rbifilter, "missing RBI packet-out-data-callback");
            ret= GST_STATE_CHANGE_FAILURE;
            goto failure;
         }
     
         rbifilter->playing = TRUE;
         rbifilter->inserting = FALSE;
         rbifilter->srcRet= GST_FLOW_OK;
         
         GST_DEBUG_OBJECT(element, "rbifilter: change_state: starting task\n");
         #ifdef USE_GST1
         gboolean rc= gst_pad_start_task(rbifilter->srcpad, (GstTaskFunction)gst_rbifilter_loop, rbifilter->srcpad, NULL);
         #else
         gboolean rc= gst_pad_start_task(rbifilter->srcpad, (GstTaskFunction)gst_rbifilter_loop, rbifilter->srcpad);
         #endif
         if ( !rc ) {
            GST_ERROR_OBJECT(rbifilter, "unable to start srcpad task");
            ret= GST_STATE_CHANGE_FAILURE;
            goto failure;
         }
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      {
         rbifilter->playing = FALSE;          

         // Wake task if waiting for items
         gst_rbifilter_lockItems( rbifilter );
         gst_rbifilter_signalNotEmpty( rbifilter );
         gst_rbifilter_unlockItems( rbifilter );
         
         GST_DEBUG_OBJECT(element, "rbifilter: change_state: stopping task\n");
         gboolean rc= gst_pad_stop_task(rbifilter->srcpad );
         if ( !rc )
         {
            GST_ERROR_OBJECT(rbifilter, "unable to stop srcpad task");
         }
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      rbifilter->inserting = FALSE;
      rbifilter->srcRet= GST_FLOW_OK;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

failure:

  return ret;
}

static void gst_rbifilter_lockItems( GstRBIFilter * rbifilter )
{
  #ifdef GLIB_VERSION_2_32 
  g_mutex_lock( &rbifilter->lockItems );
  #else
  g_mutex_lock( rbifilter->lockItems );
  #endif
}

static void gst_rbifilter_unlockItems( GstRBIFilter * rbifilter )
{
  #ifdef GLIB_VERSION_2_32 
  g_mutex_unlock( &rbifilter->lockItems );
  #else
  g_mutex_unlock( rbifilter->lockItems );
  #endif
}

static void gst_rbifilter_waitNotEmpty( GstRBIFilter * rbifilter )
{
  #ifdef GLIB_VERSION_2_32
  g_cond_wait( &rbifilter->condNotEmpty, &rbifilter->lockItems );
  #else
  g_cond_wait( rbifilter->condNotEmpty, rbifilter->lockItems );
  #endif 
}

static void gst_rbifilter_signalNotEmpty( GstRBIFilter * rbifilter )
{
  #ifdef GLIB_VERSION_2_32
  g_cond_signal( &rbifilter->condNotEmpty );
  #else
  g_cond_signal( rbifilter->condNotEmpty );
  #endif
}

static gboolean gst_rbifilter_add_event( GstRBIFilter *rbifilter, GstEvent *event )
{
  gboolean ret = TRUE;
  
  gst_rbifilter_lockItems( rbifilter );

  if ( rbifilter->playing ) {
     rbifilter->pendingItems= g_list_append( rbifilter->pendingItems, GINT_TO_POINTER(pendingItem_Event) );
     rbifilter->pendingItems= g_list_append( rbifilter->pendingItems, event );
     gst_rbifilter_signalNotEmpty( rbifilter );
  } else {
     ret = gst_pad_push_event( rbifilter->srcpad, event );
  }
  
  gst_rbifilter_unlockItems( rbifilter );
  
  return ret;
}

static gboolean gst_rbifilter_add_buffer( GstRBIFilter *rbifilter, GstBuffer *buffer )
{
  gboolean ret = TRUE;
  
  gst_rbifilter_lockItems( rbifilter );

  if ( rbifilter->playing ) {
     rbifilter->pendingItems= g_list_append( rbifilter->pendingItems, GINT_TO_POINTER(pendingItem_Buffer) );
     rbifilter->pendingItems= g_list_append( rbifilter->pendingItems, buffer );
     gst_rbifilter_signalNotEmpty( rbifilter );
  } else {
     ret = gst_pad_push( rbifilter->srcpad, buffer );
  }
  
  gst_rbifilter_unlockItems( rbifilter );

  return ret;
}

static void
gst_rbifilter_flush( GstRBIFilter *rbifilter )
{
  GList *walk, *free= NULL;
  
  GST_DEBUG_OBJECT(rbifilter, "rbifilter: flush: enter\n");
  gst_rbifilter_lockItems( rbifilter );

  walk= rbifilter->pendingItems;
  while( walk )
  {
     if ( GPOINTER_TO_INT(walk->data) == pendingItem_Buffer ) {
        free= walk;
     }
     walk= g_list_next( walk );
     if ( free ) {
        rbifilter->pendingItems= g_list_delete_link( rbifilter->pendingItems, free );
        free= walk;
     }
     walk= g_list_next( walk );
     if ( free ) {
        GstBuffer *buffer= (GstBuffer*)free->data;
        gst_buffer_unref( buffer );
        rbifilter->pendingItems= g_list_delete_link( rbifilter->pendingItems, free );
     }
     free= NULL;
  }

  gst_rbifilter_unlockItems( rbifilter );
  GST_DEBUG_OBJECT(rbifilter, "rbifilter: flush: exit\n");
}

#ifndef GST_USE1
static gboolean
gst_rbifilter_set_caps (GstPad * pad, GstCaps * caps)
{
  GstRBIFilter *rbifilter;
  GstPad *otherpad;

  rbifilter = GST_RBIFILTER (gst_pad_get_parent (pad));
  otherpad = (pad == rbifilter->srcpad) ? rbifilter->sinkpad : rbifilter->srcpad;
  gst_object_unref (rbifilter);

  return gst_pad_set_caps (otherpad, caps);
}
#endif

static gboolean 
#ifdef USE_GST1
gst_rbifilter_sink_event(GstPad *pad, GstObject *parent, GstEvent *event )
#else
gst_rbifilter_sink_event(GstPad *pad, GstEvent *event )
#endif
{
  gboolean ret;
  GstRBIFilter *rbifilter;
  
  rbifilter = GST_RBIFILTER (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT(rbifilter, "rbifilter: got EVENT %d : %s\n", GST_EVENT_TYPE(event), GST_EVENT_TYPE_NAME(event) );
  switch( GST_EVENT_TYPE(event) ) {
     #ifdef USE_GST1
     case GST_EVENT_SEGMENT:
     #else
     case GST_EVENT_NEWSEGMENT:
     #endif
        ret= gst_rbifilter_add_event( rbifilter, event );
        break;
     case GST_EVENT_EOS:
        ret= gst_rbifilter_add_event( rbifilter, event );
        break;
     case GST_EVENT_FLUSH_START:
        gst_rbifilter_flush( rbifilter);
        ret= gst_rbifilter_add_event( rbifilter, event );
        break;
     case GST_EVENT_FLUSH_STOP:
        ret= gst_rbifilter_add_event( rbifilter, event );
        break;
     default:
        ret = gst_pad_event_default( pad,
                                     #ifdef USE_GST1
                                     parent,
                                     #endif 
                                     event ); 
        break;
  }
    
  gst_object_unref (rbifilter);
  return ret;
}


static GstFlowReturn
#ifdef USE_GST1
gst_rbifilter_chain (GstPad * pad, GstObject *parent, GstBuffer * buffer)
#else
gst_rbifilter_chain (GstPad * pad, GstBuffer * buffer)
#endif
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstRBIFilter *rbifilter;

  rbifilter = GST_RBIFILTER (gst_pad_get_parent (pad));
    
  if ( rbifilter->srcRet == GST_FLOW_OK ) {
  
    if ( rbifilter->rbiContext ) {
    
      gst_rbifilter_add_buffer( rbifilter, buffer );
      
    }
  } else {
  
     ret= rbifilter->srcRet;
     
  }
  gst_object_unref (rbifilter);
  
  return ret;
}

static void
gst_rbifilter_loop( GstPad * pad )
{
  GstFlowReturn ret;
  GstRBIFilter *rbifilter;
  rbifilter = GST_RBIFILTER (gst_pad_get_parent (pad));

  if ( rbifilter->rbiContext ) {
    GList *first;
    PendingItemType type= pendingItem_None;
    GstEvent *event= NULL;
    GstBuffer *buffer= NULL;
    gboolean usePendingItems;

    /*
     * Calling packet in callback with null buffer checks if we are currently inserting
     */
    rbifilter->inserting = ((packetInCB)rbifilter->rbiPacketInCallback)( rbifilter->rbiContext, 0, 0 );;

    gst_rbifilter_lockItems( rbifilter );

    while( !rbifilter->inserting && !rbifilter->pendingItems && rbifilter->playing )
    {
       gst_rbifilter_waitNotEmpty( rbifilter );
    }

    if ( rbifilter->pendingItems ) {
    
       first= rbifilter->pendingItems;
       type= (PendingItemType)GPOINTER_TO_INT(first->data);
       rbifilter->pendingItems= g_list_delete_link( rbifilter->pendingItems, first );
       first= rbifilter->pendingItems;
       switch( type ) {
          case pendingItem_Event:
             event= (GstEvent*)first->data;
             break;
          case pendingItem_Buffer:
             buffer= (GstBuffer*)first->data;
             break;
          case pendingItem_None:
          default:
             break;
       }
       rbifilter->pendingItems= g_list_delete_link( rbifilter->pendingItems, first );
       
    }

    gst_rbifilter_unlockItems( rbifilter );

    if ( type == pendingItem_Buffer ) {
      gboolean pushBuffer;
       
      #ifdef USE_GST1
      GstMapInfo map;
      int size;

      gst_buffer_map (buffer, &map, (GstMapFlags)GST_MAP_READWRITE);
      
      size= map.size;
      pushBuffer= ((packetInCB)rbifilter->rbiPacketInCallback)( rbifilter->rbiContext, map.data, &size );
      
      if ( size != map.size )
      {
         gst_buffer_set_size( buffer, size );
      }
      gst_buffer_unmap (buffer, &map);
      #else
      unsigned char *data;
      int size, originalSize;
      data = GST_BUFFER_DATA(buffer);
      originalSize = size = GST_BUFFER_SIZE(buffer);
      
      pushBuffer= ((packetInCB)rbifilter->rbiPacketInCallback)( rbifilter->rbiContext, data, &size );
      
      if ( size != originalSize )
      {
         GST_BUFFER_SIZE(buffer)= size;
      }
      #endif
      
      if ( !pushBuffer ) {
         gst_buffer_unref( buffer );
         type = pendingItem_None;
      }
    }

    if ( (type == pendingItem_None) && (rbifilter->inserting == TRUE) ) {
      int size= 0;

      size = ((packetOutSizeCB)rbifilter->rbiPacketOutSizeCallback)( rbifilter->rbiContext );
      if ( size > 0 ) {
        
        #ifdef USE_GST1
        buffer= gst_buffer_new_allocate( 0,  // default allocator
                                         size,
                                         0 ); // no GstAllocationParams
        #else
        buffer= gst_buffer_new_and_alloc( size );
        #endif
      
        if ( buffer ) {
          
          #ifdef USE_GST1
          GstMapInfo map;
        
          gst_buffer_map (buffer, &map, (GstMapFlags)GST_MAP_READWRITE);
        
          size= ((packetOutDataCB)rbifilter->rbiPacketOutDataCallback)( rbifilter->rbiContext, map.data, map.size );
          
          if ( size != map.size )
          {
             gst_buffer_set_size( buffer, size );
          }
        
          gst_buffer_unmap (buffer, &map);
          #else
          unsigned char *data;
          data = GST_BUFFER_DATA(buffer);

          size= ((packetOutDataCB)rbifilter->rbiPacketOutDataCallback)( rbifilter->rbiContext, data, size );
          
          if ( size != (int)GST_BUFFER_SIZE(buffer) )
          {
             GST_BUFFER_SIZE(buffer)= size;
          }
          #endif

          if ( size ) {          
             type= pendingItem_Buffer;
          } else {
             type= pendingItem_None;
             if ( buffer ) {
                gst_buffer_unref( buffer );
                buffer= NULL;
             }
          }
          
        } else {
          GST_ERROR_OBJECT(rbifilter, "unable to alloc output gst buffer");
        }
      }
    }

    switch( type ) {
       case pendingItem_Event:
          if ( event ) {
             gst_pad_push_event( rbifilter->srcpad, event );
          }
          break;
       case pendingItem_Buffer:
          if ( buffer ) {    
            ret = gst_pad_push( rbifilter->srcpad, buffer );
            if ( ret != GST_FLOW_OK ) {
              GST_ERROR_OBJECT(rbifilter, "error pushing buffer: %d: pausing task", ret);
              
              rbifilter->srcRet = ret;
              
              gst_task_pause( GST_PAD_TASK(pad) );
            }                
          }
          break;
       case pendingItem_None:
       default:
          break;
    }
  }  


  gst_object_unref (rbifilter);
}

static gboolean
rbifilter_init (GstPlugin * plugin)
{
  gst_element_register (plugin, "rbifilter", GST_RANK_NONE,
      gst_rbifilter_get_type ());

  return TRUE;
}

#ifndef PACKAGE
#define PACKAGE "myfirstrbifilter"
#endif

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
#ifdef USE_GST1
    rbifilter,
#else
    "rbifilter",
#endif
    "Accepts and forwards mpegts data monitoring for RBI triggers.  When triggered inserts replacement stream data.",
    rbifilter_init, 
    VERSION, 
    "LGPL", 
    PACKAGE_NAME,
    GST_PACKAGE_ORIGIN )



/** @} */
/** @} */
