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
 * @addtogroup DTCP_DECRYPT
 * @{
 **/

/**
* @defgroup gst-plugins-rdk
* @{
* @defgroup dtcpdecrypt
* @{
**/


#ifndef __GST_VLDTCPDEC_H__
#define __GST_VLDTCPDEC_H__

#include <gst/gst.h>
#include "dtcpmgr.h"

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_VLDTCPDEC \
  (gst_vl_dtcp_dec_get_type())
#define GST_VLDTCPDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VLDTCPDEC,GstVlDtcpDec))
#define GST_VLDTCPDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VLDTCPDEC,GstVlDtcpDecClass))
#define GST_IS_VLDTCPDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VLDTCPDEC))
#define GST_IS_VLDTCPDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VLDTCPDEC))

typedef struct _GstVlDtcpDec      GstVlDtcpDec;
typedef struct _GstVlDtcpDecClass GstVlDtcpDecClass;

struct _GstVlDtcpDec
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gboolean silent;

  gboolean ake_init_ready;

  gboolean ake_result;

  gboolean dtcp_element_bypass;

  int dtcp_port;

  char dtcp_src_ip[50];

  DTCP_SESSION_HANDLE pDtcpSession;

};

struct _GstVlDtcpDecClass 
{
  GstElementClass parent_class;
};

GType gst_vl_dtcp_dec_get_type (void);

G_END_DECLS

#endif /* __GST_VLDTCPDEC_H__ */


/** @} */
/** @} */
/** @} */
