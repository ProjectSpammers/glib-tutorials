#include "tutorials/timeout_example.h"
#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>

// function typedef
typedef void (*TutorialFunc)();

GMainLoop *loop;

typedef struct {
  const char *name; // command name
  TutorialFunc func;
} Tutorial;

Tutorial tutorials[] = {
    {"timeout", timeout_tutorial},
    // { "example", example_tutorial }, // Future tutorials go here
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

  loop = g_main_loop_new(NULL, FALSE);
  gboolean found = FALSE;

  for (int i = 0; tutorials[i].name != NULL; i++) {
    if (strcmp(argv[1], tutorials[i].name) == 0) {
      printf("Running tutorial: %s\n", tutorials[i].name);

      tutorials[i].func();

      found = TRUE;
      break;
    }
  }

  if (!found) {
    printf("Error: Command '%s' not found.\n", argv[1]);
    print_help(argv[0]);
    return 1;
  }

  g_main_loop_run(loop);
  g_main_loop_unref(loop);

  return 0;
}
