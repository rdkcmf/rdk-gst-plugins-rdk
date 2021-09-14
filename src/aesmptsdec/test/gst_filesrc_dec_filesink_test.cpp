#include <iostream>
#include <stdio.h>
#include <string>
#include <thread>
#include <chrono>
#include <gst/gst.h>

#define USE_AESDEC


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
    printf("Syntax: %s  <input file> <length of playback in seconds> <optional: output file>\n", argv[0]);
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
    GstElement *playsinkbin = gst_element_factory_make ("filesink", "file-sink");
    g_object_set (G_OBJECT (playsinkbin),
        "location", argv[3], 
        NULL);
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
