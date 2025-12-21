#include "notification-sender.h"

void send_notification(int argc, char *argv[]) {
  G_GNUC_UNUSED int _argc = argc;
  G_GNUC_UNUSED char **_argv = argv;
  GError *error = NULL;
  GDBusConnection *connection =
      g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

  if (!connection) {
    g_printerr("Error connecting to the dbus: %s", error->message);
    g_error_free(error);
    return;
  }

  GVariant *params = g_variant_new(
      "(susssasa{sv}i)", "Ankara", 0, "computer", "Notification from C",
      "Hello, this was sent via D-Bus.", NULL, NULL, 5000);

  gchar *str = NULL;
  g_variant_get(params, "(susssasa{sv}i)", &str, NULL, NULL, NULL, NULL, NULL,
                NULL, NULL);
  g_print("Getting a single s: %s\n\n", str);

  free(str);

  GVariant *results = g_dbus_connection_call_sync(
      connection, "org.freedesktop.Notifications",
      "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
      "Notify", params, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

  if (error) {
    g_printerr("Failed to send notifications: %s", error->message);
    g_error_free(error);
    return;
  }

  guint32 id;
  g_variant_get(results, "(u)", &id);
  g_print("Notification sent succesfully with ID: %u\n", id);

  g_object_unref(connection);
  g_variant_unref(results);
}
