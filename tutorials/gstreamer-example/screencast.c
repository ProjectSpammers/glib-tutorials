#include "screencast.h"
#include "../common/utils.h"
#include "gio/gio.h"
#include "glib.h"

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
  g_main_loop_quit(state->loop);
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