// Copyright 2018 Blueye Robotics AS
#include <gst/gst.h>
#include <signal.h>
#include <blkid/blkid.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <unistd.h>
#include "sys/time.h"
#include "GPMF_common.h"
#include "GPMF_writer.h"

GMainLoop *loop = NULL;


GstElement *pipeline;
GstElement *enc;
GstElement *mp4mux, *filesink;
GstElement *appsrc;

guint sourceid = 0;        /* To control the GSource */

uint64_t recording_beginning;

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
size_t handleACCL;
size_t handleGYRO;
size_t handleGPS;
//size_t handleISOG;
//size_t handleSHUT;
char buffer[8192];
uint32_t *payload, payload_size;

#pragma pack(pop)

static gboolean push_data (gpointer data) {
  auto now = msNow();
  static auto before = now;
  if (now - before < 1000)
    return TRUE;
  before = now;
  size_t queued;
  //g_object_get(appsrc, "current-level-bytes", &queued, NULL);
  //g_print("i%d", queued);
  g_print("i");
  GstBuffer *gbuffer;
  GstFlowReturn ret;

  /* Create a new empty buffer */
  gbuffer = gst_buffer_new();

  //gst_buffer_fill(gbuffer, 0, (gpointer)&((*buf)[0]), buf->size()); 

  //char b[4];
  //static uint64_t counter = 0;
  ////g_print("%d", counter);
  ////snprintf(b, 10, "ii%d", counter++);
  //b[0] = 0x08;
  //b[1] = 0x08;
  //b[2] = 0x08;
  //b[3] = counter++ % 0xff;
  //GstMemory *mem = gst_allocator_alloc(NULL, strlen(b), NULL);
  //gst_buffer_append_memory(gbuffer, mem);
  //gst_buffer_fill(gbuffer, 0, b, strlen(b));

  static uint32_t count = 0;
  ++count;
  uint32_t err;

  int16_t acc[200*3];
  for (int16_t i = 0; i < 200*3; i+=3)
  {
    acc[i] = 4078;
    acc[i+1] = -20;
    acc[i+2] = 536;
  }
  err = GPMFWriteStreamStore(handleACCL, STR2FOURCC("ACCL"), 's', 3*sizeof(int16_t), 200, acc, GPMF_FLAGS_NONE);
  if (err) printf("err = %d\n", err);

  int16_t gyro[399*3];
  for (int16_t i = 0; i < 399*3; i+=3)
  {
    gyro[i] = 1811;
    gyro[i+1] = 136;
    gyro[i+2] = 181;
  }
  err = GPMFWriteStreamStore(handleGYRO, STR2FOURCC("GYRO"), 's', 3*sizeof(int16_t), 399, gyro, GPMF_FLAGS_NONE);
  if (err) printf("err = %d\n", err);

  static int32_t lat = 406450466;
  static int32_t lng = -740687085;
  static int32_t alt = -33035;
  ++lat;
  ++lng;
  --alt;
  //if (count % 64 < 32)
  //{
  //  ++lat;
  //  ++lng;
  //}
  //else
  //{
  //  --lat;
  //  --lng;
  //}
  char utcdata[17];
  static uint8_t mins = 0;
  uint8_t secs = count % 60;
  if (secs == 0)
    ++mins;
  snprintf(utcdata, 17, "17062012%02d%02d.515", mins, secs);
  GPMFWriteStreamStore(handleGPS, STR2FOURCC("GPSU"), 'c', 16*sizeof(char), 1, &utcdata, GPMF_FLAGS_STICKY);

  int32_t gps[18*5];
  for (uint32_t i = 0; i < 18*5; i+=5)
  {
    gps[i] = lat;
    gps[i+1] = lng;
    gps[i+2] = alt;
    gps[i+3] = 0;
    gps[i+4] = 0;
  }
  err = GPMFWriteStreamStore(handleGPS, STR2FOURCC("GPS5"), 'l', 5*sizeof(int32_t), 18, &gps, GPMF_FLAGS_NONE);
  if (err) printf("err = %d\n", err);

  //float isog[4];
  //for (uint32_t i = 0; i < 4; i++)
  //{
  //  isog[i] = 1.414;
  //}
  //err = GPMFWriteStreamStore(handleISOG, STR2FOURCC("ISOG"), 'f', 1*sizeof(float), 4, &isog, GPMF_FLAGS_NONE);
  //if (err) printf("err = %d\n", err);

  //float shut[4];
  //for (uint32_t i = 0; i < 4; i++)
  //{
  //  shut[i] = 0;
  //}
  //err = GPMFWriteStreamStore(handleSHUT, STR2FOURCC("SHUT"), 'f', 1*sizeof(float), 4, &shut, GPMF_FLAGS_NONE);
  //if (err) printf("err = %d\n", err);

  //device_metacc* p = (device_metacc*)handleACCL;
  //GstMemory *mem = gst_allocator_alloc(NULL, p->payload_curr_size, NULL);
  //gst_buffer_append_memory(gbuffer, mem);
  //gst_buffer_fill(buffer, 0, p->payload_buffer, p->payload_alloc_size); //payload_curr_size);

  GPMFWriteGetPayload(gpmfhandle, GPMF_CHANNEL_TIMED, (uint32_t *)buffer, sizeof(buffer), &payload, &payload_size);

  //printf("payload_size = %d\n", payload_size);

  ////Using the GPMF_Parser, output some of the contents
  //GPMF_stream gs;
  //if (GPMF_OK == GPMF_Init(&gs, payload, payload_size))
  //{
  //  GPMF_ResetState(&gs);
  //  do
  //  { 
  //    PrintGPMF(&gs);  // printf current GPMF KLV
  //  } while (GPMF_OK == GPMF_Next(&gs, GPMF_RECURSE_LEVELS));
  //}
  //printf("\n");

  GstMemory *mem = gst_allocator_alloc(NULL, payload_size, NULL);
  gst_buffer_append_memory(gbuffer, mem);
  gst_buffer_fill(gbuffer, 0, payload, payload_size); //payload_curr_size);

  //static GstClockTime timestamp = 0;
  GstClockTime timestamp = now - recording_beginning;
  timestamp *= 1e6;
  GST_BUFFER_PTS (gbuffer) = timestamp;
  //GST_BUFFER_DURATION (gbuffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (gbuffer) = gst_util_uint64_scale_int (1, GST_SECOND, SAMPLE_RATE);
  //timestamp += GST_BUFFER_DURATION (gbuffer);
  //g_print("\t%lu\t", timestamp);

  /* Push the buffer into the appsrc */
  g_signal_emit_by_name (appsrc, "push-buffer", gbuffer, &ret);

  /* Free the buffer now that we are done with it */
  gst_buffer_unref (gbuffer);

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
  GPMFWriteStreamClose(handleACCL);
  GPMFWriteStreamClose(handleGYRO);
  GPMFWriteStreamClose(handleGPS);
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

bool create_pipeline()
{
  g_print("create_pipeline\n");

  std::string launch_string = 
    "videotestsrc is-live=true"
    " ! queue max-size-bytes=0 max-size-time=0 max-size-buffers=0"
    " ! x264enc name=enc"
    " ! queue max-size-bytes=0 max-size-time=0 max-size-buffers=0"
    " ! taginject name=taginject"
    " ! mp4mux name=mp4mux"
    " ! filesink name=filesink location=out.mp4";
  g_print("launch_string: %s\n", launch_string.c_str());
  //g_object_set(src, "pattern", 2, NULL);

  pipeline = gst_parse_launch (launch_string.c_str(), NULL);

  enc = gst_bin_get_by_name(GST_BIN(pipeline), "enc");

  mp4mux = gst_bin_get_by_name(GST_BIN(pipeline), "mp4mux");
  filesink = gst_bin_get_by_name(GST_BIN(pipeline), "filesink");

  appsrc = gst_element_factory_make("appsrc", NULL);
  GstCaps *caps = gst_caps_from_string("text/x-raw, format=(string)utf8");

  /* Configure appsrc */
  g_object_set (appsrc, "caps", caps, NULL);
  g_object_set(G_OBJECT(appsrc), 
                "stream-type", 0, // GST_APP_STREAM_TYPE_STREAM 
                "format", GST_FORMAT_TIME, 
                "is-live", TRUE, 
                //"do-timestamp", TRUE,
                NULL); 

  gst_bin_add_many(GST_BIN(pipeline), appsrc, NULL);
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

  char bufACCL[4096];
  char bufGYRO[8192];
  char bufGPS[4096];
  int16_t s;
  float f;
  char txt[80];
  uint32_t err;

  handleACCL = GPMFWriteStreamOpen(gpmfhandle, GPMF_CHANNEL_TIMED, GPMF_DEVICE_ID_CAMERA, "Camera", bufACCL, sizeof(bufACCL));
  if (handleACCL == 0)
  {
    g_print("Couldn't create handleACCL\n");
    exit(1);
  }
  handleGYRO = GPMFWriteStreamOpen(gpmfhandle, GPMF_CHANNEL_TIMED, GPMF_DEVICE_ID_CAMERA, "Camera", bufGYRO, sizeof(bufGYRO));
  if (handleGYRO == 0)
  {
    g_print("Couldn't create handleGYRO\n");
    exit(1);
  }
  handleGPS = GPMFWriteStreamOpen(gpmfhandle, GPMF_CHANNEL_TIMED, GPMF_DEVICE_ID_CAMERA, "Camera", bufGPS, sizeof(bufGPS));
  if (handleGPS == 0)
  {
    g_print("Couldn't create handleGPS\n");
    exit(1);
  }
  //handleISOG = GPMFWriteStreamOpen(gpmfhandle, GPMF_CHANNEL_TIMED, GPMF_DEVICE_ID_CAMERA, "Camera", NULL, 0);
  //if (handleISOG == 0)
  //{
  //  g_print("Couldn't create handleISOG\n");
  //  exit(1);
  //}
  //handleSHUT = GPMFWriteStreamOpen(gpmfhandle, GPMF_CHANNEL_TIMED, GPMF_DEVICE_ID_CAMERA, "Camera", NULL, 0);
  //if (handleSHUT == 0)
  //{
  //  g_print("Couldn't create handleSHUT\n");
  //  exit(1);
  //}

  //Initialize sensor stream with any sticky data
  //sprintf_s(txt, 80, "Accelerometer (up/down, right/left, forward/back)");
  //err = GPMFWriteStreamStore(handleACCL, GPMF_KEY_STREAM_NAME, 'c', strlen(txt), 1, &txt, GPMF_FLAGS_STICKY);
  //if (err) printf("err = %d\n", err);
  char siun[5] = "m/sA";
  siun[3] = 0xb2; // squared
  err = GPMFWriteStreamStore(handleACCL, STR2FOURCC("SIUN"), 'c', 4*sizeof(char), 1, &siun, GPMF_FLAGS_STICKY);
  if (err) printf("err = %d\n", err);
  s = 418;
  err = GPMFWriteStreamStore(handleACCL, GPMF_KEY_SCALE, 's', sizeof(s), 1, &s, GPMF_FLAGS_STICKY);
  if (err) printf("err = %d\n", err);
  f = 31.062;
  err = GPMFWriteStreamStore(handleACCL, STR2FOURCC("TMPC"), 'f', sizeof(f), 1, &f, GPMF_FLAGS_STICKY);
  if (err) printf("err = %d\n", err);

  //sprintf_s(txt, 80, "Gyroscope (z,x,y)");
  //GPMFWriteStreamStore(handleGYRO, GPMF_KEY_STREAM_NAME, GPMF_TYPE_STRING_ASCII, strlen(txt), 1, &txt, GPMF_FLAGS_STICKY);
  sprintf_s(txt, 80, "rad/s");
  GPMFWriteStreamStore(handleGYRO, STR2FOURCC("SIUN"), 'c', strlen(txt), 1, &txt, GPMF_FLAGS_STICKY);
  s = 3755;
  GPMFWriteStreamStore(handleGYRO, GPMF_KEY_SCALE, 's', sizeof(s), 1, &s, GPMF_FLAGS_STICKY);
  f = 31.062;
  GPMFWriteStreamStore(handleGYRO, STR2FOURCC("TMPC"), 'f', sizeof(f), 1, &f, GPMF_FLAGS_STICKY);

  uint32_t L = 0;
  GPMFWriteStreamStore(handleGPS, STR2FOURCC("GPSF"), 'L', sizeof(L), 1, &L, GPMF_FLAGS_STICKY);
  char utcdata[17] = "180120221500.515";
  GPMFWriteStreamStore(handleGPS, STR2FOURCC("GPSU"), 'c', 16*sizeof(char), 1, &utcdata, GPMF_FLAGS_STICKY);
  //sprintf_s(txt, 80, "GPS (Lat., Long., Alt., 2D speed, 3D speed)");
  //err = GPMFWriteStreamStore(handleGPS, GPMF_KEY_STREAM_NAME, 'c', strlen(txt), 1, &txt, GPMF_FLAGS_STICKY);
  //if (err) printf("err = %d\n", err);
  uint16_t S = 9999;
  err = GPMFWriteStreamStore(handleGPS, STR2FOURCC("GPSP"), 'S', sizeof(S), 1, &S, GPMF_FLAGS_STICKY);
  if (err) printf("err = %d\n", err);
  sprintf_s(txt, 80, "degdegmAAm/sm/s");
  txt[7] = '\0';
  txt[8] = '\0';
  err = GPMFWriteStreamStore(handleGPS, STR2FOURCC("UNIT"), 'c', 3*sizeof(char), 5, &txt, GPMF_FLAGS_STICKY);
  if (err) printf("err = %d\n", err);
  int32_t ss[5] = {10000000, 10000000, 1000, 1000, 100};
  err = GPMFWriteStreamStore(handleGPS, GPMF_KEY_SCALE, 'l', sizeof(int32_t), 5, ss, GPMF_FLAGS_STICKY);
  if (err) printf("err = %d\n", err);

  //sprintf_s(txt, 80, "Sensor gain (ISO x100)");
  //GPMFWriteStreamStore(handleISOG, GPMF_KEY_STREAM_NAME, GPMF_TYPE_STRING_ASCII, strlen(txt), 1, &txt, GPMF_FLAGS_STICKY);

  //sprintf_s(txt, 80, "Exposure time (shutter speed)");
  //GPMFWriteStreamStore(handleSHUT, GPMF_KEY_STREAM_NAME, GPMF_TYPE_STRING_ASCII, strlen(txt), 1, &txt, GPMF_FLAGS_STICKY);
  //sprintf_s(txt, 80, "s");
  //GPMFWriteStreamStore(handleSHUT, STR2FOURCC("SIUN"), 'c', strlen(txt), 1, &txt, GPMF_FLAGS_STICKY);

  //Flush any stale data before starting video capture.
  err = GPMFWriteGetPayload(gpmfhandle, GPMF_CHANNEL_TIMED, (uint32_t *)buffer, sizeof(buffer), &payload, &payload_size);
  if (err) printf("err = %d\n", err);




  loop = g_main_loop_new (NULL, TRUE);


  gst_init(&argc, &argv);

  pipeline = gst_pipeline_new("test-pipeline");

  create_pipeline();

  GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE(pipeline));
  gst_bus_add_watch (bus, (GstBusFunc) bus_message, NULL);
  gst_object_unref(bus);


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
