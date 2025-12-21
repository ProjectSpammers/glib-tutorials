#include "screencast.h"
#include "../common/utils.h"
#include "gio/gio.h"
#include "glib.h"
#include <gst/gst.h>
#include <stdio.h>

#define PORTAL_BUS_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define SCREENCAST_INTERFACE "org.freedesktop.portal.ScreenCast"
#define REQUEST_INTERFACE "org.freedesktop.portal.Request"

typedef struct {
  GMainLoop *loop;
  GDBusConnection *connection;
  gchar *sanitized_name;
  gchar *session_path;
} ScreencastState;

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
  default:
    break;
  }
  return TRUE;
}

static gchar* get_default_monitor_source() {
    FILE *fp;
    char path[1024];

    fp = popen("pactl get-default-sink", "r");
    if (fp == NULL) {
        g_printerr("Ses cihazi bilgisi alinamadi (pactl hatasi).\n");
        return NULL;
    }

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

static void start_stream(guint32 id,ScreencastState *state){
  g_print("\n>>> GStreamer Baslatiliyor... Node ID: %d\n", id);
  GstElement *pipeline;
  gst_init(NULL, NULL);
  gchar *audio_device = get_default_monitor_source();
  char *pipeline_str = g_strdup_printf(
      "matroskamux name=mux ! filesink location=capture.mkv "
      
      // --- VIDEO ---
      "pipewiresrc path=%u do-timestamp=true ! "
      "queue max-size-buffers=3 leaky=downstream ! "
      "videoconvert ! "
      "nvh264enc preset=low-latency-hq ! h264parse ! "
      "queue ! mux.video_0 "
      
      // --- AUDIO ---
      // buffer-time=200000: 200ms buffer, takılmaları önler
      "pulsesrc device=%s do-timestamp=true buffer-time=200000 ! " 
      "audioconvert ! "
      "audioresample ! " // ÖNEMLİ: Farklı örnekleme hızlarını (44.1/48k) çevirir
      "opusenc ! "
      "queue ! mux.audio_0",
      id,audio_device);

  GError *error = NULL;
  pipeline = gst_parse_launch(pipeline_str, &error);
  g_free(pipeline_str);

  if (error) {
    g_printerr("Pipeline Hatasi: %s\n", error->message);
    g_error_free(error);
    g_main_loop_quit(state->loop);
    return;
  }

  GstBus *bus = gst_element_get_bus(pipeline);
  gst_bus_add_watch(bus, bus_call, state->loop);
  gst_object_unref(bus);

  gst_element_set_state(pipeline, GST_STATE_PLAYING);
}

static void start_screencast(ScreencastState *state) {
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
  ScreencastState *state = user_data;
  guint32 response_code;
  GVariant *res;
  g_variant_get(parameters, "(u@a{sv})", &response_code, &res);
  if (response_code != 0) {
    g_printerr("Couldn't get the proper response code for starting screencast.\n");
    g_main_loop_quit(state->loop);
    g_variant_unref(res);
    g_main_loop_quit(state->loop);
  }
  GVariant *streams =
    g_variant_lookup_value(res, "streams", G_VARIANT_TYPE("a(ua{sv})"));
  if (streams == NULL)
  {
    g_printerr("No streams found in the response.\n");
    g_variant_unref(res);    
    g_main_loop_quit(state->loop);
  }
  GVariantIter iter;
  g_variant_iter_init(&iter, streams);
  guint32 stream_id;
  if(g_variant_iter_next(&iter, "(u@a{sv})", &stream_id, NULL)) {
    start_stream(stream_id,state);
  } else {
    g_printerr("No valid stream options found.\n");
  }

}

static void on_select_response(GDBusConnection *conn, const gchar *sender_name,
                               const gchar *object_path,
                               const gchar *interface_name,
                               const gchar *signal_name, GVariant *parameters,
                               gpointer user_data) {
  ScreencastState *state = user_data;
  guint32 response_code;
  g_variant_get(parameters, "(u@a{sv})", &response_code, NULL);

  if (response_code != 0) {
    g_printerr("Couldn't get the proper response code for screen selection.\n");
    g_main_loop_quit(state->loop);
    return;
  }
  start_screencast(state);
}

static void select_sources(ScreencastState *state,
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

  gchar *request_path =
      g_strdup_printf("%s/request/%s/%s", PORTAL_OBJECT_PATH,
                      state->sanitized_name, token_select);
  g_dbus_connection_signal_subscribe(
      state->connection, PORTAL_BUS_NAME, REQUEST_INTERFACE, "Response",
      request_path, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_select_response, state,
      NULL);

  g_free(token_select);
  g_free(request_path);
}

static gchar *create_session(ScreencastState *state) {
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
    g_printerr("CreateSession error: %s\n", error->message);
    g_error_free(error);
    g_variant_unref(res);
    g_free(token_session);
    return NULL;
  }

  g_variant_unref(res);
  return token_session;
}

static void screencast_state_free(ScreencastState *state) {
  if (state == NULL) {
    return;
  }
  g_object_unref(state->connection);
  g_main_loop_unref(state->loop);
  g_free(state->sanitized_name);
  g_free(state->session_path);
  g_free(state);
}

void screencast_tutorial() {
  ScreencastState *state = g_new0(ScreencastState, 1);

  g_print("Starting screencast tutorial.\n");

  GError *error = NULL;
  state->connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

  if (error) {
    g_printerr("Connection error: %s\n", error->message);
    g_error_free(error);
    g_free(state);
    return;
  }

  state->loop = g_main_loop_new(NULL, FALSE);

  gchar *token_session = create_session(state);
  if (token_session == NULL) {
    screencast_state_free(state);
    return;
  }

  select_sources(state, token_session);

  g_free(token_session);

  g_main_loop_run(state->loop);

  screencast_state_free(state);
}