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
#include <iostream>
#include <stdio.h>
#include <string>
#include <thread>
#include <chrono>
#include <gst/gst.h>

#define USE_AESDEC

/* Based on sample code which is  Copyright (C) GStreamer developers
Licensed under the MIT License */
static void pad_added_handler (GstElement *src, GstPad *new_pad, gpointer data)
{

    GstElement *h264parser = (GstElement *)data;
    GstPad *sink_pad = gst_element_get_static_pad(h264parser, "sink");
    GstPadLinkReturn ret;
    GstCaps *new_pad_caps = NULL;
    GstStructure *new_pad_struct = NULL;
    const gchar *new_pad_type = NULL;


    printf("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));


    /* If our converter is already linked, we have nothing to do here */
    if (gst_pad_is_linked(sink_pad))
    {
        printf("We are already linked. Ignoring.\n");
        goto exit;
    }

    /* Check the new pad's type */
    new_pad_caps = gst_pad_get_current_caps(new_pad);
    new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
    new_pad_type = gst_structure_get_name(new_pad_struct);
    if (!g_str_has_prefix(new_pad_type, "video"))
    {
        printf("It has type '%s' which is not video. Ignoring.\n", new_pad_type);
        goto exit;
    }

    /* Attempt the link */
    ret = gst_pad_link(new_pad, sink_pad);
    if (GST_PAD_LINK_FAILED(ret))
    {
        printf("Type is '%s' but link failed.\n", new_pad_type);
    }
    else
    {
        printf("Link succeeded (type '%s').\n", new_pad_type);
    }

exit:
    /* Unreference the new pad's caps, if we got them */
    if (new_pad_caps != NULL)
        gst_caps_unref(new_pad_caps);

    /* Unreference the sink pad */
    gst_object_unref(sink_pad);
}

static const char* GstStateStr(GstState st)
{
  switch (st)
  {
    case GST_STATE_VOID_PENDING:  return "GST_STATE_VOID_PENDING";
    case GST_STATE_NULL:          return "GST_STATE_NULL";
    case GST_STATE_READY:         return "GST_STATE_READY";
    case GST_STATE_PAUSED:        return "GST_STATE_PAUSED";
    case GST_STATE_PLAYING:       return "GST_STATE_PLAYING";
    default:                      return "GST_STATE:unknown";
  }
}

static gboolean bus_call(GstBus * bus, GstMessage * msg, gpointer data)
{
    //printf("[bus_call enter] msg=%d,%s pid=%p\n", GST_MESSAGE_TYPE(msg), GST_MESSAGE_TYPE_NAME(msg), (void*)pthread_self());

	switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
        printf("RECEIVED EOS : STOPPING PLAYBACK\n");
        break;

	case GST_MESSAGE_ELEMENT:
        printf("RECEIVED GST ELEMENT MESSAGE");
		break;

    case GST_MESSAGE_STATE_CHANGED:
        GstState oldState;
        GstState newState;
        GstState pendingState;
        gst_message_parse_state_changed (msg, &oldState, &newState, &pendingState);

        printf("[bus_call] State changed for '%s' to %s\n", GST_MESSAGE_SRC_NAME(msg), GstStateStr(newState));
        printf("[bus_call] GST_MESSAGE_SRC=%p\n", (void *)GST_MESSAGE_SRC(msg));
        break;

    case GST_MESSAGE_DURATION:
        printf("[MediaPlayer bus_call] DURATION CHANGED EVENT RECEIVED");
        break;

    default: 
        break;
    }

    //printf("[bus_call exit]\n");
   return TRUE;
}



int main(int argc, char *argv[])
{
    printf("Syntax: %s  <input file> <length of playback in seconds>\n", argv[0]);
    if(3 > argc)
        return -1;

    printf("Enter %s\n", argv[0]);
    int length_of_playback = std::atoi(argv[2]);
    gst_init (&argc, &argv);

    GMainLoop *loop = g_main_loop_new (NULL, FALSE);
    GstBus *bus;
    guint bus_watch_id;

    /* Create gstreamer elements */
    GstElement *pipeline = gst_pipeline_new ("sample-rtp-player");

    GstElement *queue_1 = gst_element_factory_make("queue", "queue no 1");

    GstElement *filesrc   = gst_element_factory_make ("filesrc", "file-source");
#ifdef USE_AESDEC
    GstElement *aesdecfilter = gst_element_factory_make ("aesmptsdecrypt", "aes-decrypt");
#endif

#ifdef BUILD_FOR_DESKTOP
    GstElement * tsdemux = gst_element_factory_make ("tsdemux", "tsdemux");
    GstElement * h264parse = gst_element_factory_make ("h264parse", "h264parse");
    GstElement * avdec_h264 = gst_element_factory_make ("avdec_h264", "avdec_h264");
    GstElement * autovideosink = gst_element_factory_make ("autovideosink", "autovideosink");

    GstElement *playsinkbin = gst_bin_new ("fakesinkbin");
    gst_bin_add_many (GST_BIN (playsinkbin), tsdemux, h264parse, avdec_h264, autovideosink, NULL);
    g_signal_connect (tsdemux, "pad-added", G_CALLBACK (pad_added_handler), h264parse);
    gst_element_link_many (h264parse, avdec_h264, autovideosink, NULL);
    GstPad *pad = gst_element_get_static_pad (tsdemux, "sink");
    gst_element_add_pad (playsinkbin, gst_ghost_pad_new ("sink", pad));
    gst_object_unref (GST_OBJECT (pad));
#else
    GstElement *playsinkbin = gst_element_factory_make ("playersinkbin", "sink-bin");

    g_object_set(G_OBJECT(playsinkbin), "rectangle", "0,0,1920,1080", NULL);

#endif

    if(!pipeline) printf ("rtpplayer: could not load pipeline \n");
    if(!filesrc) printf ("rtpplayer: could not load  filesource\n");
    if(!queue_1) printf ("rtpplayer: could not load queue \n");
#ifdef USE_AESDEC
    if(!aesdecfilter) printf ("rtpplayer: could not load  aes dec filter\n");
#endif
    if(!playsinkbin) printf ("rtpplayer: could not load sink\n");

    if (!pipeline || !filesrc || !queue_1 || 
#ifdef USE_AESDEC
    !aesdecfilter ||
#endif
    !playsinkbin ){
        printf ("One element could not be created. Exiting.\n");
        return -1;
    }



    bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

    bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);

    gst_object_unref (bus);

    //GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
    //guint bus_event_id = gst_bus_add_watch(bus, bus_call, NULL);
    //gst_object_unref(bus);


    g_object_set (G_OBJECT (filesrc),
        "location", argv[1], 
        "blocksize", 3760, 
        NULL);
#ifdef USE_AESDEC
#if 0
    g_object_set (G_OBJECT (aesdecfilter),
        "aesdec_mode", "aes_128_cbc",
#warn "Substitute actual key and iv below"
        "aesdec_key_str", "0x00000000000000000000000000000000",
        "aesdec_iv_str", "0x00000000000000000000000000000000",
        NULL);
#else
    GByteArray *key = g_byte_array_sized_new(16);
    GByteArray *iv = g_byte_array_sized_new(16);
#warn "Substitute actual key and iv below"
    memset(key->data, 0x00, 16); key->len = 16;
    memset(iv->data, 0x00, 16); iv->len = 16;
    g_object_set (G_OBJECT (aesdecfilter),
        "aesdec_mode", "aes_128_cbc",
        "aesdec_key", key,
        "aesdec_iv", iv,
        NULL);
    g_byte_array_unref(key);
    g_byte_array_unref(iv);
#endif
#endif
    /* we add all elements into the pipeline */

    gst_bin_add_many (GST_BIN (pipeline),
                      filesrc, queue_1,
#ifdef USE_AESDEC
                      aesdecfilter,
#endif
                      playsinkbin,NULL);


    /* we link the elements together */
    gst_element_link_many (filesrc, queue_1, 
#ifdef USE_AESDEC    
    aesdecfilter, 
#endif    
    playsinkbin,NULL);

    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    

    std::thread mythread([loop, length_of_playback] () {
        std::this_thread::sleep_for(std::chrono::seconds(length_of_playback));
        printf("Killer thread waking up from sleep.\n");
        g_main_loop_quit(loop);
        printf("Killer thread exiting\n");
    });
    printf("Running the main loop\n");
    g_main_loop_run (loop);



    /* Out of the main loop, clean up nicely */
    printf ("Back from main loop\n");
    gst_element_set_state (pipeline, GST_STATE_NULL);
    mythread.join();
    printf ("Deleting pipeline\n");
    gst_object_unref (GST_OBJECT (pipeline));
    g_source_remove (bus_watch_id);
    g_main_loop_unref (loop);    
    return 0;
}
