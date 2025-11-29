project(
  'mwc',
  'c',
  version: '0.1.0',
  license: 'MIT',
)

add_project_arguments('-DWLR_USE_UNSTABLE', language: 'c')

wayland = dependency('wayland-server')
wayland_scanner = find_program('wayland-scanner')

wayland_protocols = dependency('wayland-protocols')
protocol_dir = wayland_protocols.get_variable('pkgdatadir')

protocol_files = [
  protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
  protocol_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml',
  protocol_dir / 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml',
  protocol_dir / 'staging/cursor-shape/cursor-shape-v1.xml',
  'protocols/wlr-layer-shell-unstable-v1.xml',
]

generated = []
foreach f : protocol_files
  filename = f.split('/')[-1]
  dest = filename.replace('.xml', '-protocol.h')
  generated += custom_target(
    output: dest,
    input: f,
    command: [
      wayland_scanner, 'server-header',
      '@INPUT@', '@OUTPUT@'
    ],
    build_by_default: true
  )
endforeach

src = [
  'src/config.c',
  'src/decoration.c',
  'src/dnd.c',
  'src/gamma_control.c',
  'src/helpers.c',
  'src/ipc.c',
  'src/keybinds.c',
  'src/keyboard.c',
  'src/layer_surface.c',
  'src/layout.c',
  'src/mwc.c',
  'src/output.c',
  'src/pointer.c',
  'src/popup.c',
  'src/rendering.c',
  'src/session_lock.c',
  'src/something.c',
  'src/toplevel.c',
  'src/workspace.c'
]

inc = [
  '.',
  'src',
  'util'
]

deps = [
  dependency('wayland-server'),
  dependency('scenefx-0.2', version: '>=0.2', fallback: 'scenefx'),
  dependency('wlroots-0.18', version: ['>=0.18.0', '<0.19.0'], fallback: 'wlroots'),
  dependency('xkbcommon'),
  dependency('libinput'),
]

executable('mwc',
  src,
  generated,
  dependencies: deps,
  include_directories: include_directories(inc),
  link_args : '-lm',
  install: true
)

executable('mwc-ipc',
  'mwc-ipc/mwc-ipc.c',
  include_directories: include_directories('util'),
  install: true
)

install_data('default.conf', install_dir: get_option('datadir') / 'mwc')
install_data('LICENSE', install_dir: get_option('datadir') / 'licenses/mwc')
install_data('mwc.desktop', install_dir: get_option('datadir') / 'wayland-sessions')
install_data('mwc-portals.conf', install_dir: get_option('datadir') / 'xdg-desktop-portal')

