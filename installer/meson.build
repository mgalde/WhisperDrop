gtk4_dep = dependency('gtk4')

installer_c_args = [
  '-Wno-deprecated-declarations',   # GtkMessageDialog, GtkDialog deprecated in GTK 4.10
]

executable('whisperdrop-installer',
  sources : ['whisperdrop-installer.c'],
  dependencies : [gtk4_dep],
  c_args : installer_c_args,
  install : false)

executable('whisperdrop-uninstaller',
  sources : ['whisperdrop-uninstaller.c'],
  dependencies : [gtk4_dep],
  c_args : installer_c_args,
  install : false)
