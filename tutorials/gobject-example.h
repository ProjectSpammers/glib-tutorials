#ifndef GOBJECT_EXAMPLE_H
#define GOBJECT_EXAMPLE_H

#include <glib-object.h>
#define EXAMPLE_PERSON_TYPE (example_person_get_type())

G_DECLARE_FINAL_TYPE(ExamplePerson, example_person, EXAMPLE, PERSON, GObject)

ExamplePerson *example_person_new(void);

#endif
