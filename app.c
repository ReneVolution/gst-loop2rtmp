#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>


/* Structure to contain all decoding chains */
typedef struct _AppElements {

    GMainLoop *loop;
    GstElement *pipeline;
    GstElement *source;
    GstElement *src_q;
    GstElement *demuxer;
    GstElement *vq_in;
    GstElement *vq_out;
    GstElement *aq_in;
    GstElement *aq_out;
    GstElement *identity;
    GstElement *h264parser;
    GstElement *aacparser;
    GstElement *sink_q;
    GstElement *muxer;
    GstElement *rtmpsink;
} AppElements;


/* Process keyboard input */
static gboolean
handle_keyboard (GIOChannel *source, GIOCondition cond, AppElements *app)
{
  gchar *str = NULL;
   
  if (g_io_channel_read_line (source, &str, NULL, NULL, NULL) != G_IO_STATUS_NORMAL)
  {
    return TRUE;
  }
   
  switch (g_ascii_tolower (str[0])) {
    case 'q':
      g_main_loop_quit (app->loop);
      break;
    default:
      break;
    }
   
  g_free (str);
   
  return TRUE;
}

static gboolean
bus_callback (GstBus *bus, GstMessage *msg, gpointer data)
{

  AppElements *app = (AppElements*) data;

  switch (GST_MESSAGE_TYPE(msg))
  {
    case GST_MESSAGE_EOS: {
      /* end of stream received */
      g_print("Received End-of-Stream Signal\n");
      g_main_loop_quit (app->loop);
      break;
    }
    case GST_MESSAGE_SEGMENT_DONE: {
      g_print ("Received SEGMENT DONE Message\n");
      if (!gst_element_seek(app->pipeline, 1.0, GST_FORMAT_TIME,
                            (GstSeekFlags)(GST_SEEK_FLAG_SEGMENT),
                            GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_END, GST_CLOCK_TIME_NONE))

      {
        g_printerr("Seek failed!\n");
      }
      break;
    }
    case GST_MESSAGE_ERROR: {
      gchar *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      g_main_loop_quit (app->loop);
      break;
    }
    default:
      /* unhandled message */
      break;
  }

  return TRUE;
}


static void
on_no_more_pads (GstElement *demuxer,
                 AppElements *app)
{
  gst_bin_add_many (GST_BIN (app->pipeline), app->sink_q, app->rtmpsink, NULL);
  gst_element_set_state(app->muxer, GST_STATE_PAUSED);
  
  /* link muxer to sink */
  gst_element_link_many (app->muxer, app->sink_q, app->rtmpsink, NULL);
  
  gst_element_set_state(app->sink_q, GST_STATE_PAUSED);
  gst_element_set_state(app->rtmpsink, GST_STATE_PAUSED);

  gst_element_seek(app->pipeline, 1.0, GST_FORMAT_TIME,
                   (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SEGMENT),
                   GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_END, GST_CLOCK_TIME_NONE);
  
  gst_element_set_state (app->pipeline, GST_STATE_PLAYING);
  
  /* Enable DOT File creation for debug puposes */
  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN (app->pipeline),
                                    GST_DEBUG_GRAPH_SHOW_ALL,
                                    "loop2rtmp");
}

static void
on_pad_added (GstElement *src,
              GstPad *new_pad,
              AppElements *app)
{
  GstPad *sink_pad = NULL;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;

  /* Check first the kind of pad */

#if (GST_VERSION_MAJOR == 1)
  new_pad_caps = gst_pad_query_caps (new_pad, NULL);
#else
  new_pad_caps = gst_pad_get_caps (new_pad);
#endif

  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  new_pad_type = gst_structure_get_name (new_pad_struct);

  /* Add Muxer only once */
  if (gst_bin_get_by_name(GST_BIN (app->pipeline), "muxer") == NULL)
  {
    gst_bin_add(GST_BIN (app->pipeline), app->muxer);
    gst_element_set_state(app->muxer, GST_STATE_PAUSED);
  }

  /* Check for AAC Audio */
  if (g_str_has_prefix (new_pad_type, "audio/mpeg")) {
    g_print ("Found AAC Audio pad\n");
    
    /* connect to aac decoding chain */
    gst_bin_add_many (GST_BIN (app->pipeline), app->aq_in,
                      app->aacparser, app->aq_out, NULL);
    
    /* link audio elements */
    gst_element_link_many (app->aq_in, app->aacparser, app->aq_out, app->muxer, NULL);
    sink_pad = gst_element_get_static_pad (app->aq_in, "sink");
    gst_pad_link (new_pad, sink_pad);
    
    gst_element_set_state(app->aq_in, GST_STATE_PAUSED);
    gst_element_set_state(app->aacparser, GST_STATE_PAUSED);
    gst_element_set_state(app->aq_out, GST_STATE_PAUSED);

    goto exit;

  }

  /* Check for H.264 Video */
  if (g_str_has_prefix (new_pad_type, "video/x-h264")) {
    g_print ("Found H.264 Video Pad\n");
    
    /* connect to h.264 decoding chain */
    gst_bin_add_many (GST_BIN (app->pipeline), app->vq_in,
                      app->h264parser, app->identity, app->vq_out, NULL);
    
    /* link video elements */
    gst_element_link_many (app->vq_in, app->h264parser, app->identity, app->vq_out, app->muxer, NULL);
    sink_pad = gst_element_get_static_pad (app->vq_in, "sink");
    gst_pad_link (new_pad, sink_pad);

    gst_element_set_state(app->vq_in, GST_STATE_PAUSED);
    gst_element_set_state(app->h264parser, GST_STATE_PAUSED);
    gst_element_set_state(app->identity, GST_STATE_PAUSED);
    gst_element_set_state(app->vq_out, GST_STATE_PAUSED);

    goto exit;
  }

  g_printerr ("Pad-Type '%s' is currently not supported", new_pad_type);

  exit:
    /* cleanup */
    if (new_pad_caps != NULL)
      gst_caps_unref (new_pad_caps);

    if (sink_pad != NULL)
      gst_object_unref (sink_pad);
}


GstElement *
gst_element_factory_make_or_error (const gchar *factoryname, const gchar *name)
{
  GstElement *element;
  element = gst_element_factory_make (factoryname, name);
  if (!element)
  {
    g_printerr ("Element \'%s\' could not be created. Exiting...\n", factoryname);
    /* cleanup */
    exit(1);
  }

  return element;
}


gint main(gint argc, gchar *argv[])
{
  AppElements app;
  GstBus *bus;
  GIOChannel *io_stdin;
  guint bus_watch_id;

  /* Init */
  gst_init (&argc, &argv);

  /* We need a Videofile and a RTMP-Destination as args */
  if (argc != 3)
  {
    g_print("Usage: %s \"MP4-Videofile\" \"rtmp://yourhost.com/app/stream live=1\"\n", argv[0]);
    return -1;
  }

  /* Create and connect the Pipeline Elements 
   *                              || dmux.video_00 -> identity -> queue -> h264parse -> queue ||
   *  filesrc -> queue -> qtdemux ||                                                          || flvmux -> queue -> rtmpsink
   *                              || dmux.audio_00 -> queue   ->  aacparse    ->  queue       ||
   */
  
  
  app.loop = g_main_loop_new(NULL, FALSE);  

  app.pipeline = gst_pipeline_new ("file-looper");
  if (!app.pipeline)
  {
    g_printerr ("Pipeline could not be created. Exiting.\n");
    return -1;
  }

  app.source = gst_element_factory_make_or_error("filesrc", "file-source");
  app.src_q = gst_element_factory_make_or_error("queue", "src-queue");
  app.demuxer = gst_element_factory_make_or_error("qtdemux", "demuxer");
  app.vq_in = gst_element_factory_make_or_error("queue", "vq_in");
  app.aq_in = gst_element_factory_make_or_error("queue", "aq_in");
  app.vq_out = gst_element_factory_make_or_error("queue", "vq_out");
  app.aq_out = gst_element_factory_make_or_error("queue", "aq_out");
  app.h264parser = gst_element_factory_make_or_error("h264parse", "h264-parser");
  app.identity = gst_element_factory_make_or_error("identity", "ident");
  app.aacparser = gst_element_factory_make_or_error("aacparse", "aac-parser");
  app.muxer = gst_element_factory_make_or_error("flvmux", "muxer");
  app.sink_q = gst_element_factory_make_or_error("queue", "sinkqueue");
  app.rtmpsink = gst_element_factory_make_or_error("rtmpsink", "sink");
  

  /* set the source location */
  g_object_set (G_OBJECT (app.source), "location", argv[1], NULL);

  /* flvmuxer settings */
  g_object_set (G_OBJECT (app.muxer), "streamable", TRUE, NULL);

  /* Set SPS/PPS handling == TRUE */
  g_object_set (G_OBJECT (app.h264parser), "config-interval", TRUE, NULL);

  /* Set single segment handling */
  g_object_set (G_OBJECT (app.identity), "single-segment", TRUE,
                                         "silent", FALSE,
                                         "sync", TRUE,  NULL);

  /* set destination server */
  g_object_set (G_OBJECT (app.rtmpsink), "location", argv[2], NULL);

  /* Connect the Pipeline Bus with out callback */
  bus = gst_pipeline_get_bus (GST_PIPELINE (app.pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_callback, &app);
  gst_object_unref (bus);

  /* First add all elements to the pipeline */
  gst_bin_add_many (GST_BIN (app.pipeline),
                    app.source, app.src_q, app.demuxer, NULL);
  
  /* Now we can link our source elements */
  gst_element_link_many (app.source, app.src_q, app.demuxer, NULL);

  /* Connect demuxer with decoder pads */
  g_signal_connect (app.demuxer, "pad-added", G_CALLBACK (on_pad_added), &app);
  
  /* If all pads handled we can init the rest of the pipeline */
  g_signal_connect (app.demuxer, "no-more-pads", G_CALLBACK (on_no_more_pads), &app);

  /* Setting up the Pipeline using Segments */
  gst_element_set_state (app.pipeline, GST_STATE_PAUSED);
  
  /* Add a keyboard watch so we get notified of keystrokes */
#ifdef _WIN32
  io_stdin = g_io_channel_win32_new_fd (fileno (stdin));
#else
  io_stdin = g_io_channel_unix_new (fileno (stdin));
#endif
  g_io_add_watch (io_stdin, G_IO_IN, (GIOFunc)handle_keyboard, &app);

  /* run the pipeline */
  g_print ("Running...\n");
  g_main_loop_run (app.loop);

  /* cleanup */
  g_print ("STOPPING Pipeline\n");
  gst_element_set_state (app.pipeline, GST_STATE_NULL);

  g_print ("DELETING Pipeline\n");
  gst_object_unref (GST_OBJECT (app.pipeline));

  g_print ("DELETING Bus\n");
  g_source_remove (bus_watch_id);

  return 0;
}
