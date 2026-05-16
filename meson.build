project('whisperdrop', 'c',
  version : '0.7.2',
  default_options : [
    'warning_level=2',
    'c_std=gnu11',
  ])

gtk4_dep = dependency('gtk4')
soup_dep = dependency('libsoup-3.0')
json_dep = dependency('json-glib-1.0')

gnome = import('gnome')

app_conf = configuration_data()
app_conf.set('APP_VERSION', meson.project_version())

configure_file(
  input : 'src/build-config.h.in',
  output : 'build-config.h',
  configuration : app_conf)

resources = gnome.compile_resources(
  'whisperdrop-resources',
  'data/whisperdrop.gresource.xml',
  source_dir : '.',
  c_name : 'whisperdrop_resources')

# Build the main app first so the installer subdir can embed it
whisperdrop_exe = executable('WhisperDrop',
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

# Installer and uninstaller — must come after whisperdrop_exe is defined
# macOS: no graphical installer; ship the bare binary or .app bundle instead
if host_machine.system() != 'darwin'
  subdir('installer')
endif
