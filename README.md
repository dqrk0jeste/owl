<div align="center">
<h1>owl</h1>

<img src="assets/logo.png" alt="logo" width="300">

> owl is a minimal tiling wayland compositor based on wlroots.
</div>

<br>

## features
- tiling and floating toplevels
- multimonitor and workspaces support
- easily configurable
- custom keybinds, resolutions etc

> owl is made mainly for myself, implementing just enough for my workflow. that means a lot of things is just not there. if you are looking for something more ready to use take a look at hyprland, sway or river. 

## showcase

<div align="center">

<img src="assets/showcase-1.png" alt="logo" width="300">
<img src="assets/showcase-2.png" alt="logo" width="300">
<img src="assets/showcase-3.png" alt="logo" width="300">

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

it is recommended to install it by running `make install`, which will also load the default config. then you can run it with just `owl`

if you choose to unistall it you can do so with `make unistall`.

## configuration

configuration is done in a configuration file found at `$XDG_CONFIG_HOME/owl/owl.conf` or `$HOME/.config/owl/owl.conf`. if no config is found a default config will be used (only if `owl` is installed, see previous).

you can find the default config in the repo, as well as my personal config `example.conf`.

every line is a config value made up from a keyword followed by one or more arguments, everything separated by spaces. if you need a space in an argument, use quotes e.g. "something with spaces".

## todo
- there are some known issues that need fixing, see `known_issues.md`
- window rules for floating windows
- opacity settings
- animations
- mouse clicks for shortcuts (moving and resizing toplevels for example)
- more configuration options, mainly when it comes to tiling capabilities and layouts.

## special thanks to
- Krasa, for making the cute owl
