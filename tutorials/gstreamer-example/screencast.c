#include "screencast.h"
#include "gio/gio.h"
#include "glib.h"

#define PORTAL_BUS_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define SCREENCAST_INTERFACE "org.freedesktop.portal.ScreenCast"
#define REQUEST_INTERFACE "org.freedesktop.portal.Request"

GMainLoop *loop;
GDBusConnection *connection;
gchar *sanitized_name;
gchar *session_path;

gchar *sanitize_sender_name(const gchar *sender_name) {
  if (sender_name == NULL) {
    return NULL;
  }

  gchar *s;

  if (sender_name[0] == ':') {
    s = g_strdup(sender_name + 1);
  } else {
    s = g_strdup(sender_name);
  }

  g_strdelimit(s, ".", '_');

  return s;
}

gchar *generate_token(const gchar *prefix) {
  if (prefix == NULL) {
    prefix = "";
  }

  guint32 random_num = g_random_int_range(0, 100000);

  return g_strdup_printf("%s%u", prefix, random_num);
}
static void on_start_response(GDBusConnection *conn, const gchar *sender_name,
                              const gchar *object_path,
                              const gchar *interface_name,
                              const gchar *signal_name, GVariant *parameters,
                              gpointer user_data) {
  guint32 response_code;
  GVariant *res;
  g_variant_get(parameters, "(u@a{sv})", &response_code, &res);
}

static void on_select_response(GDBusConnection *conn, const gchar *sender_name,
                               const gchar *object_path,
                               const gchar *interface_name,
                               const gchar *signal_name, GVariant *parameters,
                               gpointer user_data) {
  guint32 response_code;
  g_variant_get(parameters, "(u@a{sv})", &response_code, NULL);

  if (response_code != 0) {
    g_printerr("Couldn't get the proper response code for screen selection.\n");
    g_main_loop_quit(loop);
    return;
  }
  gchar *token_start = generate_token("tk_start");
  gchar *start_request_path = g_strdup_printf(
      "%s/request/%s/%s", PORTAL_OBJECT_PATH, sanitized_name, token_start);

  GVariantBuilder start_opts;
  g_variant_builder_init(&start_opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&start_opts, "{sv}", "handle_token",
                        g_variant_new_string(token_start));

  g_dbus_connection_call(
      conn, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH, SCREENCAST_INTERFACE, "Start",
      g_variant_new("(osa{sv})", session_path, "", &start_opts), NULL,
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);

  g_dbus_connection_signal_subscribe(
      conn, PORTAL_BUS_NAME, REQUEST_INTERFACE, "Response", start_request_path,
      NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_start_response, NULL, NULL);
}

void screencast_tutorial() {

  loop = g_main_loop_new(NULL, FALSE);

  g_print("Starting screencast tutorial.\n");

  GError *error = NULL;
  connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

  if (error) {
    g_printerr("Connection error.\n");
    g_error_free(error);
    return;
  }

  gchar *token_create = generate_token("tk_create");
  gchar *token_session = generate_token("tk_sess");
  gchar *token_select = generate_token("tk_slct");

  GVariantBuilder create_session_opts;
  g_variant_builder_init(&create_session_opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&create_session_opts, "{sv}", "handle_token",
                        g_variant_new_string(token_create));
  g_variant_builder_add(&create_session_opts, "{sv}", "session_handle_token",
                        g_variant_new_string(token_session));

  GVariant *res = g_dbus_connection_call_sync(
      connection, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH, SCREENCAST_INTERFACE,
      "CreateSession", g_variant_new("(a{sv})", &create_session_opts), NULL,
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

  if (error) {
    g_printerr("CreateSession error.\n");
    g_error_free(error);
    g_variant_unref(res);
    return;
  }

  g_variant_unref(res);

  const gchar *unique_name = g_dbus_connection_get_unique_name(connection);
  sanitized_name = sanitize_sender_name(unique_name);
  session_path = g_strdup_printf("%s/session/%s/%s", PORTAL_OBJECT_PATH,
                                 sanitized_name, token_session);
  GVariantBuilder select_sources_opts;
  g_variant_builder_init(&select_sources_opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&select_sources_opts, "{sv}", "handle_token",
                        g_variant_new_string(token_select));

  g_dbus_connection_call(
      connection, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH, SCREENCAST_INTERFACE,
      "SelectSources",
      g_variant_new("(oa{sv})", session_path, &select_sources_opts), NULL,
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);

  g_print("Waiting for screen selection\n");

  gchar *request_path = g_strdup_printf("%s/request/%s/%s", PORTAL_OBJECT_PATH,
                                        sanitized_name, token_select);
  g_dbus_connection_signal_subscribe(
      connection, PORTAL_BUS_NAME, REQUEST_INTERFACE, "Response", request_path,
      NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_select_response, NULL, NULL);

  g_main_loop_run(loop);
}
