#include "screencast-webrtc.h"
#include "../common/utils.h"
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <gst/webrtc/webrtc.h>
#include <stdio.h>
#include <string.h>

#define PORTAL_BUS_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define SCREENCAST_INTERFACE "org.freedesktop.portal.ScreenCast"
#define REQUEST_INTERFACE "org.freedesktop.portal.Request"

typedef struct {
  GMainLoop *loop;
  GDBusConnection *connection;
  gchar *sanitized_name;
  gchar *session_path;
  gchar *session_token;

  GstElement *pipeline;
  GstElement *webrtcbin;
} ScreencastWebRTCState;


static void select_sources(ScreencastWebRTCState *state);

// --- WebRTC Helper Functions ---

static void send_sdp_to_peer(const gchar *type, const gchar *sdp_string) {
  gchar *escaped_sdp = g_strescape(sdp_string, NULL);
  g_print("\n\n=== SDP %s START ===\n", type);
  g_print("{\"type\": \"%s\", \"sdp\": \"%s\"}", type, escaped_sdp);
  g_print("\n=== SDP %s END ===\n\n", type);
  g_free(escaped_sdp);
}

static void on_offer_created(GstPromise *promise, gpointer user_data) {
  ScreencastWebRTCState *state = (ScreencastWebRTCState *)user_data;
  GstStructure *reply;
  GstWebRTCSessionDescription *offer = NULL;
  const gchar *sdp_string;

  if (gst_promise_wait(promise) != GST_PROMISE_RESULT_REPLIED) return;
  reply = gst_promise_get_reply(promise);
  gst_structure_get(reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
  gst_promise_unref(promise);

  if (offer) {
    g_signal_emit_by_name(state->webrtcbin, "set-local-description", offer, NULL);
    sdp_string = gst_sdp_message_as_text(offer->sdp);
    send_sdp_to_peer("offer", sdp_string);
    gst_webrtc_session_description_free(offer);
  }
}

static void on_negotiation_needed(GstElement *element, gpointer user_data) {
  ScreencastWebRTCState *state = (ScreencastWebRTCState *)user_data;
  GstPromise *promise = gst_promise_new_with_change_func(on_offer_created, state, NULL);
  g_signal_emit_by_name(state->webrtcbin, "create-offer", NULL, promise);
}

static void on_ice_candidate(GstElement *webrtc, guint mlineindex, gchar *candidate, gpointer user_data) {
  g_print("\n{\"candidate\": \"%s\", \"sdpMLineIndex\": %u}\n", candidate, mlineindex);
}

// --- Pipeline ---

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
  ScreencastWebRTCState *state = (ScreencastWebRTCState *)data;
  switch (GST_MESSAGE_TYPE(msg)) {
  case GST_MESSAGE_ERROR: {
    gchar *debug;
    GError *error;
    gst_message_parse_error(msg, &error, &debug);
    g_printerr("\nERROR: %s\n", error->message);
    if (debug) g_printerr("Debug Info: %s\n", debug);
    g_error_free(error);
    g_free(debug);
    g_main_loop_quit(state->loop);
    break;
  }
  default: break;
  }
  return TRUE;
}

static gchar *get_default_monitor_source() {
  FILE *fp;
  char path[1024];
  fp = popen("pactl get-default-sink", "r");
  if (!fp) return NULL;
  if (fgets(path, sizeof(path) - 1, fp)) {
    path[strcspn(path, "\n")] = 0;
    path[strcspn(path, "\r")] = 0;
  } else {
    pclose(fp);
    return NULL;
  }
  pclose(fp);
  return g_strdup_printf("%s.monitor", path);
}

static void start_stream(guint32 id, ScreencastWebRTCState *state) {
  g_print("\n>>> Starting WebRTC Pipeline... Node ID: %d\n", id);
  gst_init(NULL, NULL);
  gchar *audio_device = get_default_monitor_source();
  
  char *pipeline_str = g_strdup_printf(
      "webrtcbin name=sendrecv stun-server=stun://stun.l.google.com:19302 bundle-policy=max-bundle latency=0 "

      // --- VIDEO ---
      "pipewiresrc path=%u do-timestamp=true ! "
      "queue max-size-buffers=3 leaky=downstream ! " // Kritik Tampon
      "videoconvert ! "
      "videoscale ! videorate ! "
      
      "video/x-raw(memory:SystemMemory),format=NV12,width=1920,height=1080,framerate=60/1 ! "

      "nvh264enc "
      "bitrate=8000 "          
      "rc-mode=cbr "
      "preset=low-latency-hq "
      "tune=ultra-low-latency "
      "gop-size=60 "
      "zerolatency=true "
      "qos=false ! "           

      "h264parse ! "
      
      "video/x-h264,stream-format=byte-stream,profile=constrained-baseline ! "
      
      "rtph264pay config-interval=-1 pt=96 ! "
      "application/x-rtp,media=video,encoding-name=H264,payload=96 ! "
      "queue ! sendrecv. "

      "pulsesrc device=%s do-timestamp=true buffer-time=200000 ! "
      "audioconvert ! "
      "audioresample ! "
      "opusenc ! "
      "rtpopuspay pt=97 ! "
      "queue ! sendrecv. ",
      id, audio_device ? audio_device : "0");

  if (audio_device) g_free(audio_device);

  GError *error = NULL;
  state->pipeline = gst_parse_launch(pipeline_str, &error);
  g_free(pipeline_str);

  if (error) {
    g_printerr("Pipeline Error: %s\n", error->message);
    g_error_free(error);
    g_main_loop_quit(state->loop);
    return;
  }

  state->webrtcbin = gst_bin_get_by_name(GST_BIN(state->pipeline), "sendrecv");
  g_signal_connect(state->webrtcbin, "on-negotiation-needed", G_CALLBACK(on_negotiation_needed), state);
  g_signal_connect(state->webrtcbin, "on-ice-candidate", G_CALLBACK(on_ice_candidate), state);

  GstBus *bus = gst_element_get_bus(state->pipeline);
  gst_bus_add_watch(bus, bus_call, state);
  gst_object_unref(bus);

  gst_element_set_state(state->pipeline, GST_STATE_PLAYING);
  g_print("WebRTC Pipeline Playing. Copy JSON to browser.\n");
}

// --- PORTAL ZİNCİRİ (Chained Logic) ---

static void on_start_response(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params, gpointer user_data) {
  ScreencastWebRTCState *state = user_data;
  guint32 response_code;
  GVariant *res;
  
  g_variant_get(params, "(u@a{sv})", &response_code, &res);
  if (response_code != 0) {
    g_printerr("Start Request Denied.\n");
    g_main_loop_quit(state->loop);
    g_variant_unref(res);
    return;
  }

  GVariant *streams = g_variant_lookup_value(res, "streams", G_VARIANT_TYPE("a(ua{sv})"));
  if (streams) {
    GVariantIter iter;
    g_variant_iter_init(&iter, streams);
    guint32 stream_id;
    if (g_variant_iter_next(&iter, "(u@a{sv})", &stream_id, NULL)) {
      start_stream(stream_id, state);
    }
    g_variant_unref(streams);
  }
  g_variant_unref(res);
}

static void start_screencast(ScreencastWebRTCState *state) {
  gchar *token = generate_token("tk_start");
  GVariantBuilder opts;
  g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&opts, "{sv}", "handle_token", g_variant_new_string(token));

  g_print("Sending Start Request...\n");

  GError *error = NULL;
  GVariant *ret = g_dbus_connection_call_sync(
      state->connection, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH, SCREENCAST_INTERFACE, "Start",
      g_variant_new("(osa{sv})", state->session_path, "", &opts),
      NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

  if (error) {
    g_printerr("Start Failed: %s\n", error->message);
    g_error_free(error);
    g_free(token);
    return;
  }

  gchar *req_path;
  g_variant_get(ret, "(o)", &req_path);
  g_variant_unref(ret);

  g_dbus_connection_signal_subscribe(state->connection, PORTAL_BUS_NAME, REQUEST_INTERFACE, "Response",
                                     req_path, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_start_response, state, NULL);
  g_free(req_path);
  g_free(token);
}

static void on_select_response(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params, gpointer user_data) {
  ScreencastWebRTCState *state = user_data;
  guint32 response_code;
  g_variant_get(params, "(u@a{sv})", &response_code, NULL);

  if (response_code != 0) {
    g_printerr("Source selection failed.\n");
    g_main_loop_quit(state->loop);
    return;
  }
  
  g_print("Sources selected. Starting screencast...\n");
  start_screencast(state);
}

static void select_sources(ScreencastWebRTCState *state) {
  gchar *token = generate_token("tk_slct");
  GVariantBuilder opts;
  g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&opts, "{sv}", "handle_token", g_variant_new_string(token));
  g_variant_builder_add(&opts, "{sv}", "types", g_variant_new_uint32(1 | 2));
  g_variant_builder_add(&opts, "{sv}", "cursor_mode", g_variant_new_uint32(2));

  g_print("Requesting Source Selection...\n");

  GError *error = NULL;
  GVariant *ret = g_dbus_connection_call_sync(
      state->connection, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH, SCREENCAST_INTERFACE, "SelectSources",
      g_variant_new("(oa{sv})", state->session_path, &opts),
      NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

  if (error) {
    g_printerr("SelectSources Failed: %s\n", error->message);
    g_error_free(error);
    g_free(token);
    return;
  }

  gchar *req_path;
  g_variant_get(ret, "(o)", &req_path);
  g_variant_unref(ret);

  g_print("Portal assigned Select path: %s\n", req_path);
  
  g_dbus_connection_signal_subscribe(state->connection, PORTAL_BUS_NAME, REQUEST_INTERFACE, "Response",
                                     req_path, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_select_response, state, NULL);
  g_free(req_path);
  g_free(token);
}

static void on_create_session_response(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params, gpointer user_data) {
  ScreencastWebRTCState *state = user_data;
  guint32 response_code;
  GVariant *results;
  g_variant_get(params, "(u@a{sv})", &response_code, &results);

  if (response_code != 0) {
    g_printerr("CreateSession Failed.\n");
    g_main_loop_quit(state->loop);
    return;
  }

  gchar *remote_handle = NULL;
  if (g_variant_lookup(results, "session_handle", "s", &remote_handle)) {
      g_free(state->session_path);
      state->session_path = g_strdup(remote_handle);
      g_free(remote_handle);
  }
  g_variant_unref(results);

  g_print("Session created. Proceeding to Select Sources...\n");
  select_sources(state); // ZİNCİRLEME GEÇİŞ
}

static void create_session(ScreencastWebRTCState *state) {
  state->session_token = generate_token("tk_sess");
  const gchar *unique = g_dbus_connection_get_unique_name(state->connection);
  state->sanitized_name = sanitize_sender_name(unique);
  state->session_path = g_strdup_printf("%s/session/%s/%s", PORTAL_OBJECT_PATH, state->sanitized_name, state->session_token);

  gchar *token = generate_token("tk_crt");
  GVariantBuilder opts;
  g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&opts, "{sv}", "handle_token", g_variant_new_string(token));
  g_variant_builder_add(&opts, "{sv}", "session_handle_token", g_variant_new_string(state->session_token));

  g_print("Creating Session...\n");

  GError *error = NULL;
  GVariant *ret = g_dbus_connection_call_sync(
      state->connection, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH, SCREENCAST_INTERFACE, "CreateSession",
      g_variant_new("(a{sv})", &opts),
      NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

  if (error) {
    g_printerr("CreateSession Failed: %s\n", error->message);
    g_error_free(error);
    g_free(token);
    return;
  }

  gchar *req_path;
  g_variant_get(ret, "(o)", &req_path);
  g_variant_unref(ret);

  g_dbus_connection_signal_subscribe(state->connection, PORTAL_BUS_NAME, REQUEST_INTERFACE, "Response",
                                     req_path, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_create_session_response, state, NULL);
  g_free(req_path);
  g_free(token);
}

// --- Main ---

static void process_sdp_answer(ScreencastWebRTCState *state, const gchar *json_input) {
  const char *sdp_start = strstr(json_input, "\"sdp\"");
  if (!sdp_start) return;
  char *val_start = strchr(sdp_start, ':');
  if (!val_start) return;
  val_start = strchr(val_start, '"');
  if (!val_start) return;
  val_start++;
  char *val_end = strchr(val_start, '"');
  if (!val_end) return;

  gchar *raw_sdp = g_strndup(val_start, val_end - val_start);
  gchar *unescaped = g_strcompress(raw_sdp);
  g_free(raw_sdp);

  GstSDPMessage *sdp_msg;
  if (gst_sdp_message_new_from_text(unescaped, &sdp_msg) == GST_SDP_OK) {
    GstWebRTCSessionDescription *answer = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdp_msg);
    GstPromise *p = gst_promise_new();
    g_signal_emit_by_name(state->webrtcbin, "set-remote-description", answer, p);
    gst_promise_unref(p);
    gst_webrtc_session_description_free(answer);
    g_print("Remote SDP set. Connection should start!\n");
  }
  g_free(unescaped);
}

static gboolean on_stdin_input(GIOChannel *channel, GIOCondition condition, gpointer user_data) {
  ScreencastWebRTCState *state = user_data;
  gchar *line = NULL;
  if (g_io_channel_read_line(channel, &line, NULL, NULL, NULL) == G_IO_STATUS_NORMAL) {
    gchar *trimmed = g_strchomp(line);
    if (g_str_has_prefix(trimmed, "{")) process_sdp_answer(state, trimmed);
    else if (g_strcmp0(trimmed, "exit") == 0) g_main_loop_quit(state->loop);
    g_free(line);
  }
  return TRUE;
}

void screencast_webrtc_tutorial(int argc, char *argv[]) {
  ScreencastWebRTCState *state = g_new0(ScreencastWebRTCState, 1);
  GError *error = NULL;

  g_print("Starting WebRTC Screencast (Robust Version).\n");
  state->connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

  if (error) {
    g_printerr("DBus Error: %s\n", error->message);
    g_error_free(error);
    return;
  }

  state->loop = g_main_loop_new(NULL, FALSE);
  GIOChannel *stdin_ch = g_io_channel_unix_new(0);
  g_io_add_watch(stdin_ch, G_IO_IN, on_stdin_input, state);
  g_io_channel_unref(stdin_ch);

  // ZİNCİRİ BAŞLAT
  create_session(state);

  g_main_loop_run(state->loop);

  if (state->session_path) {
    g_dbus_connection_call(state->connection, PORTAL_BUS_NAME, state->session_path,
                           "org.freedesktop.portal.Session", "Close", NULL, NULL,
                           G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
  }
  if (state->pipeline) {
      gst_element_set_state(state->pipeline, GST_STATE_NULL);
      gst_object_unref(state->pipeline);
  }
  g_object_unref(state->connection);
  g_main_loop_unref(state->loop);
  g_free(state->sanitized_name);
  g_free(state->session_path);
  g_free(state);
}