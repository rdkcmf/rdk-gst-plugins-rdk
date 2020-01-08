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
* @defgroup gst-plugins-rdk
* @{
* @defgroup dtcpencrypt
* @{
**/


#ifndef __GST_VLDTCPENC_H__
#define __GST_VLDTCPENC_H__

#include <gst/gst.h>
#include "dtcpmgr.h"

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_VLDTCPENC \
  (gst_vl_dtcp_enc_get_type())
#define GST_VLDTCPENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VLDTCPENC,GstVlDtcpEnc))
#define GST_VLDTCPENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VLDTCPENC,GstVlDtcpEncClass))
#define GST_IS_VLDTCPENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VLDTCPENC))
#define GST_IS_VLDTCPENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VLDTCPENC))

typedef struct _GstVlDtcpEnc      GstVlDtcpEnc;
typedef struct _GstVlDtcpEncClass GstVlDtcpEncClass;

#define MAX_PACKET_SIZE (3*(188+4))

struct _GstVlDtcpEnc
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gboolean silent;

  gboolean emi;

  DTCP_SESSION_HANDLE pDtcpSession;

  gboolean havePAT;
  gint versionPAT;
  gint program;
  gint pmtPid;
  gint pcrPid;
  gboolean havePMT;
  gint versionPMT;
  gboolean haveNewPMT;
  gint newPMTSize;
  guchar newPMT[MAX_PACKET_SIZE];
};

struct _GstVlDtcpEncClass 
{
  GstElementClass parent_class;
};

GType gst_vl_dtcp_enc_get_type (void);

G_END_DECLS

#endif /* __GST_VLDTCPENC_H__ */


/** @} */
/** @} */
