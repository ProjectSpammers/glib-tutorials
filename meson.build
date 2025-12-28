project(
  'glib-tutorials',
  'c',
  version: '0.1',
  default_options: ['warning_level=3', 'c_std=c23'],
)

glib_dep = dependency('glib-2.0')
gobject_dep = dependency('gobject-2.0')
gio_dep = dependency('gio-2.0')
gst_dep = dependency('gstreamer-1.0')
gst_video_dep = dependency('gstreamer-video-1.0')
gst_webrtc_dep = dependency('gstreamer-webrtc-1.0')
json_glib_dep = dependency('json-glib-1.0')

gio_unix_dep = dependency('gio-unix-2.0', required: false)

src_files = [
  'main.c',
  'tutorials/common/utils.c',
  'tutorials/timeout-example/timeout.c',
  'tutorials/gobject-example/example-person.c',
  'tutorials/gio-example/notification-sender.c',
  'tutorials/gstreamer-example/screencast.c',
  'tutorials/gstreamer-example/screencast-webrtc.c',
  'tutorials/gstreamer-example/sound_exclusion.c',
]

executable(
  'glib-tutorials',
  src_files,
  dependencies: [
    glib_dep,
    gobject_dep,
    gio_dep,
    gio_unix_dep,
    gst_dep,
    gst_video_dep,
    gst_webrtc_dep,
    json_glib_dep,
  ],
  install: true,
)

bash_completion = dependency('bash-completion', required: false)
if bash_completion.found()
  bash_comp_dir = bash_completion.get_variable('completionsdir')
else
  bash_comp_dir = get_option('datadir') / 'bash-completion/completions'
endif

install_data(
  'completions/glib-tutorials.bash',
  rename: 'glib-tutorials',
  install_dir: bash_comp_dir,
)

zsh_comp_dir = get_option('datadir') / 'zsh/site-functions'
install_data('completions/_glib-tutorials', install_dir: zsh_comp_dir)

