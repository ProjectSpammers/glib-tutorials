#include "sound_exclusion.h"
#include <glib.h>

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif

#define VIRTUAL_SINK_NAME "GStreamer_Broadcast"
#define VIRTUAL_SINK_DESC "Auto_Channel_For_Broadcast"

gchar *original_sink = NULL;
gint32 null_module_id = -1;
gint32 loop_module_id = -1;

void run_command_output(const gchar *cmd, gchar **result) {
  GError *error = NULL;
  gchar *stdout_output = NULL;
  gint exit_status = 0;

  gboolean success = g_spawn_command_line_sync(cmd, &stdout_output,
                                               NULL,
                                               &exit_status, &error);

  if (!success) {
    g_printerr("Error: Failed to execute command -> %s\nMessage: %s\n", cmd,
               error->message);
    g_error_free(error);
    *result = NULL;
    return;
  }

  if (stdout_output) {
    g_strstrip(stdout_output);
  }

  *result = stdout_output;
}

void run_command(const gchar *cmd) {
  GError *error = NULL;
  if (!g_spawn_command_line_sync(cmd, NULL, NULL, NULL, &error)) {
    g_printerr("Error executing command: %s\n", error->message);
    g_error_free(error);
  }
}

gint32 load_module(const gchar *cmd) {
  gchar *output = NULL;
  run_command_output(cmd, &output);

  gint32 id = -1;
  if (output && *output != '\0') {
    id = (gint32)g_ascii_strtoll(output, NULL, 10);
  }

  g_free(output);
  return id;
}

void restore_system() {
  g_print("\n[*] Restoring system configuration...\n");

  if (original_sink != NULL && g_utf8_strlen(original_sink, -1) > 0) {
    gchar *cmd = g_strdup_printf("pactl set-default-sink %s", original_sink);
    run_command(cmd);
    g_print(" -> Default sink restored: %s\n", original_sink);
    g_free(cmd);
  }

  if (null_module_id != -1) {
    gchar *cmd = g_strdup_printf("pactl unload-module %d", null_module_id);
    run_command(cmd);
    g_print(" -> Virtual sink removed (ID: %d)\n", null_module_id);
    g_free(cmd);
    null_module_id = -1;
  }

  if (loop_module_id != -1) {
    gchar *cmd = g_strdup_printf("pactl unload-module %d", loop_module_id);
    run_command(cmd);
    g_print(" -> Loopback removed (ID: %d)\n", loop_module_id);
    g_free(cmd);
    loop_module_id = -1;
  }

  g_print("[OK] Exited successfully.\n");

  if (original_sink) {
    g_free(original_sink);
    original_sink = NULL;
  }
}

void get_excluded_sound() {
  restore_system();

  run_command_output("pactl get-default-sink", &original_sink);

  if (original_sink == NULL) {
    g_printerr("Error: Could not determine default sink.\n");
    return;
  }

  g_print("[*] Current Physical Sink: %s\n", original_sink);

  if (g_strcmp0(original_sink, VIRTUAL_SINK_NAME) == 0) {
    g_printerr("ERROR: Virtual sink is already the default. Please reset the "
               "system first.\n");
    return;
  }

  g_print("[*] Creating virtual sink...\n");
  gchar *cmd_null =
      g_strdup_printf("pactl load-module module-null-sink sink_name=%s "
                      "sink_properties=device.description=%s",
                      VIRTUAL_SINK_NAME, VIRTUAL_SINK_DESC);
  null_module_id = load_module(cmd_null);
  g_free(cmd_null);

  g_print("[*] Enabling loopback...\n");
  gchar *cmd_loop = g_strdup_printf(
      "pactl load-module module-loopback source=%s.monitor sink=%s",
      VIRTUAL_SINK_NAME, original_sink);
  loop_module_id = load_module(cmd_loop);
  g_free(cmd_loop);

  g_print("[*] Setting default sink to VIRTUAL SINK...\n");
  gchar *cmd_def =
      g_strdup_printf("pactl set-default-sink %s", VIRTUAL_SINK_NAME);
  run_command(cmd_def);
  g_free(cmd_def);

  g_print("-----------------------\n");

  g_print("\nAudio Sources:\n");
  run_command(
      "pactl list sink-inputs | grep -E 'Sink Input #|application.name'");

  g_print("\n[Waiting for Command]\n");
  g_print("1. 'exclude {id} {id}...' -> Move given IDs to physical card (Hide "
          "from broadcast)\n");
  g_print("2. 'exit'                 -> Restore system and exit\n");
  g_print("> ");

  GIOChannel *channel = g_io_channel_unix_new(STDIN_FILENO);
  gchar *input_line = NULL;
  gsize len = 0;
  GError *err = NULL;

  if (g_io_channel_read_line(channel, &input_line, &len, NULL, &err) ==
      G_IO_STATUS_NORMAL) {
    if (input_line) {
      g_strstrip(input_line);

      if (g_strcmp0(input_line, "exit") == 0) {
        restore_system();
      } else if (g_str_has_prefix(input_line, "exclude ")) {
        gchar **tokens = g_strsplit(input_line + 8, " ", -1);
        gchar **current = tokens;

        while (*current != NULL) {
          guint32 app_id = (guint32)g_ascii_strtoull(*current, NULL, 10);
          if (app_id > 0) {
            g_print(" -> Moving App %u to Physical Card...\n", app_id);
            gchar *move_cmd = g_strdup_printf("pactl move-sink-input %u %s",
                                              app_id, original_sink);
            run_command(move_cmd);
            g_free(move_cmd);
          }
          current++;
        }
        g_strfreev(tokens);
      } else {
        g_print("Unknown command. Type 'exclude ID' or 'exit'.\n");
      }
      g_free(input_line);
    }
  } else {
    if (err) {
      g_printerr("Error reading input: %s\n", err->message);
      g_error_free(err);
    }
  }

  g_io_channel_unref(channel);
}
