<div align="center">
  <h1>mwc</h1>
  <img src="assets/preview.png" alt="preview" width="500">
  <br>
</div>
<br>

## about
`mwc` came to existence out of pure will to create a compositor tailored to my taste and needs. the whole point of `mwc` is to be as simple and predictable to use and not get in the way of its user with any confusing behaviour. i choose master (stack) layout (hence the name `master wayland compositor`), because it is dead simple - tiling is done in a really easy-to-understand manner - toplevels are stacked horizontally until the `master_count`-th one, and then vertically.

all the features implemented up to this point (and all that will be implemented it the future) are done in the simplest possible way i could think off, and all the features requested are added only if they provide something to the end user while maintaining the current simplicity of the compositor.

although `mwc` is aiming to be really simple in its behaviour, it does provide a lot of (opt-in) features to improve its looks such as animations, transparency, rounded corners, blur etc. these are here for all the users who like thinkering with their setup and can be disabled completely, or used in any capacity.

## features
- tiling and floating toplevels
- master layout with support for multiple masters, ideal for wide monitors
- optimized for the keyboard focused workflow
- great multitasking with multi-monitor and workspaces support
- smooth and customizable animations
- easy configuration with hot reloading on save
- eye-candy (opacity, blur, rounded corners and shadows)
- powerful ipc for integrating with your apps

## dependencies
- meson *
- ninja *
- wayland-protocols *
- wayland
- libinput
- libdrm
- pixman
- libxkbcommon
- wlroots 18.0
- scenefx 0.2
- fcft
- json-c

> \* compile-time dependencies

## building
```bash
git clone https://github.com/dqrk0jeste/mwc
cd mwc
meson setup build
ninja -C build
```

## installation

### arch
`mwc` is available in the arch user repository as `mwc` (versioned releases) and `mwc-git` (latest features). you can install it with your favourite aur helper
```bash
yay -S mwc
```

### other distros
```bash
git clone https://github.com/dqrk0jeste/mwc
cd mwc
meson setup build --prefix=/usr/local --buildtype=release
ninja -C build install
```

## post install
if you need to interact with sandboxed applications and/or screenshare you will need xdg-desktop-portals. by default `mwc` needs
- xdg-desktop-portal (base)
- xdg-desktop-portal-wlr (for screensharing)
- xdg-desktop-portal-gtk (for everything else)

## usage
```bash
mwc [--debug]
```

> you probably want to run it from a tty

## configuration
configuration is done in a configuration file found at `$XDG_CONFIG_HOME/mwc/mwc.conf` or `$HOME/.config/mwc/mwc.conf`. if no config is found a default config will be used (you need `mwc` installed, see above).

> note: you can use other configuration location by setting `MWC_CONFIG_PATH` before running `mwc`.

for detailed documentation see `examples/example.conf`. you can also find the default config in the repo.

## gallery
<div align="center">
<img src="assets/gallery-1.png" alt="logo" width="500">
<img src="assets/gallery-2.png" alt="logo" width="500">
</div>

## donate
if you want to support this project financially you can do so [here](https://ko-fi.com/darkonikolic)
