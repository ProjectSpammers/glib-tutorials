#include "utils.h"

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
