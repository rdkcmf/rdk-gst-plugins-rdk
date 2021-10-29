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
 
/**
 * SECTION:element-gstaesmptsdecrypt
 *
 * The aesmptsdecrypt element will decrypt MPEG-TS packets encrypted using AES.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 filesrc location=<path to file> blocksize=3760 ! queue ! aesmptsdecrypt aesdec_mode="aes_128_cbc" aesdec_key_str=<key in hex format> aesdec_iv_str=<iv in hex format> ! queue !  tsdemux ! h264parse ! avdec_h264 ! autovideosink
 * ]|
 * The above pipeline will read an encrypted TS file, decrypt and play it back.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <cstdint>
#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include "gstaesmptsdecrypt.h"

GST_DEBUG_CATEGORY_STATIC(gst_aesmptsdecrypt_debug_category);
#define GST_CAT_DEFAULT gst_aesmptsdecrypt_debug_category

/* prototypes */

static void gst_aesmptsdecrypt_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_aesmptsdecrypt_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void gst_aesmptsdecrypt_dispose(GObject *object);
static void gst_aesmptsdecrypt_finalize(GObject *object);
static gboolean gst_aesmptsdecrypt_start(GstBaseTransform *trans);
static gboolean gst_aesmptsdecrypt_stop(GstBaseTransform *trans);
static GstFlowReturn gst_aesmptsdecrypt_transform_ip(GstBaseTransform *trans, GstBuffer *buf);

enum
{
    PROP_0,
    PROP_AESDEC_MODE,
    PROP_AESDEC_KEY,
    PROP_AESDEC_KEY_STR,
    PROP_AESDEC_IV,
    PROP_AESDEC_IV_STR,
};

#define STATIC_CAPS                \
    "video/mpegts;"                  \
    "video/mpegts, "                 \
    "  systemstream=(boolean)true, " \
    "  packetsize=(int)188;"
/* pad templates */

static GstStaticPadTemplate gst_aesmptsdecrypt_src_template =
GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(STATIC_CAPS));

static GstStaticPadTemplate gst_aesmptsdecrypt_sink_template =
GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(STATIC_CAPS));

/* class initialization */

G_DEFINE_TYPE_WITH_CODE(GstAesmptsdecrypt, gst_aesmptsdecrypt, GST_TYPE_BASE_TRANSFORM,
        GST_DEBUG_CATEGORY_INIT(gst_aesmptsdecrypt_debug_category, "aesmptsdecrypt", 0,
            "debug category for aesmptsdecrypt element"));

static void gst_aesmptsdecrypt_class_init(GstAesmptsdecryptClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS(klass),
            &gst_aesmptsdecrypt_src_template);
    gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS(klass),
            &gst_aesmptsdecrypt_sink_template);

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass),
            "Decryptor for AES-encrypted MPEG transport stream", "Filter/mpegts/decryptor", "Decrypts AES-encrypted MPEG-TS packets",
            "csojan <chaithents@tataelxsi.co.in>");

    gobject_class->set_property = gst_aesmptsdecrypt_set_property;
    gobject_class->get_property = gst_aesmptsdecrypt_get_property;

    g_object_class_install_property(gobject_class, PROP_AESDEC_MODE,
            g_param_spec_string("aesdec_mode", "AES decryption mode", "Type of decryption to use on the incoming stream", "",
                G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_AESDEC_KEY_STR,
            g_param_spec_string("aesdec_key_str", "Decryption key as a string 0x<keys in hex>", "Decryption key in string form, for convenience when testing with gst-launch", "",
                G_PARAM_WRITABLE));

    g_object_class_install_property(gobject_class, PROP_AESDEC_IV_STR,
            g_param_spec_string("aesdec_iv_str", "Initialization vector as a string 0x<IV in hex>", "Initialization vector in string form, for convenience when testing with gst-launch", "",
                G_PARAM_WRITABLE));

    g_object_class_install_property(gobject_class, PROP_AESDEC_KEY,
            g_param_spec_boxed("aesdec_key", "Decryption key", "Decryption key",
                G_TYPE_BYTE_ARRAY, G_PARAM_WRITABLE));

    g_object_class_install_property(gobject_class, PROP_AESDEC_IV,
            g_param_spec_boxed("aesdec_iv", "Initialization vector", "Initialization vector",
                G_TYPE_BYTE_ARRAY, G_PARAM_WRITABLE));

    gobject_class->dispose = gst_aesmptsdecrypt_dispose;
    gobject_class->finalize = gst_aesmptsdecrypt_finalize;
    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_aesmptsdecrypt_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_aesmptsdecrypt_stop);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_aesmptsdecrypt_transform_ip);
}

const int DEFAULT_CIPHER_LENGTH = 16;
static void gst_aesmptsdecrypt_init(GstAesmptsdecrypt *aesmptsdecrypt)
{
    GST_DEBUG_OBJECT(aesmptsdecrypt, "init");
    aesmptsdecrypt->m_aes_mode = new std::string("unknown");
}

static bool convert_text_to_byte_array(param_array_t &myvec, const std::string &in)
{
    bool ret = true;
    //GST_DEBUG_OBJECT(nullptr, "Converting input string %s of size %ld\n", in.c_str(), in.size());

    if (('0' != in[0]) || ('x' != in[1]) || (3 > in.size()))
    {
        GST_DEBUG_OBJECT(nullptr, "Error! Input pattern doesn't match 0xABCD1234... pattern\n");
        return false;
    }

    unsigned int converted_length = (in.size() - 2) / 2; //Discount 'Ox' from the input when calculating length
    bool odd_length = (0 == (in.size() % 2) ? false : true);
    if (true == odd_length)
    {
        converted_length++;
    }
    //GST_DEBUG_OBJECT(nullptr, "Estimated output size is %du bytes\n", converted_length);

    try
    {
        auto read_offset = 2;
        if (true == odd_length)
        {
            uint8_t decoded_byte = static_cast<uint8_t>(std::stoul(in.substr(read_offset++, 1), nullptr, 16));
            myvec.push_back(decoded_byte);
            converted_length--;
        }

        while (0 < converted_length--)
        {
            uint8_t decoded_byte = static_cast<uint8_t>(std::stoul(in.substr(read_offset, 2), nullptr, 16));
            myvec.push_back(decoded_byte);
            read_offset += 2;
        }
    }
    catch (...)
    {
        GST_DEBUG_OBJECT(nullptr, "Caught exception\n");
        myvec.clear();
        ret = false;
    }
    GST_DEBUG_OBJECT(nullptr, "Conversion complete: ");
    return ret;
}

static aes_mode convert_aes_mode_to_enum(const std::string &in)
{
    GST_DEBUG_OBJECT(nullptr, "Mode conversion: input is %s\n", in.c_str());
    if (in == "aes_128_cbc")
        return AES_128_CBC;
    else
        return AES_MODE_MAX;
}

static bool identify_integer_packet_boundaries(unsigned char *buffer_start, const unsigned int buffer_length, unsigned char *&ts_packet_start, unsigned int &revised_length)
{
    bool ret = false;
    unsigned char *ptr = buffer_start;
    if (TS_PACKET_SIZE > buffer_length)
    {
        GST_DEBUG_OBJECT(nullptr, "Buffer too small for a TS packet. Reject.\n");
        return ret;
    }
    while (ptr < (buffer_start + buffer_length))
    {
        if ((0x47 == *ptr) && (0x47 == ptr[TS_PACKET_SIZE])) //TODO: Stick with validating just 2 packets for now
        {
            ts_packet_start = ptr;
            ret = true;
            break;
        }
        else
            ptr++;
    }

    if (true == ret)
    {
        //Found at least one full packet. Compute where the last full packet's boundary will fall.
        unsigned int buffer_length_ahead = buffer_length - (ptr - buffer_start);
        revised_length = (buffer_length_ahead / TS_PACKET_SIZE) * TS_PACKET_SIZE;
        if (buffer_length != revised_length)
            GST_DEBUG_OBJECT(nullptr, "buffer_start %p, packet_start: %p, buffer_length: %d, revised_buffer_length: %d.\n", buffer_start, ts_packet_start, buffer_length, revised_length);
    }
    else
    {
        GST_DEBUG_OBJECT(nullptr, "No sync byte found in buffer. Reject.\n");
    }
    return ret;
}

void gst_aesmptsdecrypt_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GstAesmptsdecrypt *aesmptsdecrypt = GST_AESMPTSDECRYPT(object);

    GST_DEBUG_OBJECT(aesmptsdecrypt, "set_property");

    switch (property_id)
    {

        case PROP_AESDEC_MODE:
            *aesmptsdecrypt->m_aes_mode = g_value_get_string(value);
            GST_DEBUG_OBJECT(aesmptsdecrypt, "Setting mode %s", aesmptsdecrypt->m_aes_mode->c_str());
            break;

        case PROP_AESDEC_KEY:
            {
                GByteArray* ptr = (GByteArray*)g_value_get_boxed (value);
                aesmptsdecrypt->m_key = std::make_unique<param_array_t>();
                aesmptsdecrypt->m_key->reserve(ptr->len);
                for(guint i = 0; i < ptr->len; i++)
                {
                    aesmptsdecrypt->m_key->push_back(ptr->data[i]);
                }
                GST_DEBUG_OBJECT(aesmptsdecrypt, "Setting PROP_AESDEC_KEY. Key size is %ld.\n", aesmptsdecrypt->m_key->size());
                break;
            }

        case PROP_AESDEC_KEY_STR:
            {
                std::string key_string(g_value_get_string(value));
                aesmptsdecrypt->m_key = std::make_unique<param_array_t>();
                aesmptsdecrypt->m_key->reserve(DEFAULT_CIPHER_LENGTH);
                convert_text_to_byte_array(*(aesmptsdecrypt->m_key), key_string);
                GST_DEBUG_OBJECT(aesmptsdecrypt, "Setting PROP_AESDEC_KEY. Key size is %ld.\n", aesmptsdecrypt->m_key->size());
                break;
            }

        case PROP_AESDEC_IV:
            {
                GByteArray* ptr = (GByteArray*)g_value_get_boxed (value);
                aesmptsdecrypt->m_iv = std::make_unique<param_array_t>();
                aesmptsdecrypt->m_iv->reserve(ptr->len);
                for(guint i = 0; i < ptr->len; i++)
                {
                    aesmptsdecrypt->m_iv->push_back(ptr->data[i]);
                }
                GST_DEBUG_OBJECT(aesmptsdecrypt, "Setting PROP_AESDEC_IV. IV size is %ld.\n", aesmptsdecrypt->m_iv->size());
                break;
            }

        case PROP_AESDEC_IV_STR:
            {
                std::string iv_string(g_value_get_string(value));
                aesmptsdecrypt->m_iv = std::make_unique<param_array_t>();
                aesmptsdecrypt->m_iv->reserve(DEFAULT_CIPHER_LENGTH);
                convert_text_to_byte_array(*(aesmptsdecrypt->m_iv), iv_string);
                GST_DEBUG_OBJECT(aesmptsdecrypt, "Setting PROP_AESDEC_IV_STR. IV size is %ld.\n", aesmptsdecrypt->m_iv->size());
                break;
            }

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

void gst_aesmptsdecrypt_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GstAesmptsdecrypt *aesmptsdecrypt = GST_AESMPTSDECRYPT(object);

    GST_DEBUG_OBJECT(aesmptsdecrypt, "get_property");

    switch (property_id)
    {
        case PROP_AESDEC_MODE:
            g_value_set_string(value, aesmptsdecrypt->m_aes_mode->c_str());
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

void gst_aesmptsdecrypt_dispose(GObject *object)
{
    GstAesmptsdecrypt *aesmptsdecrypt = GST_AESMPTSDECRYPT(object);

    GST_DEBUG_OBJECT(aesmptsdecrypt, "dispose");

    /* clean up as possible.  may be called multiple times */

    G_OBJECT_CLASS(gst_aesmptsdecrypt_parent_class)->dispose(object);
}

void gst_aesmptsdecrypt_finalize(GObject *object)
{
    GstAesmptsdecrypt *aesmptsdecrypt = GST_AESMPTSDECRYPT(object);
    delete aesmptsdecrypt->m_aes_mode;
    GST_DEBUG_OBJECT(aesmptsdecrypt, "finalize");

    /* clean up object here */

    G_OBJECT_CLASS(gst_aesmptsdecrypt_parent_class)->finalize(object);
}

/* states */
static gboolean gst_aesmptsdecrypt_start(GstBaseTransform *trans)
{
    GstAesmptsdecrypt *aesmptsdecrypt = GST_AESMPTSDECRYPT(trans);
    GST_DEBUG_OBJECT(aesmptsdecrypt, "start");
    aes_mode mode = convert_aes_mode_to_enum(*aesmptsdecrypt->m_aes_mode);
    if (AES_MODE_MAX == mode)
    {
        GST_ERROR_OBJECT(aesmptsdecrypt, "Invalid AES mode!");
        return FALSE;
    }

    aesmptsdecrypt->m_stream_processor = std::make_unique<scte52_processor>(mode);
    aesmptsdecrypt->m_stream_processor->set_parameters(std::move(aesmptsdecrypt->m_key), std::move(aesmptsdecrypt->m_iv)); //TODO: rethink move if there will be multiple start-stop cycles.

    return TRUE;
}

static gboolean gst_aesmptsdecrypt_stop(GstBaseTransform *trans)
{
    GstAesmptsdecrypt *aesmptsdecrypt = GST_AESMPTSDECRYPT(trans);

    GST_DEBUG_OBJECT(aesmptsdecrypt, "stop.\n");
    aesmptsdecrypt->m_stream_processor.reset();

    return TRUE;
}

static GstFlowReturn gst_aesmptsdecrypt_transform_ip(GstBaseTransform *trans, GstBuffer *buf)
{
    GstAesmptsdecrypt *aesmptsdecrypt = GST_AESMPTSDECRYPT(trans);
    GstMapInfo mapinfo;

#if 1
    // Temporary ugly hack. If this works, we'll use the pointer to write anyway and hopefully get away with it while avoiding a memcpy
    if (TRUE == gst_buffer_map(buf, &mapinfo, GST_MAP_READ))
#else
        if (TRUE == gst_buffer_map(buf, &mapinfo, GST_MAP_READWRITE)) // For reasons that aren't clear, the underlying buffer isn't writable and this leads to a copy operation. Inefficient.
#endif
        {
            // unsigned char *ts_packet_start;
            // unsigned int revised_length;
            // if(true == identify_integer_packet_boundaries(mapinfo.data, mapinfo.size, ts_packet_start, revised_length))
            //   aesmptsdecrypt->m_stream_processor->process_buffer(ts_packet_start, revised_length); //TODO: handle return
            aesmptsdecrypt->m_stream_processor->process_buffer(mapinfo.data, mapinfo.size); //TODO: handle return
            gst_buffer_unmap(buf, &mapinfo);
        }
        else
            GST_DEBUG_OBJECT(aesmptsdecrypt, "Buffer map failed.");

    return GST_FLOW_OK;
}

static gboolean plugin_init(GstPlugin *plugin)
{
    return gst_element_register(plugin, "aesmptsdecrypt", GST_RANK_NONE,
            GST_TYPE_AESMPTSDECRYPT);
}

#ifndef VERSION
#define VERSION "0.1"
#endif
#ifndef PACKAGE
#define PACKAGE "aesmptsdecrypt"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "gstaesmptsdecrypt"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "https://gstreamer.freedesktop.org/"
#endif

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
        GST_VERSION_MINOR,
        aesmptsdecrypt,
        "Decrypts AES-encrypted MPEG-TS packets",
        plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
