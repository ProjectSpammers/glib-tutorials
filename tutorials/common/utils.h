#ifndef UTILS_H
#define UTILS_H

#include <glib.h>

gchar *sanitize_sender_name(const gchar *sender_name);
gchar *generate_token(const gchar *prefix);

#endif // UTILS_H
