/*
 * Copyright 2021 RDK Management
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

#ifndef _GST_AESMPTSDECRYPT_H_
#define _GST_AESMPTSDECRYPT_H_

#include <string>
#include <memory>
#include <gst/base/gstbasetransform.h>
#include "gstaesdec_types.h"
#include "scte52_processor.h"

G_BEGIN_DECLS

#define GST_TYPE_AESMPTSDECRYPT   (gst_aesmptsdecrypt_get_type())
#define GST_AESMPTSDECRYPT(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AESMPTSDECRYPT,GstAesmptsdecrypt))
#define GST_AESMPTSDECRYPT_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AESMPTSDECRYPT,GstAesmptsdecryptClass))
#define GST_IS_AESMPTSDECRYPT(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AESMPTSDECRYPT))
#define GST_IS_AESMPTSDECRYPT_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AESMPTSDECRYPT))

typedef struct _GstAesmptsdecrypt GstAesmptsdecrypt;
typedef struct _GstAesmptsdecryptClass GstAesmptsdecryptClass;

struct _GstAesmptsdecrypt
{
    GstBaseTransform base_aesmptsdecrypt;

    std::string *m_aes_mode;
    std::unique_ptr <param_array_t> m_key;
    std::unique_ptr <param_array_t> m_iv;
    std::unique_ptr <scte52_processor> m_stream_processor;
};

struct _GstAesmptsdecryptClass
{
    GstBaseTransformClass base_aesmptsdecrypt_class;
};

GType gst_aesmptsdecrypt_get_type (void);

G_END_DECLS

#endif
