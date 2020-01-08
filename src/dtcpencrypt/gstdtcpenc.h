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
  *  @defgroup DTCP_ENCRYPT  DTCP Encryption
  *  DTCP encrypt is a gstreamer filter element which encrypts incoming data and pushes encrypted data to source pads.
  *  DTCP encrypt requires initialization of DTCP libs done in RMF applications (e.g Media Streamer) which includes
  *  - Creation of a socket and listening for AKE request
  *  - Processing Authentication and send Exchange key to sink devices upon DTCP request from sink
  *  - EMI (Encrypt Mode Indicator) value is set as a gstreamer property and is used while encrypting data
  *
  *  @b Properties
  *  - cci        - Stream CCI value for encrypting Default:0
  *  - srctype    - Source Element Type (QAM or HN) Default:0
  *  - buffersize - Gstreamer buffer size (DTCP Buffer Size) Default:131036
  *  - remoteip   - Remote IP (client ip) address
  *  - keylabel   - DTCP Exchange Key Label Default:0
  *  - reset      - Reset the dtcp session to clean up residue data
  *  @ingroup  GST_PLUGINS
 **/


/**
* @defgroup gst-plugins-rdk
* @{
* @defgroup dtcpencrypt
* @{
**/


#ifndef __GST_DTCPENC_H__
#define __GST_DTCPENC_H__

#include <gst/gst.h>
#include "dtcpmgr.h"

G_BEGIN_DECLS

/**
  *  @addtogroup DTCP_ENCRYPT
  *  @{
 **/

/* #defines don't like whitespacey bits */
#define GST_TYPE_DTCPENC \
  (gst_dtcp_enc_get_type())
#define GST_DTCPENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DTCPENC,GstDtcpEnc))
#define GST_DTCPENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DTCPENC,GstDtcpEncClass))
#define GST_IS_DTCPENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DTCPENC))
#define GST_IS_DTCPENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DTCPENC))

typedef struct _GstDtcpEnc      GstDtcpEnc;
typedef struct _GstDtcpEncClass GstDtcpEncClass;

#define QAMSRC 0
#define HNSRC  1

#define GST_DTCPENC_EVENT_BASE    (0x0610)
#define GST_DTCPENC_EVENT_AKE_FAILED (GST_DTCPENC_EVENT_BASE + 1)

#define MAX_PACKET_SIZE (3*(188+4))

struct _GstDtcpEnc
{
  GstElement element;                 /**< Gstreamer Element */

  GstPad *sinkpad, *srcpad;           /**< Gst pad elements Source pad and Sink pad for linking elements */

  gboolean silent;                    /**< Indicates verbose output */
  gboolean isFirstPacket;             /**< Boolean value, checks for the initial data packet received     */

  guchar cci;                         /**< Stream CCI(Copy Control information)  value for encrypting         */

  guchar EMI;                         /**< Encryption mode Indicator - Copy-Free, Copy-No-More              */

  gint srctype;                       /**< Source Element Type (QAM or HN)          */

  gint buffersize;                    /**< Gstreamer buffer size (DTCP Buffer Size) */

  gint tspacketsize;                  /**< Transport stream packet size            */

  gchar remoteip[50];                 /**< Remote IP (client ip) address            */

  guint exchange_key_label;           /**< DTCP Exchange Key Label                 */

  DTCP_SESSION_HANDLE pDtcpSession;   /**< DTCP session */

  gboolean havePAT;                   /**< Flag indicates the presence of PAT table */
  gint versionPAT;                    /**< Acquired PAT version*/
  gint program;                       /**< Program number */
  gint pmtPid;                        /**< PMT(Program Map Table) PID */
  gint pcrPid;                        /**< PCR(Program Clock Reference) PID */
  gboolean havePMT;                   /**< Indicates the presence of PMT */
  gint versionPMT;                    /**< PMT Version */
  gboolean haveNewPMT;                /**< Indicates the PMT Change  */
  gint newPMTSize;                    /**< Size of new PMT table     */
  guchar newPMT[MAX_PACKET_SIZE];     /**< New PMT table              */
};

struct _GstDtcpEncClass 
{
  GstElementClass parent_class;
};

GType gst_dtcp_enc_get_type (void);   /** Gtype element to register, Used with gst_element_register()dtcpencrypt */

/** @} */

G_END_DECLS

#endif /* __GST_DTCPENC_H__ */


/** @} */
/** @} */
