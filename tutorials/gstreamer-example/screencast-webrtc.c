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

  GstElement *pipeline;
  GstElement *webrtcbin;
} ScreencastWebRTCState;

// Forward declarations
static void on_start_response(GDBusConnection *conn, const gchar *sender_name,
                              const gchar *object_path,
                              const gchar *interface_name,
                              const gchar *signal_name, GVariant *parameters,
                              gpointer user_data);

static void on_select_response(GDBusConnection *conn, const gchar *sender_name,
                               const gchar *object_path,
                               const gchar *interface_name,
                               const gchar *signal_name, GVariant *parameters,
                               gpointer user_data);

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

  g_assert_nonnull(state->webrtcbin);

  GstPromiseResult result = gst_promise_wait(promise);
  if (result != GST_PROMISE_RESULT_REPLIED) {
    g_printerr("Error: SDP offer creation failed.\n");
    return;
  }

  reply = gst_promise_get_reply(promise);
  gst_structure_get(reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer,
                    NULL);
  gst_promise_unref(promise);

  if (!offer) {
    g_printerr("Error: No offer found in promise reply.\n");
    return;
  }

  g_signal_emit_by_name(state->webrtcbin, "set-local-description", offer, NULL);

  sdp_string = gst_sdp_message_as_text(offer->sdp);
  send_sdp_to_peer("offer", sdp_string);

  gst_webrtc_session_description_free(offer);
}

static void on_negotiation_needed(GstElement *element, gpointer user_data) {
  ScreencastWebRTCState *state = (ScreencastWebRTCState *)user_data;
  g_print("Negotiation needed. Creating offer...\n");
  GstPromise *promise =
      gst_promise_new_with_change_func(on_offer_created, state, NULL);
  g_signal_emit_by_name(state->webrtcbin, "create-offer", NULL, promise);
}

static void on_ice_candidate(GstElement *webrtc, guint mlineindex,
                             gchar *candidate, gpointer user_data) {
  g_print("\n=== ICE CANDIDATE ===\n");
  g_print("{\"candidate\": \"%s\", \"sdpMLineIndex\": %u}\n", candidate,
          mlineindex);
  g_print("=====================\n");
}

// Helper to manually extract SDP from JSON string to avoid json-glib dependency
static void process_sdp_answer(ScreencastWebRTCState *state,
                               const gchar *json_input) {
  g_print("Processing SDP Answer...\n");

  // 1. Simple search for the "sdp" field
  const char *sdp_start_tag = "\"sdp\"";
  char *sdp_start = strstr(json_input, sdp_start_tag);
  if (!sdp_start) {
    g_printerr("Error: JSON does not contain 'sdp' field.\n");
    return;
  }

  // Move past "sdp" and find the colon and opening quote
  char *val_start = strchr(sdp_start, ':');
  if (!val_start)
    return;
  val_start = strchr(val_start, '"');
  if (!val_start)
    return;
  val_start++; // Skip the quote

  // Find the closing quote
  char *val_end = strchr(val_start, '"');
  if (!val_end) {
    g_printerr("Error: Malformed JSON string.\n");
    return;
  }

  // Copy the raw SDP string
  size_t len = val_end - val_start;
  gchar *raw_sdp = g_strndup(val_start, len);

  // 2. Unescape the string (convert \r\n text to actual newlines)
  gchar *unescaped_sdp = g_strcompress(raw_sdp);
  g_free(raw_sdp);

  g_print("SDP Answer extracted and unescaped.\n");

  // 3. Create Session Description
  GstWebRTCSessionDescription *answer = NULL;
  GstSDPMessage *sdp_msg = NULL;

  if (gst_sdp_message_new_from_text(unescaped_sdp, &sdp_msg) != GST_SDP_OK) {
    g_printerr("Error: Failed to parse SDP text.\n");
    g_free(unescaped_sdp);
    return;
  }

  answer =
      gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdp_msg);
  g_assert_nonnull(answer);

  // 4. Set Remote Description
  GstPromise *promise = gst_promise_new();
  g_signal_emit_by_name(state->webrtcbin, "set-remote-description", answer,
                        promise);

  // Wait for the result (optional, but good for debugging)
  gst_promise_wait(promise);
  gst_promise_unref(promise);
  gst_webrtc_session_description_free(answer);
  g_free(unescaped_sdp);

  g_print("Remote description set successfully. Streaming should start.\n");
}

// --- Standard GStreamer & Portal Functions ---

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
  ScreencastWebRTCState *state = (ScreencastWebRTCState *)data;
  switch (GST_MESSAGE_TYPE(msg)) {
  case GST_MESSAGE_ERROR: {
    gchar *debug;
    GError *error;
    gst_message_parse_error(msg, &error, &debug);
    gst_debug_bin_to_dot_file_with_ts(GST_BIN(state->pipeline),
                                      GST_DEBUG_GRAPH_SHOW_ALL,
                                      "pipeline_error");

    g_printerr("\nERROR: %s\n", error->message);
    if (debug)
      g_printerr("Debug Info: %s\n", debug);
    g_error_free(error);
    g_free(debug);
    g_main_loop_quit(state->loop);
    break;
  }
  default:
    break;
  }
  return TRUE;
}

static gchar *get_default_monitor_source() {
  FILE *fp;
  char path[1024];

  fp = popen("pactl get-default-sink", "r");
  if (fp == NULL)
    return NULL;

  if (fgets(path, sizeof(path) - 1, fp) != NULL) {
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
  g_print("\n>>> Starting GStreamer WebRTC Pipeline... Node ID: %d\n", id);

  gst_init(NULL, NULL);
  gchar *audio_device = get_default_monitor_source();

  char *pipeline_str = g_strdup_printf(
    "webrtcbin name=sendrecv bundle-policy=max-bundle latency=0 "

    // --- VIDEO ---
    "pipewiresrc path=%u do-timestamp=true ! "

    // FIX: Removed glupload/gldownload loop.
    // Added videoconvert to handle the raw PipeWire format (DMA or SHM)
    "videoconvert ! "

    // Moved queue here to decouple source generation from processing
    "queue max-size-buffers=3 leaky=downstream ! "

    "videoscale ! videorate ! "
    "video/x-raw,width=1920,height=1080,framerate=60/1 ! "

    "nvh264enc "
    "bitrate=10000 "
    "rc-mode=cbr "
    "preset=low-latency-hq "
    "tune=ultra-low-latency "
    "gop-size=60 "
    "zerolatency=true ! "

    "h264parse ! "
    "rtph264pay config-interval=1 ! "
    "application/x-rtp,media=video,encoding-name=H264,payload=96 ! "
    "queue ! sendrecv. "

    // --- AUDIO ---
    "pulsesrc device=%s do-timestamp=true buffer-time=200000 ! "
    "audioconvert ! "
    "audioresample ! "
    "opusenc ! "
    "rtpopuspay ! "
    "queue ! sendrecv. ",
    id, audio_device);

  g_free(audio_device);

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
  if (!state->webrtcbin) {
    g_printerr("Error: Could not find 'sendrecv' (webrtcbin) in pipeline.\n");
    g_main_loop_quit(state->loop);
    return;
  }

  g_signal_connect(state->webrtcbin, "on-negotiation-needed",
                   G_CALLBACK(on_negotiation_needed), state);
  g_signal_connect(state->webrtcbin, "on-ice-candidate",
                   G_CALLBACK(on_ice_candidate), state);

  GstBus *bus = gst_element_get_bus(state->pipeline);
  gst_bus_add_watch(bus, bus_call, state);
  gst_object_unref(bus);

  gst_element_set_state(state->pipeline, GST_STATE_PLAYING);
}

static void start_screencast(ScreencastWebRTCState *state) {
  gchar *token_start = generate_token("tk_start");
  gchar *start_request_path =
      g_strdup_printf("%s/request/%s/%s", PORTAL_OBJECT_PATH,
                      state->sanitized_name, token_start);

  GVariantBuilder start_opts;
  g_variant_builder_init(&start_opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&start_opts, "{sv}", "handle_token",
                        g_variant_new_string(token_start));

  g_dbus_connection_call(
      state->connection, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH,
      SCREENCAST_INTERFACE, "Start",
      g_variant_new("(osa{sv})", state->session_path, "", &start_opts), NULL,
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);

  g_dbus_connection_signal_subscribe(
      state->connection, PORTAL_BUS_NAME, REQUEST_INTERFACE, "Response",
      start_request_path, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_start_response,
      state, NULL);

  g_free(token_start);
  g_free(start_request_path);
}

static void on_start_response(GDBusConnection *conn, const gchar *sender_name,
                              const gchar *object_path,
                              const gchar *interface_name,
                              const gchar *signal_name, GVariant *parameters,
                              gpointer user_data) {
  ScreencastWebRTCState *state = user_data;
  guint32 response_code;
  GVariant *res;
  g_variant_get(parameters, "(u@a{sv})", &response_code, &res);
  if (response_code != 0) {
    g_printerr("Error starting screencast.\n");
    g_main_loop_quit(state->loop);
    g_variant_unref(res);
    return;
  }
  GVariant *streams =
      g_variant_lookup_value(res, "streams", G_VARIANT_TYPE("a(ua{sv})"));
  if (!streams) {
    g_main_loop_quit(state->loop);
    return;
  }
  GVariantIter iter;
  g_variant_iter_init(&iter, streams);
  guint32 stream_id;
  if (g_variant_iter_next(&iter, "(u@a{sv})", &stream_id, NULL)) {
    start_stream(stream_id, state);
  }
  g_variant_unref(streams);
}

static void on_select_response(GDBusConnection *conn, const gchar *sender_name,
                               const gchar *object_path,
                               const gchar *interface_name,
                               const gchar *signal_name, GVariant *parameters,
                               gpointer user_data) {
  ScreencastWebRTCState *state = user_data;
  guint32 response_code;
  g_variant_get(parameters, "(u@a{sv})", &response_code, NULL);
  if (response_code != 0) {
    g_main_loop_quit(state->loop);
    return;
  }
  start_screencast(state);
}

static void select_sources(ScreencastWebRTCState *state,
                           const gchar *token_session) {
  gchar *token_select = generate_token("tk_slct");
  const gchar *unique_name =
      g_dbus_connection_get_unique_name(state->connection);
  state->sanitized_name = sanitize_sender_name(unique_name);
  state->session_path = g_strdup_printf("%s/session/%s/%s", PORTAL_OBJECT_PATH,
                                        state->sanitized_name, token_session);
  GVariantBuilder select_sources_opts;
  g_variant_builder_init(&select_sources_opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&select_sources_opts, "{sv}", "handle_token",
                        g_variant_new_string(token_select));

  g_dbus_connection_call(
      state->connection, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH,
      SCREENCAST_INTERFACE, "SelectSources",
      g_variant_new("(oa{sv})", state->session_path, &select_sources_opts),
      NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);

  g_print("Waiting for screen selection\n");
  gchar *request_path = g_strdup_printf("%s/request/%s/%s", PORTAL_OBJECT_PATH,
                                        state->sanitized_name, token_select);
  g_dbus_connection_signal_subscribe(
      state->connection, PORTAL_BUS_NAME, REQUEST_INTERFACE, "Response",
      request_path, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_select_response, state,
      NULL);
  g_free(token_select);
  g_free(request_path);
}

static gchar *create_session(ScreencastWebRTCState *state) {
  GError *error = NULL;
  gchar *token_create = generate_token("tk_create");
  gchar *token_session = generate_token("tk_sess");
  GVariantBuilder create_session_opts;
  g_variant_builder_init(&create_session_opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&create_session_opts, "{sv}", "handle_token",
                        g_variant_new_string(token_create));
  g_variant_builder_add(&create_session_opts, "{sv}", "session_handle_token",
                        g_variant_new_string(token_session));

  GVariant *res = g_dbus_connection_call_sync(
      state->connection, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH,
      SCREENCAST_INTERFACE, "CreateSession",
      g_variant_new("(a{sv})", &create_session_opts), NULL,
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  g_free(token_create);
  if (error) {
    g_error_free(error);
    g_free(token_session);
    return NULL;
  }
  g_variant_unref(res);
  return token_session;
}

static gboolean on_stdin_input(GIOChannel *channel, GIOCondition condition,
                               gpointer user_data) {
  ScreencastWebRTCState *state = user_data;
  gchar *input = NULL;
  GIOStatus status;

  if (condition & (G_IO_HUP | G_IO_ERR)) {
    g_main_loop_quit(state->loop);
    return FALSE;
  }

  status = g_io_channel_read_line(channel, &input, NULL, NULL, NULL);

  if (status == G_IO_STATUS_NORMAL) {
    gchar *trimmed = g_strchomp(input);

    // Check for exit command
    if (strcmp(trimmed, "exit") == 0) {
      g_print("\nExit command received. Stopping stream.\n");
      g_main_loop_quit(state->loop);
      g_free(input);
      return FALSE;
    }

    // Check for JSON answer (Simple detection: starts with '{')
    if (g_str_has_prefix(trimmed, "{")) {
      process_sdp_answer(state, trimmed);
    } else {
      g_print("Unknown input. Paste JSON answer or type 'exit'.\n");
    }

  } else {
    g_main_loop_quit(state->loop);
    return FALSE;
  }

  g_free(input);
  return TRUE;
}

static void screencast_state_free(ScreencastWebRTCState *state) {
  if (state == NULL)
    return;
  g_dbus_connection_call(state->connection, PORTAL_BUS_NAME,
                         state->session_path, "org.freedesktop.portal.Session",
                         "Close", NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                         NULL, NULL);
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

void screencast_webrtc_tutorial(int argc, char *argv[]) {
  ScreencastWebRTCState *state = g_new0(ScreencastWebRTCState, 1);
  GError *error = NULL;

  g_print("Starting screencast WebRTC tutorial.\n");
  state->connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

  if (error) {
    g_printerr("Connection error: %s\n", error->message);
    g_error_free(error);
    screencast_state_free(state);
    return;
  }

  state->loop = g_main_loop_new(NULL, FALSE);

  GIOChannel *stdin_channel = g_io_channel_unix_new(fileno(stdin));
  g_io_add_watch(stdin_channel, G_IO_IN | G_IO_HUP | G_IO_ERR, on_stdin_input,
                 state);
  g_io_channel_unref(stdin_channel);

  gchar *token_session = create_session(state);
  if (token_session == NULL) {
    screencast_state_free(state);
    return;
  }

  select_sources(state, token_session);
  g_free(token_session);

  g_print(
      "\nType 'exit' to stop.\n"
      "Paste the Remote SDP Answer JSON (single line) to start streaming.\n");
  g_main_loop_run(state->loop);

  screencast_state_free(state);
}
