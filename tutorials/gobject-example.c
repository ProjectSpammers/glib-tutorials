#include "gobject-example.h"
#include "glib.h"

struct _ExamplePerson {
  GObject parent_instance;
  gchar *name;
  gint age;
};

G_DEFINE_TYPE(ExamplePerson, example_person, G_TYPE_OBJECT)

static void example_person_class_init(ExamplePersonClass *klass) {}

static void example_person_init(ExamplePerson *self) {
  self->name = g_strdup("Initial name"); // use g_strdup for
  self->age = 30;
}
