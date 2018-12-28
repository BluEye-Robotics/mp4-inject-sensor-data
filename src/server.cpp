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
#include "sys/time.h"
#include "GPMF_common.h"
#include "GPMF_writer.h"

GMainLoop *loop = NULL;


GstPad *tee_pad;
GstPad *record_queue_pad, *display_queue_pad;
GstElement *pipeline, *src;
GstElement *record_queue, *mp4mux, *probe_queue, *filesink, *enc;
GstElement *appsrc;
GstElement *audioenc;

guint64 num_samples;   /* Number of samples generated so far (for timestamp generation) */
guint sourceid = 0;        /* To control the GSource */

uint64_t recording_beginning;

#define CHUNK_SIZE 3   /* Amount of bytes we are sending in each buffer */
#define SAMPLE_RATE 1 /* Samples per second we are sending */

inline uint64_t msNow(){
  struct timeval tp;
  gettimeofday(&tp, NULL);

  uint64_t ms = tp.tv_sec*1000 + tp.tv_usec/1000;
  return ms;
}

#define sprintf_s(a,b,c) sprintf(a,c)

#pragma pack(push)
#pragma pack(1)   //GPMF sensor data structures are always byte packed.

size_t gpmfhandle = 0;
size_t handleA;

typedef struct sensorAdata  // Example 10-byte pack structure.
{
  uint32_t flags;
  uint8_t ID[6];
} sensorAdata;

#pragma pack(pop)

static gboolean push_data (gpointer data) {
  auto now = msNow();
  static auto before = now;
  if (now - before < 1000)
    return TRUE;
  before = now;
  g_print("i");
  GstBuffer *buffer;
  GstFlowReturn ret;

  /* Create a new empty buffer */
  buffer = gst_buffer_new();

  //gst_buffer_fill(buffer, 0, (gpointer)&((*buf)[0]), buf->size()); 

  //char b[4];
  //static uint64_t counter = 0;
  ////g_print("%d", counter);
  ////snprintf(b, 10, "ii%d", counter++);
  //b[0] = 0x08;
  //b[1] = 0x08;
  //b[2] = 0x08;
  //b[3] = counter++ % 0xff;
  //GstMemory *mem = gst_allocator_alloc(NULL, strlen(b), NULL);
  //gst_buffer_append_memory(buffer, mem);
  //gst_buffer_fill(buffer, 0, b, strlen(b));

  uint32_t samples;
  uint32_t err;
  sensorAdata Adata[4];
  static uint32_t count = 0;

  samples = 1 + (rand() % 3); //1-4 values
  for (uint32_t i = 0; i < samples; i++)
  {
    Adata[i].flags = count++;
    Adata[i].ID[0] = 1;
    Adata[i].ID[1] = 2;
    Adata[i].ID[2] = 3;
    Adata[i].ID[3] = 4;
    Adata[i].ID[4] = 5;
    Adata[i].ID[5] = 6;
  }
  err = GPMFWriteStreamStore(handleA, STR2FOURCC("SnrA"), GPMF_TYPE_COMPLEX, sizeof(sensorAdata), samples, Adata, GPMF_FLAGS_NONE);
  if (err) printf("err = %d\n", err);

  device_metadata* p = (device_metadata*)handleA;

  GstMemory *mem = gst_allocator_alloc(NULL, p->payload_curr_size, NULL);
  gst_buffer_append_memory(buffer, mem);
  gst_buffer_fill(buffer, 0, p->payload_buffer, p->payload_alloc_size); //payload_curr_size);

  static GstClockTime timestamp = 0;
  //GstClockTime timestamp = now - recording_beginning - 1;
  //timestamp *= 1e5;
  GST_BUFFER_PTS (buffer) = timestamp;
  GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, SAMPLE_RATE);
  timestamp += GST_BUFFER_DURATION (buffer);
  //g_print("\t%lu\t", timestamp);

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
  g_source_remove (sourceid);
  gst_element_send_event(enc, gst_event_new_eos());
  gst_element_send_event(appsrc, gst_event_new_eos());
  GPMFWriteStreamClose(handleA);
  GPMFWriteServiceClose(gpmfhandle);
  //gst_element_send_event(pipeline, gst_event_new_eos());
  //exit(signum);
}

static gboolean bus_message (GstBus * bus, GstMessage * msg, gpointer user_data)
{
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
        g_main_loop_quit (loop);
        break;
      case GST_MESSAGE_EOS:
        g_print ("End-Of-Stream reached.\n");
        g_main_loop_quit (loop);
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
        g_printerr ("Unexpected message received: %s\n", GST_MESSAGE_TYPE_NAME(msg));
        break;
    }
    gst_message_unref (msg);
  }
  return TRUE;
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
  g_object_set(src, "is-live", TRUE, NULL);

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
  gpmfhandle = GPMFWriteServiceInit();
  if (gpmfhandle == 0)
  {
    g_print("Couldn't create gpmfhandle\n");
    exit(1);
  }

  char buffer[8192];
  char sensorA[4096];
  uint32_t *payload, payload_size;
  uint32_t tmp,faketime,fakedata;
  char txt[80];

  handleA = GPMFWriteStreamOpen(gpmfhandle, GPMF_CHANNEL_TIMED, GPMF_DEVICE_ID_CAMERA, "MyCamera", sensorA, sizeof(sensorA));
  if (handleA == 0)
  {
    g_print("Couldn't create handleA\n");
    exit(1);
  }

  //Initialize sensor stream with any sticky data
  sprintf_s(txt, 80, "Sensor A");
  GPMFWriteStreamStore(handleA, GPMF_KEY_STREAM_NAME, GPMF_TYPE_STRING_ASCII, strlen(txt), 1, &txt, GPMF_FLAGS_STICKY);
  sprintf_s(txt, 80, "LB[6]"); // matching sensorAdata
  GPMFWriteStreamStore(handleA, GPMF_KEY_TYPE, GPMF_TYPE_STRING_ASCII, strlen(txt), 1, &txt, GPMF_FLAGS_STICKY);

  //Flush any stale data before starting video capture.
  GPMFWriteGetPayload(gpmfhandle, GPMF_CHANNEL_TIMED, (uint32_t *)buffer, sizeof(buffer), &payload, &payload_size);





  GstMessage *msg;
  GstStateChangeReturn ret;
  GstBus *bus;

  loop = g_main_loop_new (NULL, TRUE);


  gst_init(&argc, &argv);

  pipeline = gst_pipeline_new("test-pipeline");

  startRecord();

  bus = gst_pipeline_get_bus (GST_PIPELINE(pipeline));
  gst_bus_add_watch (bus, (GstBusFunc) bus_message, NULL);
  gst_object_unref(bus);


  //g_signal_connect (appsrc, "need-data", G_CALLBACK (cb_need_data), NULL);
  g_signal_connect (appsrc, "need-data", G_CALLBACK (start_feed), NULL);
  g_signal_connect (appsrc, "enough-data", G_CALLBACK (stop_feed), NULL);

  signal(SIGINT, shutdown);

  recording_beginning = msNow();

  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
  {
    g_print("Couldn't play stream\n");
    exit(1);
  }

  g_print("running...\n");
  g_main_loop_run (loop);
  g_print("stopped\n");

  //std::thread t0([&](){
  //  sleep(5);
  //    push_data(NULL);
  //  sleep(5);
  //    push_data(NULL);
  //  while(!terminate)
  //  {
  //    push_data(NULL);
  //    usleep(100000);
  //  }
  //  });

  /* create a server instance */
  /* start serving */
  gst_object_unref (bus);
  g_main_loop_unref (loop);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  return 0;
}
