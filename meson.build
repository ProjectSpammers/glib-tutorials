project(
  'glib-tutorials',
  'c',
  version: '0.1',
  default_options: ['warning_level=3', 'c_std=c23'],
)

glib_dep = dependency('glib-2.0')
gobject_dep = dependency('gobject-2.0')
gio_dep = dependency('gio-2.0')

gio_unix_dep = dependency('gio-unix-2.0', required: false)

src_files = [
  'main.c',
  'tutorials/timeout-example/timeout.c',
  'tutorials/gobject-example/example-person.c',
  'tutorials/gio-example/notification-sender.c',
  'tutorials/gstreamer-example/screencast.c',
]

executable(
  'glib_app',
  src_files,
  dependencies: [glib_dep, gobject_dep, gio_dep, gio_unix_dep],
  install: true,
)
