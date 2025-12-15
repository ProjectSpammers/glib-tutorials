#include "timeout_example.h"
#include "glib.h"
#include <stdio.h>

static gboolean trying_timeout(gpointer data) {
  printf("Working\n");
  return TRUE;
}

void timeout_tutorial() { g_timeout_add(1000, trying_timeout, NULL); }
