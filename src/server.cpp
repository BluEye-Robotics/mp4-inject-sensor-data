// Copyright 2018 Blueye Robotics AS
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <signal.h>
#include <blkid/blkid.h>
#include <sys/stat.h>
#include <mutex>
#include <stdio.h>
#include <thread>
#include <unistd.h>

GMainLoop *loop = NULL;


GstPad *tee_pad;
GstPad *record_queue_pad, *display_queue_pad;
GstElement *pipeline, *src;
GstElement *record_queue, *mp4mux, *probe_queue, *filesink, *enc;
GstElement *appsrc;
GstElement *audioenc;

guint64 num_samples;   /* Number of samples generated so far (for timestamp generation) */
guint sourceid;        /* To control the GSource */

#define CHUNK_SIZE 1024   /* Amount of bytes we are sending in each buffer */
#define SAMPLE_RATE 44100 /* Samples per second we are sending */

static gboolean push_data (gpointer data) {
  g_print("push\n");
  GstBuffer *buffer;
  GstFlowReturn ret;

  /* Create a new empty buffer */
  buffer = gst_buffer_new();

  //gst_buffer_fill(buffer, 0, (gpointer)&((*buf)[0]), buf->size()); 

  char b[10];
  static uint64_t counter = 0;
  snprintf(b, 10, "ii%d", counter++);
  GstMemory *mem = gst_allocator_alloc(NULL, strlen(b), NULL);
  gst_buffer_append_memory(buffer, mem);
  gst_buffer_fill(buffer, 0, b, strlen(b));

  /* Push the buffer into the appsrc */
  g_signal_emit_by_name (appsrc, "push-buffer", buffer, &ret);

  /* Free the buffer now that we are done with it */
  gst_buffer_unref (buffer);

  if (ret != GST_FLOW_OK) {
    /* We got some error, stop sending data */
    return FALSE;
  }

  return TRUE;
}

static void start_feed (GstElement *source, guint size, gpointer throwaway) {
  if (sourceid == 0) {
    g_print ("Start feeding\n");
    sourceid = g_idle_add ((GSourceFunc) push_data, NULL);
  }
}

static void stop_feed (GstElement *source, gpointer throwaway) {
  if (sourceid != 0) {
    g_print ("Stop feeding\n");
    g_source_remove (sourceid);
    sourceid = 0;
  }
}

void shutdown(int signum)
{
  g_print("exit(%d)\n", signum);
  gst_element_send_event(enc, gst_event_new_eos());
  gst_element_send_event(appsrc, gst_event_new_eos());
  //exit(signum);
}

bool startRecord()
{
  g_print("startRecord\n");
  src = gst_element_factory_make("videotestsrc", "src");
  record_queue = gst_element_factory_make("queue", "record_queue");
  enc = gst_element_factory_make("x264enc", "enc");
  probe_queue = gst_element_factory_make("queue", "probe_queue");
  mp4mux = gst_element_factory_make("qtmux", NULL);
  filesink = gst_element_factory_make("filesink", NULL);

  //g_object_set(src, "device", "/dev/video0", NULL);

  g_object_set(filesink, "location", "out.mp4", NULL);

  g_object_set(G_OBJECT(record_queue), "max-size-bytes", 0, "max-size-time", (guint64) 3 * GST_SECOND, "max-size-buffers", 0, "leaky", 2, NULL);
  g_object_set(G_OBJECT(probe_queue), "max-size-bytes", 0, "max-size-time", (guint64) 3 * GST_SECOND, "max-size-buffers", 0, "leaky", 2, NULL);

  gst_bin_add_many(GST_BIN(pipeline), src, record_queue, enc, probe_queue, mp4mux, NULL);
  gst_element_link_many(src, record_queue, enc, probe_queue, mp4mux, NULL);


  appsrc = gst_element_factory_make("appsrc", NULL);
  GstCaps *caps = gst_caps_from_string("text/x-raw, format=(string)utf8");
  //appsrc = gst_element_factory_make("autoaudiosrc", NULL);
  //audioenc = gst_element_factory_make("faac", NULL);

  /* Configure appsrc */
  g_object_set (appsrc, "caps", caps, NULL);
  g_object_set(G_OBJECT(appsrc), 
                "stream-type", 0, // GST_APP_STREAM_TYPE_STREAM 
                "format", GST_FORMAT_TIME, 
                "is-live", TRUE, 
                "do-timestamp", TRUE,
                NULL); 
  //g_signal_connect (appsrc, "need-data", G_CALLBACK (start_feed), NULL);
  //g_signal_connect (appsrc, "enough-data", G_CALLBACK (stop_feed), NULL);

  gst_bin_add_many(GST_BIN(pipeline), appsrc, NULL);
  //gst_element_link_many(appsrc, mp4mux, NULL);
  {
    GstPad *srcpad = gst_element_get_static_pad(appsrc, "src");
    GstPad *sinkpad = gst_element_get_request_pad(mp4mux, "gpmf_0");
    gst_pad_link (srcpad, sinkpad);
  }

  gst_bin_add_many(GST_BIN(pipeline), filesink, NULL);
  gst_element_link_many(mp4mux, filesink, NULL);

  return true;
}

int main(int argc, char *argv[])
{
  GstMessage *msg;
  GstStateChangeReturn ret;
  GstBus *bus;
  gboolean terminate = FALSE;


  gst_init(&argc, &argv);

  pipeline = gst_pipeline_new("test-pipeline");

  startRecord();


  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  bus = gst_element_get_bus (pipeline);

  signal(SIGINT, shutdown);

  std::thread t0([&](){
    sleep(5);
    while(!terminate)
    {
      push_data(NULL);
      usleep(100000);
    }
    });

  do 
  {
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
        (GstMessageType)(GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

    /* Parse message */
    if (msg != NULL) {
      GError *err;
      gchar *debug_info;

      switch (GST_MESSAGE_TYPE (msg)) 
      {
        case GST_MESSAGE_ERROR:
          gst_message_parse_error (msg, &err, &debug_info);
          g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
          g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
          g_clear_error (&err);
          g_free (debug_info);
          terminate = TRUE;
          break;
        case GST_MESSAGE_EOS:
          g_print ("End-Of-Stream reached.\n");
          terminate = TRUE;
          break;
        case GST_MESSAGE_STATE_CHANGED:
          /* We are only interested in state-changed messages from the pipeline */
          if (GST_MESSAGE_SRC (msg) == GST_OBJECT (pipeline)) {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
            g_print ("Pipeline state changed from %s to %s:\n",
                gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
          }
          break;
          case GST_MESSAGE_ELEMENT:{
            const GstStructure *s = gst_message_get_structure (msg);

            if (gst_structure_has_name (s, "GstBinForwarded")) {
              GstMessage *forward_msg = NULL;

              gst_structure_get (s, "message", GST_TYPE_MESSAGE, &forward_msg, NULL);
              if (GST_MESSAGE_TYPE (forward_msg) == GST_MESSAGE_EOS) {
                g_print ("EOS from element %s\n",
                    GST_OBJECT_NAME (GST_MESSAGE_SRC (forward_msg)));
                gst_element_set_state (filesink, GST_STATE_NULL);
                gst_element_set_state (mp4mux, GST_STATE_NULL);
                gst_element_set_state (filesink, GST_STATE_PLAYING);
                gst_element_set_state (mp4mux, GST_STATE_PLAYING);
              }
              gst_message_unref (forward_msg);
            }
            }
            break;
        default:
          /* We should not reach here */
          g_printerr ("Unexpected message received.\n");
          break;
      }
      gst_message_unref (msg);
    }
  } while (!terminate);
  /* create a server instance */
  /* start serving */
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  return 0;
}
