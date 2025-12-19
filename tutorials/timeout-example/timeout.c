#include "timeout.h"
#include "glib.h"
#include <stdio.h>

static gboolean trying_timeout(gpointer user_data) {
  GMainLoop *loop = (GMainLoop *)user_data;
  static guint counter = 0;
  const guint MAX_COUNT = 5;

  printf("Working...\n");
  counter++;

  if (counter >= MAX_COUNT) {
    printf("Timeout limit reached. Quitting main loop.\n");
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

void timeout_tutorial() {
  GMainLoop *loop = g_main_loop_new(NULL, FALSE);

  printf("Starting timeout tutorial. Will run for 5 seconds.\n");

  g_timeout_add(1000, trying_timeout, loop);

  g_main_loop_run(loop);
  g_main_loop_unref(loop);

  printf("Timeout tutorial finished.\n");
}
