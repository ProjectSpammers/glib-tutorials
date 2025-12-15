#ifndef GOBJECT_EXAMPLE_H
#define GOBJECT_EXAMPLE_H

#include <glib-object.h>
#define EXAMPLE_PERSON_TYPE (example_person_get_type())
G_DECLARE_FINAL_TYPE(ExamplePerson, example_person, EXAMPLE, PERSON, GObject)

ExamplePerson *example_person_new(void);

// Getters
const gchar *example_person_get_name(ExamplePerson *self);
gint example_person_get_age(ExamplePerson *self);

// Setters
void example_person_set_name(ExamplePerson *self, const gchar *name);
void example_person_set_age(ExamplePerson *self, gint age);

// Tutorial function
void gobject_tutorial_get_set(void);

#endif
