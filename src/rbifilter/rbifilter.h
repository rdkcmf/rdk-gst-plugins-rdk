/*
 * Copyright 2013 RDK Management
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
  *  @defgroup RBI_FILTER   RBI filter
  *  RBI filter is a gstreamer filter element  used to perform ad-insertion on content delivered via QAMSource.
  *  The RBI filter element is positioned downstream to QAMSource in the GStreamer pipeline
  *  and it monitors QAMSource output for specific in-band signalling indicating an ad-insertion opportunity.
  *  When it detects such an opportunity, it replaces the original AV packets with that of an ad segment
  *  downloaded separately via IP channels.
  *
  *  This Ad insertion system has the following features:
  *  - Uses SCTE-35 triggers
  *  - Supports multiple simultaneous independent insertions
  *  - Insertion without channel changing
  *  - Platform independent implementation; does not require splicing hardware
  *  - Insertion for live, time-shifted, and recorded content
  *
  *  @b Properties
  *  - rbi-packet-in-callback - RBI packet input callback .
  *  - rbi-packet-out-size-callback -  RBI packet output size callback.
  *  - rbi-packet-out-data-callback -  RBI packet output data callback.
  *  - rbi-context - Context to pass to RBI packet processor.
  *  @ingroup  GST_PLUGINS
 **/

/**
* @defgroup gst-plugins-rdk
* @{
* @defgroup rbifilter
* @{
**/


#ifndef __RMF_RBIFILTER_H__
#define __RMF_RBIFILTER_H__


#include <glib.h>
#include <gst/gst.h>

G_BEGIN_DECLS

/**
  *  @addtogroup RBI_FILTER
  * @{
 **/
#define GST_TYPE_RBIFILTER \
  (gst_rbifilter_get_type())
#define GST_RBIFILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RBIFILTER,GstRBIFilter))
#define GST_RBIFILTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RBIFILTER,GstRBIFilterClass))
#define GST_IS_RBIFILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RBIFILTER))
#define GST_IS_RBIFILTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RBIFILTER))

typedef struct _GstRBIFilter GstRBIFilter;
typedef struct _GstRBIFilterClass GstRBIFilterClass;

/**
 * GstRBIFilter:
 *
 * Opaque #GstRBIFilter data structure
*/
struct _GstRBIFilter {
  GstElement 	 element;                     /**< Gstreamer Element */

  GstPad *srcpad;                             /**< Gstreamer Source Pad */
  GstPad *sinkpad;                            /**< Gstreamer Sink Pad  */
  
  #ifdef GLIB_VERSION_2_32 
  GCond condNotEmpty;                         /**< Gcond structure that represents the condition */
  GMutex lockItems;                           /**< Mutex variable */
  #else
  GCond *condNotEmpty;                        /**< Gcond structure that represents the condition */ 
  GMutex *lockItems;                          /**< Mutex variable */
  #endif
  GList *pendingItems;                        /**< Doubly Linked list */
  gboolean inserting;                         /**< Boolean flag indicates ad is inserted or not */
  gboolean playing;                           /**< Ad is playing or not */
  GstFlowReturn srcRet;                       /**< Result of passing data to a pad */ 
  
  void *rbiContext;                            /**< Context to pass to RBI packet processor  */
  void *rbiPacketInCallback;                   /**< RBI packet input callback of form gboolean (*cb)( void *ctx, unsigned char* packet													s, int* len )*/
  void *rbiPacketOutSizeCallback;              /**< RBI packet output size callback of form int (*cb)( void *ctx ) */
  void *rbiPacketOutDataCallback;              /**< RBI packet output data callback of form int (*cb)( void *ctx, unsigned char* packe													ts, int len ) */
};

struct _GstRBIFilterClass {
  GstElementClass parent_class;
};

G_GNUC_INTERNAL GType gst_rbifilter_get_type (void); /**< GType element used with gst_element_register() 
														  at the time of rbifilter plugin initialisation  */

G_END_DECLS

#endif /* __GST_RBIFILTER_H__ */


/** @} */     /** End of doxygen tag RBI_FILTER */
/** @} */
/** @} */
