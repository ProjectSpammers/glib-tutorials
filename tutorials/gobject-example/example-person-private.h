#ifndef EXAMPLE_PERSON_PRIVATE_H
#define EXAMPLE_PERSON_PRIVATE_H

#include "example-person.h"

struct _ExamplePerson {
  GObject parent_instance;
  gchar *name;
  gint age;
};

void example_person_emit_yo(ExamplePerson *self);

#endif // !EXAMPLE_PERSON_PRIVATE_H
