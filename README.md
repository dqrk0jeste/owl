<div align="center">
<h1>owl</h1>

<img src="assets/logo.png" alt="logo" width="300">

> owl is a minimal tiling wayland compositor based on wlroots.
</div>

<br>

## features
- both tiling and floating toplevels
- keyboard focused workflow
- multimonitor and workspaces support
- easy configuration
- custom keybinds, monitor layouts etc
- ipc for communication

> owl is made mainly for myself, implementing just enough for my workflow. that means a lot of things is just not there. if you are looking for something more mature take a look at hyprland, sway or river. 

## showcase

<div align="center">

<img src="assets/showcase-1.png" alt="logo" width="500">
<img src="assets/showcase-2.png" alt="logo" width="500">
<img src="assets/showcase-3.png" alt="logo" width="500">

</div>

## dependencies
- wayland-protocols
- wayland-scanner
- wayland-server
- pixman
- libdrm
- libinput
- xkbcommmon
- wlroots >= 19.0 (git version on aur)

if you are already using a wayland compositor you probably have those installed already.

## building

```bash
git clone https://github.com/dqrk0jeste/owl
cd owl
make
```

## usage

```bash
build/owl
```

> it is best to run it from a tty.

## installation
it is recommended to install `owl` by running `make install`. it will also load the default config. then you can run it with just `owl`.

if you choose to uninstall it you can do so with `make unistall`.

## configuration
configuration is done in a configuration file found at `$XDG_CONFIG_HOME/owl/owl.conf` or `$HOME/.config/owl/owl.conf`. if no config is found a default config will be used (only if `owl` is installed, see previous).

for detailed documentation see `examples/example.conf`. you can also find the default config in the repo.

## todo
- there are some known issues that need fixing, see `known_issues.md`
- opacity settings
- animations
- mouse clicks for keybinds (moving and resizing toplevels for example)
- more configuration options

## special thanks to
- Krasa, for making the cute owl
