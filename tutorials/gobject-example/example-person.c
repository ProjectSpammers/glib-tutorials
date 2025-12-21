#include "example-person.h"
#include "glib-object.h"
#include "glib.h"

typedef struct {
  gfloat salary;
} ExamplePersonPrivate; // These are private values
                        // Setting them in the C file in such way makes them
                        // private

/* G_DEFINE_TYPE(ExamplePerson, example_person, G_TYPE_OBJECT) */ // IF there
                                                                  // are no
                                                                  // private
                                                                  // values we
                                                                  // can define
                                                                  // the type
                                                                  // with this;
G_DEFINE_TYPE_WITH_PRIVATE(ExamplePerson, example_person, G_TYPE_OBJECT)

enum { PROP_0, PROP_NAME, LAST_PROP };
enum { YO, LAST_SIGNAL };

static GParamSpec *properties[LAST_PROP];
static guint signals[LAST_SIGNAL];

static void example_person_get_property(GObject *object, guint prop_id,
                                        GValue *value, GParamSpec *pspec) {
  ExamplePerson *self = (ExamplePerson *)object;
  switch (prop_id) {
  case PROP_NAME:
    g_value_set_string(value, example_person_get_name(self));
    break;
  }
}

static void example_person_set_property(GObject *object, guint prop_id,
                                        const GValue *value,
                                        GParamSpec *pspec) {
  ExamplePerson *self = (ExamplePerson *)object;
  switch (prop_id) {
  case PROP_NAME:
    example_person_set_name(self, g_value_get_string(value));
    break;
  }
}

static void example_person_class_init(ExamplePersonClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->get_property = example_person_get_property;
  object_class->set_property = example_person_set_property;

  properties[PROP_NAME] =
      g_param_spec_string("name", "Name", "The name of the person", NULL,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties(object_class, LAST_PROP, properties);

  signals[YO] = g_signal_new("yo", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                             0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

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
  if (g_strcmp0(name, self->name) != 0) {
    g_free(self->name);
    self->name = g_strdup(name);
  }
}

void example_person_set_age(ExamplePerson *self, gint age) { self->age = age; }

void gobject_tutorial_get_set(int argc, char *argv[]) {
  G_GNUC_UNUSED int _argc = argc;
  G_GNUC_UNUSED char **_argv = argv;
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
