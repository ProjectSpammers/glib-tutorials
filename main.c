#include "tutorials/gio-example/notification-sender.h"
#include "tutorials/gobject-example/example-person.h"
#include "tutorials/gstreamer-example/screencast-webrtc.h"
#include "tutorials/gstreamer-example/screencast.h"
#include "tutorials/timeout-example/timeout.h"
#include "tutorials/gstreamer-example/sound_exclusion.h"
#include <gio/gio.h>
#include <glib.h>

typedef void (*TutorialFunc)(gint, gchar **);

typedef struct {
  const gchar *name; // command name
  TutorialFunc func;
} Tutorial;

void screencast_webrtc_with_sound_exclusion(gint argc, gchar *argv[]){
    get_excluded_sound();
    screencast_webrtc_tutorial(2,argv);
    restore_system();
}

Tutorial tutorials[] = {
    {"timeout", timeout_tutorial},
    {"gobject-get-set", gobject_tutorial_get_set},
    {"dbus-notification", send_notification},
    {"screencast", screencast_tutorial},
    {"screencast-webrtc", screencast_webrtc_tutorial},
    {"screencast-webrtc-with-sound-exclusion", screencast_webrtc_with_sound_exclusion},
    {NULL, NULL} // end of the array
};

void print_help(const gchar *prog_name) {
  g_print("Usage: %s <command>\n", prog_name);
  g_print("Available commands:\n");

  for (gint i = 0; tutorials[i].name != NULL; i++) {
    g_print("  - %s\n", tutorials[i].name);
  }
}

gint main(gint argc, gchar *argv[]) {
  if (argc < 2) {
    print_help(argv[0]);
    return 1;
  }

  if (g_strcmp0(argv[1], "--list-commands") == 0) {
    for (gint i = 0; tutorials[i].name != NULL; i++) {
      g_print("%s\n", tutorials[i].name);
    }
    return 0;
  }

  gboolean found = FALSE;

  for (gint i = 0; tutorials[i].name != NULL; i++) {
    if (g_strcmp0(argv[1], tutorials[i].name) == 0) {
      g_print("Running tutorial: %s\n", tutorials[i].name);

      tutorials[i].func(argc - 1, argv + 1);

      found = TRUE;
      break;
    }
  }

  if (!found) {
    g_print("Error: Command '%s' not found.\n", argv[1]);
    print_help(argv[0]);
    return 1;
  }

  return 0;
}
