<div align="center">
<h1>owl</h1>
<img src="https://github.com/dqrk0jeste/owl/blob/main/assets/owl.gif" width="500"/>
<br>
minimal tiling wayland compositor based on wlroots.
</div>

<br>

## features
- tiling and floating toplevels
- master layout with support for multiple masters, ideal for wide monitors
- keyboard focused workflow
- great multitasking with multimonitor support and workspaces out of the box
- smooth and customizable animations
- easy configuration with custom keybinds, monitor layouts etc
- ipc for integrating with other apps

> owl is made mainly for myself, implementing just enough for my workflow. that means a lot of things is just not there. if you are looking for something more mature take a look at hyprland, sway or river. 

## showcase
<div align="center">

<img src="assets/showcase-1.png" alt="logo" width="500">
<img src="assets/showcase-2.png" alt="logo" width="500">

</div>

## dependencies
- make
- wayland-protocols
- wayland-scanner
- wayland-server
- pixman
- libdrm
- libinput
- xkbcommmon
- wlroots >= 19.0 (git version on aur)

## building
```bash
git clone https://github.com/dqrk0jeste/owl
cd owl
make
```

## installation
it is recommended to install `owl` by running
```bash
make install
```
it will also install the default config to `/usr/share/owl/default.conf`

> if you want to uninstall it you can do so with `make unistall`.

## usage
```bash
owl
```

> it is recommended to run it from a tty.

## configuration
configuration is done in a configuration file found at `$XDG_CONFIG_HOME/owl/owl.conf` or `$HOME/.config/owl/owl.conf`. if no config is found a default config will be used (you need `owl` installed, see above).

for detailed documentation see `examples/example.conf`. you can also find the default config in the repo.

## nixos
you can install `owl` by using [chaotic-cx/nyx](https://github.com/chaotic-cx/nyx) flake! 

in [chaotic-cx/nyx](https://github.com/chaotic-cx/nyx) avaibale both `owl-wlr_git` package and `chaotic.owl-wlr` option. to enable `owl` just add:
```Nix
chaotic.owl = {
  enable = true;
  extraPackages = with pkgs; [
    kitty
    rofi
    grimblast
  ];
};
```
to your **nixos** configuration!

## todos
- [ ] issues need fixing, see `known-issues.md`
- [ ] monitor hotplugging
- [x] more natural (output layout aware) output switching
- [ ] mouse clicks for keybinds (for moving and resizing toplevels)
- [ ] more ipc capabilities
- [ ] opacity settings
- [x] animations
