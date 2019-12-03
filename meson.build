project('ssql',
  'c',
  default_options: ['c_std=c11', 'warning_level=3'])

glib  = dependency('glib-2.0')
gio   = dependency('gio-2.0')

gnome  = import('gnome')

test_enums = gnome.mkenums_simple(
  'test-enum-types',
  sources: ['test-enums.h'])

executable('ssql',
  sources: [ test_enums, 'main.c' ],
  dependencies: [
    glib, gio
  ])
