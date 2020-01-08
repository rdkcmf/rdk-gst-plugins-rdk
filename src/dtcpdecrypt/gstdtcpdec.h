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
 * @defgroup GST_PLUGINS Gst Plugins
 * GStreamer is an open-source multimedia framework written in C using the GObject framework.
 * It has a pipeline-based architecture, which allows to freely configure the data flow across a
 * variety of plugins that handle different data formats.
 * GStreamer elements are implemented as shared objects are provided by plugins.
 * The GStreamer distribution offers several basic sets of plugins:
 * - gst-plugins-base - contains high-quality, well-maintained plugins handling the basic multimedia processing tasks.
 * - gst-plugins-good - good-quality plugins that the community consider well-written and correctly working.                             All of these plugins also have a clean license - LGPL (or compatible).
 * - gst-plugins-bad - a set of plugins that although being mostly good but are often missing something
 (a good review, documentation or active maintainer) and therefore are not up to be considered "good".
 * - gst-plugins-ugly - these plugins, although having relatively good quality, may cause distribution problems
 mostly due to licensing problems.
 * The RDK requires only the "base" and "good" plugin sets. Additionally some SOC-specific plugins
 * may be required for hardware-accelerated audio/video playback. Based on the device type and
 * capabilities RMF requires the support of several GStreamer plug-in elements.
 *
 * @b Source @b elements
 * - httpsrc (Generic Gstreamer plugin)
 * - qamtunersrc (Device-specific)
 *
 * @b Parse/Filter @b elements
 * - rbifilter (Generic)
 * - dtcp (Generic + SoC)
 *
 * @b Sink @b elements
 * - playersinkbin (Generic + SoC)
 Bin which includes demux, decoder and sink elements
 * - httpsink (Generic)
 *
 * @defgroup DTCP_DECRYPT DTCP Decryption
 * Initiate AKE with source at the time of element make and do decrypt data.
 * DTCP decrypt properties:
 * - dtcp_src_ip  - DTCP source IP Address for Initiating AKE request.
 * - dtcp_port    - DTCP port number on which DTCP Source will listen the AKE commands.
 * - AKE_Result   - Check whether AKE is successful or not.
 * - buffersize   - Gst buffer size (DTCP Buffer size).
 * - loglevel     - DTCP Manager log level.
 * @ingroup GST_PLUGINS
**/

/**
* @defgroup gst-plugins-rdk
* @{
* @defgroup dtcpdecrypt
* @{
**/

#ifndef __GST_DTCPDEC_H__
#define __GST_DTCPDEC_H__
#include <gst/gst.h>
#include "dtcpmgr.h"

G_BEGIN_DECLS

/**
 * @addtogroup DTCP_DECRYPT
 * @{
 */
/** #defines don't like whitespacey bits */
#define GST_TYPE_DTCPDEC \
  (gst_dtcp_dec_get_type())
#define GST_DTCPDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DTCPDEC,GstDtcpDec))
#define GST_DTCPDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DTCPDEC,GstDtcpDecClass))
#define GST_IS_DTCPDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DTCPDEC))
#define GST_IS_DTCPDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DTCPDEC))

typedef struct _GstDtcpDec      GstDtcpDec;

typedef struct _GstDtcpDecClass GstDtcpDecClass;

/**
 * @brief Describes the DTCP decrypt properties.
 */
struct _GstDtcpDec
{
  GstElement element;                  /**< Gstreamer Element */

  GstPad *sinkpad, *srcpad;            /**< Gst pad elements Source pad and Sink pad for linking elements */

  gboolean silent;                     /**< Indicates verbose output */
  gboolean isFirstPacket;              /**< Boolean value indicates the first packet */

//  gboolean ake_init_ready;

  gboolean ake_result;                 /**< Boolean value indicates AKE is successful or not. */

  gboolean dtcp_element_bypass;        /**< Boolean value indicates DTCP decryption needs to be bypassed  */

  gint dtcp_port;                      /**< DTCP port number on which DTCP Source will listen the AKE commands  */

  char dtcp_src_ip[50];                /**< DTCP source IP Address for Initiating AKE request */

  gint buffersize;                     /**< Gstreamer buffer size */

  gint loglevel;                       /**< DTCP Manager log levels */

  DTCP_SESSION_HANDLE pDtcpSession;    /**< DTCP manager session */

};

struct _GstDtcpDecClass 
{
  GstElementClass parent_class;
};

GType gst_dtcp_dec_get_type (void);     /** Gtype element to register, Used with gst_element_register() */

G_END_DECLS

#endif /* __GST_DTCPDEC_H__ */


/** @} */ // End of Doxygen tag DTCP_DECRYPT
/** @} */
/** @} */
