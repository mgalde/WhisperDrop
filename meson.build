project('whisperdrop', 'c',
  version : '0.5.0',
  default_options : [
    'warning_level=2',
    'c_std=gnu11',
  ])

gtk4_dep = dependency('gtk4')
soup_dep = dependency('libsoup-3.0')
json_dep = dependency('json-glib-1.0')

gnome = import('gnome')

resources = gnome.compile_resources(
  'whisperdrop-resources',
  'data/whisperdrop.gresource.xml',
  source_dir : '.',
  c_name : 'whisperdrop_resources')

subdir('installer')

executable('WhisperDrop',
  sources : [
    'src/main.c',
    'src/app.c',
    'src/worker.c',
    'src/updater.c',
    resources,
  ],
  dependencies : [gtk4_dep, soup_dep, json_dep],
  # GtkComboBoxText is deprecated in GTK 4.10 but still functional
  c_args : ['-Wno-deprecated-declarations'],
  install : true)
