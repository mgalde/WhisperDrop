gtk4_dep = dependency('gtk4')

python = find_program('python3')

embed_script = files('embed_binary.py')

# Embed the compiled WhisperDrop binary as a C byte array.
# whisperdrop_exe is defined in the parent meson.build before subdir().
embedded_bin_c = custom_target('embedded-whisperdrop-bin',
  input  : whisperdrop_exe,
  output : 'whisperdrop_embedded_bin.c',
  command: [python, embed_script, '@INPUT@', 'whisperdrop_binary', '@OUTPUT@'])

# Embed the application icon so the installer shows the real logo.
embedded_ico_c = custom_target('embedded-whisperdrop-ico',
  input  : files('../WhisperDrop.png'),
  output : 'whisperdrop_embedded_ico.c',
  command: [python, embed_script, '@INPUT@', 'whisperdrop_icon', '@OUTPUT@'])

installer_c_args = [
  '-Wno-deprecated-declarations',   # GtkMessageDialog deprecated in GTK 4.10
]

executable('whisperdrop-installer',
  sources : [
    'whisperdrop-installer.c',
    embedded_bin_c,
    embedded_ico_c,
  ],
  dependencies : [gtk4_dep],
  c_args : installer_c_args,
  install : false)

executable('whisperdrop-uninstaller',
  sources : ['whisperdrop-uninstaller.c'],
  dependencies : [gtk4_dep],
  c_args : installer_c_args,
  install : false)
