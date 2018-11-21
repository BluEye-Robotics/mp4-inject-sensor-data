// Copyright 2018 Blueye Robotics AS
#include <gst/rtsp-server/rtsp-server.h>
#include <signal.h>
#include <blkid/blkid.h>
#include <sys/stat.h>
#include <mutex>
#include <stdio.h>

GMainLoop *loop = NULL;

#define STRINGSIZE 100
#define DEFAULT_RTSP_PORT "8554"
#define DEVICE            "/dev/video0"
#define CAPS_STRING       "video/x-raw, width=1280, height=720, framerate=30/1"
#define VIDEO_PARSER      "x264enc"
#define MEDIADIR          "/tmp"
#define MEDIAPARTITION    "/dev/sda2"

  GstMessage *msg;
  GstStateChangeReturn ret;
  GstBus *bus;
GstPad *tee_pad;
GstPad *record_queue_pad, *display_queue_pad;
GstElement *pipeline, *src, *tee, *caps_queue, *capsfilter;
GstElement *display_queue, *display_parse, *rtph264;
GstElement *record_queue, *mp4mux, *probe_queue, *taginject, *filesink, *record_parse;
std::mutex stop_record_mutex;

int framerate = 30;
char media_type[STRINGSIZE];
int height = 1080;
int width  = 1920;


void shutdown(int signum)
{
  g_print("exit(%d)\n", signum);
  gst_element_send_event(record_parse, gst_event_new_eos());
  //exit(signum);
}


#define CAPS_EXTRACT_PARAM(caps, param) \
{ \
  GstStructure *structure = gst_caps_get_structure(caps, 0); \
  gchar *str = gst_value_serialize(gst_structure_get_value(structure, #param)); \
  (param) = atoi(str); \
  g_free(str); \
}

#define CAPS_EXTRACT_MEDIA_TYPE(caps, media_type_var) \
{ \
  GstStructure *structure = gst_caps_get_structure(caps, 0); \
  const gchar* str = gst_structure_get_name(structure); \
  snprintf(media_type_var, sizeof(media_type_var), "%s", str); \
}

bool startRecord()
{
  g_print("startRecord\n");
  src = gst_element_factory_make("videotestsrc", "src");
  record_queue = gst_element_factory_make("queue", "record_queue");
  record_parse = gst_element_factory_make(VIDEO_PARSER, "record_parse");
  probe_queue = gst_element_factory_make("queue", "probe_queue");
  mp4mux = gst_element_factory_make("mp4mux", NULL);
  filesink = gst_element_factory_make("filesink", NULL);

  g_object_set(filesink, "location", "out.mp4", NULL);

  g_object_set(G_OBJECT(record_queue), "max-size-bytes", 0, "max-size-time", (guint64) 3 * GST_SECOND, "max-size-buffers", 0, "leaky", 2, NULL);
  g_object_set(G_OBJECT(probe_queue), "max-size-bytes", 0, "max-size-time", (guint64) 3 * GST_SECOND, "max-size-buffers", 0, "leaky", 2, NULL);

  gst_bin_add_many(GST_BIN(pipeline), src, record_queue, record_parse, probe_queue, mp4mux, filesink, NULL);
  gst_element_link_many(src, record_queue, record_parse, probe_queue, mp4mux, filesink, NULL);


  return true;
}


int main(int argc, char *argv[])
{
  gboolean terminate = FALSE;
  gst_init(&argc, &argv);

  pipeline = gst_pipeline_new("test-pipeline");

  startRecord();


  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  bus = gst_element_get_bus (pipeline);

  signal(SIGINT, shutdown);

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
