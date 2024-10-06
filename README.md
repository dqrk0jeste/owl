<div align="center">
<h1>richfetch</h1>

<img src="assets/logo.png" alt="logo" width="300">

> owl is a tiling wayland compositor based on wlroots, focused on simplicity and minimalism.
</div>

<br>

[![Badge License]][License] 
![Badge Language] 
[![Badge Pull Requests]][Pull Requests] 
[![Badge Issues]][Issues] 

<br>

## Features:
- switching between tiling and floating toplevels
- multimonitor and workspaces support
- easily configurable
- custom keybinds, monitor modes etc

> owl is made as a fun side-project, implementing just enough for my needs. you are free to contribute to it, fork it and make it better, but if you are looking for something more feature-rich then take a look at Hyprland, sway or river. 

## Building

### dependencies
- wayland-protocols
- wayland-scanner
- pixman
- libdrm
- xkbcommmon
- wlroots >= 19.0 (git version on aur)

if you are already using a wayland compositor you should have those installed already.

```bash
git clone https://github.com/dqkr0jeste/owl
cd owl
make
```

## usage

```bash
build/owl
```
> it is best to run it from a tty.

you can install to path it by running `make install`, which will also load the default config. then you can run it by

```bash
owl
```

if you choose to unistall it you can run `make unistall`.

## configuration

configuration is done in a configuration file found at `$XDG_CONFIG_HOME/owl/owl.conf` or `$HOME/.config/owl/owl.conf`. if no config is found a default config will be loaded (only if owl is installed, see previous).

you can find the default config in the repo, as well as my personal config `example.conf`.

every line is a config value made up from a keyword followed by one or more arguments, everything separated by spaces.

## todo
- there are some known issues that need fixing, see `known_issues.md`
- window rules for floating windows
- opacity for windows
- animations
- more configuration options, mainly when it comes to tiling capabilities and layouts.
- mouse clicks in shortcuts

## special thanks to
- Krasa, who made the cute owl
