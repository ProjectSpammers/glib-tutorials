#include "screencast.h"
#include "../common/utils.h"
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
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
  gchar *session_token; // Token'ı saklıyoruz
  gchar *output_path;
} ScreencastState;

static void select_sources(ScreencastState *state);


static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
  GMainLoop *loop = (GMainLoop *)data;
  switch (GST_MESSAGE_TYPE(msg)) {
  case GST_MESSAGE_ERROR: {
    gchar *debug;
    GError *error;
    gst_message_parse_error(msg, &error, &debug);
    g_printerr("\nERROR: %s\n", error->message);
    if (debug) g_printerr("Debug Info: %s\n", debug);
    g_error_free(error);
    g_free(debug);
    g_main_loop_quit(loop);
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
  if (fp == NULL) return NULL;
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


static void start_stream(guint32 id, ScreencastState *state) {
  g_print("\n>>> Starting Recording Pipeline... Node ID: %d\n", id);
  GstElement *pipeline;
  gst_init(NULL, NULL);
  gchar *audio_device = get_default_monitor_source();
  
  char *pipeline_str = g_strdup_printf(
      "matroskamux name=mux ! filesink location=%s "

      // --- VIDEO ---
      "pipewiresrc path=%u do-timestamp=true ! "
      "queue max-size-buffers=3 leaky=downstream ! "
      "videoconvert ! "
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
      "queue ! mux.video_0 "

      // --- AUDIO ---
      "pulsesrc device=%s do-timestamp=true buffer-time=200000 ! "
      "audioconvert ! "
      "audioresample ! " 
      "opusenc ! "
      "queue ! mux.audio_0",
      state->output_path, id, audio_device ? audio_device : "0");
      
  if (audio_device) g_free(audio_device);

  GError *error = NULL;
  pipeline = gst_parse_launch(pipeline_str, &error);
  g_free(pipeline_str);

  if (error) {
    g_printerr("Pipeline Error: %s\n", error->message);
    g_error_free(error);
    g_main_loop_quit(state->loop);
    return;
  }

  GstBus *bus = gst_element_get_bus(pipeline);
  gst_bus_add_watch(bus, bus_call, state->loop);
  gst_object_unref(bus);

  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  g_print("Recording started! Check: %s\n", state->output_path);
}


static void on_start_response(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params, gpointer user_data) {
  ScreencastState *state = user_data;
  guint32 response_code;
  GVariant *res;
  
  g_variant_get(params, "(u@a{sv})", &response_code, &res);
  if (response_code != 0) {
    g_printerr("Start request denied/cancelled.\n");
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

static void start_screencast(ScreencastState *state) {
  gchar *token = generate_token("tk_start");
  GVariantBuilder opts;
  g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&opts, "{sv}", "handle_token", g_variant_new_string(token));

  g_print("Requesting Start...\n");

  GError *error = NULL;
  GVariant *ret = g_dbus_connection_call_sync(
      state->connection, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH, SCREENCAST_INTERFACE, "Start",
      g_variant_new("(osa{sv})", state->session_path, "", &opts),
      NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

  if (error) {
    g_printerr("Start Call Failed: %s\n", error->message);
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
  ScreencastState *state = user_data;
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

static void select_sources(ScreencastState *state) {
  gchar *token = generate_token("tk_slct");
  GVariantBuilder opts;
  g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&opts, "{sv}", "handle_token", g_variant_new_string(token));
  g_variant_builder_add(&opts, "{sv}", "types", g_variant_new_uint32(1 | 2)); // Monitor | Window
  g_variant_builder_add(&opts, "{sv}", "cursor_mode", g_variant_new_uint32(2)); // Embedded

  g_print("Requesting Source Selection...\n");

  GError *error = NULL;
  GVariant *ret = g_dbus_connection_call_sync(
      state->connection, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH, SCREENCAST_INTERFACE, "SelectSources",
      g_variant_new("(oa{sv})", state->session_path, &opts),
      NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

  if (error) {
    g_printerr("SelectSources Call Failed: %s\n", error->message);
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
  ScreencastState *state = user_data;
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

  g_print("Session created: %s. Now selecting sources...\n", state->session_path);
  select_sources(state); // ZİNCİRLEME GEÇİŞ
}

static void create_session(ScreencastState *state) {
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
    g_printerr("CreateSession Call Failed: %s\n", error->message);
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

static gboolean on_stdin_input(GIOChannel *channel, GIOCondition condition, gpointer user_data) {
  ScreencastState *state = user_data;
  gchar *input = NULL;
  if (g_io_channel_read_line(channel, &input, NULL, NULL, NULL) == G_IO_STATUS_NORMAL) {
    if (g_strcmp0(g_strchomp(input), "exit") == 0) {
      g_main_loop_quit(state->loop);
    }
    g_free(input);
  }
  return TRUE;
}

static gchar *output_file = NULL;
static GOptionEntry entries[] = {
    {"output", 'o', 0, G_OPTION_ARG_STRING, &output_file, "Output file path", "FILE"},
    {NULL}};

void screencast_tutorial(int argc, char *argv[]) {
  ScreencastState *state = g_new0(ScreencastState, 1);
  GError *error = NULL;

  GOptionContext *context = g_option_context_new("- screencast utility");
  g_option_context_add_main_entries(context, entries, NULL);
  g_option_context_parse(context, &argc, &argv, NULL);
  g_option_context_free(context);

  state->output_path = output_file ? g_strdup(output_file) : g_build_filename(g_get_current_dir(), "capture.mkv", NULL);
  if (output_file) g_free(output_file);

  state->connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (error) { g_printerr("DBus Error: %s\n", error->message); return; }

  state->loop = g_main_loop_new(NULL, FALSE);

  // Input İzleyici
  GIOChannel *stdin_ch = g_io_channel_unix_new(0);
  g_io_add_watch(stdin_ch, G_IO_IN, on_stdin_input, state);
  g_io_channel_unref(stdin_ch);

  // BAŞLAT
  create_session(state);

  g_print("Running... Type 'exit' to stop.\n");
  g_main_loop_run(state->loop);

  // Temizlik
  if (state->session_path) {
      g_dbus_connection_call(state->connection, PORTAL_BUS_NAME, state->session_path,
                             "org.freedesktop.portal.Session", "Close", NULL, NULL,
                             G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
  }
  g_object_unref(state->connection);
  g_main_loop_unref(state->loop);
  g_free(state->sanitized_name);
  g_free(state->session_path);
  g_free(state->output_path);
  g_free(state);
}