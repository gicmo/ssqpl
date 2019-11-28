project('ssql',
  'c',
  default_options: ['c_std=c11', 'warning_level=3'])

glib  = dependency('glib-2.0')
gio   = dependency('gio-2.0')

executable('ssql',
  sources: [ 'main.c' ],
  dependencies: [
    glib, gio
  ])
