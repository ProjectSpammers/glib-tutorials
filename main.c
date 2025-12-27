#include "tutorials/gio-example/notification-sender.h"
#include "tutorials/gobject-example/example-person.h"
#include "tutorials/gstreamer-example/screencast-webrtc.h"
#include "tutorials/gstreamer-example/screencast.h"
#include "tutorials/timeout-example/timeout.h"
#include "tutorials/sound-exclusion/sound_exclusion.h"
#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

typedef void (*TutorialFunc)(int, char **);

typedef struct {
  const char *name; // command name
  TutorialFunc func;
} Tutorial;

void screencast_webrtc_with_sound_exclusion(int argc, char *argv[]){
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

void print_help(const char *prog_name) {
  printf("Usage: %s <command>\n", prog_name);
  printf("Available commands:\n");

  for (int i = 0; tutorials[i].name != NULL; i++) {
    printf("  - %s\n", tutorials[i].name);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    print_help(argv[0]);
    return 1;
  }

  gboolean found = FALSE;

  for (int i = 0; tutorials[i].name != NULL; i++) {
    if (strcmp(argv[1], tutorials[i].name) == 0) {
      printf("Running tutorial: %s\n", tutorials[i].name);

      tutorials[i].func(argc - 1, argv + 1);

      found = TRUE;
      break;
    }
  }

  if (!found) {
    printf("Error: Command '%s' not found.\n", argv[1]);
    print_help(argv[0]);
    return 1;
  }

  return 0;
}
