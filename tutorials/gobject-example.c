#include "gobject-example.h"
#include "glib-object.h"
#include "glib.h"

struct _ExamplePerson {
  GObject parent_instance;
  gchar *name;
  gint age;
};

G_DEFINE_TYPE(ExamplePerson, example_person, G_TYPE_OBJECT)

static void example_person_class_init(ExamplePersonClass *klass) {}

static void example_person_init(ExamplePerson *self) {
  self->name = g_strdup("Initial name");
  self->age = 30;
}

ExamplePerson *example_person_new(void) {
  return g_object_new(EXAMPLE_PERSON_TYPE, 0);
}

// Getters
const gchar *example_person_get_name(ExamplePerson *self) { return self->name; }
gint example_person_get_age(ExamplePerson *self) { return self->age; }

// Setters
void example_person_set_name(ExamplePerson *self, const gchar *name) {
  if (g_strcmp0(name, self->name) == 0) {
    g_free(self->name);
    self->name = g_strdup(name);
  }
}
void example_person_set_age(ExamplePerson *self, gint age) { self->age = age; }

void gobject_tutorial_get_set(void) {
  ExamplePerson *ahmet = example_person_new();

  g_print("Running setters\n\n--------------");
  example_person_set_age(ahmet, 25);
  example_person_set_name(ahmet, "Ahmet");

  g_print("Running getters\n\n--------------");
  g_print("The name of the ExamplePerson variable is: %s\n",
          example_person_get_name(ahmet));
  g_print("The age of the ExamplePerson variable is: %d\n",
          example_person_get_age(ahmet));
}
